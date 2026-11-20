#include "core.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

using namespace dc;
static void put(const fs::path& p,const std::string& value){
    fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot generate stress fixture");
    f<<value;
}
static std::string name(int n){std::ostringstream o;o<<std::setw(3)<<std::setfill('0')<<n;return o.str();}
int main(){
    auto root=fs::temp_directory_path()/("dc_stress_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        constexpr int dirs=80, per_dir=48, copy_files=100;
        for(int d=0;d<dirs;++d)for(int f=0;f<per_dir;++f)
            put(root/("folder_"+name(d))/("file_"+name(f)+".txt"),"unique content "+name(d)+"/"+name(f));
        for(int f=0;f<copy_files;++f){
            auto val="intentionally duplicate content "+name(f);
            put(root/"copy_A"/("photo_"+name(f)+".jpg"),val);
            put(root/"copy_B"/("photo_"+name(f)+".jpg"),val);
        }
        auto inv=scan(root);
        assert(inv.complete()&&inv.files.size()==size_t(dirs*per_dir+2*copy_files));
        auto dup=deduplicate(inv);
        assert(dup.errors.empty()&&dup.groups.size()==copy_files);
        auto groups=duplicate_dirs(inv);
        bool saw=false;
        for(const auto& g:groups)if(g.paths.size()==2&&g.paths[0]==fs::path("copy_A")&&g.paths[1]==fs::path("copy_B"))saw=true;
        assert(saw);
        RenameOptions rename;rename.dir.clear();rename.pattern=L"*";rename.include_files=false;rename.prefix=L"batch_";
        auto plan=plan_rename(inv,rename);
        assert(plan.moves.size()==size_t(dirs+2));
        size_t count=0;for(const auto& m:plan.moves)count+=m.expected_source.size();
        assert(count==inv.files.size()+inv.dirs.size()-1);
        ArchiveOptions archive;archive.mode=ArchiveMode::ByType;
        auto archive_plan=plan_archive(inv,archive);
        assert(archive_plan.moves.size()==inv.files.size());
        std::atomic_bool cancel{true};
        try { (void)directory_relations(inv,100,&cancel);assert(false); }
        catch(const std::runtime_error&) {}
        fs::remove_all(root);
        std::cout<<"LARGE DIRECTORY STRESS PASSED ("<<inv.files.size()<<" files)\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"STRESS FAILED: "<<e.what()<<" at "<<root<<"\n";return 2;}
}
