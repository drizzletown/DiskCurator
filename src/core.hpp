#pragma once
#include "scan_progress.hpp"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dc {
namespace fs = std::filesystem;
using U64 = std::uint64_t;
std::string utf8(const fs::path& p);
fs::path from_utf8(const std::string& s);
std::string human(U64 n);
#ifdef _WIN32
std::wstring extended_path(const fs::path& path);
#endif
std::string lowercase(std::string s);
std::string extension_category(const fs::path& p);
bool glob(const std::wstring& pattern, const std::wstring& text);
bool is_inside(const fs::path& relative);

struct File { fs::path rel; U64 bytes=0; fs::file_time_type mtime{}; int content=-1; std::string sha256; };
struct Folder { fs::path rel; U64 bytes=0; U64 count=0; };
struct Inventory {
    fs::path root;
    std::vector<File> files;
    std::vector<Folder> dirs;
    std::vector<std::string> errors;
    // Disposable cache persistence is independent of content completeness.
    bool index_persisted=false;
    std::string index_error;
    U64 bytes=0;
    bool complete() const { return errors.empty(); }
};
struct InventorySummary {
    U64 file_count=0, directory_count=0, bytes=0;
    U64 directory_errors=0, reparse_skipped=0, other_errors=0;
    U64 error_count() const { return directory_errors + reparse_skipped + other_errors; }
};
// Count ordinary entries without retaining a file list, reading contents or saving a cache.
InventorySummary scan_inventory(const fs::path& root, const ScanProgressCallback& scan_progress={},
                                const std::atomic_bool* cancel=nullptr);
using Progress = std::function<void(const std::string&)>;
Inventory scan(const fs::path& root, const Progress& progress={}, const std::atomic_bool* cancel=nullptr,
               const ScanProgressCallback& scan_progress={});
// Full initial inventory: compute SHA-256 for EVERY regular file and write an
// external, rebuildable content index. No old index is trusted as proof.
Inventory scan_hashed(const fs::path& root, const Progress& progress={}, const std::atomic_bool* cancel=nullptr,
                      const ScanProgressCallback& scan_progress={});
// Multi-volume discovery result. Each volume is an independent scan root.
// Matching SHA-256+size identifies CANDIDATES, not byte-proven duplicates.
// No cross-volume mutation API is exposed: a volume must be explicitly selected.
struct ScopeOccurrence {size_t volume=0, file=0;};
struct ScopeHashGroup {U64 bytes=0; std::string sha256; std::vector<ScopeOccurrence> members;};
struct FullScan {
    std::vector<Inventory> volumes;
    std::vector<std::string> errors;
    std::vector<ScopeHashGroup> hash_candidates;
    U64 total_bytes=0;
    size_t total_files=0;
    bool complete() const {
        if(!errors.empty())return false;
        for(const auto& v:volumes)if(!v.complete())return false;
        return true;
    }
};
fs::path hash_cache_root();
FullScan scan_roots_hashed(const std::vector<fs::path>& roots,
                           const Progress& progress={}, const std::atomic_bool* cancel=nullptr,
                           const ScanProgressCallback& scan_progress={});


struct Duplicates { std::vector<std::vector<size_t>> groups; std::vector<std::string> errors; U64 wasted=0; };
Duplicates deduplicate(Inventory& inv, const Progress& progress={}, const std::atomic_bool* cancel=nullptr,
                       const ScanProgressCallback& scan_progress={});
struct DirGroup { std::vector<fs::path> paths; U64 bytes=0; };
std::vector<DirGroup> duplicate_dirs(const Inventory& inv);

struct DiffRow { std::string state; fs::path rel; U64 left=0,right=0; };
struct Comparison { fs::path left,right; std::vector<DiffRow> rows; size_t identical=0, only_left=0, only_right=0, changed=0; bool left_in_right=false, right_in_left=false; size_t content_overlap=0, left_file_count=0, right_file_count=0; };
Comparison compare_dirs(const Inventory& inv, const fs::path& left, const fs::path& right);
// A directory pair can be examined on demand, regardless of automatic
// recommendation thresholds. Fractions are only defined when their denominator
// is nonzero. Equality and similarity are distinct conclusions.
struct DirectoryPairReport {
    Comparison comparison;
    size_t shared_file_occurrences=0;
    size_t shared_distinct_contents=0;
    size_t same_relative_files=0;
    size_t same_path_different_content=0;
    size_t left_only_files=0, right_only_files=0;
    size_t left_dirs=0, right_dirs=0;
    size_t left_empty_dirs=0, right_empty_dirs=0;
    std::optional<double> smaller_coverage;
    std::optional<double> multiset_jaccard;
    std::optional<double> same_path_fraction;
    bool exact_tree=false;
    bool disjoint_for_merge=false;
};
DirectoryPairReport evaluate_directory_pair(const Inventory& inv, const fs::path& left, const fs::path& right);
// Reuse scan hashes but verify only the selected subtree union. Content reads
// outside the pair are excluded; returned equality is byte-verified.
DirectoryPairReport evaluate_hashed_directory_pair(const Inventory& inv,
    const fs::path& left, const fs::path& right, const Progress& progress={},
    const std::atomic_bool* cancel=nullptr, const ScanProgressCallback& scan_progress={});
// Exhaustive, bounded discovery of directory pairs with at least one
// meaningful byte-verified content overlap; not an automatic merge decision.
// A truncated listing explicitly reports how many candidates were found.
struct OverlapCandidate {
    fs::path left,right;
    size_t shared=0,distinct_shared=0,left_count=0,right_count=0;
    double smaller_coverage=0.0,multiset_jaccard=0.0;
};
struct OverlapListing {
    std::vector<OverlapCandidate> pairs;
    size_t total_candidates=0;
    bool truncated=false;
};
OverlapListing find_overlapping_dirs(const Inventory& inv, size_t max_results=200,
                                   const std::atomic_bool* cancel=nullptr);
struct Relation { fs::path left,right; size_t matched=0, left_count=0, right_count=0; std::string relation; };
// Read-only hints, NEVER a statement that directories are interchangeable.
// Both paths are disjoint, scanned descendants of the chosen single root.
// Source -> destination is a proposed direction, not inferred backup chronology.
struct MergeSuggestion {
    fs::path source, destination;
    size_t source_files=0, destination_files=0; // all regular files, including ignored heuristic evidence
    size_t evidence_source_files=0, evidence_destination_files=0;
    size_t shared_contents=0;       // min-multiplicity overlap of verified meaningful contents
    size_t same_relative_files=0;   // equal relative path AND verified equal bytes
    size_t source_only=0, destination_only=0; // absent relative paths
    size_t changed_at_same_path=0;
    size_t unresolved_collisions=0;  // collisions whose source version is not already retained
    double overlap=0;              // shared / min(source_files, destination_files)
    int confidence=0;              // 3=strong, 2=likely, 1=review; never automatic execution
    bool useful=false;             // candidate direction has content to copy
};
// Requires full stable content indexing. If scan incomplete, refuses to recommend.
// max_pairs limits displayed output. May reject pathological all-to-all searches
// rather than silently report incomplete results as exhaustive.
std::vector<MergeSuggestion> recommend_merges(const Inventory& inv,
    size_t max_pairs=100, const std::atomic_bool* cancel=nullptr);
std::vector<Relation> directory_relations(const Inventory& inv, size_t max_pairs=1000, const std::atomic_bool* cancel=nullptr);

struct Photo { fs::path rel; U64 dhash=0; std::string date; bool exif_date=false; bool valid=false; std::string error; };
Photo probe_photo(const fs::path& abs_path);
struct PhotoPair { fs::path a,b; int distance=0; };
struct PhotoReport { std::vector<Photo> photos; std::vector<PhotoPair> pairs; std::vector<std::string> errors; };
PhotoReport similar_photos(const Inventory& inv, int max_distance=7, const Progress& progress={}, const std::atomic_bool* cancel=nullptr);

// Optional source snapshot is checked again before staging a move. It protects
// previewed file and folder operations from silently moving newly-added data.
struct SnapshotEntry {
    fs::path rel; bool directory=false; U64 bytes=0; fs::file_time_type mtime{};
    bool operator==(const SnapshotEntry& other) const {
        return rel==other.rel && directory==other.directory && bytes==other.bytes && mtime==other.mtime;
    }
};
struct Move {
    fs::path from,to; fs::path verify_against; // all paths relative to root
    fs::path verify_second; // second independent retained keeper for quarantine
    std::vector<SnapshotEntry> expected_source; // empty for internal/manual moves
    std::vector<fs::path> keepers; // complete set of policy-selected retained file paths
    std::string scanned_sha256; // SHA-256 computed by a full initial scan, if available
    struct KeeperProof {fs::path rel; U64 bytes=0; fs::file_time_type mtime{}; std::string sha256;};
    std::vector<KeeperProof> keeper_proofs;
    Move()=default;
    Move(fs::path source, fs::path destination, fs::path compare_against={},
         std::vector<SnapshotEntry> expected={}, fs::path second_keeper={})
      : from(std::move(source)),to(std::move(destination)),
        verify_against(std::move(compare_against)),verify_second(std::move(second_keeper)),
        expected_source(std::move(expected)) {}
};
// All fields can be selected by the user. Conservative defaults do not impose
// a hard-coded two-copy rule. Zero active copies require explicit risk consent;
// this application still uses reversible quarantine, never permanent deletion.
struct RetentionPolicy {
    unsigned min_active_copies=2;
    bool distinct_file_objects=true;  // false counts separate hard-link paths
    bool verify_bytes_before_change=true; // false trusts FULL-SCAN SHA256 + unchanged metadata
    bool allow_zero_active=false; // separately acknowledged high-risk choice
};
struct CleanupSummary {
    size_t duplicate_groups=0;
    size_t groups_at_or_below_minimum=0;
    size_t groups_without_independent_keepers=0;
    U64 planned_bytes=0; // logical size of files to move into reversible quarantine
};
struct Plan {
    std::string name;
    std::vector<Move> moves;
    std::vector<std::string> warnings;
    RetentionPolicy retention{};
    std::optional<CleanupSummary> cleanup_summary{};
};
struct FileRename { fs::path source, new_filename; };
bool valid_file_name(const fs::path& name);
struct RenameOptions { fs::path dir; std::wstring pattern=L"*"; std::wstring find,replace,prefix,suffix; int number_start=0; int digits=3; bool include_files=true, include_dirs=true; std::optional<FileRename> single_file; };
Plan plan_rename(const Inventory& inv, const RenameOptions& opt);
// Merge is additive: the source remains untouched, and the destination never overwrites.
struct CopyTask { fs::path from,to; U64 bytes=0; fs::file_time_type mtime{}; };
struct MergeSkip { fs::path from,to; U64 bytes=0; fs::file_time_type from_time{},to_time{}; };
struct MergePlan {
    fs::path source,destination;
    std::vector<SnapshotEntry> source_snapshot,destination_snapshot;
    std::vector<MergeSkip> skipped;
    std::vector<CopyTask> copies;
    std::vector<fs::path> directories; // paths to create in the selected root
    size_t identical=0;
    size_t conflicts=0; // same relative filename, different verified content
    std::vector<std::string> warnings;
};
MergePlan plan_merge_dirs(const Inventory& inv,const fs::path& source,const fs::path& destination);
MergePlan plan_hashed_directory_merge(const Inventory& inv,
    const fs::path& source, const fs::path& destination, const Progress& progress={},
    const std::atomic_bool* cancel=nullptr, const ScanProgressCallback& scan_progress={});
// One-root proposed directory structure. Suggestions and edits are read-only.
// A mapping with source="." applies only to files directly inside the root.
struct StructureRule {
    fs::path source, target; // target relative to proposed output_base
    std::string reason;
    size_t files=0;
    U64 bytes=0;
};
struct StructureFileRule {
    fs::path source; // exact original file, relative to selected root
    fs::path target; // requested file path relative to output_base
};
struct StructureSuggestion {
    fs::path output_base=L"整理结果";
    std::vector<StructureRule> rules;
    std::vector<StructureFileRule> file_rules; // optional precise overrides, explicitly honored
    std::vector<std::string> warnings;
};
struct StructureMapping {
    fs::path from, to; // exact original file -> destination relative to root
    std::string action; // COPY / REUSE_EXISTING / REUSE_PLANNED
    U64 bytes=0;
    fs::file_time_type source_time{};
    U64 source_fingerprint=0; // change detector; exact copy verified separately
};
struct StructurePlan {
    fs::path output_base;
    std::vector<StructureRule> rules;
    std::vector<SnapshotEntry> root_snapshot;
    std::vector<CopyTask> copies;
    std::vector<MergeSkip> existing_skips;
    std::vector<fs::path> directories;
    std::vector<StructureMapping> mappings;
    std::vector<std::string> warnings;
    size_t renamed_conflicts=0;
    U64 bytes_to_copy=0;
};
StructureSuggestion suggest_structure(const Inventory& inv, const fs::path& output_base=L"整理结果",
                                      const std::atomic_bool* cancel=nullptr);
StructurePlan plan_structure(const Inventory& inv, const StructureSuggestion& proposal);
Plan plan_clean_duplicates(const Inventory& inv, const Duplicates& duplicates,
                           RetentionPolicy policy={});
// User-controlled selection; the configured retention policy is verified at
// planning and execution. SHA-256 alone is never proof of equal file content.
Plan plan_quarantine_selected(const Inventory& inv, const Duplicates& duplicates,
                              const std::vector<fs::path>& selected, RetentionPolicy policy={});
struct RetirementProposal {
    fs::path source_folder;
    Plan quarantine;
    U64 staged_bytes=0; // quarantining DOES NOT immediately free disk space
    std::vector<fs::path> blocked; // insufficient distinct, verified copies OUTSIDE source
};
// Scoped cleanup isolates files according to the configured keeper count and
// independence policy, counting keepers outside the chosen source directory.
RetirementProposal plan_retire_directory(const Inventory& inv, const Duplicates& duplicates,
                                          const fs::path& source_folder, RetentionPolicy policy={});
// Read-only hash-observation planning. Candidate identities are private to the
// planner; the input inventory remains unverified. execute_plan enforces the
// user's retention and byte-recheck policy on the chosen files and keepers.
Plan plan_quarantine_hashed_selected(const Inventory& inv,
    const std::vector<fs::path>& selected, RetentionPolicy policy={});
Plan plan_clean_hashed_duplicates(const Inventory& inv, RetentionPolicy policy={});
RetirementProposal plan_retire_hashed_directory(const Inventory& inv,
    const fs::path& source_folder, RetentionPolicy policy={});
enum class ArchiveMode { ByType, ByYearMonth };
struct ArchiveOptions { ArchiveMode mode=ArchiveMode::ByType; fs::path destination=L"Organized"; bool recursive=true; bool use_mtime_fallback=true; };
Plan plan_archive(const Inventory& inv, const ArchiveOptions& opt, const Progress& progress={}, const std::atomic_bool* cancel=nullptr);
struct Result { bool success=false; std::string message; std::string transaction; };
Result execute_structure_plan(const fs::path& root, const StructurePlan& plan);
Result execute_plan(const fs::path& root, const Plan& plan);
Result execute_merge_plan(const fs::path& root, const MergePlan& plan);
Result undo_latest(const fs::path& root);
std::vector<std::string> recover_incomplete(const fs::path& root);
struct HistoryItem { std::string transaction,label,status; size_t items=0; };
std::vector<HistoryItem> history(const fs::path& root);
std::vector<std::pair<std::string,std::vector<std::string>>> backup_safety(const Inventory& inv, const Duplicates& duplicates);
std::string csv_quote(const std::string& s);
}
