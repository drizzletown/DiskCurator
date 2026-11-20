#include "core.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace dc {
namespace {
struct HashKey {
    U64 bytes;
    std::string_view digest;
    bool operator==(const HashKey& other) const {
        return bytes==other.bytes&&digest==other.digest;
    }
};
struct HashKeyHasher {
    size_t operator()(const HashKey& key) const {
        const auto a=std::hash<U64>{}(key.bytes);
        const auto b=std::hash<std::string_view>{}(key.digest);
        return a^(b+size_t(0x9e3779b9)+(a<<6)+(a>>2));
    }
};
struct PlanningSnapshot {Inventory inventory;Duplicates candidates;};
PlanningSnapshot planning_snapshot(const Inventory& observed) {
    if(!observed.complete())
        throw std::runtime_error("Hash-based planning requires a complete scan; review scan issues first");
    PlanningSnapshot snapshot{observed,{}};
    struct Group {int identity;size_t first;size_t duplicates=std::numeric_limits<size_t>::max();};
    std::unordered_map<HashKey,Group,HashKeyHasher> groups;
    int next=0;
    for(size_t i=0;i<snapshot.inventory.files.size();++i) {
        auto& file=snapshot.inventory.files[i];
        if(file.sha256.size()!=64||file.sha256.find_first_not_of("0123456789abcdef")!=std::string::npos)
            throw std::runtime_error("Hash-based planning requires a valid full-file SHA-256 for every file");
        if(file.rel.empty()||!is_inside(file.rel))
            throw std::runtime_error("Hash-based planning encountered an unsafe file path");
        const auto inserted=groups.emplace(HashKey{file.bytes,file.sha256},Group{next,i});
        auto& group=inserted.first->second;
        if(inserted.second) {
            if(next==std::numeric_limits<int>::max())throw std::runtime_error("Too many content groups for planning");
            ++next;
        }else {
            if(group.duplicates==std::numeric_limits<size_t>::max()) {
                group.duplicates=snapshot.candidates.groups.size();
                snapshot.candidates.groups.push_back({group.first});
            }
            snapshot.candidates.groups[group.duplicates].push_back(i);
            snapshot.candidates.wasted+=file.bytes;
        }
        // These IDs exist only in this private adapter for existing planners.
        // They are never copied into the user's observation inventory and are
        // not exposed to directory-equality or byte-verification APIs.
        file.content=group.identity;
    }
    return snapshot;
}
void annotate_hash_plan(Plan& plan) {
    // execute_plan and validate_plan currently use this prefix to apply the
    // retention guard. Keep it intact; clarify the evidence in preview text.
    plan.name+=" (scan-hash candidates)";
    for(auto& warning:plan.warnings) {
        const std::string old="active byte-verified keeper paths";
        auto at=warning.find(old);
        if(at!=std::string::npos)warning.replace(at,old.size(),"active candidate keeper paths");
        const std::string retirement="policy-approved, byte-verified keepers";
        at=warning.find(retirement);
        if(at!=std::string::npos)warning.replace(at,retirement.size(),"candidate keepers selected by policy");
    }
    plan.warnings.insert(plan.warnings.begin(),plan.retention.verify_bytes_before_change?
        "Preview uses full-scan SHA-256 candidates only. Actual source and retained-copy bytes will be compared during execution according to the selected keeper policy.":
        "Preview uses full-scan SHA-256 candidates only. You selected metadata/hash-only execution: file contents will NOT be re-read before quarantine.");
}
}
Plan plan_quarantine_hashed_selected(const Inventory& inv,const std::vector<fs::path>& selected,RetentionPolicy policy) {
    auto snapshot=planning_snapshot(inv);
    auto plan=plan_quarantine_selected(snapshot.inventory,snapshot.candidates,selected,policy);
    annotate_hash_plan(plan);return plan;
}
Plan plan_clean_hashed_duplicates(const Inventory& inv,RetentionPolicy policy) {
    auto snapshot=planning_snapshot(inv);
    auto plan=plan_clean_duplicates(snapshot.inventory,snapshot.candidates,policy);
    annotate_hash_plan(plan);return plan;
}
RetirementProposal plan_retire_hashed_directory(const Inventory& inv,const fs::path& source_folder,RetentionPolicy policy) {
    auto snapshot=planning_snapshot(inv);
    auto proposal=plan_retire_directory(snapshot.inventory,snapshot.candidates,source_folder,policy);
    annotate_hash_plan(proposal.quarantine);return proposal;
}
}
