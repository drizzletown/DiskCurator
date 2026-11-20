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
