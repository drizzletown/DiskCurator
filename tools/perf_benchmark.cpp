// Read-only performance profiling for DiskCurator's C++17 core.
// It deliberately does not create transaction workspaces or change files.
#include "core.hpp"
#include "observations.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <fstream>
#include <sys/resource.h>
#endif

namespace {
using clock_type=std::chrono::steady_clock;
struct Counters {std::uint64_t logical_reads=0,peak_memory=0;};
Counters counters(){
    Counters c;
#ifdef _WIN32
    IO_COUNTERS io{};
    if(GetProcessIoCounters(GetCurrentProcess(),&io))c.logical_reads=io.ReadTransferCount;
    PROCESS_MEMORY_COUNTERS pm{};
    if(GetProcessMemoryInfo(GetCurrentProcess(),&pm,sizeof(pm)))c.peak_memory=pm.PeakWorkingSetSize;
#else
    std::ifstream in("/proc/self/io");std::string key;unsigned long long value=0;
    while(in>>key>>value)if(key=="rchar:")c.logical_reads=value;
    struct rusage ru{};
    if(getrusage(RUSAGE_SELF,&ru)==0)c.peak_memory=std::uint64_t(ru.ru_maxrss)*1024ULL;
#endif
    return c;
}
void result(const char* label,clock_type::time_point started,Counters before,const std::string& comment={}){
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now()-started).count();
    const auto after=counters();
    std::cout<<label<<'\t'<<ms<<'\t'<<(after.logical_reads>=before.logical_reads?after.logical_reads-before.logical_reads:0)
             <<'\t'<<after.peak_memory<<'\t'<<comment<<'\n';
}
const char* scan_phase_label(dc::ScanPhase phase){
    switch(phase){
        case dc::ScanPhase::Enumerating:return "scan_enumeration";
        case dc::ScanPhase::Hashing:return "scan_sha256";
        case dc::ScanPhase::SavingIndex:return "scan_save_index";
        case dc::ScanPhase::Analyzing:return "scan_hash_groups";
        case dc::ScanPhase::Verifying:return "verify_bytes";
        case dc::ScanPhase::Complete:return "complete";
    }
    return "unknown";
}
std::string scan_summary(const dc::ScanProgress& s){
    return "files="+std::to_string(s.files_discovered)+
        "; attempted="+std::to_string(s.files_completed)+
        "; inventory_bytes="+std::to_string(s.total_bytes)+
        "; content_read_bytes="+std::to_string(s.bytes_read)+
        "; errors="+std::to_string(s.errors);
}
void usage(){std::cerr<<"Usage: dc_benchmark ROOT [--scan | --full] [--overlaps] [--pair LEFT RIGHT]\n"
    "  Default: metadata-only scan. --scan profiles the actual complete SHA256\n"
    "  scan and result observations, with phase times and content-read bytes.\n"
    "  --full is the older metadata + byte-exact duplicate analysis (not the GUI scan).\n"
    "  --overlaps / --pair imply --full and cannot be mixed with --scan.\n"
    "  User files are read only; --scan writes the rebuildable application cache.\n"
    "  process_read_bytes is logical process I/O, not physical device traffic.\n";}

}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
#else
int main(int argc,char** argv){
#endif
    if(argc<2){usage();return 2;}
    try{
        const dc::fs::path root=argv[1];
        bool scan_mode=false,full=false,overlaps=false,has_pair=false;
        dc::fs::path left,right;
        for(int i=2;i<argc;++i){
            const auto arg=dc::utf8(dc::fs::path(argv[i]));
            if(arg=="--scan")scan_mode=true;
            else if(arg=="--full")full=true;
            else if(arg=="--overlaps"){overlaps=true;full=true;}
            else if(arg=="--pair"&&i+2<argc){left=argv[++i];right=argv[++i];has_pair=true;full=true;}
            else {usage();return 2;}
        }
        if(scan_mode&&full){usage();return 2;}
        std::cout<<"phase\tms\tprocess_read_bytes\tpeak_working_set_bytes\tresult\n";
        auto start=clock_type::now();auto before=counters();
        if(scan_mode){
            bool phase_started=false;
            dc::ScanPhase phase=dc::ScanPhase::Enumerating;
            auto phase_time=start;auto phase_counters=before;
            dc::ScanProgress last;
            auto telemetry=[&](const dc::ScanProgress& s){
                if(!phase_started||s.phase!=phase){
                    if(phase_started&&phase!=dc::ScanPhase::Complete)
                        result(scan_phase_label(phase),phase_time,phase_counters,scan_summary(s));
                    phase_started=true;phase=s.phase;
                    phase_time=clock_type::now();phase_counters=counters();
                }
                last=s;
            };
            const auto scan=dc::scan_roots_hashed({root},{},nullptr,telemetry);
#ifdef _WIN32
            const std::string backend="; hash_backend=Windows CNG SHA256";
#else
            const std::string backend="; hash_backend=portable SHA256";
#endif
            result("scan_full_sha256_total",start,before,scan_summary(last)+backend);
            start=clock_type::now();before=counters();
            const auto observations=dc::observe_scan(scan);
            size_t files=0,folders=0;
            for(const auto& v:observations.files)files+=v.size();
            for(const auto& v:observations.folders)folders+=v.size();
            result("build_observations",start,before,"files="+std::to_string(files)+
                "; directories="+std::to_string(folders)+
                "; incomplete_folders="+std::to_string(observations.incomplete_folders)+
                "; hash_candidate_groups="+std::to_string(scan.hash_candidates.size()));
            return scan.complete()?0:3;
        }
        dc::Inventory inv=dc::scan(root);
        result("scan_metadata",start,before,"files="+std::to_string(inv.files.size())+"; dirs="+std::to_string(inv.dirs.size())+"; bytes="+std::to_string(inv.bytes)+"; errors="+std::to_string(inv.errors.size()));
        if(!full)return inv.complete()?0:3;
        if(!inv.complete()){std::cerr<<"Incomplete scan: refuse full equality analysis\n";return 3;}
        start=clock_type::now();before=counters();
        auto dup=dc::deduplicate(inv);
        result("deduplicate_exact",start,before,"duplicate_groups="+std::to_string(dup.groups.size())+"; errors="+std::to_string(dup.errors.size()));
        if(!dup.errors.empty())return 3;
        start=clock_type::now();before=counters();
        auto dir=dc::duplicate_dirs(inv);
        result("duplicate_directories",start,before,"directory_groups="+std::to_string(dir.size()));
        if(overlaps){
            start=clock_type::now();before=counters();
            try{auto x=dc::find_overlapping_dirs(inv,200);
                result("find_overlapping_dirs",start,before,"candidates="+std::to_string(x.total_candidates)+"; truncated="+(x.truncated?"true":"false"));}
            catch(const std::exception& e){result("find_overlapping_dirs",start,before,std::string("BLOCKED: ")+e.what());return 4;}
        }
        if(has_pair){
            start=clock_type::now();before=counters();
            auto pair=dc::evaluate_directory_pair(inv,left,right);
            result("evaluate_directory_pair",start,before,"shared="+std::to_string(pair.shared_file_occurrences));
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<"Benchmark failed: "<<e.what()<<'\n';return 3;}
}
