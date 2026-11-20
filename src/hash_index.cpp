#include "core.hpp"
#include <algorithm>
#include <cstdio>
#include <array>
#include <chrono>
#include <cstring>
#include <cwctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

namespace dc {
namespace {
using Word=std::uint32_t;
constexpr std::array<Word,64> K={{
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2}};
constexpr std::array<Word,8> INIT={{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
 0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19}};
Word rotr(Word x,unsigned n){return (x>>n)|(x<<(32-n));}
struct Sha256 {
 std::array<Word,8> h=INIT;
 std::array<unsigned char,64> block{};
 std::uint64_t bytes=0;
 size_t used=0;
 void transform(const unsigned char* data){
   Word w[64];for(int i=0;i<16;++i){const int n=4*i;w[i]=(Word(data[n])<<24)|(Word(data[n+1])<<16)|(Word(data[n+2])<<8)|Word(data[n+3]);}
   for(int i=16;i<64;++i){auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
      auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
      w[i]=w[i-16]+s0+w[i-7]+s1;}
   auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
   for(int i=0;i<64;++i){auto S1=rotr(e,6)^rotr(e,11)^rotr(e,25);
     auto ch=(e&f)^(~e&g);auto t1=hh+S1+ch+K[size_t(i)]+w[i];
     auto S0=rotr(a,2)^rotr(a,13)^rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);
     auto t2=S0+maj;hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
   h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
 }
 void transform(){transform(block.data());}
 void update(const char* p,size_t n){
   bytes+=n;
   if(used){const auto take=std::min(n,64-used);std::memcpy(block.data()+used,p,take);
     used+=take;p+=take;n-=take;if(used==64){transform();used=0;}}
   while(n>=64){transform(reinterpret_cast<const unsigned char*>(p));p+=64;n-=64;}
   if(n){std::memcpy(block.data(),p,n);used=n;}
 }
 std::string finish(){
   const std::uint64_t bits=bytes*8;
   block[used++]=0x80;
   if(used>56){while(used<64)block[used++]=0;transform();used=0;}
   while(used<56)block[used++]=0;
   for(int i=7;i>=0;--i)block[used++]=static_cast<unsigned char>(bits>>(8*i));
   transform();std::ostringstream o;o<<std::hex<<std::setfill('0');
   for(auto v:h)o<<std::setw(8)<<v;
   return o.str();
 }
};
std::string hex_text(const std::string& s){static const char* d="0123456789abcdef";std::string out;out.reserve(s.size()*2);
 for(unsigned char x:s){out.push_back(d[x>>4]);out.push_back(d[x&15]);}return out;}
void check_cancel(const std::atomic_bool* cancel) {
 if(cancel&&cancel->load())throw std::runtime_error("Operation canceled");
}
using ScanClock=std::chrono::steady_clock;
class ScanReporter {
 const ScanProgressCallback& callback_;
 const Progress& text_;
 ScanClock::time_point started_=ScanClock::now(),last_=started_;
 bool content_update_sent_=false;
public:
 ScanProgress state;
 ScanReporter(const fs::path& root,const ScanProgressCallback& callback,const Progress& text)
    :callback_(callback),text_(text){state.root=root;}
 void emit(bool force=false) {
   const auto now=ScanClock::now();
   if(!force&&now-last_<std::chrono::milliseconds(100))return;
   last_=now;state.elapsed_seconds=std::chrono::duration<double>(now-started_).count();
   if(callback_)callback_(state);
   if(text_&&state.phase==ScanPhase::Hashing)
     text_("SHA-256 indexed "+std::to_string(state.files_completed)+" / "+
           std::to_string(state.files_discovered)+" files; "+human(state.bytes_processed)+
           " / "+human(state.total_bytes));
 }
 void enumerate(const ScanProgress& status) {
   state=status;state.root_index=1;state.root_count=1;emit(true);
 }
 void read(U64 bytes) {
   state.bytes_read+=bytes;state.bytes_processed+=bytes;
   // A single large file must expose an in-file progress/cancellation point,
   // even when it is read faster than the normal UI publication interval.
   const bool first=!content_update_sent_;
   content_update_sent_=true;emit(first);
 }
};
#ifdef _WIN32
void cng_check(NTSTATUS status,const char* operation) {
 if(status<0)throw std::runtime_error(std::string(operation)+" failed (NTSTATUS "+
                                     std::to_string(static_cast<unsigned long>(status))+")");
}
class CngProvider {
public:
 BCRYPT_ALG_HANDLE handle=nullptr;
 DWORD object_size=0;
 CngProvider() {
   cng_check(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,
                                        BCRYPT_HASH_REUSABLE_FLAG),"BCryptOpenAlgorithmProvider");
   try {
     DWORD received=0,hash_size=0;
     cng_check(BCryptGetProperty(handle,BCRYPT_OBJECT_LENGTH,
         reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&received,0),"BCryptGetProperty");
     if(received!=sizeof(object_size)||!object_size)throw std::runtime_error("Invalid CNG hash object size");
     cng_check(BCryptGetProperty(handle,BCRYPT_HASH_LENGTH,
         reinterpret_cast<PUCHAR>(&hash_size),sizeof(hash_size),&received,0),"BCryptGetProperty");
     if(received!=sizeof(hash_size)||hash_size!=32)throw std::runtime_error("Invalid CNG SHA-256 output size");
   }catch(...){BCryptCloseAlgorithmProvider(handle,0);handle=nullptr;throw;}
 }
 ~CngProvider(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
 CngProvider(const CngProvider&)=delete;
 CngProvider& operator=(const CngProvider&)=delete;
};
const CngProvider& sha256_provider(){static const CngProvider provider;return provider;}
// One provider is cached for the process, one reusable hashing object and one
// I/O buffer for a scan. No DLL setup, large buffer allocation, or zero-fill is
// repeated for every small file. Windows CNG selects its implementation.
class FileDigest {
 std::vector<unsigned char> object_;
 BCRYPT_HASH_HANDLE hash_=nullptr;
 bool active_=false;
public:
 FileDigest() {
   const auto& provider=sha256_provider();object_.resize(provider.object_size);
   cng_check(BCryptCreateHash(provider.handle,&hash_,object_.data(),
       static_cast<ULONG>(object_.size()),nullptr,0,BCRYPT_HASH_REUSABLE_FLAG),"BCryptCreateHash");
 }
 ~FileDigest(){if(hash_)BCryptDestroyHash(hash_);}
 FileDigest(const FileDigest&)=delete;
 FileDigest& operator=(const FileDigest&)=delete;
 void begin() {
   if(active_){std::array<unsigned char,32> discarded{};
     cng_check(BCryptFinishHash(hash_,discarded.data(),static_cast<ULONG>(discarded.size()),0),"BCryptFinishHash");}
   active_=true;
 }
 void update(const char* data,size_t size) {
   cng_check(BCryptHashData(hash_,reinterpret_cast<PUCHAR>(const_cast<char*>(data)),
                          static_cast<ULONG>(size),0),"BCryptHashData");
 }
 std::string finish() {
   std::array<unsigned char,32> result{};
   cng_check(BCryptFinishHash(hash_,result.data(),static_cast<ULONG>(result.size()),0),"BCryptFinishHash");
   active_=false;
   return hex_text(std::string(reinterpret_cast<const char*>(result.data()),result.size()));
 }
};
class SequentialReader {
 HANDLE handle_=INVALID_HANDLE_VALUE;
public:
 explicit SequentialReader(const fs::path& path) {
   // Read sharing permits other readers but denies concurrent writers/renames
   // while this file is being hashed. Reparse files are rejected even if a leaf
   // was replaced after enumeration; never traverse a newly introduced link.
   const auto native=extended_path(path);
   handle_=CreateFileW(native.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                      FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
   if(handle_==INVALID_HANDLE_VALUE)
     throw std::runtime_error("Cannot open file for SHA-256: "+utf8(path)+": "+
                              std::system_category().message(static_cast<int>(GetLastError())));
   BY_HANDLE_FILE_INFORMATION info{};
   if(!GetFileInformationByHandle(handle_,&info)||GetFileType(handle_)!=FILE_TYPE_DISK||
      (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))!=0){
     CloseHandle(handle_);handle_=INVALID_HANDLE_VALUE;
     throw std::runtime_error("SHA-256 requires an ordinary disk file: "+utf8(path));
   }
 }
 ~SequentialReader(){if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
 SequentialReader(const SequentialReader&)=delete;
 SequentialReader& operator=(const SequentialReader&)=delete;
 size_t read(char* data,size_t size) {
   DWORD count=0;
   if(!ReadFile(handle_,data,static_cast<DWORD>(size),&count,nullptr))
     throw std::runtime_error("File read failed: "+
                              std::system_category().message(static_cast<int>(GetLastError())));
   return static_cast<size_t>(count);
 }
};
#else
class FileDigest {
 Sha256 hash_;
public:
 void begin(){hash_=Sha256{};}
 void update(const char* data,size_t size){hash_.update(data,size);}
 std::string finish(){return hash_.finish();}
};
class SequentialReader {
 std::ifstream in_;
public:
 explicit SequentialReader(const fs::path& path):in_(path,std::ios::binary) {
   if(!in_)throw std::runtime_error("Cannot open file for SHA-256: "+utf8(path));
 }
 size_t read(char* data,size_t size) {
   in_.read(data,static_cast<std::streamsize>(size));
   if(in_.bad())throw std::runtime_error("File read failed");
   return static_cast<size_t>(in_.gcount());
 }
};
#endif
std::string hash_file(const fs::path& p,U64 size,fs::file_time_type mtime,
                      FileDigest& shared_hash,std::vector<char>& buffer,
                      ScanReporter& reporter,const std::atomic_bool* cancel){
#ifdef _WIN32
 auto& hash=shared_hash;
#else
 // The portable state is only 112 bytes and needs no external setup. Keep it
 // local so the compiler can prove that input bytes do not alias digest state.
 (void)shared_hash;FileDigest hash;
#endif
 check_cancel(cancel);
 std::error_code ec;
 if(fs::is_symlink(fs::symlink_status(p,ec))||ec||
    fs::file_size(p,ec)!=size||ec||fs::last_write_time(p,ec)!=mtime||ec)
    throw std::runtime_error("File changed before hashing: "+utf8(p));
 SequentialReader in(p);hash.begin();U64 left=size;
 while(left){
   check_cancel(cancel);
   const auto wanted=static_cast<size_t>(std::min<U64>(left,buffer.size()));
   const auto count=in.read(buffer.data(),wanted);
   if(count){
     hash.update(buffer.data(),count);left-=static_cast<U64>(count);
     reporter.read(static_cast<U64>(count));
   }
   if(count!=wanted)throw std::runtime_error("File became unreadable: "+utf8(p));
 }
 check_cancel(cancel);
 char extra=0;
 if(in.read(&extra,1)!=0)throw std::runtime_error("File length changed while hashing: "+utf8(p));
 if(fs::file_size(p,ec)!=size||ec||fs::last_write_time(p,ec)!=mtime||ec||
    fs::is_symlink(fs::symlink_status(p,ec))||ec)
    throw std::runtime_error("File changed during hashing: "+utf8(p));
 return hash.finish();
}
fs::path index_dir(){
#ifdef _WIN32
 std::wstring local(32768,L'\0');
 const DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",local.data(),static_cast<DWORD>(local.size()));
 if(count>0&&count<local.size()){
     local.resize(count);
     return fs::path(local)/L"DiskCurator"/L"Cache";
 }
#else
 const char* xdg=std::getenv("XDG_CACHE_HOME");if(xdg&&*xdg)return fs::u8path(xdg)/"DiskCurator";
 const char* home=std::getenv("HOME");if(home&&*home)return fs::u8path(home)/".cache"/"DiskCurator";
#endif
 return fs::temp_directory_path()/"DiskCurator-Cache";
}
bool inside_or_same(const fs::path& root,const fs::path& item){
 auto r=fs::absolute(root).lexically_normal(),p=fs::absolute(item).lexically_normal();
 auto a=r.begin(),b=p.begin();
 for(;a!=r.end();++a,++b){if(b==p.end())return false;
#ifdef _WIN32
   auto x=a->wstring(),y=b->wstring();
   if(x.size()!=y.size())return false;
   for(size_t i=0;i<x.size();++i)if(std::towlower(x[i])!=std::towlower(y[i]))return false;
#else
   if(*a!=*b)return false;
#endif
 }
 return true;
}
void save_index(Inventory& inv,const Progress& progress,ScanReporter& reporter,
                const std::atomic_bool* cancel){
 fs::path temp;
 try{
   const auto dir=index_dir();
   // The cache may lie on the scanned system volume. scan() explicitly excludes
   // this application-owned folder, preventing self-indexing or recursion.
   std::error_code ec;fs::create_directories(dir,ec);if(ec)throw std::runtime_error(ec.message());
   Sha256 key;const auto root=utf8(inv.root);key.update(root.data(),root.size());
   const auto destination=dir/("root-"+key.finish()+".tsv");
   temp=destination;
   static std::atomic_uint64_t next_temp{0};
   temp+=L".new-"+std::to_wstring(ScanClock::now().time_since_epoch().count())+
         L"-"+std::to_wstring(next_temp.fetch_add(1));
   {
    std::ofstream o(temp,std::ios::binary|std::ios::trunc);
    if(!o)throw std::runtime_error("Cannot create hash index");
    o<<"DiskCurator SHA256 v1\t"<<hex_text(root)<<'\t'<<(inv.complete()?"COMPLETE":"PARTIAL")<<"\n";
    for(const auto& f:inv.files){
       check_cancel(cancel);
       if(f.sha256.size()==64)
         o<<hex_text(utf8(f.rel))<<'\t'<<f.bytes<<'\t'<<f.mtime.time_since_epoch().count()<<'\t'<<f.sha256<<'\n';
       reporter.emit();
    }
    o.flush();if(!o)throw std::runtime_error("Cannot write hash index");
   }
   // Cache is disposable; no user file is ever deleted/replaced here.
   check_cancel(cancel);
#ifdef _WIN32
   const auto native_temp=extended_path(temp),native_destination=extended_path(destination);
   if(!MoveFileExW(native_temp.c_str(),native_destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
       throw std::runtime_error(std::system_category().message(static_cast<int>(GetLastError())));
#else
   fs::rename(temp,destination,ec);if(ec)throw std::runtime_error(ec.message());
#endif
   inv.index_persisted=true;inv.index_error.clear();
 }catch(const std::exception& e){
   std::error_code ignored;if(!temp.empty())fs::remove(temp,ignored);
   check_cancel(cancel);
   inv.index_persisted=false;inv.index_error=e.what();
   if(progress)progress(std::string("Hash index not persisted (scan hashes retained in memory): ")+e.what());
 }
}
}
std::string sha256_text(const std::string& text){
 Sha256 digest;digest.update(text.data(),text.size());return digest.finish();
}
fs::path hash_cache_root(){return index_dir();}
Inventory scan_hashed(const fs::path& root,const Progress& progress,const std::atomic_bool* cancel,
                      const ScanProgressCallback& scan_progress){
 // Never scan the cache itself or one of its descendants: that would index
 // mutable application data. A volume root is permitted because scan() skips
 // the cache branch during directory traversal.
 if(inside_or_same(index_dir(),root))
    throw std::runtime_error("Hash cache cannot itself be selected for scanning");
 ScanReporter reporter(root,scan_progress,progress);
 reporter.emit(true);check_cancel(cancel);
 auto inv=scan(root,progress,cancel,[&](const ScanProgress& s){reporter.enumerate(s);});
 reporter.state.phase=ScanPhase::Hashing;reporter.state.root=inv.root;
 reporter.state.current_file.clear();reporter.state.files_discovered=inv.files.size();
 reporter.state.total_bytes=inv.bytes;reporter.state.files_completed=0;
 reporter.state.bytes_processed=0;reporter.state.bytes_read=0;
 reporter.state.errors=inv.errors.size();reporter.state.totals_known=true;
 reporter.emit(true);check_cancel(cancel);
 FileDigest digest;
 std::vector<char> buffer(1024U*1024U);
 U64 accounted=0;
 for(auto& f:inv.files){
   check_cancel(cancel);
   reporter.state.current_file=f.rel;
   try{f.sha256=hash_file(inv.root/f.rel,f.bytes,f.mtime,digest,buffer,reporter,cancel);}
   catch(const std::exception& e){check_cancel(cancel);inv.errors.push_back(utf8(f.rel)+": "+e.what());}
   accounted+=f.bytes;reporter.state.bytes_processed=accounted;
   ++reporter.state.files_completed;reporter.state.errors=inv.errors.size();
   reporter.emit();
 }
 reporter.state.current_file.clear();reporter.emit(true);check_cancel(cancel);
 reporter.state.phase=ScanPhase::SavingIndex;reporter.emit(true);
 save_index(inv,progress,reporter,cancel);check_cancel(cancel);
 reporter.state.phase=ScanPhase::Complete;reporter.emit(true);
 return inv;
}
FullScan scan_roots_hashed(const std::vector<fs::path>& roots,
                           const Progress& progress,const std::atomic_bool* cancel,
                           const ScanProgressCallback& scan_progress){
 const auto started=ScanClock::now();
 ScanProgress last_status;
 auto publish=[&](ScanProgress status,size_t root_index){
    status.root_index=root_index;status.root_count=roots.size();
    status.elapsed_seconds=std::chrono::duration<double>(ScanClock::now()-started).count();
    last_status=status;if(scan_progress)scan_progress(status);
 };
 if(roots.empty())throw std::runtime_error("No volumes or directories were selected for full scan");
 // Reject nested or repeated roots. Besides double-counting files, overlapping
 // roots could cause inconsistent content identities across scopes.
 std::vector<fs::path> normalized;normalized.reserve(roots.size());
 for(const auto& root:roots){
    std::error_code ec;auto abs=fs::absolute(root,ec).lexically_normal();
    if(ec)throw std::runtime_error("Invalid scan root: "+utf8(root));
    for(const auto& prior:normalized)
       if(inside_or_same(prior,abs)||inside_or_same(abs,prior))
          throw std::runtime_error("Full scan requires non-overlapping roots: "+utf8(abs));
    normalized.push_back(abs);
 }
 FullScan report;
 for(size_t i=0;i<normalized.size();++i){
    if(cancel&&cancel->load())throw std::runtime_error("Operation canceled");
    const auto& root=normalized[i];
    if(progress)progress("Scanning volume "+std::to_string(i+1)+"/"+
                         std::to_string(normalized.size())+": "+utf8(root));
    try{
       auto inv=scan_hashed(root,progress,cancel,[&](const ScanProgress& s){publish(s,i+1);});
       report.total_bytes+=inv.bytes;
       report.total_files+=inv.files.size();
       report.volumes.push_back(std::move(inv));
    }catch(const std::exception& e){
       if(cancel&&cancel->load())throw;
       report.errors.push_back("Volume "+utf8(root)+": "+e.what());
    }
 }
 // Hash groups are marked CANDIDATES only. The existing per-root content
 // verifier is applied after a volume is selected for operations.
 last_status.phase=ScanPhase::Analyzing;last_status.current_file.clear();
 publish(last_status,roots.size());
 auto last_group_update=ScanClock::now();
 std::map<std::pair<U64,std::string>,std::vector<ScopeOccurrence>> groups;
 for(size_t v=0;v<report.volumes.size();++v)
    for(size_t f=0;f<report.volumes[v].files.size();++f){
       check_cancel(cancel);
       if(ScanClock::now()-last_group_update>=std::chrono::milliseconds(100)){
          last_group_update=ScanClock::now();publish(last_status,roots.size());
       }
       const auto& file=report.volumes[v].files[f];
       if(file.sha256.size()==64)
          groups[{file.bytes,file.sha256}].push_back({v,f});
    }
 for(auto& group:groups){
    check_cancel(cancel);
    if(group.second.size()<2)continue;
    report.hash_candidates.push_back({group.first.first,group.first.second,std::move(group.second)});
 }
 std::sort(report.hash_candidates.begin(),report.hash_candidates.end(),
    [](const ScopeHashGroup& a,const ScopeHashGroup& b){
        auto aa=a.bytes*static_cast<U64>(a.members.size()-1);
        auto bb=b.bytes*static_cast<U64>(b.members.size()-1);
        return aa==bb?a.members.size()>b.members.size():aa>bb;
    });
 check_cancel(cancel);last_status.phase=ScanPhase::Complete;
 publish(last_status,roots.size());
 return report;
}
} // namespace dc
