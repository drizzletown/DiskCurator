#include "core.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <tuple>
#include <stdexcept>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#endif
#ifdef __APPLE__
#include <stdio.h>
#endif
#endif

namespace dc {
std::string utf8(const fs::path& p) { return p.generic_u8string(); }
fs::path from_utf8(const std::string& s) { return fs::u8path(s); }
std::string lowercase(std::string s) { for(auto& c:s) if(c>='A'&&c<='Z') c=char(c+('a'-'A')); return s; }
std::string human(U64 n) { const char* u[]={"B","KiB","MiB","GiB","TiB","PiB"}; double x=double(n); int i=0; while(x>=1024&&i<5) {x/=1024;++i;} std::ostringstream s; s<<std::fixed<<std::setprecision(i?2:0)<<x<<" "<<u[i]; return s.str(); }
static std::wstring fold(std::wstring s) { std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return wchar_t(std::towlower(c));});return s; }
static std::wstring path_key(const fs::path& p) {return fold(p.lexically_normal().generic_wstring());}
static std::wstring component_key(const fs::path& p) {return fold(p.filename().wstring());}
bool is_inside(const fs::path& rel) { if(rel.empty()||rel==fs::path(".")) return true; if(rel.is_absolute()||rel.has_root_name()||rel.has_root_directory()) return false; for(auto& p:rel) if(p=="..") return false; return true; }
bool glob(const std::wstring& pattern, const std::wstring& text) {
    auto p=fold(pattern),s=fold(text); size_t i=0,j=0,star=std::wstring::npos,after=0;
    while(j<s.size()) { if(i<p.size()&&(p[i]==L'?'||p[i]==s[j])){++i;++j;}
        else if(i<p.size()&&p[i]==L'*'){star=i++;after=j;}
        else if(star!=std::wstring::npos){i=star+1;j=++after;} else return false; }
    while(i<p.size()&&p[i]==L'*')++i;
    return i==p.size();
}
static bool reparse(const fs::path& p) {
#ifdef _WIN32
    auto a=GetFileAttributesW(p.c_str()); return a!=INVALID_FILE_ATTRIBUTES && (a&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
#else
    std::error_code ec; return fs::is_symlink(fs::symlink_status(p,ec));
#endif
}
static void check_abort(const std::atomic_bool* p) {if(p&&p->load()) throw std::runtime_error("Operation canceled");}
Inventory scan(const fs::path& input, const Progress& progress,const std::atomic_bool* cancel,
               const ScanProgressCallback& scan_progress) {
    Inventory inv;std::error_code ec;inv.root=fs::absolute(input,ec).lexically_normal();
    if(ec||!fs::is_directory(inv.root,ec)||ec||reparse(inv.root))throw std::runtime_error("Root is not a readable ordinary directory");
    const auto started=std::chrono::steady_clock::now();
    auto last_report=started;
    ScanProgress state;state.root=inv.root;state.phase=ScanPhase::Enumerating;
    auto report=[&](const fs::path& current,bool force=false){
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-last_report<std::chrono::milliseconds(100))return;
        last_report=now;
        state.current_file=current;state.files_discovered=inv.files.size();
        state.total_bytes=inv.bytes;state.errors=inv.errors.size();
        state.elapsed_seconds=std::chrono::duration<double>(now-started).count();
        if(scan_progress)scan_progress(state);
        if(progress)progress("Enumerating: "+std::to_string(inv.files.size())+" files, "+human(inv.bytes));
    };
    // These excluded paths are constant for the whole traversal. Resolving the
    // cache and folding the root for every entry was costly on tiny-file disks.
    const auto system_key=path_key(inv.root/fs::path(L".DiskCurator-System"));
    const auto cache_key=path_key(hash_cache_root());
    report(inv.root,true);
    inv.dirs.push_back({{},0,0});
    std::vector<fs::path> todo{inv.root};
    while(!todo.empty()) {
        check_abort(cancel);auto dir=todo.back();todo.pop_back();
        fs::directory_iterator it(dir,fs::directory_options::none,ec),end;
        if(ec){inv.errors.push_back("Cannot list "+utf8(dir)+": "+ec.message());ec.clear();report(dir);continue;}
        for(;it!=end;it.increment(ec)){
            check_abort(cancel);
            if(ec){inv.errors.push_back("Iteration in "+utf8(dir)+": "+ec.message());ec.clear();break;}
            auto p=it->path();report(p);
            const auto entry_key=path_key(p);
            if(entry_key==system_key&&fs::exists(p/fs::path(L"owner.txt")))continue;
            // Application's disposable index must not be scanned when the
            // system drive itself is selected for a full-volume inventory.
            if(entry_key==cache_key)continue;
            auto st=it->symlink_status(ec);
            if(ec){inv.errors.push_back("Cannot stat "+utf8(p)+": "+ec.message());ec.clear();continue;}
            if(fs::is_symlink(st)||reparse(p)){inv.errors.push_back("Skipped reparse point: "+utf8(p));continue;}
            auto rel=p.lexically_relative(inv.root);
            if(!is_inside(rel)) {inv.errors.push_back("Unsafe path: "+utf8(p));continue;}
            if(fs::is_directory(st)) { inv.dirs.push_back({rel,0,0});todo.push_back(p); }
            else if(fs::is_regular_file(st)) {
                auto sz=it->file_size(ec); if(ec){inv.errors.push_back("Cannot size "+utf8(p)+": "+ec.message());ec.clear();continue;}
                auto mt=it->last_write_time(ec);if(ec){inv.errors.push_back("Cannot time "+utf8(p)+": "+ec.message());ec.clear();continue;}
                inv.files.push_back({rel,sz,mt,-1,{}});inv.bytes+=sz;
            }else inv.errors.push_back("Unsupported filesystem object: "+utf8(p));
        }
        if(ec){inv.errors.push_back("Iteration in "+utf8(dir)+": "+ec.message());ec.clear();}
        report(dir);
    }
    std::sort(inv.files.begin(),inv.files.end(),[](auto&a,auto&b){return a.rel<b.rel;});
    std::sort(inv.dirs.begin(),inv.dirs.end(),[](auto&a,auto&b){return a.rel<b.rel;});
    std::map<fs::path,size_t> idx;for(size_t i=0;i<inv.dirs.size();++i)idx[inv.dirs[i].rel]=i;
    // Count direct members once, then roll each directory into its parent.
    // Path ordering puts a parent before its descendants, so reverse traversal
    // avoids revisiting every file for every ancestor in deep backup trees.
    for(const auto& f:inv.files){
        check_abort(cancel);const auto i=idx.find(f.rel.parent_path());
        if(i!=idx.end()){inv.dirs[i->second].bytes+=f.bytes;++inv.dirs[i->second].count;}
        report(f.rel);
    }
    for(size_t i=inv.dirs.size();i>0;--i){
        check_abort(cancel);const auto& d=inv.dirs[i-1];
        if(d.rel.empty())continue;
        const auto parent=idx.find(d.rel.parent_path());
        if(parent!=idx.end()){inv.dirs[parent->second].bytes+=d.bytes;inv.dirs[parent->second].count+=d.count;}
        report(d.rel);
    }
    state.totals_known=true;report({},true);check_abort(cancel);
    return inv;
}
static constexpr U64 FNV=14695981039346656037ULL;
static U64 fnv(const char* p,size_t n,U64 h=FNV){for(size_t i=0;i<n;++i){h^=static_cast<unsigned char>(p[i]);h*=1099511628211ULL;}return h;}
using ByteProgress=std::function<void(U64)>;
static std::optional<U64> sample(const fs::path& p,U64 expected, fs::file_time_type time,
    const std::atomic_bool* cancel=nullptr,const ByteProgress& bytes={}) {
    check_abort(cancel);
    std::error_code ec;if(fs::file_size(p,ec)!=expected||ec)return{};
    std::ifstream in(p,std::ios::binary);if(!in)return{};std::array<char,65536> b{};
    U64 h=fnv(reinterpret_cast<const char*>(&expected),sizeof(expected));
    auto n=static_cast<std::streamsize>(std::min<U64>(expected,b.size()));
    if(n){in.read(b.data(),n);if(bytes)bytes(U64(in.gcount()));if(!in)return{};}
    h=fnv(b.data(),size_t(n),h);
    if(expected>b.size()){check_abort(cancel);in.clear();in.seekg(static_cast<std::streamoff>(expected-b.size()));in.read(b.data(),std::streamsize(b.size()));if(bytes)bytes(U64(in.gcount()));if(!in)return{};h=fnv(b.data(),b.size(),h);}
    if(fs::file_size(p,ec)!=expected||ec||fs::last_write_time(p,ec)!=time||ec)return{};
    return h;
}
static std::optional<U64> full_fingerprint(const fs::path& p,U64 expected,fs::file_time_type time,
    const std::atomic_bool* cancel=nullptr,const ByteProgress& bytes={}){
    check_abort(cancel);
    std::error_code ec;if(fs::file_size(p,ec)!=expected||ec)return {};
    std::ifstream in(p,std::ios::binary);if(!in)return {};
    std::array<char,262144> b{};U64 h=fnv(reinterpret_cast<const char*>(&expected),sizeof(expected));
    U64 left=expected;
    while(left){check_abort(cancel);auto n=static_cast<std::streamsize>(std::min<U64>(left,b.size()));
        in.read(b.data(),n);if(bytes)bytes(U64(in.gcount()));if(!in)return {};
        h=fnv(b.data(),size_t(n),h);left-=U64(n);
    }
    if(in.peek()!=EOF)return {};
    if(fs::file_size(p,ec)!=expected||ec||fs::last_write_time(p,ec)!=time||ec)return {};
    return h;
}
static std::optional<bool> equal_bytes(const fs::path& a,const fs::path& b,U64 expected, std::optional<fs::file_time_type> ta={}, std::optional<fs::file_time_type> tb={},
    const std::atomic_bool* cancel=nullptr,const ByteProgress& bytes={}) {
    check_abort(cancel);
    std::error_code ec;
    if(fs::file_size(a,ec)!=expected||ec||fs::file_size(b,ec)!=expected||ec)return{};
    std::ifstream x(a,std::ios::binary),y(b,std::ios::binary);if(!x||!y)return{};
    std::array<char,262144> xb,yb;U64 left=expected;bool identical=true;
    while(left){check_abort(cancel);auto n=static_cast<std::streamsize>(std::min<U64>(left,xb.size()));
        x.read(xb.data(),n);if(bytes)bytes(U64(x.gcount()));if(!x)return{};
        check_abort(cancel);y.read(yb.data(),n);if(bytes)bytes(U64(y.gcount()));if(!y)return{};
        if(!std::equal(xb.begin(),xb.begin()+n,yb.begin())){identical=false;break;}left-=U64(n);}
    if(identical&&(x.peek()!=EOF||y.peek()!=EOF))return{};
    if(fs::file_size(a,ec)!=expected||ec||fs::file_size(b,ec)!=expected||ec)return{};
    if(ta && (fs::last_write_time(a,ec)!=*ta||ec))return{};
    if(tb && (fs::last_write_time(b,ec)!=*tb||ec))return{};
    return identical;
}
Duplicates deduplicate(Inventory& inv, const Progress& progress,const std::atomic_bool* cancel,
                       const ScanProgressCallback& scan_progress) {
    Duplicates d;
    const auto started=std::chrono::steady_clock::now();auto last_report=started;
    ScanProgress state;state.phase=ScanPhase::Verifying;state.root=inv.root;
    state.files_discovered=inv.files.size();
    // Exact comparison can split a hash bucket on collisions. Its eventual
    // I/O total is unknown; report actual bytes and candidate-file progress
    // instead of inventing a byte percentage or ETA.
    auto report=[&](bool force=false){
        const auto now=std::chrono::steady_clock::now();
        if(!force&&now-last_report<std::chrono::milliseconds(100))return;
        last_report=now;state.errors=d.errors.size();
        state.elapsed_seconds=std::chrono::duration<double>(now-started).count();
        if(scan_progress)scan_progress(state);
    };
    auto bytes=[&](U64 n){state.bytes_read+=n;state.bytes_processed=state.bytes_read;report(state.bytes_read==n);};
    report(true);
    for(auto& f:inv.files){check_abort(cancel);f.content=-1;report();}
    // Eager SHA-256 scan: all file hashes are already recorded. A SHA match
    // only generates candidates; we STILL perform bytewise equality checks.
    const bool indexed=std::all_of(inv.files.begin(),inv.files.end(),
        [&](const File& f){check_abort(cancel);return f.sha256.size()==64;});
    if(indexed){
        std::map<std::pair<U64,std::string>,std::vector<size_t>> buckets;
        for(size_t i=0;i<inv.files.size();++i){check_abort(cancel);buckets[{inv.files[i].bytes,inv.files[i].sha256}].push_back(i);report();}
        int next=0;
        for(auto& [key,members]:buckets){check_abort(cancel);
            std::vector<std::vector<size_t>> verified;
            for(auto i:members){check_abort(cancel);state.current_file=inv.files[i].rel;bool matched=false;
                for(auto& group:verified){auto first=group.front();
                    auto eq=equal_bytes(inv.root/inv.files[i].rel,inv.root/inv.files[first].rel,
                                        key.first,inv.files[i].mtime,inv.files[first].mtime,cancel,bytes);
                    if(!eq){d.errors.push_back("Unstable or unreadable verification: "+utf8(inv.files[i].rel));matched=true;break;}
                    if(*eq){group.push_back(i);matched=true;break;}
                }
                if(!matched)verified.push_back({i});
                ++state.files_completed;report();
            }
            for(auto& group:verified){const auto id=next++;
                for(auto i:group)inv.files[i].content=id;
                if(group.size()>1){d.wasted+=key.first*U64(group.size()-1);d.groups.push_back(std::move(group));}
            }
            if(progress&&(state.files_completed%64==0||state.files_completed==inv.files.size()))
                progress("Verified SHA-256 candidates "+std::to_string(state.files_completed)+" / "+std::to_string(inv.files.size()));
        }
        std::sort(d.groups.begin(),d.groups.end(),[&](auto&a,auto&b){return inv.files[a[0]].bytes*(a.size()-1)>inv.files[b[0]].bytes*(b.size()-1);});
        report(true);check_abort(cancel);return d;
    }
    std::map<U64,std::vector<size_t>> buckets;
    for(size_t i=0;i<inv.files.size();++i){check_abort(cancel);buckets[inv.files[i].bytes].push_back(i);report();}
    int next=0;size_t done=0;
    for(auto& [size,members]:buckets){check_abort(cancel);
        if(members.size()==1){inv.files[members[0]].content=next++;++done;state.files_completed=done;report();continue;}
        std::map<U64,std::vector<size_t>> sampled;
        for(auto i:members){state.current_file=inv.files[i].rel;auto h=sample(inv.root/inv.files[i].rel,size,inv.files[i].mtime,cancel,bytes);
            if(!h){d.errors.push_back("Cannot read stable file: "+utf8(inv.files[i].rel));continue;}sampled[*h].push_back(i);}
        for(auto& [sample_hash,indices]:sampled){(void)sample_hash;
            // Full-file fingerprints reduce expensive O(n^2) comparisons for files whose
            // first/last 64 KiB match. Fingerprints are NEVER used as proof of equality.
            std::map<U64,std::vector<size_t>> full_groups;
            for(auto i:indices){state.current_file=inv.files[i].rel;auto hash=full_fingerprint(inv.root/inv.files[i].rel,size,inv.files[i].mtime,cancel,bytes);
                if(!hash){d.errors.push_back("Cannot read stable full file: "+utf8(inv.files[i].rel));continue;}
                full_groups[*hash].push_back(i);
            }
            for(auto& [full_hash,entries]:full_groups){(void)full_hash;std::vector<std::vector<size_t>> subsets;
                for(auto i:entries){state.current_file=inv.files[i].rel;bool found=false;for(auto& sub:subsets){auto j=sub.front();auto eq=equal_bytes(inv.root/inv.files[i].rel,inv.root/inv.files[j].rel,size,inv.files[i].mtime,inv.files[j].mtime,cancel,bytes);
                        if(!eq){d.errors.push_back("Unstable or unreadable comparison: "+utf8(inv.files[i].rel));found=true;break;}
                        if(*eq){sub.push_back(i);found=true;break;}}
                    if(!found)subsets.push_back({i});}
                for(auto& g:subsets){int id=next++;for(auto i:g)inv.files[i].content=id;
                    if(g.size()>1){d.wasted+=size*U64(g.size()-1);d.groups.push_back(std::move(g));}}
            }
        }
        done+=members.size();state.files_completed=done;report();if(progress&&done%32==0)progress("Verified "+std::to_string(done)+" candidate files");
    }
    std::sort(d.groups.begin(),d.groups.end(),[&](auto&a,auto&b){return inv.files[a[0]].bytes*(a.size()-1)>inv.files[b[0]].bytes*(b.size()-1);});
    report(true);check_abort(cancel);return d;
}
std::vector<DirGroup> duplicate_dirs(const Inventory& inv) {
    if(!inv.complete())throw std::runtime_error("Cannot assert directory equality: scan is incomplete");
    for(auto& f:inv.files)if(f.content<0)throw std::runtime_error("Identify all file contents before comparing directory trees");
    using Signature=std::vector<std::pair<std::wstring,int>>;
    std::map<fs::path,Signature> nodes;std::map<fs::path,U64> sizes;
    for(auto& d:inv.dirs){nodes[d.rel];sizes[d.rel]=d.bytes;}
    for(auto& f:inv.files)nodes[f.rel.parent_path()].emplace_back(L"F:"+component_key(f.rel),f.content);
    std::vector<fs::path> order;for(auto& d:inv.dirs)order.push_back(d.rel);
    auto depth=[](const fs::path& p){return std::distance(p.begin(),p.end());};
    std::sort(order.begin(),order.end(),[&](auto&a,auto&b){return depth(a)>depth(b);});
    std::map<Signature,int> intern;std::map<int,std::vector<fs::path>> groups;int next=0;
    for(auto& dir:order){auto& s=nodes[dir];std::sort(s.begin(),s.end());auto [it,newid]=intern.try_emplace(s,next);
        if(newid)++next;
        int id=it->second;groups[id].push_back(dir);
        if(!dir.empty())nodes[dir.parent_path()].emplace_back(L"D:"+component_key(dir),id);
    }
    std::vector<DirGroup> out;for(auto& [id,paths]:groups)if(paths.size()>1){(void)id;std::sort(paths.begin(),paths.end());out.push_back({paths,sizes[paths[0]]});}
    std::sort(out.begin(),out.end(),[](auto&a,auto&b){return a.bytes>b.bytes;});return out;
}
static fs::path relative_to(const fs::path& p,const fs::path& base){if(base.empty())return p;return p.lexically_relative(base);}
static bool beneath(const fs::path& path,const fs::path& ancestor) {
    if(ancestor.empty())return true;
    auto a=path_key(path),b=path_key(ancestor);
    return a==b||(a.size()>b.size()&&a.compare(0,b.size()+1,b+L"/")==0);
}
Comparison compare_dirs(const Inventory& inv,const fs::path& l,const fs::path& r) {
    if(!inv.complete())throw std::runtime_error("Directory comparison unavailable on an incomplete scan");
    for(const auto& f:inv.files)if(f.content<0)
        throw std::runtime_error("Directory comparison needs verified file contents; rescan after resolving unreadable or changed files");
    if(!is_inside(l)||!is_inside(r))throw std::runtime_error("Directory must be within scanned root");
    auto known=[&](const fs::path& p){return std::any_of(inv.dirs.begin(),inv.dirs.end(),[&](auto&d){return path_key(d.rel)==path_key(p);});};
    if(!known(l)||!known(r))throw std::runtime_error("Select two scanned directories");
    Comparison o;o.left=l;o.right=r;
    struct Entry{bool dir=false;int content=-1;U64 size=0;};
    using Entries=std::map<std::wstring,std::pair<fs::path,Entry>>;
    auto build=[&](const fs::path& base){Entries m;for(auto&f:inv.files)if(beneath(f.rel,base)){
           auto rel=relative_to(f.rel,base);m[path_key(rel)]={rel,{false,f.content,f.bytes}};}
        for(auto& d:inv.dirs)if(d.rel!=base&&beneath(d.rel,base)){
           auto rel=relative_to(d.rel,base);m[path_key(rel)]={rel,{true,-1,0}};}
        return m;};
    auto a=build(l),b=build(r);o.left_in_right=o.right_in_left=true;
    std::set<std::wstring> keys;for(auto&[k,v]:a)keys.insert(k);for(auto&[k,v]:b)keys.insert(k);
    for(auto& key:keys){auto ai=a.find(key),bi=b.find(key);bool left=ai!=a.end(),right=bi!=b.end();
        fs::path path=left?ai->second.first:bi->second.first;
        if(left&&!right){o.only_left++;o.left_in_right=false;o.rows.push_back({"ONLY LEFT",path,ai->second.second.size,0});}
        else if(!left&&right){o.only_right++;o.right_in_left=false;o.rows.push_back({"ONLY RIGHT",path,0,bi->second.second.size});}
        else {auto x=ai->second.second,y=bi->second.second;
            if(x.dir==y.dir&&(x.dir||(x.content>=0&&x.content==y.content))){o.identical++;}
            else {o.changed++;o.left_in_right=false;o.right_in_left=false;o.rows.push_back({"CHANGED",path,x.size,y.size});}}
    }
    std::map<int,int> lc,rc;for(auto&f:inv.files){if(beneath(f.rel,l)){++o.left_file_count;if(f.content>=0)lc[f.content]++;}
        if(beneath(f.rel,r)){++o.right_file_count;if(f.content>=0)rc[f.content]++;}}
    for(auto&[id,count]:lc)o.content_overlap+=std::min(count,rc[id]);
    return o;
}
// Manual pair assessment: preserve content multiplicity and avoid inflated
// similarity when the same bytes are repeated under many filenames.
DirectoryPairReport evaluate_directory_pair(const Inventory& inv,const fs::path& l,const fs::path& r){
    DirectoryPairReport report;
    report.comparison=compare_dirs(inv,l,r); // checks scan/content completeness and roots
    if(path_key(l)==path_key(r))throw std::runtime_error("Choose two different folders");
    report.exact_tree=report.comparison.left_in_right&&report.comparison.right_in_left;
    report.disjoint_for_merge=!l.empty()&&!r.empty()&&!beneath(l,r)&&!beneath(r,l)&&
        !beneath(l,fs::path(L".DiskCurator-System"))&&!beneath(r,fs::path(L".DiskCurator-System"));
    using Files=std::map<std::wstring,const File*>;
    Files left,right;
    std::map<int,size_t> lc,rc;
    for(const auto& f:inv.files){
        if(beneath(f.rel,l)){
            left.emplace(path_key(relative_to(f.rel,l)),&f);++lc[f.content];
        }
        if(beneath(f.rel,r)){
            right.emplace(path_key(relative_to(f.rel,r)),&f);++rc[f.content];
        }
    }
    for(const auto& [key,f]:left){
        const auto it=right.find(key);
        if(it==right.end())++report.left_only_files;
        else if(f->content==it->second->content)++report.same_relative_files;
        else ++report.same_path_different_content;
    }
    for(const auto& [key,f]:right){(void)f;
        if(left.find(key)==left.end())++report.right_only_files;
    }
    for(const auto& [id,count]:lc){
        const auto it=rc.find(id);
        if(it!=rc.end()&&it->second){
            report.shared_file_occurrences+=std::min(count,it->second);
            ++report.shared_distinct_contents;
        }
    }
    const size_t nleft=left.size(),nright=right.size();
    const size_t minimum=std::min(nleft,nright);
    const size_t union_count=nleft+nright-report.shared_file_occurrences;
    if(minimum)report.smaller_coverage=double(report.shared_file_occurrences)/double(minimum);
    if(union_count)report.multiset_jaccard=double(report.shared_file_occurrences)/double(union_count);
    const size_t path_total=report.same_relative_files+report.same_path_different_content;
    if(path_total)report.same_path_fraction=double(report.same_relative_files)/double(path_total);
    for(const auto& d:inv.dirs){
        if(d.rel!=l&&beneath(d.rel,l)){++report.left_dirs;if(d.count==0)++report.left_empty_dirs;}
        if(d.rel!=r&&beneath(d.rel,r)){++report.right_dirs;if(d.count==0)++report.right_empty_dirs;}
    }
    return report;
}

OverlapListing find_overlapping_dirs(const Inventory& inv,size_t max_results,const std::atomic_bool* cancel){
    if(!inv.complete())throw std::runtime_error("Similar directory discovery requires a complete scan");
    for(const auto& f:inv.files)if(f.content<0)
        throw std::runtime_error("Similar directory discovery requires completed file-content verification");
    struct Dir {fs::path path;size_t count=0;};
    std::vector<Dir> dirs;
    std::map<std::wstring,size_t> indices;
    for(const auto& dir:inv.dirs){
        if(dir.rel.empty()||dir.count==0||beneath(dir.rel,fs::path(L".DiskCurator-System")))continue;
        indices[path_key(dir.rel)]=dirs.size();dirs.push_back({dir.rel,0});
    }
    if(dirs.size()>30000)throw std::runtime_error("Too many directories for complete overlap discovery; narrow the scan root");
    // Each physical file occurrence is counted under all of its ancestors.
    // Zero-length or ubiquitous OS metadata does not establish directory
    // provenance. They are still compared in an explicitly selected A/B pair.
    std::map<int,std::map<size_t,size_t>> postings;
    size_t refs=0;
    for(const auto& f:inv.files){
        check_abort(cancel);
        const auto n=fold(f.rel.filename().wstring());
        if(!f.bytes||n==L"thumbs.db"||n==L"desktop.ini"||n==L".ds_store")continue;
        auto parent=f.rel.parent_path();
        while(!parent.empty()&&parent!=fs::path(".")){
            auto it=indices.find(path_key(parent));
            if(it!=indices.end()){
                ++dirs[it->second].count;
                ++postings[f.content][it->second];
                if(++refs>2000000)throw std::runtime_error("Too many ancestor-file associations; narrow the scan root");
            }
            parent=parent.parent_path();
        }
    }
    struct Shared {size_t occurrences=0,distinct=0;};
    std::map<std::pair<size_t,size_t>,Shared> matches;
    for(const auto& [content,bins]:postings){(void)content;check_abort(cancel);
        if(bins.size()>320)throw std::runtime_error("One content occurs across too many directories; narrow the scan root");
        for(auto a=bins.begin();a!=bins.end();++a){auto b=a;++b;
            for(;b!=bins.end();++b){
                if(beneath(dirs[a->first].path,dirs[b->first].path)||
                   beneath(dirs[b->first].path,dirs[a->first].path))continue;
                const auto key=std::make_pair(a->first,b->first);
                auto found=matches.find(key);
                if(found==matches.end()){
                    if(matches.size()>=250000)throw std::runtime_error("Too many overlapping directory pairs; narrow the scan root");
                    found=matches.emplace(key,Shared{}).first;
                }
                found->second.occurrences+=std::min(a->second,b->second);
                ++found->second.distinct;
            }
        }
    }
    OverlapListing out;out.total_candidates=matches.size();
    out.pairs.reserve(matches.size());
    for(const auto& [key,shared]:matches){
        const auto& a=dirs[key.first];const auto& b=dirs[key.second];
        const auto minimum=std::min(a.count,b.count);
        const auto joint=a.count+b.count-shared.occurrences;
        out.pairs.push_back({a.path,b.path,shared.occurrences,shared.distinct,a.count,b.count,
                             minimum?double(shared.occurrences)/double(minimum):0.0,
                             joint?double(shared.occurrences)/double(joint):0.0});
    }
    std::sort(out.pairs.begin(),out.pairs.end(),[](const auto& a,const auto& b){
        if(a.smaller_coverage!=b.smaller_coverage)return a.smaller_coverage>b.smaller_coverage;
        if(a.shared!=b.shared)return a.shared>b.shared;
        return std::tie(a.left,a.right)<std::tie(b.left,b.right);
    });
    if(out.pairs.size()>max_results){out.pairs.resize(max_results);out.truncated=true;}
    return out;
}

// The recommender uses content *identities* established by deduplicate(), not
// names or hashes alone. Relative-path agreement strengthens the evidence.
// It is read-only and intentionally conservative: no automatic edits follow.
std::vector<MergeSuggestion> recommend_merges(const Inventory& inv,
                                              size_t max_pairs,const std::atomic_bool* cancel){
    if(!inv.complete())throw std::runtime_error("Cannot recommend merges from an incomplete directory scan");
    for(const auto& f:inv.files)if(f.content<0)
        throw std::runtime_error("Cannot recommend merges until all file contents are verified");
    if(max_pairs==0)return {};
    // Only directories with useful content are candidates; empty files and OS
    // thumbnail metadata do not count as evidence of shared backup lineage.
    auto useful_content=[](const File& f){
        if(f.bytes==0)return false;
        const auto n=fold(f.rel.filename().wstring());
        return n!=L"thumbs.db"&&n!=L"desktop.ini"&&n!=L".ds_store";
    };
    struct DirData {fs::path rel; size_t files=0;std::vector<size_t> members;};
    std::vector<DirData> dirs;
    std::map<std::wstring,size_t> positions;
    for(const auto& d:inv.dirs){
        if(d.rel.empty()||d.count<2)continue;
        positions[path_key(d.rel)]=dirs.size();
        dirs.push_back({d.rel,0,{}});
    }
    if(dirs.size()>30000)throw std::runtime_error("Too many subdirectories for merge suggestions; select a smaller root");
    // Register every file in each ancestor subtree. This permits matching
    // nested backup folders, not just direct children of the root.
    std::map<int,std::map<size_t,size_t>> by_content;
    size_t references=0;
    for(size_t i=0;i<inv.files.size();++i){
        check_abort(cancel);
        const auto& f=inv.files[i];
        const bool relevant=useful_content(f);
        auto parent=f.rel.parent_path();
        while(!parent.empty()&&parent!=fs::path(".")){
            const auto it=positions.find(path_key(parent));
            if(it!=positions.end()){
                auto& d=dirs[it->second];d.members.push_back(i);
                if(relevant){++d.files;++by_content[f.content][it->second];}
                if(++references>2000000)
                    throw std::runtime_error("Directory nesting too large for full merge analysis; select a smaller root");
            }
            parent=parent.parent_path();
        }
    }
    struct PairCount {size_t shared=0;};
    std::map<std::pair<size_t,size_t>,PairCount> pairs;
    for(const auto& [content,bins]:by_content){(void)content;check_abort(cancel);
        // A single ubiquitous file would produce quadratically many false
        // candidates. Refuse an extreme case, never silently omit results.
        if(bins.size()>320)
            throw std::runtime_error("Too many copies of the same content across directories; narrow the analysis root");
        for(auto a=bins.begin();a!=bins.end();++a){auto b=a;++b;
            for(;b!=bins.end();++b){
                auto& x=dirs[a->first];auto& y=dirs[b->first];
                if(beneath(x.rel,y.rel)||beneath(y.rel,x.rel))continue;
                const auto key=std::make_pair(a->first,b->first);
                auto it=pairs.find(key);
                if(it==pairs.end()){
                    if(pairs.size()>=250000)
                        throw std::runtime_error("Too many related folder pairs; select a smaller analysis root");
                    it=pairs.emplace(key,PairCount{}).first;
                }
                it->second.shared+=std::min(a->second,b->second);
            }
        }
    }
    struct Raw {size_t a,b,shared;double ratio;};
    std::vector<Raw> candidates;
    for(auto& [key,count]:pairs){
        const auto& a=dirs[key.first];const auto& b=dirs[key.second];
        const auto smaller=std::min(a.files,b.files);
        if(smaller<2||count.shared<2)continue;
        const auto ratio=double(count.shared)/double(smaller);
        // Weak overlaps are not backup evidence. A recommendation always
        // requires at least half of the smaller tree's meaningful files.
        if(ratio<0.5)continue;
        candidates.push_back({key.first,key.second,count.shared,ratio});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Raw& a,const Raw& b){
        if(a.ratio!=b.ratio)return a.ratio>b.ratio;
        if(a.shared!=b.shared)return a.shared>b.shared;
        return std::tie(a.a,a.b)<std::tie(b.a,b.b);
    });
    // We must not silently skip strong matches when an excessive number of
    // candidates arise. Ask for a narrower scope instead.
    const size_t evaluation_limit=std::max<size_t>(120,std::min<size_t>(3000,max_pairs*20));
    if(candidates.size()>evaluation_limit &&
       candidates[evaluation_limit].ratio>=0.85 && candidates[evaluation_limit].shared>=5)
        throw std::runtime_error("Too many high-overlap backup candidates; narrow the selected root");
    if(candidates.size()>evaluation_limit)candidates.resize(evaluation_limit);
    std::vector<MergeSuggestion> result;
    for(const auto& c:candidates){check_abort(cancel);
        auto ai=c.a,bi=c.b;
        // Larger file count is a proposed *destination*, never interpreted as
        // more recent. A user can reverse it using the manual merge dialog.
        if(dirs[ai].files>dirs[bi].files ||
           (dirs[ai].files==dirs[bi].files&&path_key(dirs[ai].rel)>path_key(dirs[bi].rel)))
            std::swap(ai,bi);
        const auto& src=dirs[ai];const auto& dst=dirs[bi];
        std::map<std::wstring,size_t> left,right;
        for(auto fi:src.members)left[path_key(relative_to(inv.files[fi].rel,src.rel))]=fi;
        for(auto fi:dst.members)right[path_key(relative_to(inv.files[fi].rel,dst.rel))]=fi;
        MergeSuggestion s;s.source=src.rel;s.destination=dst.rel;
        s.source_files=src.members.size();s.destination_files=dst.members.size();
        s.evidence_source_files=src.files;s.evidence_destination_files=dst.files;
        s.shared_contents=c.shared;s.overlap=c.ratio;
        for(const auto& [key,fi]:left){auto it=right.find(key);
            if(it==right.end())++s.source_only;
            else if(inv.files[fi].content==inv.files[it->second].content)++s.same_relative_files;
            else {
                ++s.changed_at_same_path;
                // A previous additive merge may have kept the source version
                // under a deterministic conflict name. Do not suggest a copy
                // that the actual merge planner would skip on retry.
                const auto relative=relative_to(inv.files[fi].rel,src.rel);
                const auto stem=relative.stem().wstring();
                const auto ext=relative.extension().wstring();
                const auto tag=std::wstring(L" (from ")+src.rel.filename().wstring()+L")";
                bool already_retained=false;
                for(int suffix=0;suffix<100000;++suffix){
                    const auto name=stem+tag+(suffix?L" ("+std::to_wstring(suffix+1)+L")":L"")+ext;
                    const auto alternate=relative.parent_path()/fs::path(name);
                    const auto alt=right.find(path_key(alternate));
                    if(alt==right.end())break; // planner uses first free alternate
                    if(inv.files[fi].content==inv.files[alt->second].content){already_retained=true;break;}
                }
                if(!already_retained)++s.unresolved_collisions;
            }
        }
        for(const auto& [key,fi]:right){(void)fi;
            if(left.find(key)==left.end())++s.destination_only;
        }
        const double relative_fraction=double(s.same_relative_files)/double(std::min(src.members.size(),dst.members.size()));
        if(c.shared>=5&&c.ratio>=0.75&&s.same_relative_files>=3&&relative_fraction>=0.40)s.confidence=3;
        else if((c.shared>=3&&c.ratio>=0.60&&s.same_relative_files>=2) ||
                (c.shared>=8&&c.ratio>=0.75&&s.same_relative_files>=1))s.confidence=2;
        else if((c.shared>=2&&c.ratio>=0.90&&s.same_relative_files>=2) ||
                (c.shared>=12&&c.ratio>=0.85))s.confidence=1;
        else continue;
        // No differences: an exact duplicate is not a *merge* proposal.
        // Instead of deleting anything, the existing duplicate-dir and
        // two-independent-copies workflows handle that case.
        if(!s.source_only&&!s.destination_only&&!s.changed_at_same_path)continue;
        s.useful=(s.source_only>0||s.unresolved_collisions>0);
        result.push_back(std::move(s));
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){
        if(a.confidence!=b.confidence)return a.confidence>b.confidence;
        if(a.overlap!=b.overlap)return a.overlap>b.overlap;
        if(a.same_relative_files!=b.same_relative_files)return a.same_relative_files>b.same_relative_files;
        if(a.shared_contents!=b.shared_contents)return a.shared_contents>b.shared_contents;
        return std::tie(a.source,a.destination)<std::tie(b.source,b.destination);
    });
    if(result.size()>max_pairs)result.resize(max_pairs);
    return result;
}

std::vector<Relation> directory_relations(const Inventory& inv,size_t max_pairs,const std::atomic_bool* cancel){
    if(!inv.complete())throw std::runtime_error("Directory relationship analysis needs a complete scan");
    for(auto& f:inv.files)if(f.content<0)throw std::runtime_error("Directory relationship analysis needs full content verification");
    std::vector<fs::path> direct;
    std::map<std::wstring,size_t> ids;
    for(auto& d:inv.dirs)if(!d.rel.empty()&&d.rel.parent_path().empty()){
        ids[path_key(d.rel)]=direct.size();direct.push_back(d.rel);
    }
    // Index each verified file-content ID by the root-level subtree containing it.
    // The min-count overlap is exact, even when the same bytes occur many times in one subtree.
    std::map<int,std::map<size_t,size_t>> inverted;
    for(auto& f:inv.files){check_abort(cancel);auto it=f.rel.begin();if(it==f.rel.end())continue;
        auto d=ids.find(path_key(*it));if(d!=ids.end()&&f.content>=0)++inverted[f.content][d->second];
    }
    std::map<std::pair<size_t,size_t>,size_t> scores;
    for(auto& [hash,bins]:inverted){check_abort(cancel);(void)hash;
        for(auto a=bins.begin();a!=bins.end();++a){auto b=a;++b;
            for(;b!=bins.end();++b)scores[{a->first,b->first}]+=std::min(a->second,b->second);
        }
    }
    struct Pair{size_t i,j,matched;};std::vector<Pair> candidates;
    candidates.reserve(scores.size());
    for(auto& [pair,matched]:scores)candidates.push_back({pair.first,pair.second,matched});
    // Empty subtrees can be equal despite having no files. Keep a bounded set of those pairs.
    std::vector<size_t> empty;
    for(size_t i=0;i<direct.size();++i){
        auto it=std::find_if(inv.dirs.begin(),inv.dirs.end(),[&](const Folder& d){return d.rel==direct[i];});
        if(it!=inv.dirs.end()&&it->count==0)empty.push_back(i);
    }
    for(size_t a=0;a<empty.size();++a)for(size_t b=a+1;b<empty.size();++b){
        if(candidates.size()>max_pairs*2+100)break;
        if(!scores.count({empty[a],empty[b]}))candidates.push_back({empty[a],empty[b],0});
    }
    std::sort(candidates.begin(),candidates.end(),[](const Pair& a,const Pair& b){
        if(a.matched!=b.matched)return a.matched>b.matched;
        return std::tie(a.i,a.j)<std::tie(b.i,b.j);
    });
    std::vector<Relation> out;
    for(auto p:candidates){check_abort(cancel);if(out.size()>=max_pairs)break;
        auto c=compare_dirs(inv,direct[p.i],direct[p.j]);
        std::string rel="PARTIAL CONTENT OVERLAP";
        if(c.left_in_right&&c.right_in_left)rel="EXACT TREE";
        else if(c.left_in_right)rel="LEFT INCLUDED BY PATH IN RIGHT";
        else if(c.right_in_left)rel="RIGHT INCLUDED BY PATH IN LEFT";
        else if(c.content_overlap==c.left_file_count&&c.left_file_count>0)rel="LEFT CONTENT INCLUDED IN RIGHT";
        else if(c.content_overlap==c.right_file_count&&c.right_file_count>0)rel="RIGHT CONTENT INCLUDED IN LEFT";
        else if(c.content_overlap==0)rel="NO SHARED FILE CONTENT";
        out.push_back({direct[p.i],direct[p.j],c.content_overlap,c.left_file_count,c.right_file_count,rel});
    }
    return out;
}
std::string extension_category(const fs::path& p){auto e=fold(p.extension().wstring());
    auto in=[&](std::initializer_list<const wchar_t*> extensions){for(auto x:extensions)if(e==x)return true;return false;};
    if(in({L".ppm",L".pgm",L".jpg",L".jpeg",L".png",L".heic",L".heif",L".webp",L".bmp",L".gif",L".tif",L".tiff",L".raw",L".cr2",L".nef",L".arw"}))return "Pictures";
    if(in({L".mp4",L".mov",L".avi",L".mkv",L".wmv",L".flv",L".mpeg",L".mts"}))return "Videos";
    if(in({L".mp3",L".wav",L".flac",L".aac",L".wma",L".ogg",L".m4a"}))return "Audio";
    if(in({L".pdf",L".doc",L".docx",L".xls",L".xlsx",L".ppt",L".pptx",L".txt",L".md",L".csv",L".rtf"}))return "Documents";
    if(in({L".zip",L".7z",L".rar",L".tar",L".gz",L".bz2",L".iso"}))return "Archives";
    if(in({L".cpp",L".h",L".hpp",L".c",L".py",L".js",L".java",L".html",L".css",L".json",L".xml",L".sql"}))return "Code";
    return "Other";
}
static int hamming(U64 a,U64 b){U64 v=a^b;int n=0;while(v){v&=v-1;++n;}return n;}
PhotoReport similar_photos(const Inventory& inv,int max_distance,const Progress& progress,const std::atomic_bool* cancel){
    if(max_distance<0||max_distance>16)throw std::runtime_error("Similarity distance must be in [0,16]");
    PhotoReport report;size_t checked=0;
    for(auto&f:inv.files){check_abort(cancel);if(extension_category(f.rel)!="Pictures")continue;
        auto p=probe_photo(inv.root/f.rel);p.rel=f.rel;++checked;
        if(p.valid)report.photos.push_back(std::move(p));
        else report.errors.push_back(utf8(f.rel)+": "+p.error);
        if(progress&&checked%30==0)progress("Decoded "+std::to_string(checked)+" images");
    }
    // BK tree over 64-bit Hamming metric. Emit candidate edges; edges are not a claim of transitive equality.
    struct Node{U64 hash;size_t index;std::map<int,size_t> kids;};std::vector<Node> nodes;
    for(size_t i=0;i<report.photos.size();++i){auto h=report.photos[i].dhash;
        if(nodes.empty()){nodes.push_back({h,i,{}});continue;}
        std::vector<size_t> todo{0};while(!todo.empty()){
            auto k=todo.back();todo.pop_back();int d=hamming(h,nodes[k].hash);
            if(d<=max_distance)report.pairs.push_back({report.photos[nodes[k].index].rel,report.photos[i].rel,d});
            auto lo=nodes[k].kids.lower_bound(std::max(0,d-max_distance));
            for(;lo!=nodes[k].kids.end()&&lo->first<=d+max_distance;++lo)todo.push_back(lo->second);
        }
        size_t k=0;while(true){int d=hamming(h,nodes[k].hash);auto it=nodes[k].kids.find(d);
            if(it==nodes[k].kids.end()){size_t idx=nodes.size();nodes[k].kids[d]=idx;nodes.push_back({h,i,{}});break;}k=it->second;}
    }
    std::sort(report.pairs.begin(),report.pairs.end(),[](auto&a,auto&b){return a.distance<b.distance;});return report;
}
static bool win_valid_name(const fs::path& p){auto s=p.filename().wstring();if(s.empty()||s==L"."||s==L"..")return false;
    if(s.back()==L' '||s.back()==L'.')return false;
    for(auto c:s)if(c<32||std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos)return false;
    // Windows treats device names as reserved even with multiple extensions,
    // e.g. CON.backup.txt, not just CON.txt.
    auto base=fold(s.substr(0,s.find(L'.')));
    if(base==L"con"||base==L"prn"||base==L"aux"||base==L"nul")return false;
    if(base.size()==4&&((base.compare(0,3,L"com")==0||base.compare(0,3,L"lpt")==0))&&base[3]>=L'1'&&base[3]<=L'9')return false;
    return true;
}
bool valid_file_name(const fs::path& name) {
    return name == name.filename() && name.native().size() <= 255 && win_valid_name(name);
}
static void validate_plan(const Plan& plan, bool allow_internal_source=false){
    std::set<std::wstring> sources,targets;for(auto&m:plan.moves){
        if(!is_inside(m.from)||m.from.empty()||!is_inside(m.to)||m.to.empty())throw std::runtime_error("Plan escapes selected root");
        for(auto& part:m.to)if(!win_valid_name(part))throw std::runtime_error("Invalid destination path component: "+utf8(m.to));
        if(!allow_internal_source && beneath(m.to,L".DiskCurator-System")){
            const fs::path prefix=fs::path(L".DiskCurator-System")/L"quarantine"/L"__TX__"/L"data";
            if(plan.name.compare(0,std::string("Quarantine verified duplicate files").size(),"Quarantine verified duplicate files")!=0||
               m.keepers.size()!=plan.retention.min_active_copies||
               (m.keepers.empty()&&!plan.retention.allow_zero_active)||!beneath(m.to,prefix))
                throw std::runtime_error("Only verified duplicate files may enter the reserved quarantine workspace");
        }
        if(!sources.insert(path_key(m.from)).second||!targets.insert(path_key(m.to)).second)throw std::runtime_error("Repeated move source or target");
        if(path_key(m.from)==path_key(m.to))throw std::runtime_error("No-op or case-only rename unsupported for safety");
        if(beneath(m.to,m.from))throw std::runtime_error("Destination is inside its own source: "+utf8(m.from));
        if(!allow_internal_source && component_key(m.from.begin()==m.from.end()?fs::path{}:*m.from.begin())==L".diskcurator-system")throw std::runtime_error("Cannot move system workspace");
    }
    // A nested selection can be detected from its ancestor chain. Avoid an
    // O(number-of-moves^2) all-pairs check for large bulk archive plans.
    for(const auto& m:plan.moves){
        auto parent=m.from.parent_path();
        while(!parent.empty()&&parent!=fs::path(".")){
            if(sources.count(path_key(parent)))
                throw std::runtime_error("Nested selections are unsafe; choose parent or child, not both");
            parent=parent.parent_path();
        }
    }
}
static std::wstring replace_all(std::wstring s,const std::wstring& f,const std::wstring& r){if(f.empty())return s;size_t pos=0;while((pos=s.find(f,pos))!=std::wstring::npos){s.replace(pos,f.size(),r);pos+=r.size();}return s;}
static std::vector<SnapshotEntry> file_snapshot(const File& f){
    return {{fs::path{},false,f.bytes,f.mtime}};
}
static std::vector<SnapshotEntry> snapshot(const Inventory& inv, const fs::path& base, bool is_dir){
    std::vector<SnapshotEntry> entries;
    if(is_dir){
        for(const auto& d:inv.dirs)if(beneath(d.rel,base))entries.push_back({path_key(d.rel)==path_key(base)?fs::path{}:relative_to(d.rel,base),true,0,{}});
        for(const auto& f:inv.files)if(beneath(f.rel,base))entries.push_back({relative_to(f.rel,base),false,f.bytes,f.mtime});
    }else{
        for(const auto& f:inv.files)if(path_key(f.rel)==path_key(base))entries.push_back({{},false,f.bytes,f.mtime});
    }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
        return std::tie(a.rel,a.directory)<std::tie(b.rel,b.directory);
    });
    return entries;
}
static void verify_snapshot(const fs::path& root,const Move& move){
    if(move.expected_source.empty())return; // manually supplied moves have no inventory precondition
    const auto& expected=move.expected_source;
    std::vector<SnapshotEntry> actual;
    if(expected.front().directory){
        auto observed=scan(root/move.from);
        if(!observed.complete())throw std::runtime_error("Source folder changed or contains unreadable entries: "+utf8(move.from));
        actual=snapshot(observed,{},true);
    }else{
        std::error_code ec;const auto path=root/move.from;
        if(!fs::is_regular_file(path,ec)||ec)throw std::runtime_error("Source file is no longer regular: "+utf8(move.from));
        auto size=fs::file_size(path,ec);if(ec)throw std::runtime_error("Cannot read source file size: "+utf8(move.from));
        auto mt=fs::last_write_time(path,ec);if(ec)throw std::runtime_error("Cannot read source file time: "+utf8(move.from));
        actual.push_back({{},false,size,mt});
    }
    if(actual!=expected)throw std::runtime_error("Source changed after preview; rescan and generate a new plan: "+utf8(move.from));
}
Plan plan_rename(const Inventory& inv,const RenameOptions& opt){
    if (opt.single_file) {
        const auto& request = *opt.single_file;
        if (!valid_file_name(request.new_filename))
            throw std::runtime_error("请输入有效文件名，可包含扩展名。");
        const auto source = std::find_if(inv.files.begin(),inv.files.end(),
            [&](const File& file){return file.rel == request.source;});
        if (source == inv.files.end()) throw std::runtime_error("所选文件未出现在当前扫描结果中。");
        const auto target = source->rel.parent_path() / request.new_filename;
        const auto key = path_key(target);
        const auto occupied = [&](const auto& item){return path_key(item.rel)==key;};
        if (std::any_of(inv.files.begin(),inv.files.end(),occupied) ||
            std::any_of(inv.dirs.begin(),inv.dirs.end(),occupied))
            throw std::runtime_error("同一目录下已有这个名称，请换一个文件名。");
        Plan plan;
        plan.name="Rename file";
        Move move{source->rel,target,{}};
        move.expected_source=file_snapshot(*source);
        plan.moves.push_back(std::move(move));
        validate_plan(plan);
        return plan;
    }
    if(!is_inside(opt.dir))throw std::runtime_error("Rename directory outside root");
    Plan plan;plan.name="Batch rename";std::set<std::wstring> existing;
    struct Selected {fs::path rel; bool dir=false; U64 bytes=0; fs::file_time_type mtime{};};
    std::vector<Selected> selected;
    for(const auto& f:inv.files)if(path_key(f.rel.parent_path())==path_key(opt.dir)&&opt.include_files&&glob(opt.pattern,f.rel.filename().wstring()))
        selected.push_back({f.rel,false,f.bytes,f.mtime});
    for(const auto& d:inv.dirs)if(!d.rel.empty()&&path_key(d.rel.parent_path())==path_key(opt.dir)&&opt.include_dirs&&glob(opt.pattern,d.rel.filename().wstring()))
        selected.push_back({d.rel,true});
    std::sort(selected.begin(),selected.end(),[](const auto& a,const auto& b){return a.rel<b.rel;});
    std::int64_t seq=opt.number_start;
    std::map<std::wstring,size_t> folder_sources; // direct-child folders are disjoint
    for(const auto& item:selected){const auto& old=item.rel;bool is_dir=item.dir;
        auto stem=is_dir?old.filename().wstring():old.stem().wstring();auto ext=is_dir?L"":old.extension().wstring();
        stem=replace_all(stem,opt.find,opt.replace);
        std::wstring number;if(seq>0){std::wostringstream s;s<<std::setw(std::max(1,opt.digits))<<std::setfill(L'0')<<seq++;number=s.str()+L"_";}
        auto dest=opt.dir/fs::path(opt.prefix+number+stem+opt.suffix+ext);
        if(path_key(old)!=path_key(dest)){
            Move move{old,dest,{}};
            if(is_dir)folder_sources[path_key(old)]=plan.moves.size();
            else move.expected_source.push_back({{},false,item.bytes,item.mtime});
            plan.moves.push_back(std::move(move));
        }
    }
    // Build all selected folder snapshots in ONE pass over the inventory.
    // This avoids O(files * selected-folders) work on large photo collections.
    auto owner_of=[&](const fs::path& rel)->std::optional<std::pair<size_t,fs::path>>{
        if(!beneath(rel,opt.dir))return {};
        auto inside=relative_to(rel,opt.dir);
        if(inside.empty()||inside==fs::path("."))return {};
        auto first=inside.begin();if(first==inside.end())return {};
        auto owner=opt.dir/(*first);
        auto found=folder_sources.find(path_key(owner));
        if(found==folder_sources.end())return {};
        const auto subpath=path_key(rel)==path_key(owner)?fs::path{}:relative_to(rel,owner);
        return std::make_pair(found->second,subpath);
    };
    for(const auto& dir:inv.dirs)if(auto item=owner_of(dir.rel))
        plan.moves[item->first].expected_source.push_back({item->second,true,0,{}});
    for(const auto& file:inv.files)if(auto item=owner_of(file.rel))
        plan.moves[item->first].expected_source.push_back({item->second,false,file.bytes,file.mtime});
    for(auto& move:plan.moves)if(folder_sources.count(path_key(move.from))){
        auto& entries=move.expected_source;
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){
            return std::tie(a.rel,a.directory)<std::tie(b.rel,b.directory);
        });
    }
    validate_plan(plan);
    // A rename may use another selected source as its target (swap or chain),
    // but it may not overwrite an unrelated item already present in the scan.
    std::set<std::wstring> sources;
    for(const auto& m:plan.moves)sources.insert(path_key(m.from));
    for(const auto& m:plan.moves){
        const auto target=path_key(m.to);
        if(sources.count(target))continue;
        for(const auto& f:inv.files)if(path_key(f.rel)==target)
            throw std::runtime_error("Rename target already exists: "+utf8(m.to));
        for(const auto& d:inv.dirs)if(path_key(d.rel)==target)
            throw std::runtime_error("Rename target directory already exists: "+utf8(m.to));
    }
    return plan;
}
// Merge is a conservative union of two sibling (or otherwise disjoint) subtrees.
// Matching paths with different bytes are retained as separate versions.
MergePlan plan_merge_dirs(const Inventory& inv,const fs::path& source,const fs::path& destination){
    if(!inv.complete())throw std::runtime_error("Directory merge needs a complete scan");
    for(const auto& f:inv.files)if(f.content<0)throw std::runtime_error("Directory merge needs complete content verification");
    if(source.empty()||destination.empty()||!is_inside(source)||!is_inside(destination)||
       path_key(source)==path_key(destination)||beneath(source,destination)||beneath(destination,source)||
       beneath(source,fs::path(L".DiskCurator-System"))||beneath(destination,fs::path(L".DiskCurator-System")))
        throw std::runtime_error("Merge requires two disjoint non-system subdirectories");
    std::set<std::wstring> existing_dirs;
    std::map<std::wstring,const File*> existing_files;
    for(const auto& d:inv.dirs)existing_dirs.insert(path_key(d.rel));
    for(const auto& f:inv.files)existing_files[path_key(f.rel)]=&f;
    if(!existing_dirs.count(path_key(source))||!existing_dirs.count(path_key(destination)))
        throw std::runtime_error("Both merge directories must exist in the scanned root");
    MergePlan plan;plan.source=source;plan.destination=destination;
    plan.source_snapshot=snapshot(inv,source,true);plan.destination_snapshot=snapshot(inv,destination,true);
    std::set<std::wstring> target_files;
    std::set<std::wstring> target_directories;
    auto check_name=[](const fs::path& path){
        for(const auto& part:path)if(!win_valid_name(part))throw std::runtime_error("Unsupported Windows filename: "+utf8(path));
    };
    // First check directory/file type collisions, including empty directories.
    // New nested paths are explicitly recorded so empty folders are preserved.
    for(const auto& d:inv.dirs)if(d.rel!=source&&beneath(d.rel,source)){
        auto relative=relative_to(d.rel,source);
        const auto dest=destination/relative;
        check_name(dest);
        if(existing_files.count(path_key(dest)))
            throw std::runtime_error("Merge directory collides with an existing file: "+utf8(dest));
        if(!existing_dirs.count(path_key(dest)))target_directories.insert(path_key(dest));
    }
    // Inventories are sorted; suffix generation is deterministic for preview.
    for(const auto& f:inv.files)if(beneath(f.rel,source)){
        const auto relative=relative_to(f.rel,source);
        auto dest=destination/relative;
        check_name(dest);
        if(existing_dirs.count(path_key(dest))||target_directories.count(path_key(dest)))
            throw std::runtime_error("Merge file collides with a directory: "+utf8(dest));
        const auto other=existing_files.find(path_key(dest));
        if(other!=existing_files.end()){
            if(other->second->content==f.content){++plan.identical;
                plan.skipped.push_back({f.rel,dest,f.bytes,f.mtime,other->second->mtime});continue;}
            ++plan.conflicts;
            // Never silently replace either version of a name collision.
            // Use a deterministic source-suffixed filename within the same folder.
            const auto stem=dest.stem().wstring();const auto ext=dest.extension().wstring();
            const auto tag=std::wstring(L" (from ")+source.filename().wstring()+L")";
            int suffix=0;
            bool already_merged=false;
            while(true){
                if(suffix>100000)throw std::runtime_error("Too many merge filename conflicts");
                auto name=stem+tag+(suffix?L" ("+std::to_wstring(suffix+1)+L")":L"")+ext;
                if(name.size()>240)throw std::runtime_error("Conflict name exceeds safe filename length: "+utf8(dest));
                auto candidate=dest.parent_path()/fs::path(name);
                check_name(candidate);
                const auto key=path_key(candidate);
                auto existing_version=existing_files.find(key);
                if(existing_version!=existing_files.end()&&existing_version->second->content==f.content){
                    ++plan.identical;
                    plan.skipped.push_back({f.rel,candidate,f.bytes,f.mtime,existing_version->second->mtime});
                    already_merged=true;break;
                }
                if(!existing_files.count(key)&&!existing_dirs.count(key)&&!target_files.count(key)&&!target_directories.count(key)){
                    dest=candidate;break;
                }
                ++suffix;
            }
            if(already_merged)continue;
        }else if(target_files.count(path_key(dest)))
            throw std::runtime_error("Merge generates duplicate destination: "+utf8(dest));
        target_files.insert(path_key(dest));
        plan.copies.push_back({f.rel,dest,f.bytes,f.mtime});
    }
    // Directory paths recorded are derived from source directories, never from
    // parent paths outside the selected merge destination.
    for(const auto& d:inv.dirs)if(d.rel!=source&&beneath(d.rel,source)){
        auto target=destination/relative_to(d.rel,source);
        if(target_directories.count(path_key(target)))plan.directories.push_back(target);
    }
    std::sort(plan.directories.begin(),plan.directories.end(),[](const auto& a,const auto& b){
        return std::make_pair(std::distance(a.begin(),a.end()),a)<std::make_pair(std::distance(b.begin(),b.end()),b);
    });
    if(plan.conflicts)plan.warnings.push_back(std::to_string(plan.conflicts)+" different-content name collisions: both versions will be kept");
    plan.warnings.push_back("Copy-only merge. Both original backup folders are retained; source is never deleted.");
    return plan;
}
static void check_retention(const RetentionPolicy& policy){
    if(policy.min_active_copies>32 || (policy.min_active_copies==0&&!policy.allow_zero_active))
        throw std::runtime_error("Retention minimum must be 1..32, or 0 with explicit allow_zero_active consent");
}
static std::vector<size_t> choose_keepers(const Inventory& inv,
                const std::vector<size_t>& candidates,const RetentionPolicy& policy){
    std::vector<size_t> keepers;
    if(policy.min_active_copies==0)return keepers;
    for(const auto id:candidates){
        bool okay=true;
        if(policy.distinct_file_objects)for(auto prior:keepers){
            std::error_code ec;
            const bool identical_object=fs::equivalent(inv.root/inv.files[id].rel,
                                                       inv.root/inv.files[prior].rel,ec);
            if(ec)throw std::runtime_error("Cannot check file object independence");
            if(identical_object){okay=false;break;}
        }
        if(okay)keepers.push_back(id);
        if(keepers.size()==policy.min_active_copies)break;
    }
    return keepers;
}
static Move quarantine_move(const File& file,const std::vector<size_t>& keepers,
                            const Inventory& inv){
    const auto source=file.rel;
    Move m(source,fs::path(L".DiskCurator-System")/L"quarantine"/L"__TX__"/L"data"/source,
        keepers.empty()?fs::path{}:inv.files[keepers.front()].rel,file_snapshot(file),
        keepers.size()>1?inv.files[keepers[1]].rel:fs::path{});
    m.scanned_sha256=file.sha256;
    for(const auto id:keepers){
        const auto& k=inv.files[id];
        m.keepers.push_back(k.rel);
        m.keeper_proofs.push_back({k.rel,k.bytes,k.mtime,k.sha256});
    }
    return m;
}
Plan plan_clean_duplicates(const Inventory& inv,const Duplicates& d,RetentionPolicy policy){
    check_retention(policy);
    if(!inv.complete()||!d.errors.empty())throw std::runtime_error("Quarantine disabled: scan/content verification incomplete");
    Plan p;p.name="Quarantine verified duplicate files";p.retention=policy;
    for(const auto& g:d.groups){
        if(g.size()<=policy.min_active_copies)continue;
        auto keepers=choose_keepers(inv,g,policy);
        if(keepers.size()!=policy.min_active_copies)continue;
        std::set<size_t> keep_set(keepers.begin(),keepers.end());
        for(auto id:g)if(!keep_set.count(id))p.moves.push_back(quarantine_move(inv.files[id],keepers,inv));
    }
    validate_plan(p);
    if(!policy.verify_bytes_before_change){
        for(const auto& m:p.moves){
            if(m.scanned_sha256.size()!=64)throw std::runtime_error("Hash-only verification requires a complete SHA-256 scan");
            for(const auto& keeper:m.keeper_proofs)if(keeper.sha256!=m.scanned_sha256)
                throw std::runtime_error("Keeper hashes do not match");
        }
        p.warnings.push_back("FAST MODE: relies on full-scan SHA-256 and unchanged file metadata; later same-size/same-time modifications may go undetected.");
    }
    if(policy.min_active_copies==0)p.warnings.push_back("HIGH RISK: zero active copies may remain. Files are only recoverable from the software quarantine.");
    p.warnings.push_back("User retention policy: "+std::to_string(policy.min_active_copies)+
        " active byte-verified keeper paths; physical independence "+(policy.distinct_file_objects?"required":"NOT required"));
    p.warnings.push_back("Quarantine is reversible and does NOT free disk capacity.");
    return p;
}
Plan plan_quarantine_selected(const Inventory& inv,const Duplicates& duplicates,
                              const std::vector<fs::path>& selected,RetentionPolicy policy){
    check_retention(policy);
    if(!inv.complete()||!duplicates.errors.empty())
        throw std::runtime_error("Manual quarantine disabled: incomplete scan or content verification");
    Plan plan;plan.name="Quarantine verified duplicate files (manual selection)";plan.retention=policy;
    if(selected.empty())throw std::runtime_error("Select one or more duplicate files first");
    std::map<std::wstring,size_t> membership;
    for(size_t g=0;g<duplicates.groups.size();++g)
        for(auto id:duplicates.groups[g]){
            if(id>=inv.files.size())throw std::runtime_error("Invalid duplicate index");
            if(inv.files[id].content<0)throw std::runtime_error("Unverified duplicate member");
            membership[path_key(inv.files[id].rel)]=g;
        }
    std::map<size_t,std::set<std::wstring>> picks;
    for(const auto& name:selected){
        if(name.empty()||!is_inside(name)||beneath(name,fs::path(L".DiskCurator-System")))
            throw std::runtime_error("Selected path outside ordinary scanned data");
        const auto key=path_key(name);
        auto it=membership.find(key);
        if(it==membership.end())throw std::runtime_error("Not a verified duplicate file: "+utf8(name));
        if(!picks[it->second].insert(key).second)throw std::runtime_error("File selected twice: "+utf8(name));
    }
    for(const auto& [group,chosen]:picks){
        const auto& members=duplicates.groups[group];std::vector<size_t> available;
        for(auto id:members)if(!chosen.count(path_key(inv.files[id].rel)))available.push_back(id);
        auto keepers=choose_keepers(inv,available,policy);
        if(keepers.size()<policy.min_active_copies)
            throw std::runtime_error("Selection would leave fewer than configured " +
                std::to_string(policy.min_active_copies)+" verified active copies: "+
                utf8(inv.files[members.front()].rel));
        for(auto id:members)if(chosen.count(path_key(inv.files[id].rel)))
            plan.moves.push_back(quarantine_move(inv.files[id],keepers,inv));
    }
    validate_plan(plan);
    if(!policy.verify_bytes_before_change){
        for(const auto& m:plan.moves){
            if(m.scanned_sha256.size()!=64)throw std::runtime_error("Hash-only verification requires a complete SHA-256 scan");
            for(const auto& keeper:m.keeper_proofs)if(keeper.sha256!=m.scanned_sha256)
                throw std::runtime_error("Keeper hashes do not match");
        }
        plan.warnings.push_back("FAST MODE: full-scan SHA-256 plus metadata; does not re-read contents before isolating files.");
    }
    if(policy.min_active_copies==0)plan.warnings.push_back("HIGH RISK: zero active copies may remain; confirm quarantine recovery path first.");
    plan.warnings.push_back("Manual duplicate quarantine, keeper minimum "+std::to_string(policy.min_active_copies)+
        (policy.distinct_file_objects?" independent file objects.":" paths; hard links may count as one per path."));
    plan.warnings.push_back("Quarantine is reversible and does not free disk capacity.");
    return plan;
}
RetirementProposal plan_retire_directory(const Inventory& inv,const Duplicates& duplicates,
                                          const fs::path& source_folder,RetentionPolicy policy){
    check_retention(policy);
    if(!inv.complete()||!duplicates.errors.empty())
        throw std::runtime_error("Retirement disabled: incomplete scan/content verification");
    if(source_folder.empty()||source_folder==L"."||!is_inside(source_folder)||beneath(source_folder,fs::path(L".DiskCurator-System")))
        throw std::runtime_error("Select an ordinary source folder inside the root");
    bool exists=false;
    for(const auto& dir:inv.dirs)if(path_key(dir.rel)==path_key(source_folder)){exists=true;break;}
    if(!exists)throw std::runtime_error("Retirement source folder not found in scan");
    RetirementProposal proposal;proposal.source_folder=source_folder;
    proposal.quarantine.name="Quarantine verified duplicate files from "+utf8(source_folder);
    proposal.quarantine.retention=policy;
    std::map<int,const std::vector<size_t>*> groups;
    for(const auto& g:duplicates.groups)for(const auto id:g)groups[inv.files[id].content]=&g;
    for(size_t id=0;id<inv.files.size();++id){
        const auto& file=inv.files[id];if(!beneath(file.rel,source_folder))continue;
        const auto it=groups.find(file.content);
        if(it==groups.end()){proposal.blocked.push_back(file.rel);continue;}
        std::vector<size_t> external;
        for(const auto candidate:*it->second)
            if(candidate!=id&&!beneath(inv.files[candidate].rel,source_folder))external.push_back(candidate);
        auto keepers=choose_keepers(inv,external,policy);
        if(keepers.size()<policy.min_active_copies){proposal.blocked.push_back(file.rel);continue;}
        proposal.quarantine.moves.push_back(quarantine_move(file,keepers,inv));
        proposal.staged_bytes+=file.bytes;
    }
    validate_plan(proposal.quarantine);
    if(!policy.verify_bytes_before_change){
        for(const auto& m:proposal.quarantine.moves){
            if(m.scanned_sha256.size()!=64)throw std::runtime_error("Hash-only verification requires a complete SHA-256 scan");
            for(const auto& keeper:m.keeper_proofs)if(keeper.sha256!=m.scanned_sha256)
                throw std::runtime_error("Keeper hashes do not match");
        }
        proposal.quarantine.warnings.push_back("FAST MODE: scan hashes plus metadata only; content is not rechecked at isolation time.");
    }
    if(policy.min_active_copies==0)proposal.quarantine.warnings.push_back("HIGH RISK: zero active copies may remain. Recovery depends on quarantine.");
    proposal.quarantine.warnings.push_back("At least "+std::to_string(policy.min_active_copies)+
        " policy-approved, byte-verified keepers remain OUTSIDE the retired folder.");
    proposal.quarantine.warnings.push_back("Quarantine does not free disk capacity. No permanent deletion.");
    return proposal;
}

static std::string year_month(fs::file_time_type ft){
    using namespace std::chrono;auto now_file=fs::file_time_type::clock::now();auto now_sys=system_clock::now();
    auto sys=now_sys+duration_cast<system_clock::duration>(ft-now_file);std::time_t t=system_clock::to_time_t(sys);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    std::ostringstream ss;ss<<(tm.tm_year+1900)<<"/"<<std::setw(4)<<std::setfill('0')<<(tm.tm_year+1900)<<"-"<<std::setw(2)<<(tm.tm_mon+1);return ss.str();
}
Plan plan_archive(const Inventory& inv,const ArchiveOptions& opt,const Progress& progress,const std::atomic_bool* cancel){
    if(!is_inside(opt.destination)||opt.destination.empty()||opt.destination==fs::path("."))throw std::runtime_error("Archive destination must be a subfolder");
    if(component_key(opt.destination)==L".diskcurator-system"||beneath(opt.destination,L".DiskCurator-System"))throw std::runtime_error("Invalid archive destination");
    Plan p;p.name=opt.mode==ArchiveMode::ByType?"Archive by file type":"Archive by photo date";
    std::set<std::wstring> reserved;
    // Repeated names (IMG_0001.jpg in hundreds of backups) must not restart
    // probing " (2)", " (3)" from scratch for every file. This cursor makes
    // destination planning nearly linear in the number of name collisions.
    std::map<std::wstring,int> next_suffix;
    size_t done=0,used_fallback=0;
    auto occupied=[&](const fs::path& rel){
        if(reserved.count(path_key(rel)))return true;
        std::error_code ec;auto st=fs::symlink_status(inv.root/rel,ec);
        if(ec==std::errc::no_such_file_or_directory)return false;
        if(ec)throw std::runtime_error("Cannot inspect archive destination "+utf8(rel)+": "+ec.message());
        return st.type()!=fs::file_type::not_found;
    };
    for(auto&f:inv.files){check_abort(cancel);if(!opt.recursive&&!f.rel.parent_path().empty())continue;
        if(beneath(f.rel,opt.destination))continue;
        fs::path sub;
        if(opt.mode==ArchiveMode::ByType)sub=from_utf8(extension_category(f.rel));
        else {
            if(extension_category(f.rel)!="Pictures")continue;
            auto meta=probe_photo(inv.root/f.rel);
            if(meta.valid&&meta.exif_date&&meta.date.size()>=7)sub=from_utf8(meta.date.substr(0,4)+"/"+meta.date.substr(0,7));
            else if(opt.use_mtime_fallback){sub=from_utf8(year_month(f.mtime));++used_fallback;}
            else continue;
        }
        auto dir=opt.destination/sub;
        auto newfile=dir/f.rel.filename();
        const auto stem=newfile.stem().wstring(),ext=newfile.extension().wstring();
        int& cursor=next_suffix[path_key(newfile)];
        if(cursor==0){cursor=2;}
        else newfile=dir/fs::path(stem+L" ("+std::to_wstring(cursor++)+L")"+ext);
        while(occupied(newfile)){
            if(cursor>1000000)throw std::runtime_error("Cannot find free destination filename");
            newfile=dir/fs::path(stem+L" ("+std::to_wstring(cursor++)+L")"+ext);
        }
        reserved.insert(path_key(newfile));p.moves.push_back({f.rel,newfile,{},file_snapshot(f)});
        if(progress && ++done%100==0)progress("Planned "+std::to_string(done)+" archive moves");
    }
    if(used_fallback)p.warnings.push_back(std::to_string(used_fallback)+" photos use file modification date, not EXIF capture date.");
    validate_plan(p);return p;
}
// Journals are append-only. Relative paths are UTF-8 hex encoded to preserve arbitrary characters.
// A process-wide, root-specific kernel mutex serializes mutations across GUI/CLI instances.
// Windows releases abandoned mutexes when a process dies; recovery then inspects the journal.
struct RootMutationLock {
#ifdef _WIN32
    HANDLE handle=nullptr;bool owns=false;
    explicit RootMutationLock(const fs::path& root){
        U64 h=FNV;for(auto c:path_key(root)){h^=static_cast<unsigned>(c);h*=1099511628211ULL;}
        std::wostringstream ss;ss<<L"Local\\DiskCurator-"<<std::hex<<h;
        handle=CreateMutexW(nullptr,FALSE,ss.str().c_str());
        if(!handle)throw std::runtime_error("Cannot acquire root operation mutex");
        auto state=WaitForSingleObject(handle,0);
        if(state!=WAIT_OBJECT_0&&state!=WAIT_ABANDONED){CloseHandle(handle);handle=nullptr;
            throw std::runtime_error("Another File Dedup Organizer instance is changing this directory");}
        owns=true;
    }
    ~RootMutationLock(){if(owns)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
#else
    explicit RootMutationLock(const fs::path&){}
#endif
    RootMutationLock(const RootMutationLock&)=delete;RootMutationLock& operator=(const RootMutationLock&)=delete;
};
static const fs::path SYS=L".DiskCurator-System";
static fs::path journal_dir(const fs::path& root){return root/SYS/L"journal";}
static std::string hex(const std::string& s){static constexpr char abc[]="0123456789abcdef";std::string o;o.reserve(s.size()*2);for(unsigned char c:s){o.push_back(abc[c>>4]);o.push_back(abc[c&15]);}return o;}
static std::string unhex(const std::string& s){if(s.size()%2)throw std::runtime_error("Corrupt journal hex");std::string o;for(size_t i=0;i<s.size();i+=2){auto h=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("Corrupt journal hex digit");};o.push_back(char((h(s[i])<<4)|h(s[i+1])));}return o;}
// A C++ stream flush only drains userspace buffers. A recovery marker must
// request an OS-level persistence barrier before the corresponding file moves.
// This improves crash consistency, but does not replace filesystem/power-loss
// fault injection: directory-entry durability is platform dependent.
static void append(const fs::path& f,const std::string& s){
    const std::string record=s+"\n";
#ifdef _WIN32
    HANDLE h=CreateFileW(f.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,
        OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open transaction journal for durable append");
    DWORD written=0;
    const bool ok=record.size()<=MAXDWORD &&
        WriteFile(h,record.data(),static_cast<DWORD>(record.size()),&written,nullptr)!=0 &&
        written==record.size() && FlushFileBuffers(h)!=0;
    const bool closed=CloseHandle(h)!=0;
    if(!ok||!closed)throw std::runtime_error("Transaction journal write/FlushFileBuffers failed");
#else
    const int fd=::open(f.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fd<0)throw std::runtime_error("Cannot open transaction journal for durable append");
    size_t position=0;bool ok=true;
    while(position<record.size()){
        const ssize_t n=::write(fd,record.data()+position,record.size()-position);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){ok=false;break;}
        position+=static_cast<size_t>(n);
    }
    if(ok && ::fsync(fd)!=0)ok=false;
    if(::close(fd)!=0)ok=false;
    if(!ok)throw std::runtime_error("Transaction journal write/fsync failed");
#endif
}
struct Tx {fs::path path;std::string id,label,state;std::vector<Move> moves,copies;std::vector<fs::path> directories;bool copy_mode=false;};
static Tx load_tx(const fs::path& journal){
    Tx t;t.path=journal;t.id=utf8(journal.stem());
    std::ifstream in(journal,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read journal");
    // A partial, corrupted or unrecognized journal must NEVER be silently
    // interpreted as a valid transaction. The state machine is append-only.
    bool labeled=false,pending=false,staged=false,restoring=false,done=false,undone=false,rolled_back=false,recovery_failed=false;
    std::string line;
    while(std::getline(in,line)){
        if(line.empty()||line.size()>8*1024*1024||line.find('\0')!=std::string::npos)
            throw std::runtime_error("Empty or oversized journal record");
        std::istringstream s(line);std::string tag;s>>tag;
        if(tag=="LABEL"){
            std::string x,extra;
            if(labeled||pending||!(s>>x)||(s>>extra))throw std::runtime_error("Invalid LABEL record");
            t.label=unhex(x);labeled=true;
        }else if(tag=="MOVE"){
            std::string a,b,c,extra;
            if(!labeled||pending||t.copy_mode||!(s>>a>>b>>c)||(s>>extra))throw std::runtime_error("Invalid MOVE record");
            t.moves.push_back({from_utf8(unhex(a)),from_utf8(unhex(b)),from_utf8(unhex(c))});
            if(t.moves.size()>1000000)throw std::runtime_error("Too many journal moves");
        }else if(tag=="COPY"){
            std::string a,b,c,extra;
            if(!labeled||pending||!t.moves.empty()||t.label.compare(0,19,"Merge directories: ")!=0||
               !(s>>a>>b>>c)||(s>>extra))throw std::runtime_error("Invalid COPY record");
            t.copy_mode=true;
            t.copies.push_back({from_utf8(unhex(a)),from_utf8(unhex(b)),from_utf8(unhex(c))});
            if(t.copies.size()>1000000)throw std::runtime_error("Too many journal copies");
        }else if(tag=="MKDIR"){
            std::string a,extra;
            if(!labeled||pending||!t.moves.empty()||t.label.compare(0,19,"Merge directories: ")!=0||
               !(s>>a)||(s>>extra))throw std::runtime_error("Invalid MKDIR record");
            t.copy_mode=true;t.directories.push_back(from_utf8(unhex(a)));
            if(t.directories.size()>1000000)throw std::runtime_error("Too many journal directories");
        }else if(tag=="PARTIAL"){
            std::string extra;
            if((s>>extra)||!t.copy_mode||!pending||done||rolled_back||recovery_failed)
                throw std::runtime_error("Invalid PARTIAL transition");
            rolled_back=true;t.state=tag;
        }else if(tag=="PENDING"){
            std::string extra;
            if((s>>extra)||!labeled||pending||(t.moves.empty()&&t.copies.empty()&&t.directories.empty()))throw std::runtime_error("Invalid PENDING transition");
            pending=true;t.state=tag;
        }else if(tag=="STAGEDALL"){
            std::string extra;
            if((s>>extra)||!pending||staged||done||rolled_back||recovery_failed)throw std::runtime_error("Invalid STAGEDALL transition");
            staged=true;t.state=tag;
        }else if(tag=="RESTORING"){
            std::string extra;
            if((s>>extra)||t.copy_mode||!staged||restoring||done||rolled_back||recovery_failed)
                throw std::runtime_error("Invalid RESTORING transition");
            restoring=true;t.state=tag;
        }else if(tag=="DONE"){
            std::string extra;
            if((s>>extra)||!staged||restoring||done||rolled_back||recovery_failed)throw std::runtime_error("Invalid DONE transition");
            done=true;t.state=tag;
        }else if(tag=="UNDONE"){
            std::string extra;
            if((s>>extra)||!done||undone)throw std::runtime_error("Invalid UNDONE transition");
            undone=true;t.state=tag;
        }else if(tag=="ROLLBACK"){
            std::string extra;
            if((s>>extra)||!pending||done||rolled_back||recovery_failed)throw std::runtime_error("Invalid ROLLBACK transition");
            rolled_back=true;t.state=tag;
        }else if(tag=="RECOVERY_FAILED"){
            std::string extra;
            if((s>>extra)||!pending||done||rolled_back||recovery_failed)throw std::runtime_error("Invalid RECOVERY_FAILED transition");
            recovery_failed=true;t.state=tag;
        }else throw std::runtime_error("Unknown journal record type");
    }
    if(!in.eof()||!pending||!labeled||(t.moves.empty()&&t.copies.empty()&&t.directories.empty()))
        throw std::runtime_error("Incomplete or unreadable transaction journal");
    // Recovery executes filesystem moves. A journal must never be allowed to turn
    // an absolute/traversal path or a forged staging path into a recovery move.
    if(t.id.empty()||t.id.size()>96||!std::all_of(t.id.begin(),t.id.end(),[](unsigned char c){
            return (c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_';
        }))throw std::runtime_error("Unsafe transaction identifier");
    if(t.moves.size()>1000000)throw std::runtime_error("Transaction contains too many moves");
    std::set<std::wstring> sources,targets;
    for(size_t i=0;i<t.moves.size();++i){const auto& m=t.moves[i];
        if(m.from.empty()||m.to.empty()||!is_inside(m.from)||!is_inside(m.to)||
           !is_inside(m.verify_against))throw std::runtime_error("Unsafe path in journal");
        const auto expected=fs::path(L".DiskCurator-System")/L"stage"/from_utf8(t.id)/from_utf8(std::to_string(i));
        if(path_key(m.verify_against)!=path_key(expected))
            throw std::runtime_error("Journal staging path does not match transaction");
        if(!sources.insert(path_key(m.from)).second||!targets.insert(path_key(m.to)).second)
            throw std::runtime_error("Duplicate move in journal");
        if(path_key(m.from)==path_key(m.to)||beneath(m.to,m.from))
            throw std::runtime_error("Unsafe source/destination in journal");
        // Historical journals are untrusted: a forged DONE record must not
        // turn Undo into a move out of our owner marker, journal or stage.
        if(beneath(m.from,SYS)){
            const bool valid_undo=t.label.compare(0,6,"Undo: ")==0 &&
                beneath(m.from,SYS/L"quarantine");
            if(!valid_undo)throw std::runtime_error("Journal source points into reserved workspace");
        }
        if(beneath(m.to,SYS)){
            const auto quarantine=SYS/L"quarantine"/from_utf8(t.id)/L"data";
            if(t.label.compare(0,std::string("Quarantine verified duplicate files").size(),"Quarantine verified duplicate files")!=0||!beneath(m.to,quarantine))
                throw std::runtime_error("Journal destination points into reserved workspace");
        }
    }
    if(t.copy_mode){
        if(!t.moves.empty()||t.label.compare(0,19,"Merge directories: ")!=0)
            throw std::runtime_error("Mixed or invalid COPY journal");
        for(size_t i=0;i<t.copies.size();++i){const auto& c=t.copies[i];
            const auto expected=SYS/L"stage"/from_utf8(t.id)/from_utf8(std::to_string(i));
            if(c.from.empty()||c.to.empty()||!is_inside(c.from)||!is_inside(c.to)||
               beneath(c.from,SYS)||beneath(c.to,SYS)||path_key(c.verify_against)!=path_key(expected)||
               path_key(c.from)==path_key(c.to)||!sources.insert(path_key(c.from)).second||
               !targets.insert(path_key(c.to)).second)
                throw std::runtime_error("Unsafe COPY in journal");
        }
        for(const auto& d:t.directories){
            if(d.empty()||!is_inside(d)||beneath(d,SYS)||!targets.insert(path_key(d)).second)
                throw std::runtime_error("Unsafe MKDIR in journal");
        }
    }
    return t;
}
// The reserved workspace is NOT trusted merely because it has the right name.
// Reject symlinks/junctions (including a forged journal or owner marker),
// unexpected file types and a workspace lacking the required ownership marker.
// This check also runs before reading historical journals, not only on writes.
static bool existing_ordinary(const fs::path& p,bool want_dir){
    std::error_code ec;auto st=fs::symlink_status(p,ec);
    if(ec==std::errc::no_such_file_or_directory)return false;
    if(ec)throw std::runtime_error("Cannot inspect workspace path: "+utf8(p)+": "+ec.message());
    if(fs::is_symlink(st)||reparse(p) ||
       (want_dir?!fs::is_directory(st):!fs::is_regular_file(st)))
        throw std::runtime_error("Unsafe workspace entry (reparse point or unexpected type): "+utf8(p));
    return true;
}
static void verify_workspace(const fs::path& root){
    if(!existing_ordinary(root/SYS,true)){
        // When there is no workspace, recovery has nothing to do.
        return;
    }
    const auto owner=root/SYS/L"owner.txt";
    if(!existing_ordinary(owner,false))
        throw std::runtime_error("Reserved workspace has no owner marker; refuse operations");
    // v1.0-rc3 accidentally wrote owner.txt through a Windows text-mode stream,
    // producing CRLF, while this reader used binary mode. Accept both exact
    // byte spellings so existing workspaces (and their journals/quarantine) are
    // usable without destructive migration. Reject any trailing payload.
    std::ifstream in(owner,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read the workspace owner marker");
    std::array<char,64> marker_bytes{};
    in.read(marker_bytes.data(),static_cast<std::streamsize>(marker_bytes.size()));
    const auto count=static_cast<size_t>(in.gcount());
    const std::string marker(marker_bytes.data(),count);
    if(in.bad()||count==marker_bytes.size()||
       (marker!="DiskCurator Native workspace v1\n" &&
        marker!="DiskCurator Native workspace v1\r\n"))
        throw std::runtime_error("Invalid workspace owner marker");
    for(auto name:{L"journal",L"stage",L"quarantine"})
        (void)existing_ordinary(root/SYS/name,true);
}
static std::vector<Tx> all_tx(const fs::path& root){
    verify_workspace(root);
    std::vector<Tx> a;std::error_code ec;auto dir=journal_dir(root);
    if(!fs::exists(dir,ec)){if(ec)throw std::runtime_error("Cannot inspect journal directory");return a;}
    fs::directory_iterator it(dir,ec),end;
    if(ec)throw std::runtime_error("Cannot list journal directory: "+ec.message());
    for(;it!=end;it.increment(ec)){
        if(ec)throw std::runtime_error("Cannot traverse journal directory: "+ec.message());
        if(it->path().extension()!=L".log")continue;
        try{if(!reparse(it->path())&&it->is_regular_file())a.push_back(load_tx(it->path()));
            else throw std::runtime_error("Journal entry is not an ordinary file");}
        catch(const std::exception& e){Tx t;t.path=it->path();t.id=utf8(it->path().stem());
            t.state="RECOVERY_FAILED";t.label=std::string("Corrupt or unreadable journal: ")+e.what();a.push_back(std::move(t));}
    }
    if(ec)throw std::runtime_error("Cannot traverse journal directory: "+ec.message());
    std::sort(a.begin(),a.end(),[](auto& x,auto& y){return x.id<y.id;});return a;
}
static std::string txid(){static std::atomic<unsigned> num{0};auto t=std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream s;s<<std::setw(20)<<std::setfill('0')<<t<<"-"<<std::setw(5)<<num++;return s.str();}
static fs::path transaction_destination(const fs::path& destination,const std::string& id){
    const auto placeholder=SYS/L"quarantine"/L"__TX__"/L"data";
    if(!beneath(destination,placeholder))return destination;
    // Validate and skip exactly the reserved prefix, preserving literal
    // "__TX__" components in the user's original file or directory name.
    auto tail=destination.begin();
    for(const auto& expected:placeholder){
        if(tail==destination.end()||component_key(*tail)!=component_key(expected))
            throw std::runtime_error("Invalid quarantine destination");
        ++tail;
    }
    auto resolved=SYS/L"quarantine"/from_utf8(id)/L"data";
    for(;tail!=destination.end();++tail)resolved/=*tail;
    return resolved;
}
static void mkdir_safe(const fs::path& root,const fs::path& relative){
    if(!is_inside(relative))throw std::runtime_error("Unsafe relative path");
    fs::path p=root;
    for(auto& part:relative){p/=part;std::error_code ec;
        if(fs::exists(p,ec)){if(ec||reparse(p)||!fs::is_directory(p,ec))throw std::runtime_error("Unsafe destination directory: "+utf8(p));}
        else if(!fs::create_directory(p,ec)||ec)throw std::runtime_error("Could not create: "+utf8(p)+" "+ec.message());
    }
}
static void init_workspace(const fs::path& root){verify_workspace(root);auto dir=root/SYS;std::error_code ec;
    if(fs::exists(dir,ec)&&!fs::exists(dir/L"owner.txt"))throw std::runtime_error("Reserved .DiskCurator-System directory already exists; refuse to use it");
    mkdir_safe(root,SYS);if(!fs::exists(dir/L"owner.txt")){
        // Always emit LF bytes on every platform, independent of the CRT locale
        // and Windows text-mode newline conversion.
        std::ofstream marker(dir/L"owner.txt",std::ios::binary|std::ios::trunc);
        marker<<"DiskCurator Native workspace v1\n";
        marker.flush();if(!marker)throw std::runtime_error("Cannot write workspace owner marker");
    }
    mkdir_safe(root,SYS/L"journal");mkdir_safe(root,SYS/L"stage");mkdir_safe(root,SYS/L"quarantine");
#ifdef _WIN32
    SetFileAttributesW(dir.c_str(),GetFileAttributesW(dir.c_str())|FILE_ATTRIBUTE_HIDDEN);
#endif
}
static bool path_exists_checked(const fs::path& p){std::error_code ec;auto b=fs::exists(p,ec);if(ec)throw std::runtime_error("Cannot check path: "+utf8(p));return b;}
static void check_source_ancestors(const fs::path& root,const fs::path& relative){
    if(!is_inside(relative)||reparse(root))throw std::runtime_error("Unsafe root/source path");
    auto p=root;for(auto it=relative.begin();it!=relative.end();++it){p/=*it;
        auto next=it;++next;if(next==relative.end())break;
        std::error_code ec;if(reparse(p)||!fs::is_directory(p,ec)||ec)throw std::runtime_error("Unsafe source parent: "+utf8(p));
    }
}
// Recovery often recreates parent folders that were removed by the interrupted
// transaction. Check *existing* ancestors for reparse points, but allow missing
// parents, which mkdir_safe recreates immediately before restoration.
static void check_recovery_ancestors(const fs::path& root,const fs::path& relative){
    if(!is_inside(relative)||reparse(root))throw std::runtime_error("Unsafe recovery path");
    auto p=root;
    for(auto it=relative.begin();it!=relative.end();++it){
        auto next=it;++next;if(next==relative.end())break;
        p/=*it;std::error_code ec;
        const bool exists=fs::exists(p,ec);
        if(ec&&ec!=std::errc::no_such_file_or_directory)throw std::runtime_error("Cannot inspect recovery parent: "+utf8(p));
        if(exists&&(reparse(p)||!fs::is_directory(p,ec)||ec))throw std::runtime_error("Unsafe recovery parent: "+utf8(p));
    }
}
// A preflight existence check cannot reserve a name. Both forward moves and
// rollback must ask the filesystem to refuse replacement atomically.
static void move_one(const fs::path& from,const fs::path& to){
    std::error_code error;
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH))
        error={static_cast<int>(GetLastError()),std::system_category()};
#elif defined(__linux__) && defined(SYS_renameat2)
    if(::syscall(SYS_renameat2,AT_FDCWD,from.c_str(),AT_FDCWD,to.c_str(),RENAME_NOREPLACE)!=0)
        error={errno,std::generic_category()};
#elif defined(__APPLE__)
    if(::renamex_np(from.c_str(),to.c_str(),RENAME_EXCL)!=0)
        error={errno,std::generic_category()};
#else
    error=std::make_error_code(std::errc::operation_not_supported);
#endif
    if(error)throw std::runtime_error("Cannot move without replacing "+utf8(from)+" -> "+utf8(to)+": "+error.message());
}
// For additive merges, recovery never deletes a destination file. Completed
// copies remain visible; unfinished temporary files are discarded from the
// software-owned stage only. The user can rescan and repeat the merge.
static void recover_copy_tx(const fs::path& root,const Tx& tx){
    for(const auto& c:tx.copies){
        check_recovery_ancestors(root,c.verify_against);
        const auto staged=root/c.verify_against;
        std::error_code ec;auto st=fs::symlink_status(staged,ec);
        if(ec==std::errc::no_such_file_or_directory)continue;
        if(ec)throw std::runtime_error("Cannot inspect merge stage: "+ec.message());
        if(!fs::is_regular_file(st)||reparse(staged))throw std::runtime_error("Unsafe merge stage object");
        if(!fs::remove(staged,ec)||ec)throw std::runtime_error("Cannot remove incomplete merge stage");
    }
    append(tx.path,"PARTIAL");
}
static void rollback_tx(const fs::path& root,const Tx& tx){
    // Never guess which of two conflicting files belongs to the transaction.
    // Stage and destination are mutually exclusive after the STAGEDALL marker.
    if(tx.state=="STAGEDALL"){
        for(const auto& m:tx.moves){const auto stage=root/m.verify_against,dest=root/m.to;
            check_recovery_ancestors(root,m.verify_against);
            check_recovery_ancestors(root,m.to);
            const bool staged=path_exists_checked(stage),committed=path_exists_checked(dest);
            if(staged&&committed)throw std::runtime_error("Recovery conflict: staged and committed copies both exist: "+utf8(m.to));
            if(staged&&reparse(stage))throw std::runtime_error("Reparse point in recovery stage: "+utf8(stage));
            if(committed&&reparse(dest))throw std::runtime_error("Reparse point in recovery target: "+utf8(dest));
            if(!staged&&!committed)throw std::runtime_error("Recovery data missing from stage and target: "+utf8(m.from));
            if(!staged){mkdir_safe(root,m.verify_against.parent_path());move_one(dest,stage);}
        }
        // All committed destinations are back in stage. Record the phase
        // before touching original names, which may overlap the destinations.
        // A second interruption can now resume even for a cyclic rename.
        append(tx.path,"RESTORING");
    }
    for(auto it=tx.moves.rbegin();it!=tx.moves.rend();++it){const auto stage=root/it->verify_against,original=root/it->from;
        check_recovery_ancestors(root,it->verify_against);
        check_recovery_ancestors(root,it->from);
        const bool staged=path_exists_checked(stage);
        const bool at_source=path_exists_checked(original);
        if(staged==at_source)
            throw std::runtime_error(staged?"Recovery conflict at "+utf8(original):
                                     "Recovery data missing from source and stage: "+utf8(it->from));
        if(reparse(staged?stage:original))throw std::runtime_error("Reparse point in recovery path: "+utf8(it->from));
        if(staged){
            mkdir_safe(root,it->from.parent_path());
            move_one(stage,original);
        }
    }
}
std::vector<std::string> recover_incomplete(const fs::path& root){RootMutationLock mutation_guard(root);std::vector<std::string> report;
    auto transactions=all_tx(root);
    for(auto& t:transactions)if(t.state=="RECOVERY_FAILED")report.push_back("MANUAL RECOVERY NEEDED "+t.id+": "+t.label);
    if(!report.empty())return report;
    // Undo commits its reverse MOVE before marking the original UNDONE. If
    // the process stopped between those durable writes, finish the marker
    // without executing the reverse MOVE again (especially a swap).
    std::map<std::string,Tx*> originals;
    for(auto& original:transactions)
        if(original.state=="DONE"&&!original.copy_mode&&original.label.compare(0,6,"Undo: ")!=0)
            originals.emplace(original.id,&original);
    for(const auto& reverse:transactions){
        if(reverse.state!="DONE"||reverse.label.compare(0,6,"Undo: ")!=0)continue;
        const auto original=originals.find(reverse.label.substr(6));
        if(original==originals.end())continue;
        const auto& forward=original->second->moves;
        const bool matches=forward.size()==reverse.moves.size()&&
            std::equal(forward.begin(),forward.end(),reverse.moves.begin(),[](const Move& a,const Move& b){
                return path_key(a.from)==path_key(b.to)&&path_key(a.to)==path_key(b.from);
            });
        if(!matches)return {"MANUAL RECOVERY NEEDED "+reverse.id+": Undo paths disagree with original journal"};
        append(original->second->path,"UNDONE");
        report.push_back("Recovered completed Undo for "+original->first);
        originals.erase(original);
    }
    for(auto& t:transactions)if(t.state=="PENDING"||t.state=="STAGEDALL"||t.state=="RESTORING"){
        try{if(t.copy_mode){recover_copy_tx(root,t);report.push_back("Recovered partial additive merge "+t.id+"; completed copies retained; rescan before retrying");}
            else {rollback_tx(root,t);append(t.path,"ROLLBACK");report.push_back("Recovered interrupted transaction "+t.id);}}
        catch(const std::exception&e){append(t.path,"RECOVERY_FAILED");report.push_back("MANUAL RECOVERY NEEDED "+t.id+": "+e.what());}}
    return report;
}
Result execute_plan(const fs::path& input,const Plan& plan){
    Result result;if(plan.moves.empty()){result.success=true;result.message="Nothing to change";return result;}
    auto root=fs::absolute(input).lexically_normal();validate_plan(plan,plan.name.compare(0,6,"Undo: ")==0);
    std::error_code root_ec;if(!fs::is_directory(root,root_ec)||root_ec||reparse(root))return {false,"Selected root is unsafe or missing",{}};
    RootMutationLock mutation_guard(root);
    for(auto& e:recover_incomplete(root))if(e.compare(0,6,"MANUAL")==0)return {false,e,{}};
    std::string id=txid();result.transaction=id;
    try{
        std::vector<Move> moves;
        std::set<std::wstring> sources,dests;
        for(size_t i=0;i<plan.moves.size();++i){auto& m=plan.moves[i];auto dst=transaction_destination(m.to,id);
            moves.push_back({m.from,dst,SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i))});
            sources.insert(path_key(m.from));if(!dests.insert(path_key(dst)).second)throw std::runtime_error("Duplicate target");
            check_source_ancestors(root,m.from);
            if(!path_exists_checked(root/m.from)||reparse(root/m.from))throw std::runtime_error("Source vanished or is a reparse point: "+utf8(m.from));
            verify_snapshot(root,m);
            if(plan.name.compare(0,std::string("Quarantine verified duplicate files").size(),"Quarantine verified duplicate files")==0){
                check_retention(plan.retention);
                if(m.keepers.size()!=plan.retention.min_active_copies ||
                   (m.keepers.size()&&path_key(m.verify_against)!=path_key(m.keepers.front())))
                    throw std::runtime_error("Quarantine keeper count does not match user policy");
                std::vector<fs::path> checked;
                std::error_code ec;
                const auto size=fs::file_size(root/m.from,ec);
                if(ec)throw std::runtime_error("Cannot inspect file before quarantine");
                for(const auto& keeper:m.keepers){
                    if(keeper.empty()||!is_inside(keeper)||path_key(keeper)==path_key(m.from)||
                       sources.count(path_key(keeper))||beneath(keeper,fs::path(L".DiskCurator-System")))
                        throw std::runtime_error("Keeper is outside allowed active set");
                    check_source_ancestors(root,keeper);
                    const auto item=root/keeper;
                    if(!path_exists_checked(item)||reparse(item)||!fs::is_regular_file(item,ec)||ec)
                        throw std::runtime_error("Keeper is missing or unsafe");
                    for(const auto& prior:checked)if(path_key(prior)==path_key(keeper))
                        throw std::runtime_error("Repeated keeper path");
                    if(plan.retention.distinct_file_objects)for(const auto& prior:checked){
                        const bool same=fs::equivalent(root/prior,item,ec);
                        if(ec||same)throw std::runtime_error("Not independent keeper file objects");
                    }
                    if(plan.retention.verify_bytes_before_change){
                        const auto equal=equal_bytes(root/m.from,item,size);
                        if(!equal||!*equal)throw std::runtime_error("Keeper content changed since scan");
                    }else{
                        if(m.scanned_sha256.size()!=64||m.keeper_proofs.size()!=m.keepers.size())
                            throw std::runtime_error("Hash-only plan lacks full scan evidence");
                        const auto& proof=m.keeper_proofs[checked.size()];
                        if(path_key(proof.rel)!=path_key(keeper)||proof.bytes!=size||
                           proof.sha256!=m.scanned_sha256||
                           fs::file_size(item,ec)!=proof.bytes||ec||
                           fs::last_write_time(item,ec)!=proof.mtime||ec)
                            throw std::runtime_error("Hash-only keeper metadata differs from full scan");
                    }
                    checked.push_back(keeper);
                }
            }
            if(!m.verify_against.empty()&&(
                    plan.name.compare(0,std::string("Quarantine verified duplicate files").size(),"Quarantine verified duplicate files")!=0 ||
                    plan.retention.verify_bytes_before_change)){
                check_source_ancestors(root,m.verify_against);
                auto from=root/m.from,keep=root/m.verify_against;std::error_code ec;
                if(!path_exists_checked(keep)||reparse(keep)||!fs::is_regular_file(from,ec)||!fs::is_regular_file(keep,ec))throw std::runtime_error("Keeper missing or unsafe: "+utf8(m.verify_against));
                auto size=fs::file_size(from,ec);auto eq=equal_bytes(from,keep,size);
                if(!eq||!*eq)throw std::runtime_error("Duplicate changed since scan: "+utf8(m.from));
            }
        }
        for(auto&m:moves)if(path_exists_checked(root/m.to)&&sources.find(path_key(m.to))==sources.end())throw std::runtime_error("Target already exists: "+utf8(m.to));
        init_workspace(root);mkdir_safe(root,SYS/L"stage"/from_utf8(id));
        auto logfile=journal_dir(root)/(from_utf8(id+".log"));
        append(logfile,"LABEL "+hex(plan.name));
        for(auto&m:moves)append(logfile,"MOVE "+hex(utf8(m.from))+" "+hex(utf8(m.to))+" "+hex(utf8(m.verify_against)));
        append(logfile,"PENDING");
        try{
            for(auto&m:moves)move_one(root/m.from,root/m.verify_against);
            // Quarantine sources are now in our stage. Check again that two
            // independent, byte-identical keepers remain *before* commit.
            // This closes the long interval between planning and staging.
            if(plan.name.compare(0,std::string("Quarantine verified duplicate files").size(),"Quarantine verified duplicate files")==0){
                for(size_t i=0;i<plan.moves.size();++i){
                    const auto& original=plan.moves[i];
                    const auto stage=root/moves[i].verify_against;
                    if(original.keepers.size()!=plan.retention.min_active_copies)
                        throw std::runtime_error("Keeper count changed after staging");
                    std::error_code check;const auto n=fs::file_size(stage,check);
                    if(check)throw std::runtime_error("Cannot stat staged quarantine file");
                    std::vector<fs::path> checked;
                    for(const auto& keeper:original.keepers){
                        check_source_ancestors(root,keeper);
                        const auto item=root/keeper;
                        if(!fs::is_regular_file(item,check)||check||reparse(item))
                            throw std::runtime_error("Keeper disappeared after staging");
                        if(plan.retention.distinct_file_objects)for(const auto& prior:checked){
                            const bool same=fs::equivalent(root/prior,item,check);
                            if(check||same)throw std::runtime_error("Keeper independence lost");
                        }
                        if(plan.retention.verify_bytes_before_change){
                            const auto same=equal_bytes(stage,item,n);
                            if(!same||!*same)throw std::runtime_error("Keeper content changed after staging");
                        }else{
                            if(original.keeper_proofs.size()!=original.keepers.size()||
                               original.scanned_sha256.size()!=64)
                                throw std::runtime_error("Hash-only keeper evidence missing");
                            const auto& proof=original.keeper_proofs[checked.size()];
                            if(path_key(proof.rel)!=path_key(keeper)||proof.bytes!=n||
                               proof.sha256!=original.scanned_sha256||
                               fs::file_size(item,check)!=proof.bytes||check||
                               fs::last_write_time(item,check)!=proof.mtime||check)
                                throw std::runtime_error("Keeper metadata changed after staging");
                        }
                        checked.push_back(keeper);
                    }
                }
            }
            append(logfile,"STAGEDALL");
            for(auto&m:moves){mkdir_safe(root,m.to.parent_path());if(path_exists_checked(root/m.to))throw std::runtime_error("Destination appeared during operation");move_one(root/m.verify_against,root/m.to);}
            append(logfile,"DONE");
            result.success=true;result.message="Applied "+std::to_string(moves.size())+" changes. Undo is available.";
        }catch(const std::exception&e){auto tx=load_tx(logfile);
            try{rollback_tx(root,tx);append(logfile,"ROLLBACK");result.message=std::string("Failed and rolled back: ")+e.what();}
            catch(const std::exception&r){append(logfile,"RECOVERY_FAILED");result.message=std::string("Recovery needed: ")+e.what()+"; "+r.what();} }
    }catch(const std::exception&e){result.message=e.what();}
    return result;
}
static void sync_staged_file(const fs::path& p){
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open staged file for FlushFileBuffers");
    const bool ok=FlushFileBuffers(h)!=0;
    CloseHandle(h);
    if(!ok)throw std::runtime_error("Cannot flush staged copy before commit");
#else
    const int fd=::open(p.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)throw std::runtime_error("Cannot open staged copy for fsync");
    const bool ok=::fsync(fd)==0;
    ::close(fd);
    if(!ok)throw std::runtime_error("Cannot fsync staged copy before commit");
#endif
}
// A copy-only merge has an additive transaction. The source files are never
// moved/deleted; visible completed copies are never automatically rolled back.
// The only auto-discarded data are incomplete files in the app-owned stage.
static void commit_merge_file_noreplace(const fs::path& stage,const fs::path& dest){
#ifdef _WIN32
    if(!MoveFileExW(stage.c_str(),dest.c_str(),MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot commit staged merge copy without overwrite: "+utf8(dest));
#else
    // Hard-link creation fails atomically when the destination already exists;
    // rename() would overwrite existing files on POSIX and must not be used.
    std::error_code ec;
    fs::create_hard_link(stage,dest,ec);
    if(ec)throw std::runtime_error("Cannot commit merge copy without overwrite: "+utf8(dest)+": "+ec.message());
    fs::remove(stage,ec); // the link at dest is already a complete verified copy
    if(ec)throw std::runtime_error("Committed merge copy but cannot remove stage link: "+ec.message());
#endif
}
Result execute_merge_plan(const fs::path& input,const MergePlan& plan){
    if(plan.copies.empty()&&plan.directories.empty())return {true,"Nothing to add; directory backups already contain the same entries",{}};
    const auto root=fs::absolute(input).lexically_normal();
    std::error_code ec;
    if(!fs::is_directory(root,ec)||ec||reparse(root))return {false,"Merge root is missing or unsafe",{}};
    if(plan.source.empty()||plan.destination.empty()||!is_inside(plan.source)||!is_inside(plan.destination)||
       beneath(plan.source,plan.destination)||beneath(plan.destination,plan.source)||
       beneath(plan.source,SYS)||beneath(plan.destination,SYS))
        return {false,"Invalid merge source or destination",{}};
    RootMutationLock guard(root);
    for(const auto& s:recover_incomplete(root))if(s.compare(0,6,"MANUAL")==0)return {false,s,{}};
    Result result;
    try{
        // Reject all newly-added/removed/renamed files after the plan preview,
        // including data in empty directories and both selected tree roots.
        auto fresh_source=scan(root/plan.source),fresh_dest=scan(root/plan.destination);
        if(!fresh_source.complete()||!fresh_dest.complete()||
            snapshot(fresh_source,{},true)!=plan.source_snapshot||
            snapshot(fresh_dest,{},true)!=plan.destination_snapshot)
            throw std::runtime_error("Merge trees changed since preview; rescan and regenerate the merge plan");
        // In addition to the structural snapshots, revalidate every skipped
        // 'identical' pair with actual bytes before any destination changes.
        for(const auto& k:plan.skipped){
            if(!beneath(k.from,plan.source)||!beneath(k.to,plan.destination))
                throw std::runtime_error("Invalid merge identical pair");
            check_source_ancestors(root,k.from);check_source_ancestors(root,k.to);
            if(reparse(root/k.from)||reparse(root/k.to))throw std::runtime_error("Reparse in merge identical pair");
            auto eq=equal_bytes(root/k.from,root/k.to,k.bytes,k.from_time,k.to_time);
            if(!eq||!*eq)throw std::runtime_error("Previously identical merge files changed; rescan");
        }
        std::set<std::wstring> reserved;
        for(const auto& d:plan.directories){
            if(d.empty()||!is_inside(d)||!beneath(d,plan.destination)||beneath(d,SYS)||
               !reserved.insert(path_key(d)).second||path_exists_checked(root/d))
                throw std::runtime_error("Invalid or occupied merge directory: "+utf8(d));
            check_recovery_ancestors(root,d);
        }
        for(const auto& c:plan.copies){
            if(c.from.empty()||c.to.empty()||!is_inside(c.from)||!is_inside(c.to)||
                !beneath(c.from,plan.source)||!beneath(c.to,plan.destination)||
                !reserved.insert(path_key(c.to)).second||path_exists_checked(root/c.to))
                throw std::runtime_error("Invalid or occupied merge destination: "+utf8(c.to));
            check_source_ancestors(root,c.from);check_recovery_ancestors(root,c.to);
            if(reparse(root/c.from)||!fs::is_regular_file(root/c.from,ec)||ec||
                fs::file_size(root/c.from,ec)!=c.bytes||ec||
                fs::last_write_time(root/c.from,ec)!=c.mtime||ec)
                throw std::runtime_error("Merge source changed after preview: "+utf8(c.from));
        }
        // Directory creation must be strictly below target root. The destination
        // base already existed at preview and was re-scanned above.
        init_workspace(root);
        const auto id=txid();result.transaction=id;
        const auto journal=journal_dir(root)/from_utf8(id+".log");
        append(journal,"LABEL "+hex("Merge directories: "+utf8(plan.source)+" -> "+utf8(plan.destination)));
        for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
            auto stage=SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
            append(journal,"COPY "+hex(utf8(c.from))+" "+hex(utf8(c.to))+" "+hex(utf8(stage)));
        }
        for(const auto& d:plan.directories)append(journal,"MKDIR "+hex(utf8(d)));
        append(journal,"PENDING");
        try{
            mkdir_safe(root,SYS/L"stage"/from_utf8(id));
            // Prepare all copies in the software-owned stage before modifying
            // the visible destination. This avoids partly written JPEGs/files.
            for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
                const auto stage=root/SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
                std::error_code cp;
                if(!fs::copy_file(root/c.from,stage,fs::copy_options::none,cp)||cp)
                    throw std::runtime_error("Cannot stage copy "+utf8(c.from)+": "+cp.message());
                auto eq=equal_bytes(root/c.from,stage,c.bytes,c.mtime);
                if(!eq||!*eq)throw std::runtime_error("Source changed or staged copy verification failed: "+utf8(c.from));
                fs::last_write_time(stage,c.mtime,cp);
                if(cp)throw std::runtime_error("Failed to preserve merged file timestamp: "+cp.message());
                sync_staged_file(stage);
            }
            append(journal,"STAGEDALL");
            for(const auto& d:plan.directories)mkdir_safe(root,d);
            for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
                const auto stage=root/SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
                check_recovery_ancestors(root,c.to);
                mkdir_safe(root,c.to.parent_path());
                if(path_exists_checked(root/c.to))throw std::runtime_error("Merge destination appeared during commit");
                commit_merge_file_noreplace(stage,root/c.to);
            }
            append(journal,"DONE");
            result.success=true;
            result.message="Merge complete: "+std::to_string(plan.copies.size())+" verified files copied, "+
                std::to_string(plan.directories.size())+" directories added. Both originals remain intact.";
        }catch(const std::exception& e){
            try{
                // Never remove visible committed copies. Only uncommitted stage
                // files are discarded, and the journal records partial completion.
                auto tx=load_tx(journal);recover_copy_tx(root,tx);
                result.message=std::string("Merge incomplete; existing copies kept, rescan before retry: ")+e.what();
            }catch(const std::exception& recovery){
                try{append(journal,"RECOVERY_FAILED");}catch(...){ }
                result.message=std::string("Merge recovery needs manual inspection: ")+e.what()+"; "+recovery.what();
            }
        }
    }catch(const std::exception& e){result.message=e.what();}
    return result;
}
Result undo_latest(const fs::path& input){auto root=fs::absolute(input).lexically_normal();
    RootMutationLock mutation_guard(root);
    for(auto& e:recover_incomplete(root))if(e.compare(0,6,"MANUAL")==0)return {false,e,{}};
    auto all=all_tx(root);
    for(auto it=all.rbegin();it!=all.rend();++it){if(it->state!="DONE"||it->label.compare(0,6,"Undo: ")==0)continue;
        if(it->copy_mode)return {false,"Merge-created files are not auto-deleted by Undo. Rescan, then use cleanup under your selected retention policy.",{}};
        Plan reverse;reverse.name="Undo: "+it->id;
        for(auto&m:it->moves)reverse.moves.push_back({m.to,m.from,{}});
        auto res=execute_plan(root,reverse);
        if(res.success){append(it->path,"UNDONE");res.message="Undid "+it->id+" ("+std::to_string(reverse.moves.size())+" entries).";}
        return res;
    }return {false,"No completed transaction available to undo",{}};
}
std::vector<HistoryItem> history(const fs::path& root){std::vector<HistoryItem> v;for(auto&t:all_tx(root))v.push_back({t.id,t.label,t.state,t.copy_mode?t.copies.size()+t.directories.size():t.moves.size()});std::reverse(v.begin(),v.end());return v;}
std::vector<std::pair<std::string,std::vector<std::string>>> backup_safety(const Inventory& inv,const Duplicates& d){
    std::vector<std::pair<std::string,std::vector<std::string>>> out;
    size_t unique=inv.files.size();size_t copied=0;U64 extra=0;
    for(auto&g:d.groups){unique-=g.size();unique++;copied+=g.size();}
    extra=d.wasted;
    out.push_back({"Scan status",{inv.complete()&&d.errors.empty()?"COMPLETE":"INCOMPLETE - destructive actions blocked"}});
    out.push_back({"Unique file contents",{std::to_string(unique)}});
    out.push_back({"Files belonging to duplicate groups",{std::to_string(copied)}});
    out.push_back({"Potential redundant logical bytes",{human(extra)}});
    out.push_back({"Backup independence",{"Not verifiable from a single root. Multiple copies on one drive do not protect against drive loss."}});
    out.push_back({"Safety rule",{"Quarantine follows the user-selected keeper count (0-32), independence and verification policy. Default is two distinct byte-verified active copies. No protection against loss of the entire drive."}});
    if(!d.errors.empty())out.push_back({"Verification warnings",d.errors});
    return out;
}
std::string csv_quote(const std::string& s){std::string out="\"";for(char c:s){if(c=='\"')out+="\"\"";else out+=c;}return out+"\"";}
// Proposed post-organization layout. This is intentionally separate from the
// original additive two-directory merge: it covers all top-level sources.
static void structure_required_complete(const Inventory& inv){
    if(!inv.complete())throw std::runtime_error("A complete scan is required for a structure proposal");
    for(const auto& f:inv.files)if(f.content<0)
        throw std::runtime_error("Verify all content identities before generating a structure proposal");
}
static void check_structure_path(const fs::path& p, bool allow_empty=false){
    if((p.empty()&&!allow_empty)||!is_inside(p)||beneath(p,SYS))
        throw std::runtime_error("Structure path escapes the selected root or reserved workspace: "+utf8(p));
    for(const auto& c:p)if(c!=L"."&&!win_valid_name(c))
        throw std::runtime_error("Invalid Windows component in suggested structure: "+utf8(p));
}
struct StructureCategories {
    std::map<std::string,size_t> totals;
    size_t files=0;
};
static std::wstring structure_bucket(const StructureCategories& counts){
    if(counts.files==0)return L"07_空目录";
    const auto& totals=counts.totals;
    auto best=std::max_element(totals.begin(),totals.end(),[](const auto& a,const auto& b){return a.second<b.second;});
    if(best==totals.end()||best->second*100<counts.files*75)return L"06_混合内容_待核对";
    const auto& name=best->first;
    if(name=="Pictures"||name=="Videos")return L"01_照片与视频";
    if(name=="Documents")return L"02_文档资料";
    if(name=="Audio")return L"03_音频";
    if(name=="Code")return L"04_源码项目";
    if(name=="Archives")return L"05_压缩归档";
    return L"06_混合内容_待核对";
}
StructureSuggestion suggest_structure(const Inventory& inv,const fs::path& output_base,const std::atomic_bool* cancel){
    check_abort(cancel);
    structure_required_complete(inv);
    check_structure_path(output_base);
    StructureSuggestion result;result.output_base=output_base;
    // Mapping granularity is one top-level directory, plus root-level files.
    // Users may edit each destination independently; every source remains mapped.
    std::unordered_map<std::wstring,StructureCategories> categories;
    size_t root_files=0;U64 root_bytes=0;
    // Classify each file once. The previous helper traversed every file for
    // every top-level source, producing O(source folders * files) work.
    for(const auto& file:inv.files){
        check_abort(cancel);
        if(file.rel.parent_path().empty()){
            ++root_files;root_bytes+=file.bytes;continue;
        }
        const auto top_level=path_key(*file.rel.begin());
        auto& counts=categories[top_level];
        ++counts.files;++counts.totals[extension_category(file.rel)];
    }
    if(root_files){
        StructureRule r;r.source=L".";r.target=L"06_混合内容_待核对/根目录散文件";
        r.reason="Root-level files are retained and mapped explicitly";
        r.files=root_files;r.bytes=root_bytes;
        result.rules.push_back(std::move(r));
    }
    for(const auto& d:inv.dirs){
        check_abort(cancel);
        if(d.rel.empty()||!d.rel.parent_path().empty()||path_key(d.rel)==path_key(output_base))continue;
        // An existing output base may be nested. Sources must be disjoint from it.
        if(beneath(output_base,d.rel))throw std::runtime_error("Output base is inside an existing source directory; choose a top-level output base");
        StructureRule r;r.source=d.rel;r.files=static_cast<size_t>(d.count);r.bytes=d.bytes;
        const auto bucket=structure_bucket(categories[path_key(d.rel)]);
        r.target=fs::path(bucket)/d.rel.filename();
        r.reason=(bucket==L"06_混合内容_待核对")?
            "Mixed or unknown content: original folder preserved, review manually":
            "Category inferred from file extensions; original relative paths preserved";
        result.rules.push_back(std::move(r));
    }
    // Offer to consolidate only very strongly supported sibling backup pairs.
    // This is a reversible EDITABLE suggestion, never an automatic mutation.
    // Conservative grouping avoids silently combining similarly named but
    // unrelated directories. Complex/massive graphs remain review-only.
    if(result.rules.size()>=2){
        try{
            const auto candidates=recommend_merges(inv,100,cancel);
            std::map<std::wstring,size_t> positions;
            for(size_t i=0;i<result.rules.size();++i)
                if(result.rules[i].source!=L".")positions[path_key(result.rules[i].source)]=i;
            std::vector<size_t> parent(result.rules.size());
            std::iota(parent.begin(),parent.end(),size_t{0});
            auto top=[&](size_t a){while(parent[a]!=a)a=parent[a];return a;};
            for(const auto& c:candidates){
                check_abort(cancel);
                if(c.confidence!=3)continue;
                const auto a=positions.find(path_key(c.source)),b=positions.find(path_key(c.destination));
                if(a==positions.end()||b==positions.end())continue; // avoid nested directory assumptions
                const auto ga=result.rules[a->second].target.parent_path();
                const auto gb=result.rules[b->second].target.parent_path();
                if(ga!=gb||ga==fs::path(L"06_混合内容_待核对"))continue;
                auto x=top(a->second),y=top(b->second);
                if(x!=y)parent[y]=x;
            }
            std::map<size_t,std::vector<size_t>> groups;
            for(size_t i=0;i<parent.size();++i)groups[top(i)].push_back(i);
            for(const auto& [g,ids]:groups){(void)g;
                check_abort(cancel);
                if(ids.size()<2)continue;
                fs::path anchor=result.rules[ids[0]].source;
                for(auto i:ids)if(result.rules[i].source<anchor)anchor=result.rules[i].source;
                auto prefix=result.rules[ids[0]].target.parent_path();
                auto proposed_name=std::wstring(L"融合_备份_")+anchor.filename().wstring();
                if(proposed_name.size()>220)proposed_name.resize(220);
                for(auto i:ids){
                    result.rules[i].target=prefix/fs::path(proposed_name);
                    result.rules[i].reason="Strong verified same-content and same-path backup overlap; suggested shared target (editable)";
                }
            }
        }catch(const std::exception& e){
            check_abort(cancel);
            result.warnings.push_back(std::string("Automatic backup grouping is incomplete: ")+e.what());
        }
    }
    if(result.rules.empty())result.warnings.push_back("No content outside the proposed output directory");
    result.warnings.push_back("Read-only recommendation. Names and extensions do not prove semantic meaning; review every mapping.");
    result.warnings.push_back("Execution first builds a verified COPY of the new structure. Original sources are not deleted.");
    result.warnings.push_back("Retiring old entries is a separate previewed action governed by the user-configured copy count, independence and content-verification policy.");
    check_abort(cancel);
    return result;
}
StructurePlan plan_structure(const Inventory& inv,const StructureSuggestion& proposal){
    structure_required_complete(inv);
    check_structure_path(proposal.output_base);
    if(std::distance(proposal.output_base.begin(),proposal.output_base.end())!=1)
        throw std::runtime_error("Output base must be one new top-level folder (or prior generated target)");
    StructurePlan p;p.output_base=proposal.output_base;p.rules=proposal.rules;
    p.root_snapshot=snapshot(inv,{},true);
    std::map<std::wstring,const File*> old_files;
    std::set<std::wstring> old_dirs;
    for(const auto& d:inv.dirs)old_dirs.insert(path_key(d.rel));
    for(const auto& f:inv.files)old_files[path_key(f.rel)]=&f;
    if(old_files.count(path_key(proposal.output_base)))throw std::runtime_error("Output folder name is already a file");
    if(old_dirs.count(path_key(proposal.output_base))){
        // Never quietly exempt a pre-existing personal folder from migration.
        // Only previously built, journaled structure roots may be resumed.
        const auto marker="Merge directories: Structure reorganization -> "+utf8(proposal.output_base);
        bool known_output=false;
        for(const auto& h:history(inv.root))
            if(h.label==marker&&(h.status=="DONE"||h.status=="PARTIAL"))known_output=true;
        if(!known_output)throw std::runtime_error("Proposed output folder already contains user data and was not created by File Dedup Organizer; choose a new output folder");
    }
    // File-level explicit mappings take precedence over heuristic category rules.
    // Targets are relative to output_base, never absolute or outside the selected root.
    std::map<std::wstring,const StructureFileRule*> overrides;
    std::set<std::wstring> explicit_targets;
    for(const auto& edit:proposal.file_rules){
        check_structure_path(edit.source);
        check_structure_path(edit.target);
        if(edit.source==L"."||beneath(edit.source,proposal.output_base)||
           !old_files.count(path_key(edit.source)))
            throw std::runtime_error("File override must reference an existing source file: "+utf8(edit.source));
        if(!overrides.emplace(path_key(edit.source),&edit).second)
            throw std::runtime_error("Repeated file-level mapping source: "+utf8(edit.source));
        if(!explicit_targets.insert(path_key(edit.target)).second)
            throw std::runtime_error("Two file-level overrides request the same destination: "+utf8(edit.target));
    }
    std::set<std::wstring> provided;
    std::map<std::wstring,const StructureRule*> by_source;
    for(const auto& r:p.rules){
        const auto src=r.source==L"."?fs::path{}:r.source;
        if(!src.empty() && (src.parent_path()!=fs::path{}||path_key(src)==path_key(proposal.output_base)||
                            !old_dirs.count(path_key(src))))
            throw std::runtime_error("A mapping must select one existing top-level source folder");
        if(r.target.empty()||!is_inside(r.target))throw std::runtime_error("Mapping target cannot be empty/outside output root");
        check_structure_path(r.target);
        if(!provided.insert(path_key(src)).second)throw std::runtime_error("A source folder is mapped twice");
        by_source[path_key(src)]=&r;
    }
    // Reject missing mappings, even if the missing directory is empty.
    for(const auto& d:inv.dirs)if(!d.rel.empty()&&d.rel.parent_path().empty()&&
                                      path_key(d.rel)!=path_key(proposal.output_base)&&!provided.count(path_key(d.rel)))
        throw std::runtime_error("An original directory has no target mapping: "+utf8(d.rel));
    for(const auto& f:inv.files)if(f.rel.parent_path().empty()&& !provided.count(L""))
        throw std::runtime_error("Root-level files lack a mapping");
    // Existing target files are only reused by exact content identity; the
    // execution phase re-verifies these via byte comparison.
    std::map<int,fs::path> result_by_content;
    for(const auto& f:inv.files)if(beneath(f.rel,proposal.output_base)){
        auto it=result_by_content.find(f.content);
        if(it==result_by_content.end()||f.rel<it->second)result_by_content[f.content]=f.rel;
    }
    std::set<std::wstring> target_files;
    std::map<std::wstring,fs::path> target_dirs;
    auto add_directory=[&](fs::path dir){
        while(!dir.empty()&&dir!=L"."){
            check_structure_path(dir);
            if(old_files.count(path_key(dir))||target_files.count(path_key(dir)))
                throw std::runtime_error("A file blocks proposed directory: "+utf8(dir));
            if(!old_dirs.count(path_key(dir)))target_dirs.emplace(path_key(dir),dir);
            dir=dir.parent_path();
        }
    };
    add_directory(proposal.output_base);
    for(const auto& r:p.rules)add_directory(proposal.output_base/r.target);
    for(const auto& d:inv.dirs){
        if(d.rel.empty()||beneath(d.rel,proposal.output_base))continue;
        const auto it=d.rel.begin();if(it==d.rel.end())continue;
        const auto rule=by_source.find(path_key(*it));
        if(rule==by_source.end())throw std::runtime_error("Missing directory mapping for "+utf8(d.rel));
        const auto relative=d.rel==rule->second->source?fs::path{}:d.rel.lexically_relative(rule->second->source);
        const auto dest=relative.empty()?proposal.output_base/rule->second->target:
            proposal.output_base/rule->second->target/relative;
        add_directory(dest);
    }
    // Process explicit file-level destinations first, so they win name conflicts
    // against automatic suggestions. The normal path can be renamed if needed.
    std::vector<const File*> ordered_files;
    for(const auto& f:inv.files)if(!beneath(f.rel,proposal.output_base))ordered_files.push_back(&f);
    std::stable_sort(ordered_files.begin(),ordered_files.end(),[&](const File* a,const File* b){
        return overrides.count(path_key(a->rel))>overrides.count(path_key(b->rel));
    });
    for(const File* file:ordered_files){
        const auto& f=*file;
        fs::path src;fs::path sub;
        if(f.rel.parent_path().empty()){sub=f.rel.filename();}
        else {src=*f.rel.begin();sub=f.rel.lexically_relative(src);}
        const auto rule=by_source.find(path_key(src));
        if(rule==by_source.end())throw std::runtime_error("Missing file mapping for "+utf8(f.rel));
        const auto edited=overrides.find(path_key(f.rel));
        const bool forced=edited!=overrides.end();
        const fs::path desired=forced ? proposal.output_base/edited->second->target :
                              proposal.output_base/rule->second->target/sub;
        check_structure_path(desired);
        // An explicitly chosen file destination is honored, even if the same
        // content could be reused elsewhere. Existing exact content at that
        // EXACT destination can be reused; different bytes must be rejected.
        if(forced){
            if(old_dirs.count(path_key(desired))||target_dirs.count(path_key(desired)))
                throw std::runtime_error("Explicit file target is an existing directory: "+utf8(desired));
            if(const auto it=old_files.find(path_key(desired));it!=old_files.end()){
                if(it->second->content!=f.content)
                    throw std::runtime_error("Explicit target has different content: "+utf8(desired));
                p.mappings.push_back({f.rel,it->second->rel,"REUSE_EXISTING",f.bytes,f.mtime,0});
                p.existing_skips.push_back({f.rel,it->second->rel,f.bytes,f.mtime,it->second->mtime});
                result_by_content[f.content]=it->second->rel;
                continue;
            }
            if(target_files.count(path_key(desired)))
                throw std::runtime_error("Explicit target already claimed by another file: "+utf8(desired));
            add_directory(desired.parent_path());
            target_files.insert(path_key(desired));
            p.copies.push_back({f.rel,desired,f.bytes,f.mtime});
            p.mappings.push_back({f.rel,desired,"COPY",f.bytes,f.mtime,0});
            result_by_content[f.content]=desired;
            p.bytes_to_copy+=f.bytes;
            continue;
        }
        // Content deduplication is explicit in the mapping table, including
        // the case where a prior run already populated the output folder.
        if(const auto found=result_by_content.find(f.content);found!=result_by_content.end()){
            const bool already=old_files.count(path_key(found->second))!=0;
            p.mappings.push_back({f.rel,found->second,already?"REUSE_EXISTING":"REUSE_PLANNED",f.bytes,f.mtime,0});
            if(already){const auto* x=old_files.at(path_key(found->second));
                p.existing_skips.push_back({f.rel,found->second,f.bytes,f.mtime,x->mtime});}
            continue;
        }
        auto target=desired;
        const auto occupied=[&](const fs::path& x){return old_files.count(path_key(x))||old_dirs.count(path_key(x))||
            target_files.count(path_key(x))||target_dirs.count(path_key(x));};
        if(occupied(target)){
            ++p.renamed_conflicts;
            auto label=src.empty()?L"根目录":src.filename().wstring();
            if(label.size()>30)label.resize(30);
            for(unsigned i=1;i<100000;++i){
                auto suffix=L" (来自"+label+(i>1?L"-"+std::to_wstring(i):L"")+L")";
                auto name=desired.stem().wstring()+suffix+desired.extension().wstring();
                if(name.size()>240)throw std::runtime_error("Conflict filename too long: "+utf8(desired));
                target=desired.parent_path()/name;
                if(!occupied(target))break;
                if(i==99999)throw std::runtime_error("Too many filename conflicts");
            }
        }
        add_directory(target.parent_path());
        if(!target_files.insert(path_key(target)).second)throw std::runtime_error("Duplicate structure target");
        p.copies.push_back({f.rel,target,f.bytes,f.mtime});
        p.mappings.push_back({f.rel,target,"COPY",f.bytes,f.mtime,0});
        result_by_content[f.content]=target;
        p.bytes_to_copy+=f.bytes;
    }
    for(const auto& entry:target_dirs)p.directories.push_back(entry.second);
    std::sort(p.directories.begin(),p.directories.end(),[](const auto& a,const auto& b){
        const auto da=std::distance(a.begin(),a.end()),db=std::distance(b.begin(),b.end());
        return da!=db?da<db:a<b;
    });
    // Guard against source content changing while both file size and mtime
    // are restored to old values. FNV detects accidental changes, while
    // source-to-staged copies are always checked byte-for-byte.
    for(auto& m:p.mappings){
        const auto sig=full_fingerprint(inv.root/m.from,m.bytes,m.source_time);
        if(!sig)throw std::runtime_error("Cannot fingerprint stable source for migration: "+utf8(m.from));
        m.source_fingerprint=*sig;
    }
    p.warnings=proposal.warnings;
    if(p.renamed_conflicts)p.warnings.push_back(std::to_string(p.renamed_conflicts)+" name conflicts retained as independently named versions");
    return p;
}
Result execute_structure_plan(const fs::path& input,const StructurePlan& plan){
    const bool nothing_to_copy=plan.copies.empty()&&plan.directories.empty();
    const auto root=fs::absolute(input).lexically_normal();
    std::error_code ec;
    if(!fs::is_directory(root,ec)||ec||reparse(root))return {false,"Unsafe or unavailable root",{}};
    try{check_structure_path(plan.output_base);if(std::distance(plan.output_base.begin(),plan.output_base.end())!=1)
        throw std::runtime_error("Output must be a top-level subdirectory");}
    catch(const std::exception& e){return {false,e.what(),{}};}
    RootMutationLock guard(root);
    for(const auto& r:recover_incomplete(root))if(r.compare(0,6,"MANUAL")==0)return {false,r,{}};
    Result result;
    try{
        auto fresh=scan(root);
        if(!fresh.complete()||snapshot(fresh,{},true)!=plan.root_snapshot)
            throw std::runtime_error("Root directory changed since proposal; rescan before executing");
        // The preview is bound to the exact bytes scanned, not just metadata.
        for(const auto& m:plan.mappings){
            check_source_ancestors(root,m.from);
            const auto sig=full_fingerprint(root/m.from,m.bytes,m.source_time);
            if(!sig||*sig!=m.source_fingerprint)
                throw std::runtime_error("Source content changed since preview: "+utf8(m.from));
        }
        std::set<std::wstring> targets;
        for(const auto& d:plan.directories){
            check_structure_path(d);
            if(!beneath(d,plan.output_base)||!targets.insert(path_key(d)).second||path_exists_checked(root/d))
                throw std::runtime_error("Unsafe or occupied proposed directory: "+utf8(d));
            check_recovery_ancestors(root,d);
        }
        for(const auto& c:plan.copies){
            check_structure_path(c.from);check_structure_path(c.to);
            if(c.from.empty()||!beneath(c.to,plan.output_base)||beneath(c.from,plan.output_base)||
                !targets.insert(path_key(c.to)).second||path_exists_checked(root/c.to))
                throw std::runtime_error("Unsafe or occupied proposed file: "+utf8(c.to));
            check_source_ancestors(root,c.from);check_recovery_ancestors(root,c.to);
            if(reparse(root/c.from)||!fs::is_regular_file(root/c.from,ec)||ec||
                fs::file_size(root/c.from,ec)!=c.bytes||ec||fs::last_write_time(root/c.from,ec)!=c.mtime||ec)
                throw std::runtime_error("Source changed after preview: "+utf8(c.from));
        }
        for(const auto& k:plan.existing_skips){
            if(!beneath(k.to,plan.output_base)||beneath(k.from,plan.output_base))
                throw std::runtime_error("Invalid reuse mapping");
            check_source_ancestors(root,k.from);check_source_ancestors(root,k.to);
            if(reparse(root/k.from)||reparse(root/k.to))throw std::runtime_error("Reparse in reuse mapping");
            auto eq=equal_bytes(root/k.from,root/k.to,k.bytes,k.from_time,k.to_time);
            if(!eq||!*eq)throw std::runtime_error("Reused target is not byte-identical to source");
        }
        if(nothing_to_copy){
            for(const auto& m:plan.mappings){
                auto eq=equal_bytes(root/m.from,root/m.to,m.bytes,m.source_time);
                if(!eq||!*eq)throw std::runtime_error("Existing organized mapping changed after preview");
            }
            return {true,"Suggested structure already exists and all mappings were reverified",{}};
        }
        init_workspace(root);
        auto id=txid();result.transaction=id;
        const auto journal=journal_dir(root)/from_utf8(id+".log");
        append(journal,"LABEL "+hex("Merge directories: Structure reorganization -> "+utf8(plan.output_base)));
        for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
            const auto stage=SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
            append(journal,"COPY "+hex(utf8(c.from))+" "+hex(utf8(c.to))+" "+hex(utf8(stage)));
        }
        for(const auto& d:plan.directories)append(journal,"MKDIR "+hex(utf8(d)));
        append(journal,"PENDING");
        try{
            mkdir_safe(root,SYS/L"stage"/from_utf8(id));
            for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
                const auto stage=root/SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
                std::error_code cp;
                if(!fs::copy_file(root/c.from,stage,fs::copy_options::none,cp)||cp)
                    throw std::runtime_error("Failed to stage organized file: "+utf8(c.from)+": "+cp.message());
                const auto eq=equal_bytes(root/c.from,stage,c.bytes,c.mtime);
                if(!eq||!*eq)throw std::runtime_error("Staged organized copy failed byte verification");
                fs::last_write_time(stage,c.mtime,cp);
                if(cp)throw std::runtime_error("Cannot preserve original modified timestamp");
                sync_staged_file(stage);
            }
            append(journal,"STAGEDALL");
            for(const auto& d:plan.directories)mkdir_safe(root,d);
            for(size_t i=0;i<plan.copies.size();++i){const auto& c=plan.copies[i];
                const auto stage=root/SYS/L"stage"/from_utf8(id)/from_utf8(std::to_string(i));
                check_recovery_ancestors(root,c.to);mkdir_safe(root,c.to.parent_path());
                if(path_exists_checked(root/c.to))throw std::runtime_error("New target appeared during commit");
                commit_merge_file_noreplace(stage,root/c.to);
            }
            // Every original path must still be represented in the result,
            // including many-to-one content dedup mappings.
            for(const auto& m:plan.mappings){
                check_source_ancestors(root,m.from);check_source_ancestors(root,m.to);
                const auto eq=equal_bytes(root/m.from,root/m.to,m.bytes);
                if(!eq||!*eq)throw std::runtime_error("Final old-to-new mapping verification failed: "+utf8(m.from));
            }
            append(journal,"DONE");result.success=true;
            result.message="Structure built and verified: "+std::to_string(plan.copies.size())+
                " new files, "+std::to_string(plan.mappings.size())+
                " original-to-target file mappings. Original data remains untouched; cleanup requires a separately approved retention-policy check.";
        }catch(const std::exception& e){
            try{recover_copy_tx(root,load_tx(journal));
                result.message=std::string("Structure incomplete; originals and completed copies retained: ")+e.what();}
            catch(const std::exception& recovery){
                try{append(journal,"RECOVERY_FAILED");}catch(...){ }
                result.message=std::string("Manual structure recovery required: ")+e.what()+"; "+recovery.what();
            }
        }
    }catch(const std::exception& e){result.message=e.what();}
    return result;
}


}
