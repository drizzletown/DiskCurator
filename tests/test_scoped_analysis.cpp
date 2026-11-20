#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace dc;
static void put(const fs::path& path,const std::string& contents){
    fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<contents;
    if(!out)throw std::runtime_error("fixture write failed");
}
static std::string read(const fs::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static bool rejected(const std::function<void()>& action){try{action();return false;}catch(const std::exception&){return true;}}
static size_t file_index(const Inventory& inv,const fs::path& path){
    for(size_t i=0;i<inv.files.size();++i)if(inv.files[i].rel==path)return i;
    throw std::runtime_error("fixture path missing");
}
int main(){
    const auto root=fs::temp_directory_path()/("dc_scoped_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        put(root/"A/common.bin","shared");put(root/"B/common.bin","shared");
        put(root/"A/conflict.txt","LEFT!");put(root/"B/conflict.txt","RIGHT");
        put(root/"A/only_a.txt","source only");put(root/"B/only_b.txt","destination only");
        fs::create_directories(root/"A/empty");fs::create_directories(root/"B/empty");
        put(root/"unrelated/common.bin","shared");
        auto observed=scan_hashed(root);assert(observed.complete());
        // An unrelated file vanished after scanning. A root-wide verification
        // would try this SHA candidate and fail. A/B work must not touch it.
        fs::remove(root/"unrelated/common.bin");
        // Synthetic 1 TiB records model large offline unrelated duplicates;
        // no such data is allocated/read. If the scope leaks, verification
        // attempts their nonexistent paths and fails the test.
        const U64 large=U64{1}<<40;
        observed.files.push_back({"unrelated/large-a.bin",large,{},-1,std::string(64,'a')});
        observed.files.push_back({"unrelated/large-b.bin",large,{},-1,std::string(64,'a')});
        observed.bytes+=large*2;
        U64 actual_reads=0;bool scoped=false;
        const auto progress=[&](const ScanProgress& p){
            if(p.phase==ScanPhase::Verifying){
                scoped=true;assert(p.files_discovered==6);actual_reads=p.bytes_read;
                if(!p.current_file.empty())assert(p.current_file.begin()->string()=="A"||p.current_file.begin()->string()=="B");
            }
        };
        auto assessment=evaluate_hashed_directory_pair(observed,"A","B",{},nullptr,progress);
        assert(scoped&&actual_reads==12); // just the two shared 6-byte files
        assert(assessment.same_relative_files==1&&assessment.same_path_different_content==1);
        assert(assessment.left_only_files==1&&assessment.right_only_files==1);
        assert(assessment.disjoint_for_merge&&!assessment.exact_tree);
        for(const auto& file:observed.files)assert(file.content==-1);
        assert(!fs::exists(root/".DiskCurator-System"));
        // Deliberately forged matching hashes cannot turn unequal actual bytes
        // into identical paths or cause the source conflict version to be lost.
        auto collision=observed;
        collision.files[file_index(collision,"B/conflict.txt")].sha256=collision.files[file_index(collision,"A/conflict.txt")].sha256;
        auto collision_report=evaluate_hashed_directory_pair(collision,"A","B");
        assert(collision_report.same_path_different_content==1&&!collision_report.exact_tree);
        auto plan=plan_hashed_directory_merge(collision,"A","B");
        assert(plan.copies.size()==2&&plan.conflicts==1&&plan.skipped.size()==1);
        assert(plan.source_snapshot.size()==5&&plan.destination_snapshot.size()==5);
        assert(!fs::exists(root/".DiskCurator-System"));
        // A completed preview still rejects changes to the selected trees.
        put(root/"A/arrived_after_preview.txt","new");
        assert(!execute_merge_plan(root,plan).success);
        fs::remove(root/"A/arrived_after_preview.txt");
        const auto common_time=fs::last_write_time(root/"B/common.bin");
        put(root/"B/common.bin","SHARED");fs::last_write_time(root/"B/common.bin",common_time);
        assert(!execute_merge_plan(root,plan).success);
        assert(read(root/"A/common.bin")=="shared");
        put(root/"B/common.bin","shared");fs::last_write_time(root/"B/common.bin",common_time);
        assert(execute_merge_plan(root,plan).success);
        assert(read(root/"B/conflict.txt")=="RIGHT");
        assert(read(root/"B/conflict (from A).txt")=="LEFT!");
        assert(read(root/"A/conflict.txt")=="LEFT!"&&read(root/"B/only_a.txt")=="source only");
        auto fresh=scan_hashed(root);
        auto repeat=plan_hashed_directory_merge(fresh,"A","B");
        assert(repeat.copies.empty()&&repeat.directories.empty());assert(execute_merge_plan(root,repeat).success);
        // Ancestor/descendant assessment uses the union once. Such a pair is
        // valid to inspect but remains forbidden for additive merge execution.
        auto ancestor=evaluate_hashed_directory_pair(fresh,"A","A/empty");
        assert(ancestor.comparison.left_file_count==3&&ancestor.comparison.right_file_count==0);
        assert(!ancestor.disjoint_for_merge);
        assert(rejected([&]{(void)plan_hashed_directory_merge(fresh,"A","A/empty");}));
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(fresh,"A/.","A");}));
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(fresh,"../A","B");}));
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(fresh,"missing","B");}));
        auto partial=fresh;partial.errors.push_back("scan incomplete");
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(partial,"A","B");}));
        auto missing=fresh;missing.files[file_index(missing,"A/common.bin")].sha256.clear();
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(missing,"A","B");}));
        std::atomic_bool cancel{true};
        assert(rejected([&]{(void)evaluate_hashed_directory_pair(fresh,"A","B",{},&cancel);}));
        cancel.store(false);
        const auto stop_on_read=[&](const ScanProgress& p){if(p.phase==ScanPhase::Verifying&&p.bytes_read>0)cancel.store(true);};
        const auto before=history(root).size();
        assert(rejected([&]{(void)plan_hashed_directory_merge(fresh,"A","B",{},&cancel,stop_on_read);}));
        assert(history(root).size()==before);assert(read(root/"B/conflict.txt")=="RIGHT");
        fs::remove_all(root);std::cout<<"scoped directory analysis / collision / merge snapshots / cancellation passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";std::error_code ec;fs::remove_all(root,ec);return 1;}
}
