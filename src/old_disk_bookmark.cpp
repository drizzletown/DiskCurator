#include "old_disk_bookmark.hpp"
#include "old_disk_refresh.hpp"
#include <windows.h>
#include <string>

namespace dc {
namespace {
std::wstring directory_key(const std::filesystem::path& root) {
    const auto id=old_disk_directory_id(root);
    return {id.begin(),id.end()};
}
}

unsigned old_disk_step(const std::filesystem::path& root) noexcept {
    try {
        const auto key = directory_key(root);
        if (key.empty()) return 0;
        const auto file = hash_cache_root() / L"old-disk-progress.ini";
        const auto value = GetPrivateProfileIntW(L"Progress", key.c_str(), 0, file.c_str());
        return value <= 5 ? value : 0;
    } catch (...) { return 0; }
}

void save_old_disk_step(const std::filesystem::path& root, unsigned step) noexcept {
    if (step > 5) return;
    try {
        const auto key = directory_key(root);
        if (key.empty()) return;
        const auto file = hash_cache_root() / L"old-disk-progress.ini";
        if (step) std::filesystem::create_directories(file.parent_path());
        else if (!std::filesystem::exists(file)) return;
        const auto value = std::to_wstring(step);
        WritePrivateProfileStringW(L"Progress", key.c_str(), step ? value.c_str() : nullptr, file.c_str());
    } catch (...) { /* An unavailable cache never prevents reviewing the disk. */ }
}
}
