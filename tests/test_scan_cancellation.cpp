#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace dc;
static void write_file(const fs::path& path,const std::string& contents){
    fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);out.write(contents.data(),std::streamsize(contents.size()));
    if(!out)throw std::runtime_error("Cannot create fixture");
}
int main(){
    const auto root=fs::temp_directory_path()/("dc_scan_cancel_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        const std::string contents(8*1024*1024,'x');
        write_file(root/"left"/"deep"/"one.bin",contents);
        write_file(root/"right"/"two.bin",contents);
        write_file(root/"unique.txt","unique");
        fs::create_directories(root/"empty"/"child");
        ScanProgress final_scan;size_t enumeration_events=0;
        auto inv=scan(root,{},nullptr,[&](const ScanProgress& p){
            assert(p.phase==ScanPhase::Enumerating);
            final_scan=p;++enumeration_events;
        });
        assert(enumeration_events>=2&&final_scan.totals_known);
        assert(final_scan.files_discovered==3&&final_scan.total_bytes==2*contents.size()+6);
        for(const auto& dir:inv.dirs){
            if(dir.rel.empty()){assert(dir.count==3&&dir.bytes==2*contents.size()+6);}
            else if(dir.rel==fs::path("left")||dir.rel==fs::path("left/deep")||dir.rel==fs::path("right"))
                assert(dir.count==1&&dir.bytes==contents.size());
            else assert(dir.count==0&&dir.bytes==0);
        }
        for(auto& f:inv.files)f.sha256=f.bytes==contents.size()?std::string(64,'a'):std::string(64,'b');
        std::atomic_bool cancel{false};U64 read_at_cancel=0;
        bool aborted=false;
        try{(void)deduplicate(inv,{},&cancel,[&](const ScanProgress& p){
            assert(p.phase==ScanPhase::Verifying);
            if(p.bytes_read){read_at_cancel=p.bytes_read;cancel.store(true);}
        });}catch(const std::runtime_error& e){aborted=std::string(e.what())=="Operation canceled";}
        assert(aborted&&read_at_cancel>0&&read_at_cancel<contents.size());
        assert(!fs::exists(root/".DiskCurator-System"));
        cancel=false;ScanProgress previous,finished;
        const auto duplicates=deduplicate(inv,{},&cancel,[&](const ScanProgress& p){
            assert(p.bytes_read>=previous.bytes_read);
            assert(p.files_completed>=previous.files_completed);
            assert(!p.totals_known); // comparison I/O has no fixed denominator
            previous=p;finished=p;
        });
        assert(duplicates.errors.empty()&&duplicates.groups.size()==1);
        assert(finished.files_completed==inv.files.size());
        assert(finished.bytes_read==2*contents.size());
        assert(fs::file_size(root/"left/deep/one.bin")==contents.size());
        cancel=false;bool enumeration_cancelled=false;
        try{(void)scan(root,{},&cancel,[&](const ScanProgress&){cancel=true;});}
        catch(const std::runtime_error& e){enumeration_cancelled=std::string(e.what())=="Operation canceled";}
        assert(enumeration_cancelled);
        fs::remove_all(root);
        std::cout<<"SCAN ROLLUP / BYTE-LEVEL VERIFY PROGRESS / CANCELLATION PASSED\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<" at "<<root<<'\n';
        std::error_code ec;fs::remove_all(root,ec);return 2;
    }
}
