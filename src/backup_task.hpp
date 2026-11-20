#pragma once
#include "core.hpp"

namespace dc {
struct BackupComparison {
    fs::path source,target;
    // File states: ONLY_SOURCE, ONLY_TARGET, CHANGED, IDENTICAL. Directory-only
    // rows include empty directories. TYPE_CONFLICT and UNKNOWN require review.
    std::vector<DiffRow> rows;
    std::vector<std::string> errors;
    // Populated only for two complete, disjoint subtrees with a safe common
    // parent. This inventory covers the two selected trees, never their siblings.
    Inventory merge_inventory;
    fs::path source_relative,target_relative;
};
BackupComparison compare_backups(const fs::path& source,const fs::path& target,
    const Progress& progress={},const std::atomic_bool* cancel=nullptr,
    const ScanProgressCallback& scan_progress={});
}
