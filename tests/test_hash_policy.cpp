#include "core.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <functional>
#include <iostream>
using namespace dc;
static void put(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);f<<s;if(!f)throw std::runtime_error("fixture write failed");}
static bool rejected(const std::function<void()>& fn){try{fn();return false;}catch(const std::exception&){return true;}}
int main(){
 const auto root=fs::temp_directory_path()/("dc_hash_policy_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  const auto cache_root=root.parent_path()/(root.filename().string()+"_index");
#ifdef _WIN32
  SetEnvironmentVariableW(L"LOCALAPPDATA",cache_root.c_str());
#else
  setenv("XDG_CACHE_HOME",cache_root.c_str(),1);
#endif
  put(root/"a"/"empty.txt","");
  put(root/"a"/"abc.txt","abc");
  for(const auto* n:{"a.bin","b.bin","c.bin","d.bin"})put(root/"copies"/n,"identical");
  put(root/"old"/"photo.jpg","photo content");
  put(root/"external"/"photo.jpg","photo content");
  auto hashed=scan_hashed(root);assert(hashed.complete());
  assert(hashed.files.size()==8);
  for(const auto& f:hashed.files){assert(f.sha256.size()==64);assert(f.sha256.find_first_not_of("0123456789abcdef")==std::string::npos);}
  for(const auto& f:hashed.files){
    if(f.rel==fs::path("a/empty.txt"))
      assert(f.sha256=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    if(f.rel==fs::path("a/abc.txt"))
      assert(f.sha256=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  }
  // Scan cache contains the externally stored SHA-256 inventory, not data in root.
  assert(!fs::exists(root/".DiskCurator-System"));
#ifdef _WIN32
  const auto store=cache_root/"DiskCurator"/"Cache";
#else
  const auto store=cache_root/"DiskCurator";
#endif
  size_t indices=0;for(const auto& e:fs::directory_iterator(store)){
     assert(e.path().extension()==fs::path(".tsv"));
     std::ifstream cache(e.path(),std::ios::binary);
     std::string header;std::getline(cache,header);
     assert(header.find("DiskCurator SHA256 v1")!=std::string::npos);
     assert(header.find("COMPLETE")!=std::string::npos);
     ++indices;
  }
  assert(indices==1);
  auto dup=deduplicate(hashed);assert(dup.errors.empty());
  RetentionPolicy one;one.min_active_copies=1;
  auto one_plan=plan_quarantine_selected(hashed,dup,{"copies/b.bin","copies/c.bin","copies/d.bin"},one);
  assert(one_plan.moves.size()==3);
  assert(one_plan.moves[0].keepers.size()==1);
  auto res=execute_plan(root,one_plan);assert(res.success);
  assert(fs::exists(root/"copies/a.bin") && !fs::exists(root/"copies/b.bin"));
  assert(undo_latest(root).success);
  RetentionPolicy three;three.min_active_copies=3;
  auto three_plan=plan_quarantine_selected(hashed,dup,{"copies/b.bin"},three);
  assert(three_plan.moves.size()==1&&three_plan.moves.front().keepers.size()==3);
  assert(execute_plan(root,three_plan).success);
  assert(fs::exists(root/"copies/a.bin")&&fs::exists(root/"copies/c.bin")&&fs::exists(root/"copies/d.bin"));
  assert(undo_latest(root).success);
  assert(rejected([&]{auto no=plan_quarantine_selected(hashed,dup,{"copies/b.bin"},RetentionPolicy{4,true});(void)no;}));
  assert(rejected([&]{auto no=plan_clean_duplicates(hashed,dup,RetentionPolicy{0,true});(void)no;}));
  // Retirement policy must count only copies outside the selected directory.
  auto p1=plan_retire_directory(hashed,dup,"old",one);
  assert(p1.quarantine.moves.size()==1);
  auto p2=plan_retire_directory(hashed,dup,"old",RetentionPolicy{});
  assert(p2.quarantine.moves.empty()&&p2.blocked.size()==1);
  put(root/"external"/"photo2.jpg","photo content");
  auto fresh=scan_hashed(root);auto fd=deduplicate(fresh);
  p2=plan_retire_directory(fresh,fd,"old",RetentionPolicy{});
  assert(p2.quarantine.moves.size()==1);
  // Hard links are one physical object if independence is enabled.
  put(root/"hard"/"same.bin","links");std::error_code ec;
  fs::create_hard_link(root/"hard"/"same.bin",root/"hard"/"alias.bin",ec);
  if(!ec){put(root/"hard"/"separate.bin","links");auto h=scan_hashed(root);auto d=deduplicate(h);
    assert(rejected([&]{(void)plan_quarantine_selected(h,d,{"hard/separate.bin"},RetentionPolicy{2,true});}));
    auto relaxed=plan_quarantine_selected(h,d,{"hard/separate.bin"},RetentionPolicy{2,false});
    assert(relaxed.moves.size()==1);assert(execute_plan(root,relaxed).success);assert(undo_latest(root).success);
  }
  // User may explicitly choose zero active keepers, but without consent
  // the planner must refuse to construct that hazardous plan.
  auto z=scan_hashed(root);auto zd=deduplicate(z);
  RetentionPolicy risk;risk.min_active_copies=0;
  assert(rejected([&]{(void)plan_quarantine_selected(z,zd,
     {"copies/a.bin","copies/b.bin","copies/c.bin","copies/d.bin"},risk);}));
  risk.allow_zero_active=true;
  auto zero_plan=plan_quarantine_selected(z,zd,
     {"copies/a.bin","copies/b.bin","copies/c.bin","copies/d.bin"},risk);
  assert(zero_plan.moves.size()==4 && zero_plan.moves.front().keepers.empty());
  assert(execute_plan(root,zero_plan).success);
  for(auto name:{"a.bin","b.bin","c.bin","d.bin"})assert(!fs::exists(root/"copies"/name));
  assert(undo_latest(root).success);
  // Hash-only mode is a user-selected, lower-assurance option, never default.
  auto fast_inv=scan_hashed(root);auto fast_dup=deduplicate(fast_inv);
  RetentionPolicy fast;fast.min_active_copies=2;fast.verify_bytes_before_change=false;
  auto fast_plan=plan_quarantine_selected(fast_inv,fast_dup,{"copies/b.bin"},fast);
  assert(fast_plan.moves.size()==1 && fast_plan.moves.front().scanned_sha256.size()==64);
  assert(fast_plan.moves.front().keeper_proofs.size()==2);
  assert(execute_plan(root,fast_plan).success);
  assert(undo_latest(root).success);
  auto metachanged=plan_quarantine_selected(fast_inv,fast_dup,{"copies/b.bin"},fast);
  put(root/"copies"/"a.bin","altered!-"); // same byte count as identical
  assert(!execute_plan(root,metachanged).success);
  put(root/"copies"/"a.bin","identical");
  // Source metadata can be restored, but full-byte checks must still reject.
  auto current=scan_hashed(root);auto cd=deduplicate(current);
  auto pending=plan_quarantine_selected(current,cd,{"copies/b.bin"},RetentionPolicy{3,true});
  const auto original_time=fs::last_write_time(root/"copies"/"b.bin");
  put(root/"copies"/"b.bin","changed!!!"); // not identical; time restored
  fs::last_write_time(root/"copies"/"b.bin",original_time);
  assert(!execute_plan(root,pending).success);
  fs::remove_all(root,ec);fs::remove_all(cache_root,ec);
  std::cout<<"HASH SHA256 / CONFIGURABLE RETENTION / BYTE VERIFICATION PASSED\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" at "<<root<<"\n";return 2;}
}
