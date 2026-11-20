#include "core.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;

static void write_file(const fs::path& p, const std::string& data){
    fs::create_directories(p.parent_path());
    std::ofstream out(p,std::ios::binary);out<<data;
    if(!out)throw std::runtime_error("cannot create fixture");
}
static bool fails(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    const auto base=fs::temp_directory_path()/fs::path("dc_scope_")/
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto a=base/"volume_A",b=base/"volume_B";
    try{
        write_file(a/"photos"/"1.jpg","same bytes");
        write_file(b/"backup"/"2.jpg","same bytes");
        write_file(a/"unique.txt","different bytes");
        fs::create_directories(b/"empty");
        // A count-only scan returns the same totals without retaining paths or making a cache.
        const auto counts_root=base/"counts";
        write_file(counts_root/"x"/"data.txt","abc");
        fs::create_directories(counts_root/"empty");
        write_file(counts_root/".DiskCurator-System"/"owner.txt","reserved");
        write_file(counts_root/".DiskCurator-System"/"internal.bin","not user data");
        std::error_code link_error;
        fs::create_directory_symlink(counts_root,counts_root/"cycle",link_error);
        dc::ScanProgress last;
        const auto counts=dc::scan_inventory(counts_root,[&](const dc::ScanProgress& p){last=p;});
        std::atomic_bool stop{false};
        assert(fails([&]{dc::scan_inventory(counts_root,[&](const auto&){stop=true;},&stop);}));
        const auto detailed=dc::scan(counts_root);
        assert(counts.file_count==1 && counts.directory_count==3 && counts.bytes==3);
        assert(counts.file_count==detailed.files.size() && counts.directory_count==detailed.dirs.size());
        assert(counts.reparse_skipped==(link_error?0U:1U) && counts.error_count()==detailed.errors.size());
        assert(last.totals_known && last.files_discovered==1 && last.total_bytes==3 && last.bytes_read==0);
        assert(last.errors==counts.error_count() && counts.directory_errors==0 && counts.other_errors==0);
        auto report=dc::scan_roots_hashed({a,b});
        assert(report.volumes.size()==2);
        assert(report.total_files==3);
        assert(report.total_bytes==10+10+15);
        assert(report.complete());
        assert(report.hash_candidates.size()==1);
        assert(report.hash_candidates.front().members.size()==2);
        assert(report.hash_candidates.front().sha256.size()==64);
        assert(!fs::exists(a/".DiskCurator-System") && !fs::exists(b/".DiskCurator-System"));
        assert(report.volumes[0].root==fs::absolute(a));
        assert(report.volumes[1].root==fs::absolute(b));
        assert(fails([&]{dc::scan_roots_hashed({a,a});}));
        assert(fails([&]{dc::scan_roots_hashed({a,a/"photos"});}));
        assert(fails([&]{dc::scan_roots_hashed({});}));
        std::atomic_bool cancelled{true};
        assert(fails([&]{dc::scan_roots_hashed({a,b},{},&cancelled);}));
        auto partial=dc::scan_roots_hashed({a,base/"not-present",b});
        assert(partial.volumes.size()==2);
        assert(!partial.complete());
        assert(partial.errors.size()==1);
        assert(partial.total_files==3);
        // Content equality assertions require the original byte verifier.
        // Hash candidates are deliberately kept read-only across volume roots.
        fs::remove_all(base);
        std::cout<<"scope tests passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";fs::remove_all(base);return 1;}
}
