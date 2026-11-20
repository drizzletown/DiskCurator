#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace dc;
static void put(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("fixture creation failed");f<<s;}
static std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read fixture");return {std::istreambuf_iterator<char>(f),{}};}
static bool fails(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
static std::string hex(const std::string& text){const char* a="0123456789abcdef";std::string ret;for(unsigned char c:text){ret+=a[c>>4];ret+=a[c&15];}return ret;}
int main(){
 const auto root=fs::temp_directory_path()/("diskcurator_merge_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
    put(root/"2023"/"common.jpg","SAME");put(root/"2024"/"common.jpg","SAME");
    put(root/"2023"/"IMG0001.jpg","OLD");put(root/"2024"/"IMG0001.jpg","NEW");
    put(root/"2023"/"old-only.jpg","OLD-ONLY");put(root/"2024"/"new-only.jpg","NEW-ONLY");
    fs::create_directories(root/"2023"/"empty"/"nested");
    auto inv=scan(root);auto dup=deduplicate(inv);
    auto plan=plan_merge_dirs(inv,"2023","2024");
    assert(plan.copies.size()==2&&plan.identical==1&&plan.conflicts==1&&plan.directories.size()==2);
    assert(fs::exists(root/"2023"/"old-only.jpg"));
    assert(!fs::exists(root/"2024"/"old-only.jpg")); // preview must be read-only
    auto result=execute_merge_plan(root,plan);
    if(!result.success)throw std::runtime_error("additive merge failed: "+result.message);
    assert(read(root/"2023"/"old-only.jpg")=="OLD-ONLY");
    assert(read(root/"2024"/"old-only.jpg")=="OLD-ONLY");
    assert(read(root/"2024"/"IMG0001.jpg")=="NEW");
    assert(read(root/"2024"/"IMG0001 (from 2023).jpg")=="OLD");
    assert(fs::is_directory(root/"2024"/"empty"/"nested"));
    assert(history(root).front().items==plan.copies.size()+plan.directories.size());
    assert(!undo_latest(root).success); // Merge cannot blindly remove a second backup
    auto again=scan(root);auto verified=deduplicate(again);
    auto repeat=plan_merge_dirs(again,"2023","2024");
    assert(repeat.copies.empty()&&repeat.directories.empty());
    assert(repeat.identical==3); // source common, old-only, conflict-version all present
    // If the destination changes after preview, abort even with unchanged filename.
    put(root/"2023"/"brand-new.txt","ADDITIONAL");
    auto scan2=scan(root);deduplicate(scan2);
    auto pending=plan_merge_dirs(scan2,"2023","2024");
    put(root/"2024"/"untracked.jpg","NEW SINCE PREVIEW");
    auto stale=execute_merge_plan(root,pending);
    assert(!stale.success&&!fs::exists(root/"2024"/"brand-new.txt"));
    // Collision between a source directory and an existing target file blocks.
    put(root/"2023"/"types"/"inside.txt","123");
    put(root/"2024"/"types","a file, not a directory");
    auto types=scan(root);deduplicate(types);
    assert(fails([&]{(void)plan_merge_dirs(types,"2023","2024");}));
    fs::remove(root/"2024"/"types");
    fs::remove_all(root/"2023"/"types");
    // Explicit recovery of an interrupted copy journal is additive; a visible
    // destination is retained and only software-owned stage bytes are discarded.
    const std::string id="fixture_merge";
    const auto stage=fs::path(".DiskCurator-System")/"stage"/id/"0";
    put(root/stage,"STAGING");
    put(root/"2024"/"committed.txt","COMMITTED");
    const std::string body="LABEL "+hex("Merge directories: 2023 -> 2024")+"\nCOPY "+
         hex("2023/common.jpg")+" "+hex("2024/committed.txt")+" "+hex(utf8(stage))+"\nPENDING\nSTAGEDALL\n";
    put(root/".DiskCurator-System"/"journal"/(id+".log"),body);
    auto recovery=recover_incomplete(root);
    assert(recovery.size()==1&&recovery[0].find("partial additive merge")!=std::string::npos);
    assert(read(root/"2024"/"committed.txt")=="COMMITTED");
    assert(!fs::exists(root/stage));
    assert(recover_incomplete(root).empty());
    // A two-copy group must remain intact, with a third independent file the
    // cleanup may quarantine only the third.
    const auto another=root/"2025"/"common.jpg";
    put(another,"SAME");
    auto all=scan(root);auto dd=deduplicate(all);auto cleanup=plan_clean_duplicates(all,dd);
    size_t common_moved=0;
    for(const auto& m:cleanup.moves)if(m.from.filename()==fs::path("common.jpg"))++common_moved;
    assert(common_moved==1);
    assert(execute_plan(root,cleanup).success);
    auto after=scan(root);auto verified_after=deduplicate(after);
    size_t remaining=0;
    for(const auto& f:after.files)if(f.rel.filename()==fs::path("common.jpg"))++remaining;
    assert(remaining==2);
    assert(undo_latest(root).success);
    // If two paths are hardlinks to one file, they cannot count as two
    // independently stored file objects for the retention rule.
#ifndef _WIN32
    put(root/"hard"/"a.bin","hardlink-data");
    fs::create_hard_link(root/"hard"/"a.bin",root/"hard"/"b.bin");
    put(root/"hard"/"c.bin","hardlink-data");
    auto hard=scan(root);auto harddup=deduplicate(hard);auto hp=plan_clean_duplicates(hard,harddup);
    size_t hard_moves=0;for(const auto& m:hp.moves)if(m.from.parent_path()==fs::path("hard"))++hard_moves;
    assert(hard_moves==1);
#endif
    std::error_code ec;fs::remove_all(root,ec);
    std::cout<<"MERGE / RETENTION TESTS PASSED\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"; fixture="<<root<<"\n";return 2;}
}
