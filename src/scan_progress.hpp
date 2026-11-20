#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace dc {
// Structured, read-only scan telemetry. Enumeration has no honest percentage
// until the complete file list is known. For multi-root scans counters refer
// to the current root, root_index is 1-based, and elapsed_seconds is cumulative.
enum class ScanPhase { Enumerating, Hashing, Verifying, SavingIndex, Analyzing, Complete };
struct ScanProgress {
    ScanPhase phase=ScanPhase::Enumerating;
    std::filesystem::path root,current_file;
    std::uint64_t files_discovered=0,files_completed=0;
    std::uint64_t total_bytes=0,bytes_processed=0,bytes_read=0,errors=0;
    std::size_t root_index=1,root_count=1;
    bool totals_known=false;
    double elapsed_seconds=0;
};
// bytes_read is actual content I/O. bytes_processed is accounted work: the
// unread remainder of a failed file is accounted when its error is recorded.
// files_completed likewise includes failed attempts. Always display errors;
// 100% of work attempted is not a claim that every file was readable or hashed.
// Delivery is throttled by elapsed time, with immediate phase/final snapshots.
using ScanProgressCallback=std::function<void(const ScanProgress&)>;
}
