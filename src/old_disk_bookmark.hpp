#pragma once
#include <filesystem>

namespace dc {
// A navigation hint for this Windows directory. A new scan is still required.
// Step zero clears the hint. No file names, decisions or write plans are saved.
unsigned old_disk_step(const std::filesystem::path& root) noexcept;
void save_old_disk_step(const std::filesystem::path& root, unsigned step) noexcept;
}
