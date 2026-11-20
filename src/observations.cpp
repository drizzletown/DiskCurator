#include "observations.hpp"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace dc {
// Directory signatures are read-only candidates, never byte-verified proof.
std::string sha256_text(const std::string& text);
namespace {
void check_cancel(const std::atomic_bool* cancelled) {
    if(cancelled && cancelled->load()) throw std::runtime_error("Observation canceled");
}
void add_field(std::string& out, const std::string& data) {
    out+=std::to_string(data.size());out+=':';out+=data;
}
struct ContentKey {
    U64 bytes;
    std::string_view digest;
    bool operator==(const ContentKey& other) const {
        return bytes==other.bytes && digest==other.digest;
    }
};
struct ContentHash {
    size_t operator()(const ContentKey& key) const {
        const auto a=std::hash<U64>{}(key.bytes);
        const auto b=std::hash<std::string_view>{}(key.digest);
        return a^(b+size_t(0x9e3779b9)+(a<<6)+(a>>2));
    }
};
struct FolderNode {
    const fs::path* path=nullptr;
    std::vector<size_t> files,children;
    size_t depth=0;
    bool complete=true;
    std::string digest;
};
}
Observations observe_scan(const FullScan& scan, const std::atomic_bool* cancelled) {
    check_cancel(cancelled);
    Observations out;
    out.files.resize(scan.volumes.size());out.folders.resize(scan.volumes.size());
    // String views borrow the immutable inventory digests for this call. A
    // million files no longer create a second million 64-character strings.
    std::unordered_map<ContentKey,size_t,ContentHash> content_counts;
    for(size_t v=0;v<scan.volumes.size();++v) {
        const auto& inv=scan.volumes[v];
        out.files[v].reserve(inv.files.size());
        for(size_t i=0;i<inv.files.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            const auto& file=inv.files[i];
            if(file.sha256.size()==64) {
                ++content_counts[{file.bytes,file.sha256}];++out.hashed_files;
            }
            out.files[v].push_back({v,i,0});
        }
    }
    for(size_t v=0;v<scan.volumes.size();++v) {
        const auto& inv=scan.volumes[v];
        for(size_t i=0;i<inv.files.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            const auto& f=inv.files[i];
            if(f.sha256.size()==64)
                out.files[v][i].hash_candidate_copies=content_counts.at({f.bytes,f.sha256});
        }
    }
    content_counts.clear();content_counts.rehash(0);
    // Intern directory signatures once across roots, keeping only a small ID
    // per displayed row. This avoids retaining a digest copy for every folder.
    std::unordered_map<std::string,size_t> folder_ids;
    std::vector<size_t> folder_counts;
    for(size_t v=0;v<scan.volumes.size();++v) {
        check_cancel(cancelled);
        const auto& inv=scan.volumes[v];
        auto& dst=out.folders[v];dst.reserve(inv.dirs.size());
        for(size_t i=0;i<inv.dirs.size();++i)dst.push_back({v,i,0});
        // Errors are currently unstructured strings. We cannot safely infer
        // which subtree an unreadable/skipped object belongs to. File hashes
        // already obtained remain observable; no directory in this root is
        // advertised as a complete tree until a complete rescan succeeds.
        if(!inv.complete()) {
            out.incomplete_folders+=inv.dirs.size();
            continue;
        }
        const fs::path root_path;
        std::vector<FolderNode> nodes;nodes.reserve(inv.dirs.size()+1);
        std::unordered_map<fs::path,size_t> positions;
        positions.reserve(inv.dirs.size()+1);
        bool valid_tree=true;
        for(const auto& dir:inv.dirs) {
            if((nodes.size()&255)==0)check_cancel(cancelled);
            const auto id=nodes.size();
            if(!positions.emplace(dir.rel,id).second){valid_tree=false;continue;}
            FolderNode n;n.path=&dir.rel;
            n.depth=static_cast<size_t>(std::distance(dir.rel.begin(),dir.rel.end()));
            nodes.push_back(std::move(n));
        }
        if(positions.find(root_path)==positions.end()) {
            positions.emplace(root_path,nodes.size());
            FolderNode n;n.path=&root_path;nodes.push_back(std::move(n));
        }
        for(size_t i=0;i<nodes.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            const auto& p=*nodes[i].path;
            if(p.empty())continue;
            const auto parent=positions.find(p.parent_path());
            if(parent==positions.end()){valid_tree=false;continue;}
            nodes[parent->second].children.push_back(i);
        }
        for(size_t i=0;i<inv.files.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            const auto& file=inv.files[i];
            const auto parent=positions.find(file.rel.parent_path());
            if(parent==positions.end()){valid_tree=false;continue;}
            auto& n=nodes[parent->second];
            if(file.sha256.size()!=64)n.complete=false;
            n.files.push_back(i);
        }
        if(!valid_tree) {
            out.incomplete_folders+=inv.dirs.size();
            continue;
        }
        std::vector<size_t> order;order.reserve(nodes.size());
        for(size_t i=0;i<nodes.size();++i)order.push_back(i);
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b) {
            return nodes[a].depth>nodes[b].depth;
        });
        // Store references while building the graph, then encode just the
        // current directory. Old code kept every file's encoded payload alive
        // for the whole volume. Depth is computed once, not in each comparison.
        for(const auto id:order) {
            check_cancel(cancelled);
            auto& n=nodes[id];
            for(const auto child:n.children)if(nodes[child].digest.empty())n.complete=false;
            if(!n.complete)continue;
            std::vector<std::pair<std::string,std::string>> entries;
            entries.reserve(n.files.size()+n.children.size());
            for(const auto index:n.files) {
                if((entries.size()&255)==0)check_cancel(cancelled);
                const auto& file=inv.files[index];
                std::string payload;
                add_field(payload,utf8(file.rel.filename()));
                add_field(payload,std::to_string(file.bytes));
                add_field(payload,file.sha256);
                entries.emplace_back("F",std::move(payload));
            }
            for(const auto child:n.children) {
                if((entries.size()&255)==0)check_cancel(cancelled);
                std::string payload;
                add_field(payload,utf8(nodes[child].path->filename()));
                add_field(payload,nodes[child].digest);
                entries.emplace_back("D",std::move(payload));
            }
            std::sort(entries.begin(),entries.end());
            std::string payload="DiskCurator-directory-v1";
            size_t encoded=0;
            for(const auto& entry:entries) {
                if((encoded++&255)==0)check_cancel(cancelled);
                add_field(payload,entry.first);add_field(payload,entry.second);
            }
            n.digest=sha256_text(payload);
        }
        for(size_t i=0;i<inv.dirs.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            const auto& digest=nodes[positions.at(inv.dirs[i].rel)].digest;
            if(digest.empty()){++out.incomplete_folders;continue;}
            const auto inserted=folder_ids.emplace(digest,folder_counts.size());
            if(inserted.second)folder_counts.push_back(0);
            const auto id=inserted.first->second;
            ++folder_counts[id];
            // Internal temporary ID + 1. Replaced with its count below.
            dst[i].tree_candidate_copies=id+1;
        }
    }
    for(auto& volume:out.folders) {
        for(size_t i=0;i<volume.size();++i) {
            if((i&255)==0)check_cancel(cancelled);
            auto& count=volume[i].tree_candidate_copies;
            if(count)count=folder_counts.at(count-1);
        }
    }
    check_cancel(cancelled);
    return out;
}
}
