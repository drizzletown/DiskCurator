#pragma once
#include "core.hpp"

namespace dc {
// Reuse only this session's candidate observations. A write plan must still
// verify the selected files. Explicit full scans always read all file bytes.
Inventory refresh_old_disk(const Inventory& prior,const std::vector<fs::path>& changed_paths,
                           const Progress& progress={},const std::atomic_bool* cancel=nullptr,
                           const ScanProgressCallback& structured={});
std::string old_disk_directory_id(const fs::path& root);
}
