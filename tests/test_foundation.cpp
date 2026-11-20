#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <functional>
using namespace dc;
static void put(const fs::path& p, const std::string& content){
    fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::trunc);
    if(!f)throw std::runtime_error("Cannot create fixture");f<<content;
}
static std::string read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static bool rejected(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    const auto root=fs::temp_directory_path()/("dc_foundation_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        // A pair with overlap, one conflict, distinct names, empty folders.
        put(root/"A"/"common.jpg","PHOTO");put(root/"B"/"common.jpg","PHOTO");
        put(root/"A"/"changed.jpg","OLD");put(root/"B"/"changed.jpg","NEW");
        put(root/"A"/"old-only.jpg","UNIQUE OLD");put(root/"B"/"new-only.jpg","UNIQUE NEW");
        put(root/"A"/"renamed-left.jpg","RENAMED");put(root/"B"/"renamed-right.jpg","RENAMED");
        fs::create_directories(root/"A"/"empty");
        // Three verified distinct physical copies, allow selecting ANY one.
        for(auto name:{"left","middle","right"})put(root/"copies"/(std::string(name)+".bin"),"TRIPLICATE");
        put(root/"unique.txt","UNIQUE");
        // Zero-byte files are legitimate duplicate CONTENT, not evidence that
        // there are three independent backup devices.
        for(auto name:{"a","b","c"})put(root/"zero"/(std::string(name)+".bin"),"");
        auto inv=scan(root);auto duplicates=deduplicate(inv);
        auto rep=evaluate_directory_pair(inv,"A","B");
        assert(rep.disjoint_for_merge && !rep.exact_tree);
        assert(rep.shared_file_occurrences==2 && rep.shared_distinct_contents==2);
        assert(rep.same_relative_files==1 && rep.same_path_different_content==1);
        assert(rep.left_only_files==2 && rep.right_only_files==2);
        assert(rep.left_dirs==1 && rep.left_empty_dirs==1 && rep.right_dirs==0);
        assert(rep.smaller_coverage && *rep.smaller_coverage==0.5);
        assert(rep.multiset_jaccard && *rep.multiset_jaccard==2.0/6.0);
        assert(rep.same_path_fraction && *rep.same_path_fraction==0.5);
        auto catalog=find_overlapping_dirs(inv,100);
        assert(!catalog.truncated);
        bool found_ab=false;
        for(const auto& item:catalog.pairs){
            if((item.left==fs::path("A")&&item.right==fs::path("B"))||
               (item.left==fs::path("B")&&item.right==fs::path("A"))){
                found_ab=true;
                assert(item.shared==2 && item.distinct_shared==2);
            }
        }
        assert(found_ab);
        auto limited=find_overlapping_dirs(inv,0);
        assert(limited.pairs.empty() && limited.truncated && limited.total_candidates>0);
        auto reverse=evaluate_directory_pair(inv,"B","A");
        assert(reverse.shared_file_occurrences==rep.shared_file_occurrences);
        assert(reverse.left_only_files==rep.right_only_files);
        assert(rejected([&]{(void)evaluate_directory_pair(inv,"A","A");}));
        assert(rejected([&]{(void)evaluate_directory_pair(inv,"A","outside");}));
        assert(!evaluate_directory_pair(inv,"A",fs::path{}).disjoint_for_merge);
        // Manual actions are read-only until execute. User must not be able to
        // remove more copies than can be proved safe in the resulting state.
        auto zero=plan_quarantine_selected(inv,duplicates,{"zero/c.bin"});
        assert(zero.moves.size()==1);
        auto chosen=plan_quarantine_selected(inv,duplicates,{"copies/left.bin"});
        assert(chosen.moves.size()==1 && chosen.moves[0].from==fs::path("copies/left.bin"));
        assert(fs::exists(root/"copies"/"left.bin"));
        assert(rejected([&]{(void)plan_quarantine_selected(inv,duplicates,{"copies/left.bin","copies/middle.bin"});}));
        assert(rejected([&]{(void)plan_quarantine_selected(inv,duplicates,{"unique.txt"});}));
        assert(rejected([&]{(void)plan_quarantine_selected(inv,duplicates,{"copies/right.bin","copies/right.bin"});}));
        assert(execute_plan(root,chosen).success);
        assert(!fs::exists(root/"copies"/"left.bin"));
        assert(fs::exists(root/"copies"/"middle.bin") && fs::exists(root/"copies"/"right.bin"));
        assert(undo_latest(root).success);
        assert(read(root/"copies"/"left.bin")=="TRIPLICATE");
        // Source edited after preview must not be quarantined using old plan.
        auto again=scan(root);auto again_dup=deduplicate(again);
        auto stale=plan_quarantine_selected(again,again_dup,{"copies/middle.bin"});
        const auto time=fs::last_write_time(root/"copies"/"middle.bin");
        put(root/"copies"/"middle.bin","MODIFIED!!");  // same length as TRIPLICATE
        fs::last_write_time(root/"copies"/"middle.bin",time);
        auto blocked=execute_plan(root,stale);
        assert(!blocked.success && read(root/"copies"/"middle.bin")=="MODIFIED!!");
        auto incomplete=again;incomplete.errors.push_back("Injected unreadable node");
        assert(rejected([&]{(void)plan_quarantine_selected(incomplete,again_dup,{"copies/left.bin"});}));
        // Content-based similarity is not directory identity. Changing the
        // layout of empty directories prevents exact-tree matches.
        put(root/"emptyA"/"x.txt","identical");put(root/"emptyB"/"x.txt","identical");
        fs::create_directories(root/"emptyA"/"only-empty");
        auto s=scan(root);deduplicate(s);
        auto nested=find_overlapping_dirs(s,500);
        bool found_empty_pair=false;
        for(const auto& item:nested.pairs)if((item.left==fs::path("emptyA")&&item.right==fs::path("emptyB"))||
            (item.left==fs::path("emptyB")&&item.right==fs::path("emptyA")))found_empty_pair=true;
        assert(found_empty_pair);
        auto p=evaluate_directory_pair(s,"emptyA","emptyB");
        assert(!p.exact_tree && p.same_relative_files==1);
        fs::create_directories(root/"bareA");fs::create_directories(root/"bareB");
        auto empties=scan(root);deduplicate(empties);
        auto empty_report=evaluate_directory_pair(empties,"bareA","bareB");
        assert(empty_report.exact_tree);
        assert(!empty_report.smaller_coverage && !empty_report.multiset_jaccard && !empty_report.same_path_fraction);
        // Hard links are NOT two independent keepers; tests on Windows too.
        put(root/"hard"/"A.bin","HARD-CONTENT");
        std::error_code ec;
        fs::create_hard_link(root/"hard"/"A.bin",root/"hard"/"B.bin",ec);
        if(!ec){
            put(root/"hard"/"C.bin","HARD-CONTENT");
            auto h=scan(root);auto d=deduplicate(h);
            assert(rejected([&]{(void)plan_quarantine_selected(h,d,{"hard/C.bin"});}));
            auto ok=plan_quarantine_selected(h,d,{"hard/B.bin"});
            assert(ok.moves.size()==1);
        }
        fs::remove_all(root,ec);
        std::cout<<"FOUNDATION: PAIR METRICS, MANUAL TWO-COPY, STALE DATA, EMPTY DIRS, HARDLINK PASSED\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FOUNDATION FAILED: "<<e.what()<<" root="<<root<<"\n";return 2;}
}
