#include "core.hpp"
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dc {
namespace {
void check_cancel(const std::atomic_bool* cancel) {
    if(cancel&&cancel->load())throw std::runtime_error("Operation canceled");
}
std::wstring path_key(const fs::path& path) {
    auto key=path.lexically_normal().generic_wstring();
    std::transform(key.begin(),key.end(),key.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});
    return key;
}
bool beneath_key(const std::wstring& path,const std::wstring& parent) {
    return parent.empty()||path==parent||
        (path.size()>parent.size()&&path.compare(0,parent.size(),parent)==0&&path[parent.size()]==L'/');
}
fs::path selected_directory(const Inventory& inventory,const fs::path& requested,
                            const std::atomic_bool* cancel) {
    if(!is_inside(requested))throw std::runtime_error("Directory must be within the scanned root");
    fs::path normalized;
    for(const auto& part:requested)
        if(part!=fs::path(".")&&!part.empty())normalized/=part;
    const auto key=path_key(normalized);
    if(beneath_key(key,L".diskcurator-system"))
        throw std::runtime_error("Select an ordinary directory, not the transaction workspace");
    const Folder* found=nullptr;
    for(const auto& folder:inventory.dirs) {
        check_cancel(cancel);
        if(path_key(folder.rel)!=key)continue;
        if(found)throw std::runtime_error("Directory identity is ambiguous in the scan");
        found=&folder;
    }
    if(!found)throw std::runtime_error("Selected directory was not present in the scan: "+utf8(requested));
    return found->rel; // Preserve the actual scanned spelling for relative paths.
}
// Re-read explicitly selected ordinary subtrees when an unrelated part of the
// disk was inaccessible. Missing paths from a partial inventory are never
// treated as evidence that a backup is complete.
void refresh_subtree(Inventory& selected,const fs::path& relative,const Progress& progress,
                     const std::atomic_bool* cancel,const ScanProgressCallback& structured) {
    const auto verify_path=[&] {
        fs::path path=selected.root;
        auto ordinary=[](const fs::path& dir) {
#ifdef _WIN32
            const auto attr=GetFileAttributesW(extended_path(dir).c_str());
            return attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_DIRECTORY) &&
                   !(attr&FILE_ATTRIBUTE_REPARSE_POINT);
#else
            return fs::is_directory(fs::symlink_status(dir));
#endif
        };
        if(!ordinary(path))throw std::runtime_error("Scan root is no longer an ordinary directory");
        for(const auto& component:relative) {
            path/=component;
            if(!ordinary(path))throw std::runtime_error("Selected directory changed or is unavailable: "+utf8(path));
        }
    };
    verify_path();
    auto fresh=scan_hashed(relative.empty()?selected.root:selected.root/relative,progress,cancel,structured,false);
    if(!fresh.complete())throw std::runtime_error("Could not fully read selected directory: "+fresh.errors.front());
    verify_path();
    for(auto& f:fresh.files){
        check_cancel(cancel);f.rel=relative/f.rel;selected.files.push_back(std::move(f));
    }
    for(auto& d:fresh.dirs){
        check_cancel(cancel);d.rel=d.rel.empty()?relative:relative/d.rel;selected.dirs.push_back(std::move(d));
    }
    selected.bytes+=fresh.bytes;
}
struct PairInventory {
    Inventory inventory;
    fs::path left,right;
    ScanProgress progress;
};
PairInventory verify_pair_inventory(const Inventory& observed,const fs::path& a,const fs::path& b,
    bool merging,const Progress& progress,const std::atomic_bool* cancel,const ScanProgressCallback& structured) {
    check_cancel(cancel);
    PairInventory pair;
    pair.left=selected_directory(observed,a,cancel);
    pair.right=selected_directory(observed,b,cancel);
    const auto left_key=path_key(pair.left),right_key=path_key(pair.right);
    if(left_key==right_key)throw std::runtime_error("Choose two different directories");
    if(merging&&(left_key.empty()||right_key.empty()||beneath_key(left_key,right_key)||beneath_key(right_key,left_key)))
        throw std::runtime_error("Merge requires two disjoint ordinary subdirectories");
    Inventory refreshed;
    if(!observed.complete()) {
        refreshed.root=observed.root;
        if(!beneath_key(left_key,right_key))refresh_subtree(refreshed,pair.left,progress,cancel,structured);
        if(!beneath_key(right_key,left_key))refresh_subtree(refreshed,pair.right,progress,cancel,structured);
    }
    const auto& source=observed.complete()?observed:refreshed;
    pair.inventory.root=observed.root;
    pair.progress.root=observed.root;pair.progress.phase=ScanPhase::Analyzing;
    const auto started=std::chrono::steady_clock::now();auto last=started;
    auto report=[&](bool force=false) {
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-last<std::chrono::milliseconds(100))return;
        last=now;pair.progress.elapsed_seconds=std::chrono::duration<double>(now-started).count();
        pair.progress.files_discovered=pair.inventory.files.size();pair.progress.total_bytes=pair.inventory.bytes;
        if(structured)structured(pair.progress);
    };
    if(progress)progress("Preparing only the selected directory pair: "+utf8(pair.left)+" <-> "+utf8(pair.right));
    report(true);
    for(const auto& directory:source.dirs) {
        check_cancel(cancel);const auto key=path_key(directory.rel);
        if(beneath_key(key,left_key)||beneath_key(key,right_key))pair.inventory.dirs.push_back(directory);
        report();
    }
    for(const auto& file:source.files) {
        check_cancel(cancel);const auto key=path_key(file.rel);
        if(beneath_key(key,left_key)||beneath_key(key,right_key)) {
            if(file.rel.empty()||!is_inside(file.rel)||file.sha256.size()!=64||
               file.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)
                throw std::runtime_error("Selected subtree lacks valid full-scan SHA-256 evidence: "+utf8(file.rel));
            pair.inventory.files.push_back(file);pair.inventory.bytes+=file.bytes;
        }
        report();
    }
    // Parent/child evaluations select a union. A shared descendant is copied
    // exactly once, and the original root and relative paths are retained so
    // source/destination execution snapshots keep their original scope.
    std::sort(pair.inventory.files.begin(),pair.inventory.files.end(),[](const File& x,const File& y){return x.rel<y.rel;});
    std::sort(pair.inventory.dirs.begin(),pair.inventory.dirs.end(),[](const Folder& x,const Folder& y){return x.rel<y.rel;});
    check_cancel(cancel);
    const auto forward=[&](const ScanProgress& snapshot) {
        pair.progress=snapshot;if(structured)structured(snapshot);
    };
    const auto verified=deduplicate(pair.inventory,progress,cancel,forward);
    if(!verified.errors.empty())throw std::runtime_error("Selected directory content verification incomplete: "+verified.errors.front());
    check_cancel(cancel);
    pair.progress.phase=ScanPhase::Analyzing;pair.progress.current_file.clear();
    if(structured)structured(pair.progress);
    return pair;
}
void report_complete(ScanProgress state,const ScanProgressCallback& progress) {
    state.phase=ScanPhase::Complete;state.current_file.clear();if(progress)progress(state);
}
}
DirectoryPairReport evaluate_hashed_directory_pair(const Inventory& observed,const fs::path& a,const fs::path& b,
    const Progress& progress,const std::atomic_bool* cancel,const ScanProgressCallback& structured) {
    auto pair=verify_pair_inventory(observed,a,b,false,progress,cancel,structured);
    auto report=evaluate_directory_pair(pair.inventory,pair.left,pair.right);
    check_cancel(cancel);report_complete(pair.progress,structured);return report;
}
MergePlan plan_hashed_directory_merge(const Inventory& observed,const fs::path& a,const fs::path& b,
    const Progress& progress,const std::atomic_bool* cancel,const ScanProgressCallback& structured,MergeMode mode) {
    auto pair=verify_pair_inventory(observed,a,b,true,progress,cancel,structured);
    auto plan=plan_merge_dirs(pair.inventory,pair.left,pair.right,mode);
    check_cancel(cancel);report_complete(pair.progress,structured);return plan;
}
}
