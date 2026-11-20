// Reproducible, memory-only observation scaling. Does not touch source files.
#include "observations.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#ifndef _WIN32
#include <sys/resource.h>
#endif
int main(int argc,char** argv){
    const size_t folders=argc>1?static_cast<size_t>(std::stoull(argv[1])):10000;
    const size_t per_folder=argc>2?static_cast<size_t>(std::stoull(argv[2])):50;
    dc::FullScan scan;scan.volumes.resize(1);auto& inv=scan.volumes.front();
    inv.root="nonexistent-observation-benchmark-root";
    inv.dirs.push_back({{},0,0});
    inv.files.reserve(folders*per_folder);inv.dirs.reserve(folders+1);
    for(size_t d=0;d<folders;++d){
        const auto dir=dc::fs::path("folder-"+std::to_string(d));
        inv.dirs.push_back({dir,per_folder*4096,per_folder});
        for(size_t f=0;f<per_folder;++f){
            auto digest=std::string(64,'a');
            // Distinct content/size per position, identical across folders.
            digest.replace(0,std::to_string(f).size(),std::to_string(f));
            inv.files.push_back({dir/("file-"+std::to_string(f)+".bin"),4096+f,{},-1,std::move(digest)});
        }
    }
    const auto started=std::chrono::steady_clock::now();
    const auto observed=dc::observe_scan(scan);
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
    if(observed.hashed_files!=folders*per_folder)return 2;
    for(const auto& f:observed.files[0])if(f.hash_candidate_copies!=folders)return 3;
    for(size_t i=1;i<observed.folders[0].size();++i)if(observed.folders[0][i].tree_candidate_copies!=folders)return 4;
    std::cout<<"files="<<inv.files.size()<<" folders="<<inv.dirs.size()<<" observation_seconds="<<elapsed;
#ifndef _WIN32
    struct rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0)std::cout<<" peak_rss_kib="<<usage.ru_maxrss;
#endif
    std::cout<<"\n";
}
