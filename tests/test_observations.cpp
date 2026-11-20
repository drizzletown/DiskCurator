#include "observations.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
static void put(const fs::path& path,const char* contents){
    fs::create_directories(path.parent_path());
    std::ofstream stream(path,std::ios::binary);stream<<contents;
    if(!stream)throw std::runtime_error("fixture write failed");
}
static size_t directory_index(const dc::Inventory& inv,const fs::path& rel){
    for(size_t i=0;i<inv.dirs.size();++i)if(inv.dirs[i].rel==rel)return i;
    throw std::runtime_error("folder missing: "+rel.string());
}
int main(){
    const auto base=fs::temp_directory_path()/("dc_observations_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        put(base/"a"/"old"/"file.jpg","same");
        put(base/"b"/"new"/"file.jpg","same");
        fs::create_directories(base/"a"/"old"/"empty");
        fs::create_directories(base/"b"/"new"/"empty");
        put(base/"b"/"altered"/"file.jpg","different");
        auto report=dc::scan_roots_hashed({base/"a",base/"b"});
        assert(report.complete());
        auto obs=dc::observe_scan(report);
        assert(obs.files.size()==2&&obs.folders.size()==2);
        assert(obs.hashed_files==3&&obs.incomplete_folders==0);
        assert(obs.files[0][0].hash_candidate_copies==2);
        bool found_same=false,found_empty=false;
        for(size_t v=0;v<2;++v)for(size_t i=0;i<report.volumes[v].dirs.size();++i){
            const auto& folder=report.volumes[v].dirs[i];
            if(folder.rel=="old"||folder.rel=="new"){
                assert(obs.folders[v][i].tree_candidate_copies==2);found_same=true;
            }
            if(folder.rel=="old/empty"||folder.rel=="new/empty"){
                assert(obs.folders[v][i].tree_candidate_copies>=2);found_empty=true;
            }
        }
        assert(found_same&&found_empty);
        auto bad=report;
        bad.volumes[0].files[0].sha256.clear();
        auto partial=dc::observe_scan(bad);
        assert(partial.incomplete_folders>0);
        assert(partial.files[0][0].hash_candidate_copies==0);
        assert(partial.folders[0][directory_index(bad.volumes[0],"old")].tree_candidate_copies==0);
        // A skipped unreadable directory is not represented by a missing file
        // hash. Do not claim tree copies for any branch of that partial root.
        auto inaccessible=report;
        inaccessible.volumes[0].errors.push_back("Cannot list old/hidden: Access denied");
        auto uncertain=dc::observe_scan(inaccessible);
        assert(uncertain.hashed_files==obs.hashed_files);
        assert(uncertain.files[0][0].hash_candidate_copies==2);
        for(const auto& folder:uncertain.folders[0])assert(folder.tree_candidate_copies==0);
        assert(uncertain.incomplete_folders==inaccessible.volumes[0].dirs.size());
        assert(uncertain.folders[1][directory_index(inaccessible.volumes[1],"new")].tree_candidate_copies==1);
        // Reject an internally incomplete hierarchy, rather than silently
        // dropping the orphan file from a root-directory signature.
        auto orphan=report;
        auto& dirs=orphan.volumes[0].dirs;
        dirs.erase(dirs.begin()+static_cast<std::ptrdiff_t>(directory_index(orphan.volumes[0],"old")));
        auto malformed=dc::observe_scan(orphan);
        for(const auto& folder:malformed.folders[0])assert(folder.tree_candidate_copies==0);
        // Many equal subtrees exercise reference indexing and shared folder
        // signature counts without any file reads or byte-verified identities.
        dc::FullScan synthetic;synthetic.volumes.resize(1);
        auto& volume=synthetic.volumes.front();volume.root=base/"nonexistent";
        constexpr size_t directory_count=256,files_per_directory=16;
        volume.dirs.push_back({{},136*directory_count,directory_count*files_per_directory});
        for(size_t d=0;d<directory_count;++d){
            const auto parent=fs::path("d"+std::to_string(d));
            volume.dirs.push_back({parent,136,files_per_directory});
            for(size_t f=0;f<files_per_directory;++f)
                volume.files.push_back({parent/("f"+std::to_string(f)),f+1,{},-1,std::string(64,'a')});
        }
        const auto large=dc::observe_scan(synthetic);
        assert(large.hashed_files==directory_count*files_per_directory&&large.incomplete_folders==0);
        for(const auto& file:large.files[0])assert(file.hash_candidate_copies==directory_count);
        for(size_t d=1;d<large.folders[0].size();++d)assert(large.folders[0][d].tree_candidate_copies==directory_count);
        assert(large.folders[0][0].tree_candidate_copies==1);
        for(const auto& file:volume.files)assert(file.content==-1);
        // Removing all source data cannot alter a view of an existing index.
        fs::remove_all(base);
        auto offline=dc::observe_scan(report);
        assert(offline.files[0][0].hash_candidate_copies==obs.files[0][0].hash_candidate_copies);
        assert(offline.folders[0][directory_index(report.volumes[0],"old")].tree_candidate_copies==2);
        std::atomic_bool cancel{true};
        bool threw=false;try{dc::observe_scan(report,&cancel);}catch(const std::exception&){threw=true;}
        assert(threw);
        fs::remove_all(base);std::cout<<"observation perspectives tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";fs::remove_all(base);return 1;}
}
