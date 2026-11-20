#pragma once
#include "core.hpp"
#include <atomic>
#include <cstddef>
#include <vector>

namespace dc {
// Read-only views derived from a completed hashed scan. Never reads source
// files and never turns SHA-256 matches into byte-verified identities.
struct FileObservation {
    size_t volume=0, file=0;
    // Includes the displayed file itself. 0 means hashing did not complete.
    size_t hash_candidate_copies=0;
};
struct FolderObservation {
    size_t volume=0, folder=0;
    // Includes this folder. 0 means a descendant lacks a hash or the root's
    // scan has errors, so the directory tree cannot be considered complete.
    size_t tree_candidate_copies=0;
};
struct Observations {
    std::vector<std::vector<FileObservation>> files;
    std::vector<std::vector<FolderObservation>> folders;
    size_t hashed_files=0;
    size_t incomplete_folders=0;
};
Observations observe_scan(const FullScan& scan, const std::atomic_bool* cancel=nullptr);
}
