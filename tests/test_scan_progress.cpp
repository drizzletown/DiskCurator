#include "core.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
using namespace dc;
void put(const fs::path& path,const std::string& bytes){
    fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(!out)throw std::runtime_error("Cannot write test fixture");
}
std::string pattern(size_t size){
    std::string bytes(size,'\0');
    for(size_t i=0;i<size;++i)bytes[i]=static_cast<char>(i%251);
    return bytes;
}
const File& find(const Inventory& inv,const fs::path& path){
    for(const auto& file:inv.files)if(file.rel==path)return file;
    throw std::runtime_error("Missing test file");
}
void check_monotonic(const std::vector<ScanProgress>& events){
    assert(!events.empty());
    U64 bytes=0,read=0,files=0;
    double elapsed=0;
    for(const auto& event:events){
        assert(event.root_index==1&&event.root_count==1);
        assert(event.elapsed_seconds>=elapsed);elapsed=event.elapsed_seconds;
        if(event.phase==ScanPhase::Enumerating)continue;
        assert(event.totals_known);
        assert(event.bytes_processed>=bytes&&event.bytes_processed<=event.total_bytes);
        assert(event.bytes_read>=read&&event.bytes_read<=event.bytes_processed);
        assert(event.files_completed>=files&&event.files_completed<=event.files_discovered);
        bytes=event.bytes_processed;read=event.bytes_read;files=event.files_completed;
    }
}
}
int main(){
    using namespace dc;
    const auto test=fs::temp_directory_path()/("dc_scan_progress_"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root=test/"data",cache=test/"cache";
    try{
#ifdef _WIN32
        SetEnvironmentVariableW(L"LOCALAPPDATA",cache.c_str());
#else
        setenv("XDG_CACHE_HOME",cache.c_str(),1);
#endif
        put(root/"00-empty","");put(root/"01-abc","abc");
        put(root/"02-million-a",std::string(1000000,'a'));
        struct Vector {size_t length;const char* digest;};
        const Vector vectors[]={
            {55,"463eb28e72f82e0a96c0a4cc53690c571281131f672aa229e0d45ae59b598b59"},
            {56,"da2ae4d6b36748f2a318f23e7ab1dfdf45acdc9d049bd80e59de82a60895f562"},
            {63,"29af2686fd53374a36b0846694cc342177e428d1647515f078784d69cdb9e488"},
            {64,"fdeab9acf3710362bd2658cdc9a29e8f9c757fcf9811603a8c447cd1d9151108"},
            {65,"4bfd2c8b6f1eec7a2afeb48b934ee4b2694182027e6d0fc075074f2fabb31781"},
            {1048589,"8210b3c7b3f97e5eaa57b89d3a1a864ac8f5fe910f6327396801c312daa80291"},
            {8388621,"f9cf8e24e7f07f24e5f3e62d7e5dd884bbc00a4ba173d697e88bb60efbd33809"}
        };
        // Expected digests are from Python hashlib, not the production helper.
        for(const auto& v:vectors)put(root/("pattern-"+std::to_string(v.length)),pattern(v.length));
        std::vector<ScanProgress> events;
        const auto inv=scan_hashed(root,{},nullptr,[&](const auto& e){events.push_back(e);});
        assert(inv.complete()&&inv.index_persisted&&inv.index_error.empty());check_monotonic(events);
        assert(events.front().phase==ScanPhase::Enumerating&&!events.front().totals_known);
        assert(events.back().phase==ScanPhase::Complete);
        assert(events.back().files_completed==inv.files.size());
        assert(events.back().bytes_read==inv.bytes&&events.back().bytes_processed==inv.bytes);
        assert(events.back().errors==0);
        assert(find(inv,"00-empty").sha256=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        assert(find(inv,"01-abc").sha256=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        assert(find(inv,"02-million-a").sha256=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        for(const auto& v:vectors)
            assert(find(inv,"pattern-"+std::to_string(v.length)).sha256==v.digest);
        // A second scan reads contents again: stored hashes are not silently
        // trusted after an edit, even when a caller restores the file mtime.
        const auto original_time=fs::last_write_time(root/"01-abc");
        put(root/"01-abc","xyz");fs::last_write_time(root/"01-abc",original_time);
        ScanProgress rescanned;
        auto fresh=scan_hashed(root,{},nullptr,[&](const auto& e){rescanned=e;});
        assert(rescanned.bytes_read==fresh.bytes);
        assert(find(fresh,"01-abc").sha256=="3608bca1e44ea6c4d268eb6db02260269892c0b42b86bbf1e77a6fa16c3c9282");
        // One large file: cancel after its first published block, before EOF.
        const auto large=test/"large";
        put(large/"large.bin",pattern(8388621));
        std::atomic_bool stop{false};bool interrupted=false,completed=false;
        U64 canceled_after=0;
        try{
            (void)scan_hashed(large,{},&stop,[&](const auto& e){
                if(e.phase==ScanPhase::Hashing&&e.bytes_read>0){canceled_after=e.bytes_read;stop=true;}
                if(e.phase==ScanPhase::Complete)completed=true;
            });
        }catch(const std::runtime_error&){interrupted=true;}
        assert(interrupted&&!completed&&canceled_after>0&&canceled_after<8388621);
        // A deleted file between enumeration and hashing remains an explicit
        // error, receives no digest, and is not counted as content bytes read.
        const auto unstable=test/"unstable";
        put(unstable/"a-fails","missing");put(unstable/"b-works","abc");
        bool removed=false;events.clear();
        auto partial=scan_hashed(unstable,{},nullptr,[&](const auto& e){
            events.push_back(e);
            if(e.phase==ScanPhase::Hashing&&!removed){fs::remove(unstable/"a-fails");removed=true;}
        });
        assert(!partial.complete()&&partial.errors.size()==1);
        assert(find(partial,"a-fails").sha256.empty());
        assert(find(partial,"b-works").sha256==find(inv,"01-abc").sha256);
        check_monotonic(events);
        assert(events.back().errors==1&&events.back().files_completed==2);
        assert(events.back().bytes_processed==10&&events.back().bytes_read==3);
        // All-empty and empty-directory scans must complete without division
        // by zero; no content bytes are fabricated to move a progress bar.
        const auto empty=test/"empty";fs::create_directories(empty);
        events.clear();const auto empty_inv=scan_hashed(empty,{},nullptr,[&](const auto& e){events.push_back(e);});
        assert(empty_inv.files.empty());check_monotonic(events);
        assert(events.back().phase==ScanPhase::Complete&&events.back().bytes_read==0);
        put(empty/"zero","");
        events.clear();(void)scan_hashed(empty,{},nullptr,[&](const auto& e){events.push_back(e);});
        assert(events.back().files_completed==1&&events.back().total_bytes==0);
        // Multivolume scope identifies each root. Elapsed time is cumulative,
        // while byte counters deliberately restart for the next labelled root.
        double elapsed=0;bool first=false,second=false;
        const auto scope=scan_roots_hashed({empty,large},{},nullptr,[&](const auto& e){
            assert(e.root_count==2&&e.root_index>=1&&e.root_index<=2);
            assert(e.elapsed_seconds>=elapsed);elapsed=e.elapsed_seconds;
            if(e.root_index==1)first=true;else second=true;
        });
        assert(scope.complete()&&scope.volumes.size()==2&&first&&second);
        // Cancellation while saving removes the uncommitted index temp and
        // does not publish Complete or overwrite a previous good index.
        stop=false;completed=false;interrupted=false;
        try{(void)scan_hashed(empty,{},&stop,[&](const auto& e){
            if(e.phase==ScanPhase::SavingIndex)stop=true;
            if(e.phase==ScanPhase::Complete)completed=true;
        });}catch(const std::runtime_error&){interrupted=true;}
        assert(interrupted&&!completed);
        for(const auto& entry:fs::directory_iterator(hash_cache_root()))
            assert(entry.path().extension()==fs::path(".tsv"));
        // A cache path blocked by an ordinary file must preserve complete
        // content results, with a durable warning independent of read errors.
        const auto blocked=test/"blocked-cache";put(blocked,"not a directory");
#ifdef _WIN32
        SetEnvironmentVariableW(L"LOCALAPPDATA",blocked.c_str());
#else
        setenv("XDG_CACHE_HOME",blocked.c_str(),1);
#endif
        const auto unpersisted=scan_hashed(root,{},nullptr);
        assert(unpersisted.complete()&&unpersisted.errors.empty());
        assert(!unpersisted.index_persisted&&!unpersisted.index_error.empty());
        assert(find(unpersisted,"01-abc").sha256==find(fresh,"01-abc").sha256);
        const auto multi_unpersisted=scan_roots_hashed({empty,large});
        assert(multi_unpersisted.complete()&&multi_unpersisted.volumes.size()==2);
        for(const auto& volume:multi_unpersisted.volumes)
            assert(!volume.index_persisted&&!volume.index_error.empty());
        std::error_code ignored;fs::remove_all(test,ignored);
        std::cout<<"SCAN PROGRESS / STREAM SHA256 / CANCEL / ERROR ACCOUNTING PASSED\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<" at "<<test<<'\n';return 2;
    }
}
