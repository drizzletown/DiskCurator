#include "old_disk.hpp"
#include <algorithm>
#include <cwctype>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dc {
namespace {
constexpr size_t none=std::numeric_limits<size_t>::max();
constexpr size_t max_pair_visits=2000000, max_pairs=100000, max_detail_visits=2500000;
void check_cancel(const std::atomic_bool* cancel){
    if(cancel&&cancel->load())throw std::runtime_error("Operation canceled");
}
bool informative(const File& file){
    if(file.bytes==0)return false;
    auto name=file.rel.filename().wstring();
    std::transform(name.begin(),name.end(),name.begin(),std::towlower);
    if(name==L"thumbs.db"||name==L"desktop.ini"||name==L".ds_store"||name==L".gitignore"||
       name==L".gitkeep"||name==L".gitattributes"||name==L".editorconfig")return false;
    const auto stem=name.substr(0,name.find(L'.'));
    return stem!=L"license"&&stem!=L"licence"&&stem!=L"copying";
}
using Content=std::pair<U64,std::string_view>;
struct Group {size_t first=none,count=0;};
struct RelativeLess {
    bool operator()(const fs::path& a,const fs::path& b)const {
#ifdef _WIN32
        return CompareStringOrdinal(a.c_str(),int(a.native().size()),b.c_str(),int(b.native().size()),TRUE)==CSTR_LESS_THAN;
#else
        return a<b;
#endif
    }
};
struct FolderRange {
    fs::path path;
    size_t parent=none,end=0,distinct=0;
};
// Files in each subtree occupy one continuous range in files_by_folder.
// This avoids keeping a separate list of descendants for every ancestor.
struct DirectoryIndex {
    std::map<fs::path,size_t> ids;
    std::vector<FolderRange> folders;
    std::vector<size_t> file_parent,files_by_folder,offset;
    DirectoryIndex(const Inventory& inv,const std::atomic_bool* cancel){
        for(const auto& dir:inv.dirs){check_cancel(cancel);if(!dir.rel.empty())ids.emplace(dir.rel,0);}
        for(auto& [path,index]:ids){index=folders.size();folders.push_back({path,none,index+1,0});}
        for(size_t i=folders.size();i-->0;){
            auto& folder=folders[i];
            const auto it=ids.find(folder.path.parent_path());
            if(it!=ids.end()){
                folder.parent=it->second;
                auto& parent=folders[it->second];
                parent.end=std::max(parent.end,folder.end);
            }
        }
        offset.resize(folders.size()+1,0);
        for(const auto& f:inv.files){
            check_cancel(cancel);
            const auto it=ids.find(f.rel.parent_path());
            const size_t parent=it==ids.end()?none:it->second;
            file_parent.push_back(parent);
            if(parent!=none)++offset[parent+1];
        }
        std::partial_sum(offset.begin(),offset.end(),offset.begin());
        files_by_folder.resize(offset.back());
        auto next=offset;
        for(size_t i=0;i<file_parent.size();++i)if(file_parent[i]!=none)
            files_by_folder[next[file_parent[i]]++]=i;
    }
    size_t file_count(size_t id)const{return offset[folders[id].end]-offset[id];}
    bool contains(size_t parent,size_t child)const{return parent<=child&&child<folders[parent].end;}
};
struct Match {size_t left,right,shared;};
double coverage(const DirectoryIndex& tree,const Match& m){
    return double(m.shared)/double(std::min(tree.folders[m.left].distinct,tree.folders[m.right].distinct));
}
bool overlaps(const DirectoryIndex& tree,size_t a,size_t b){return tree.contains(a,b)||tree.contains(b,a);}
bool same_branch_pair(const DirectoryIndex& tree,const Match& a,const Match& b){
    return (overlaps(tree,a.left,b.left)&&overlaps(tree,a.right,b.right))||
           (overlaps(tree,a.left,b.right)&&overlaps(tree,a.right,b.left));
}
std::optional<OldDiskPair> describe(const Inventory& inv,const DirectoryIndex& tree,const Match& match,
                     const std::vector<size_t>& content_ids,const std::vector<bool>& widespread,bool complete,
                     const std::atomic_bool* cancel){
    OldDiskPair out;
    const auto& a=tree.folders[match.left];
    const auto& b=tree.folders[match.right];
    out.left=a.path;out.right=b.path;
    out.left_files=tree.file_count(match.left);out.right_files=tree.file_count(match.right);
    std::set<size_t> left_contents,shared;
    std::map<fs::path,size_t,RelativeLess> left;
    std::set<fs::path,RelativeLess> right;
    for(size_t i=tree.offset[match.left];i<tree.offset[a.end];++i){
        check_cancel(cancel);
        const size_t fi=tree.files_by_folder[i];
        if(content_ids[fi]!=none)left_contents.insert(content_ids[fi]);
        if(complete&&!left.emplace(inv.files[fi].rel.lexically_relative(a.path),fi).second)return {};
    }
    for(size_t i=tree.offset[match.right];i<tree.offset[b.end];++i){
        check_cancel(cancel);
        const auto fi=tree.files_by_folder[i];
        const auto& f=inv.files[fi];
        if(left_contents.count(content_ids[fi]))shared.insert(content_ids[fi]);
        if(!complete)continue;
        const auto rel=f.rel.lexically_relative(b.path);
        if(!right.insert(rel).second)return {};
        const auto same=left.find(rel);
        if(same==left.end()){++out.right_only_paths;continue;}
        const auto& former=inv.files[same->second];
        if(former.bytes==f.bytes&&former.sha256==f.sha256)++out.same_path_files;
        else ++out.conflicting_paths;
    }
    out.shared_contents=shared.size();
    if(!complete){
        // An unreadable sibling must not hide positive matches elsewhere. The
        // absence of a file in an incomplete inventory cannot prove a difference.
        // Yields only two locations and a lower bound on their common contents.
        if(shared.size()<2)return {};
        if(shared.size()<4&&std::all_of(shared.begin(),shared.end(),[&](size_t id){return widespread[id];}))return {};
        out.left_files=out.right_files=0;
        return out;
    }
    out.coverage_percent=unsigned(100.0*shared.size()/std::min(a.distinct,b.distinct)+.5);
    out.left_only_paths=out.left_files-out.same_path_files-out.conflicting_paths;
    out.same_tree=out.same_path_files==out.left_files&&out.same_path_files==out.right_files&&
                  a.end-match.left==b.end-match.right;
    if(out.same_tree){
        std::set<fs::path,RelativeLess> dirs;
        for(size_t i=match.left+1;i<a.end;++i){
            check_cancel(cancel);
            if(!dirs.insert(tree.folders[i].path.lexically_relative(a.path)).second)return {};
        }
        for(size_t i=match.right+1;i<b.end;++i){
            check_cancel(cancel);
            if(!dirs.erase(tree.folders[i].path.lexically_relative(b.path))){out.same_tree=false;break;}
        }
    }
    // A few standard files shared by otherwise unrelated folders do not
    // establish lineage. Multiple entire snapshots can also be widespread:
    // retain those when several distinct contents agree across both sides.
    if(std::all_of(shared.begin(),shared.end(),[&](size_t id){return widespread[id];})){
        const double balance=double(shared.size())/std::max(a.distinct,b.distinct);
        if(shared.size()<3||(!out.same_tree&&(shared.size()<4||balance<0.8)))return {};
    }
    return out;
}
} // namespace

OldDiskOverview describe_old_disk_folders(const Inventory& inv,const std::atomic_bool* cancel){
    check_cancel(cancel);
    OldDiskOverview out;
    for(const auto& dir:inv.dirs){
        check_cancel(cancel);
        if(!dir.rel.empty()&&dir.rel.parent_path().empty())out.largest_folders.push_back(dir);
    }
    const size_t limit=std::min<size_t>(8,out.largest_folders.size());
    std::partial_sort(out.largest_folders.begin(),out.largest_folders.begin()+limit,out.largest_folders.end(),
        [](const Folder& a,const Folder& b){return a.bytes!=b.bytes?a.bytes>b.bytes:a.rel<b.rel;});
    out.largest_folders.resize(limit);
    for(const auto& dir:out.largest_folders)out.folder_clues.try_emplace(dir.rel);
    for(const auto& file:inv.files){
        check_cancel(cancel);
        if(file.rel.empty())continue;
        auto folder=out.folder_clues.find(*file.rel.begin());
        if(folder==out.folder_clues.end())continue;
        auto& clues=folder->second;
        ++clues.categories[extension_category(file.rel)];
        if(!clues.earliest||file.mtime<*clues.earliest)clues.earliest=file.mtime;
        if(!clues.latest||file.mtime>*clues.latest)clues.latest=file.mtime;
    }
    out.complete=inv.complete();
    return out;
}

OldDiskOverview inspect_old_disk(const Inventory& inv,const std::atomic_bool* cancel){
    auto out=describe_old_disk_folders(inv,cancel);
    // One linked index per file; distinct files need no individual heap-allocated member list.
    std::map<Content,Group> groups;
    std::vector<size_t> next_file(inv.files.size(),none);
    for(size_t i=0;i<inv.files.size();++i){
        check_cancel(cancel);
        const auto& f=inv.files[i];
        if(f.sha256.empty()){++out.unread_files;continue;}
        auto& group=groups[{f.bytes,f.sha256}];
        next_file[i]=group.first;group.first=i;++group.count;
    }
    for(const auto& [key,group]:groups){
        check_cancel(cancel);
        if(group.count<2)continue;
        ++out.duplicate_groups;out.duplicate_files+=group.count;
        out.redundant_bytes+=(group.count-1)*key.first;
    }
    out.complete=inv.complete()&&out.unread_files==0;
    if(groups.empty())return out;

    DirectoryIndex tree(inv,cancel);
    std::vector<size_t> content_ids(inv.files.size(),none);
    std::vector<size_t> last_content(tree.folders.size(),none),last_direct(tree.folders.size(),none);
    std::vector<size_t> folders,direct_folders;
    std::vector<bool> widespread(groups.size()+1,false);
    std::map<std::pair<size_t,size_t>,size_t> pairs;
    size_t ordinal=0,visits=0;
    for(const auto& [key,group]:groups){
        check_cancel(cancel);++ordinal;
        if(!key.first)continue;
        folders.clear();direct_folders.clear();
        for(size_t fi=group.first;fi!=none;fi=next_file[fi]){
            check_cancel(cancel);
            auto id=tree.file_parent[fi];
            if(id==none||!informative(inv.files[fi]))continue;
            content_ids[fi]=ordinal;
            if(last_direct[id]!=ordinal){last_direct[id]=ordinal;direct_folders.push_back(id);}
            for(;id!=none&&last_content[id]!=ordinal;id=tree.folders[id].parent){
                last_content[id]=ordinal;folders.push_back(id);
            }
        }
        for(size_t id:folders)++tree.folders[id].distinct;
        if(direct_folders.size()<2)continue;
        widespread[ordinal]=direct_folders.size()>12;
        if(visits>=max_pair_visits){out.discovery_limited=true;continue;}
        // For a hash present throughout a disk, seed neighbouring folders only.
        // Ranking later counts every shared informative hash in the shortlisted
        // pair. Thus twenty dated backups can corroborate one another while a
        // template propagated into thousands of folders cannot create O(n²) pairs.
        const bool sample=folders.size()>128;
        auto& seeds=sample?direct_folders:folders;
        if(sample){out.discovery_limited=true;std::sort(seeds.begin(),seeds.end());}
        for(size_t i=0;i<seeds.size()&&visits<max_pair_visits;++i){
            check_cancel(cancel);
            const size_t end=sample?std::min(seeds.size(),i+3):seeds.size();
            for(size_t j=i+1;j<end;++j){
                const auto left=std::min(seeds[i],seeds[j]),right=std::max(seeds[i],seeds[j]);
                if(tree.contains(left,right))continue;
                if(visits>=max_pair_visits){out.discovery_limited=true;break;}
                ++visits;
                const auto key_pair=std::make_pair(left,right);
                auto it=pairs.find(key_pair);
                if(it!=pairs.end())++it->second;
                else if(pairs.size()<max_pairs)pairs.emplace(key_pair,1);
                else out.discovery_limited=true;
            }
        }
    }
    std::vector<Match> candidates;
    for(const auto& [ids,shared]:pairs){
        check_cancel(cancel);
        Match m{ids.first,ids.second,shared};
        const auto max_count=std::max(tree.folders[m.left].distinct,tree.folders[m.right].distinct);
        if(coverage(tree,m)>=0.5&&double(shared)/double(max_count)>=0.25)candidates.push_back(m);
    }
    std::sort(candidates.begin(),candidates.end(),[&](const Match& a,const Match& b){
        if(a.shared!=b.shared)return a.shared>b.shared;
        // On equal evidence, compare at equivalent levels before considering
        // a folder nested under the opposite snapshot.
        const size_t a_total=tree.file_count(a.left)+tree.file_count(a.right);
        const size_t b_total=tree.file_count(b.left)+tree.file_count(b.right);
        if(a_total!=b_total)return a_total>b_total;
        return std::tie(a.left,a.right)<std::tie(b.left,b.right);
    });
    struct Detailed {Match match;OldDiskPair pair;};
    std::vector<Detailed> detailed;
    size_t detail_visits=0;
    for(const auto& m:candidates){
        check_cancel(cancel);
        const auto n=tree.file_count(m.left)+tree.file_count(m.right)+
                     (tree.folders[m.left].end-m.left)+(tree.folders[m.right].end-m.right);
        if(n>max_detail_visits-detail_visits){out.discovery_limited=true;continue;}
        detail_visits+=n;
        auto d=describe(inv,tree,m,content_ids,widespread,out.complete,cancel);
        if(!d||(out.complete&&!d->same_tree&&(d->shared_contents<2||(d->same_path_files==0&&d->shared_contents<3))))continue;
        detailed.push_back({m,std::move(*d)});
        if(detailed.size()==800){out.discovery_limited=true;break;}
    }
    std::sort(detailed.begin(),detailed.end(),[&](const Detailed& a,const Detailed& b){
        const auto& x=a.pair;const auto& y=b.pair;
        if(x.shared_contents!=y.shared_contents)return x.shared_contents>y.shared_contents;
        const auto anchored=[](const OldDiskPair& p){return p.same_path_files+p.conflicting_paths;};
        if(anchored(x)!=anchored(y))return anchored(x)>anchored(y);
        if(x.coverage_percent!=y.coverage_percent)return x.coverage_percent>y.coverage_percent;
        const auto balance=[&](const Detailed& item){
            return double(item.pair.shared_contents)/std::max(tree.folders[item.match.left].distinct,tree.folders[item.match.right].distinct);
        };
        if(balance(a)!=balance(b))return balance(a)>balance(b);
        return std::tie(x.left,x.right)<std::tie(y.left,y.right);
    });
    std::vector<Match> accepted;
    for(auto& d:detailed){
        check_cancel(cancel);
        if(std::any_of(accepted.begin(),accepted.end(),[&](const Match& other){return same_branch_pair(tree,d.match,other);}))continue;
        if(accepted.size()==40){out.discovery_limited=true;break;}
        accepted.push_back(d.match);out.versions.push_back(std::move(d.pair));
    }
    return out;
}
} // namespace dc
