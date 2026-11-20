#include "core.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dc;
static void put(const fs::path& p,const std::string& s){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot create fixture");f.write(s.data(),std::streamsize(s.size()));}
static bool has(const fs::path& p){return fs::exists(p);}
static bool failed(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){auto root=fs::temp_directory_path()/("dc_native_test_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 try{
  put(root/"A"/"one.txt","same-one");put(root/"B"/"one.txt","same-one");
  put(root/"A"/"small"/"x.bin","abc");put(root/"B"/"small"/"x.bin","abc");
  fs::create_directories(root/"A"/"empty");fs::create_directories(root/"B"/"empty");
  put(root/"C"/"other.txt","different");
  auto inv=scan(root);assert(inv.files.size()==5&&inv.dirs.size()==8);assert(inv.complete());
  assert(inv.bytes==31); // two 8-byte files, two 3-byte files, one 9-byte file
  auto dup=deduplicate(inv);assert(dup.groups.size()==2);assert(dup.wasted==11);
  auto dd=duplicate_dirs(inv);bool equal=false;
  for(auto& g:dd)if(g.paths.size()==2&&g.paths[0]==fs::path("A")&&g.paths[1]==fs::path("B"))equal=true;
  assert(equal);assert(!dd.empty());
  auto cmp=compare_dirs(inv,"A","B");assert(cmp.left_in_right&&cmp.right_in_left&&cmp.changed==0);
  auto partial=compare_dirs(inv,"A","C");assert(!partial.left_in_right&&!partial.right_in_left);
  auto unchecked=scan(root);
  assert(failed([&]{compare_dirs(unchecked,"A","B");})); // content IDs have not been verified

  auto rel=directory_relations(inv);assert(!rel.empty());bool saw_exact=false;
  for(auto& r:rel)if(r.relation=="EXACT TREE")saw_exact=true;
  assert(saw_exact);
  // Strict two-copy policy: two verified copies cannot be quarantined.
  auto clean=plan_clean_duplicates(inv,dup);assert(clean.moves.empty());
  // Three independently stored files permit at most one quarantine.
  put(root/"C"/"one.txt","same-one");
  inv=scan(root);dup=deduplicate(inv);clean=plan_clean_duplicates(inv,dup);
  assert(clean.moves.size()==1);
  put(root/"B"/"one.txt","altered!");auto rejected=execute_plan(root,clean);
  assert(!rejected.success&&has(root/"B"/"one.txt"));
  put(root/"B"/"one.txt","same-one");
  put(root/"C"/"one.txt","changed!");put(root/"C"/"one.txt","same-one");
  fs::last_write_time(root/"C"/"one.txt",fs::file_time_type::clock::now()-std::chrono::hours(5));
  auto stale=execute_plan(root,clean);assert(!stale.success);
  auto fresh=scan(root);auto fresh_dup=deduplicate(fresh);clean=plan_clean_duplicates(fresh,fresh_dup);
  auto cr=execute_plan(root,clean);assert(cr.success&&scan(root).files.size()==5);
  // New workspace owner markers must always have portable LF bytes. In rc3,
  // MSVC text-mode ofstream silently wrote CRLF while verification was binary.
  {
      std::ifstream owner(root/".DiskCurator-System"/"owner.txt",std::ios::binary);
      assert(owner.good());
      std::string bytes((std::istreambuf_iterator<char>(owner)),std::istreambuf_iterator<char>());
      assert(bytes=="DiskCurator Native workspace v1\n");
  }
  assert(undo_latest(root).success&&scan(root).files.size()==6);
  fs::remove(root/"C"/"one.txt");
  // Case-insensitive glob, path traversal and collision checks.
  assert(glob(L"*.JPG",L"photo.jpg")&&glob(L"a?c",L"AbC")&&!glob(L"a?",L"abcd"));
  assert(!is_inside("../elsewhere"));assert(failed([&]{Plan invalid;invalid.name="bad";invalid.moves={{"A/one.txt","../escape.txt",{}}};execute_plan(root,invalid);}));
  assert(failed([&]{Plan invalid;invalid.name="bad";invalid.moves={{"A","B",{}},{"A/one.txt","C",{}}};execute_plan(root,invalid);}));
  // The single-file form accepts a complete new name, changing only one exact source.
  RenameOptions direct;
  direct.single_file=FileRename{"A/one.txt","invoice.final.csv"};
  const auto before=scan(root);
  const auto single=plan_rename(before,direct);
  assert(single.moves.size()==1&&single.moves.front().from==fs::path("A/one.txt"));
  assert(single.moves.front().to==fs::path("A/invoice.final.csv")&&!single.moves.front().expected_source.empty());
  assert(execute_plan(root,single).success&&has(root/"A/invoice.final.csv"));
  assert(undo_latest(root).success&&has(root/"A/one.txt"));
  for(const auto invalid:{"../other.txt","CON.txt","one.txt","nested/name",""}){
      direct.single_file->new_filename=invalid;
      assert(failed([&]{(void)plan_rename(before,direct);}));
  }
  RenameOptions o;o.dir="A";o.prefix=L"X_";o.pattern=L"*.txt";
  auto ren=plan_rename(scan(root),o);assert(ren.moves.size()==1);
  auto reserved_opts=o;reserved_opts.prefix=L"";reserved_opts.find=L"one";reserved_opts.replace=L"CON.backup";
  assert(failed([&]{(void)plan_rename(scan(root),reserved_opts);}));
  auto rr=execute_plan(root,ren);assert(rr.success&&has(root/"A"/"X_one.txt"));
  assert(undo_latest(root).success&&has(root/"A"/"one.txt"));
  // Source-directory drift must stop a folder rename before moving new files.
  RenameOptions folder_opts;folder_opts.dir="";folder_opts.pattern=L"A";folder_opts.include_files=false;folder_opts.prefix=L"renamed_";
  auto folder_plan=plan_rename(scan(root),folder_opts);assert(folder_plan.moves.size()==1);
  put(root/"A"/"new_after_preview.bin","new-after-preview");
  auto drift=execute_plan(root,folder_plan);assert(!drift.success);
  assert(has(root/"A"/"new_after_preview.bin")&&!has(root/"renamed_A"));
  folder_plan=plan_rename(scan(root),folder_opts);
  auto folder_success=execute_plan(root,folder_plan);assert(folder_success.success&&has(root/"renamed_A"/"new_after_preview.bin"));
  assert(undo_latest(root).success&&has(root/"A"/"new_after_preview.bin"));
  fs::remove(root/"A"/"new_after_preview.bin");
  // Unselected, existing rename targets should be rejected at PREVIEW time.
  put(root/"A"/"X_one.txt","occupied");
  auto collision_opts=o;collision_opts.pattern=L"one.txt";
  assert(failed([&]{plan_rename(scan(root),collision_opts);}));
  fs::remove(root/"A"/"X_one.txt");
  ArchiveOptions opt;opt.mode=ArchiveMode::ByType;auto ar=plan_archive(scan(root),opt);assert(ar.moves.size()==5);
  auto er=execute_plan(root,ar);assert(er.success&&has(root/"Organized"/"Documents"/"one.txt"));
  assert(undo_latest(root).success&&has(root/"A"/"one.txt"));
  put(root/"cycle"/"a.txt","aa");put(root/"cycle"/"b.txt","bb");
  Plan cycle;cycle.name="swap";cycle.moves={{"cycle/a.txt","cycle/b.txt",{}},{"cycle/b.txt","cycle/a.txt",{}}};
  assert(execute_plan(root,cycle).success);assert(has(root/"cycle"/"a.txt"));
  assert(undo_latest(root).success);
  // Sampling equal, middle bytes different: must not be classified as duplicate.
  std::string x(200000,'A'),y=x;y[100000]='B';
  put(root/"sample"/"x.dat",x);put(root/"sample"/"y.dat",y);
  auto sampled=scan(root);auto d2=deduplicate(sampled);bool false_pair=false;
  for(auto& g:d2.groups)for(auto i:g)if(sampled.files[i].rel==fs::path("sample/x.dat"))false_pair=true;
  assert(!false_pair);
  // Directory containment by relative paths and by verified contents.
  put(root/"subset"/"file.bin","content-in-subset");put(root/"superset"/"file.bin","content-in-subset");
  put(root/"superset"/"extra.bin","extra");put(root/"different_name"/"renamed.bin","content-in-subset");
  auto tree=scan(root);auto tree_dup=deduplicate(tree);
  auto containing=compare_dirs(tree,"subset","superset");assert(containing.left_in_right&&!containing.right_in_left);
  auto relations=directory_relations(tree);bool contains_content=false;
  for(auto& r:relations)if((r.left==fs::path("subset")&&r.right==fs::path("different_name"))||(r.right==fs::path("subset")&&r.left==fs::path("different_name")))contains_content=true;
  assert(contains_content);
  // Photo similarity and chronological fallback (Linux PGM decoder; Windows WIC reader).
#ifndef _WIN32
  std::string ppm="P5\n9 8\n255\n";for(int j=0;j<8;++j)for(int i=0;i<9;++i)ppm+=char(i*20);
  put(root/"imgs"/"a.pgm",ppm);put(root/"imgs"/"b.pgm",ppm);
  auto report=similar_photos(scan(root),0);assert(report.pairs.size()==1&&report.photos.size()==2);
  ArchiveOptions dates;dates.mode=ArchiveMode::ByYearMonth;dates.destination="DateArchive";
  auto dateplan=plan_archive(scan(root),dates);assert(dateplan.moves.size()==2);
  assert(dateplan.warnings.size()==1);
#endif
  // Incomplete scans must not enable exact directory equivalence or quarantine.
  std::error_code ec;fs::create_symlink(root/"C"/"other.txt",root/"suspicious.link",ec);
  if(!ec){auto incomplete=scan(root);assert(!incomplete.complete());auto unsafe_dup=deduplicate(incomplete);
     assert(failed([&]{duplicate_dirs(incomplete);}));assert(failed([&]{plan_clean_duplicates(incomplete,unsafe_dup);}));
     fs::remove(root/"suspicious.link");}
  // Fatal recovery records block new file operations, preventing silent data loss.
  auto journal=root/".DiskCurator-System"/"journal"/"000000-manual.log";
  put(journal,"LABEL 616263\nRECOVERY_FAILED\n");
  auto blocked=execute_plan(root,cycle);assert(!blocked.success);assert(blocked.message.find("MANUAL RECOVERY")!=std::string::npos);
  fs::remove(journal);
  assert(!history(root).empty());assert(recover_incomplete(root).empty());
  std::atomic_bool cancel{true};assert(failed([&]{scan(root,{},&cancel);}));
  assert(utf8(from_utf8(u8"资料/照片.jpg"))==std::string(u8"资料/照片.jpg"));
  std::cout<<"ALL CORE TESTS PASSED\n";fs::remove_all(root);return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"; test root="<<root<<"\n";return 2;}
}
