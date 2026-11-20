#include "backup_task.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace dc {
namespace {
void check_cancel(const std::atomic_bool* cancel){
    if(cancel&&cancel->load())throw std::runtime_error("Operation canceled");
}
struct PathLess {
    bool operator()(const fs::path& a,const fs::path& b) const {
#ifdef _WIN32
        const auto& x=a.native();const auto& y=b.native();
        return CompareStringOrdinal(x.c_str(),int(x.size()),y.c_str(),int(y.size()),TRUE)==CSTR_LESS_THAN;
#else
        return a<b;
#endif
    }
};
bool same_spelling(const fs::path& a,const fs::path& b){
    return !PathLess{}(a,b)&&!PathLess{}(b,a);
}
fs::path absolute_folder(const fs::path& path){
    if(path.empty())throw std::runtime_error("请选择两个文件夹。");
    auto result=fs::absolute(path).lexically_normal();
    if(!result.has_filename()&&result!=result.root_path())result=result.parent_path();
    for(const auto& part:result)
        if(same_spelling(part,L".DiskCurator-System"))throw std::runtime_error("请选择普通资料目录。");
    return result;
}
// A drive letter can contain mounted volumes and junctions. Allow a writable
// plan only for canonical locations on a single mount, below its root.
bool ordinary_common_root(const fs::path& common,const fs::path& left,const fs::path& right){
    if(common.empty()||common==common.root_path())return false;
#ifdef _WIN32
    auto mount=[](const fs::path& p){
        std::wstring buf(32768,0);
        if(!GetVolumePathNameW(p.c_str(),buf.data(),DWORD(buf.size())))return fs::path{};
        buf.resize(wcslen(buf.c_str()));return fs::path(buf);
    };
    const auto base=mount(common);
    if(base.empty()||!same_spelling(base,mount(left))||!same_spelling(base,mount(right)))return false;
    std::error_code ec;const bool at_mount=fs::equivalent(common,base,ec);
    return !ec&&!at_mount;
#else
    struct stat a{},b{},c{},parent{};
    return stat(left.c_str(),&a)==0&&stat(right.c_str(),&b)==0&&stat(common.c_str(),&c)==0&&
           stat(common.parent_path().c_str(),&parent)==0&&a.st_dev==b.st_dev&&b.st_dev==c.st_dev&&c.st_dev==parent.st_dev;
#endif
}
struct Entry {
    const File* file=nullptr;
    bool present=false,ambiguous=false;
};
struct Pair {Entry entries[2];};
using Entries=std::map<fs::path,Pair,PathLess>;
void index_tree(Entries& entries,const Inventory& inventory,size_t side,const std::atomic_bool* cancel){
    auto insert=[&](const fs::path& rel,const File* file){
        check_cancel(cancel);
        auto& e=entries[rel].entries[side];
        e.ambiguous=e.present;e.present=true;e.file=file;
    };
    for(const auto& d:inventory.dirs)if(!d.rel.empty())insert(d.rel,nullptr);
    for(const auto& f:inventory.files)insert(f.rel,&f);
}
void append_inventory(Inventory& dst,Inventory& src,const fs::path& prefix){
    dst.bytes+=src.bytes;
    for(auto& f:src.files){f.rel=prefix/f.rel;dst.files.push_back(std::move(f));}
    for(auto& d:src.dirs){d.rel=d.rel.empty()?prefix:prefix/d.rel;dst.dirs.push_back(std::move(d));}
}
void make_merge_inventory(BackupComparison& result,Inventory& left,Inventory& right){
    std::error_code ec;
    if(!same_spelling(left.root.root_name(),right.root.root_name()))return;
    // A canonical name may cross a junction into a different user-visible
    // drive or share. Keep aliases readable, but never silently redirect writes.
    for(auto path:{left.root,right.root}){
        while(!path.empty()){
#ifdef _WIN32
            const auto attributes=GetFileAttributesW(extended_path(path).c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return;
#else
            if(fs::is_symlink(fs::symlink_status(path,ec))||ec)return;
#endif
            if(path==path.root_path())break;
            path=path.parent_path();
        }
    }
    const auto a=fs::canonical(left.root,ec);if(ec)return;
    const auto b=fs::canonical(right.root,ec);if(ec)return;
    for(const auto& path:{a,b})for(const auto& part:path)if(same_spelling(part,L".DiskCurator-System"))return;
    auto i=a.begin(),j=b.begin();fs::path common;
    while(i!=a.end()&&j!=b.end()&&same_spelling(*i,*j)){common/=*i;++i;++j;}
    if(i==a.end()||j==b.end()||!ordinary_common_root(common,a,b))return;
    for(;i!=a.end();++i)result.source_relative/=*i;
    for(;j!=b.end();++j)result.target_relative/=*j;
    auto& scoped=result.merge_inventory;scoped.root=common;
    append_inventory(scoped,left,result.source_relative);append_inventory(scoped,right,result.target_relative);
}
}

BackupComparison compare_backups(const fs::path& source,const fs::path& target,const Progress& progress,
    const std::atomic_bool* cancel,const ScanProgressCallback& structured){
    BackupComparison result;result.source=absolute_folder(source);result.target=absolute_folder(target);
    std::error_code ec;
    if(fs::equivalent(result.source,result.target,ec)&&!ec)throw std::runtime_error("请选择两个不同的文件夹。");
    Inventory trees[2];
    const auto started=std::chrono::steady_clock::now();
    for(size_t side=0;side<2;++side){
        const auto& dir=side?result.target:result.source;
        auto report=[&](ScanProgress status){
            status.root_index=side+1;status.root_count=2;
            status.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            if(structured)structured(status);
        };
        try{trees[side]=scan_hashed(dir,progress,cancel,report,false);}
        catch(const std::runtime_error& e){check_cancel(cancel);trees[side].errors.emplace_back(e.what());}
        for(const auto& error:trees[side].errors)result.errors.push_back(utf8(dir)+": "+error);
    }
    Entries entries;index_tree(entries,trees[0],0,cancel);index_tree(entries,trees[1],1,cancel);
    bool type_conflict=false;
    auto last=started;ScanProgress verifying;verifying.phase=ScanPhase::Verifying;verifying.root=result.source;
    verifying.files_discovered=entries.size();
    for(const auto& [path,pair]:entries){
        check_cancel(cancel);
        if(structured){
            const auto now=std::chrono::steady_clock::now();
            if(now-last>=std::chrono::milliseconds(100)){
                last=now;verifying.current_file=path;verifying.elapsed_seconds=std::chrono::duration<double>(now-started).count();
                structured(verifying);
            }
        }
        ++verifying.files_completed;
        const auto& a=pair.entries[0];const auto& b=pair.entries[1];
        std::string state;
        if(a.ambiguous||b.ambiguous){
            state="UNKNOWN";result.errors.push_back("路径大小写存在歧义："+utf8(path));
        }else if((a.file&&a.file->sha256.empty())||(b.file&&b.file->sha256.empty())||
                 (!a.present&&!trees[0].complete())||(!b.present&&!trees[1].complete()))state="UNKNOWN";
        else if(!b.present)state=a.file?"ONLY_SOURCE":"ONLY_SOURCE_DIR";
        else if(!a.present)state=b.file?"ONLY_TARGET":"ONLY_TARGET_DIR";
        else if(bool(a.file)!=bool(b.file)){state="TYPE_CONFLICT";type_conflict=true;}
        else if(!a.file)continue; // A directory present on both sides adds no difference.
        else if(a.file->bytes!=b.file->bytes||a.file->sha256!=b.file->sha256)state="CHANGED";
        else {
            const auto equal=equal_scanned_files(trees[0].root,*a.file,trees[1].root,*b.file,cancel);
            if(!equal){state="UNKNOWN";result.errors.push_back("核验时文件发生变化或无法读取："+utf8(path));}
            else state=*equal?"IDENTICAL":"CHANGED";
        }
        result.rows.push_back({state,path,a.file?a.file->bytes:0,b.file?b.file->bytes:0});
    }
    check_cancel(cancel);
    if(result.errors.empty()&&!type_conflict)make_merge_inventory(result,trees[0],trees[1]);
    return result;
}
}
