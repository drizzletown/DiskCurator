#pragma once
#include "core.hpp"

namespace dc {
// Observations from the scan only. Relative paths and content hashes never
// establish backup chronology; a selected pair can be verified separately.
struct OldDiskPair {
    fs::path left, right;
    size_t left_files=0, right_files=0, shared_contents=0;
    size_t same_path_files=0, conflicting_paths=0, left_only_paths=0, right_only_paths=0;
    unsigned coverage_percent=0;  // Distinct informative contents in the smaller side.
    bool same_tree=false;         // Matching files and directories, including empty directories.
};
struct FolderClues {
    std::map<std::string,size_t> categories;
    std::optional<fs::file_time_type> earliest, latest;
};
struct OldDiskOverview {
    bool complete=false, discovery_limited=false;
    size_t duplicate_groups=0, duplicate_files=0, unread_files=0;
    U64 redundant_bytes=0;        // Logical estimate at one copy per hash group.
    std::vector<Folder> largest_folders;
    std::map<fs::path,FolderClues> folder_clues;
    // In an incomplete inventory, pairs contain only left, right and the
    // observed shared_contents. All other pair metrics remain unset (zero).
    std::vector<OldDiskPair> versions;
};
OldDiskOverview describe_old_disk_folders(const Inventory& inv,const std::atomic_bool* cancel=nullptr);
OldDiskOverview inspect_old_disk(const Inventory& inv,const std::atomic_bool* cancel=nullptr);
}
