#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
using namespace dc;
static void put(const fs::path& p,const std::string& content){
    fs::create_directories(p.parent_path());
    std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<content;
    if(!out)throw std::runtime_error("fixture write failed");
}
static bool rejected(const std::function<void()>& operation){
    try{operation();return false;}catch(const std::exception&){return true;}
}
static void assert_observation_only(const Inventory& inv){
    for(const auto& file:inv.files)assert(file.content==-1);
}
static size_t file_index(const Inventory& inv,const fs::path& path){
    for(size_t i=0;i<inv.files.size();++i)if(inv.files[i].rel==path)return i;
    throw std::runtime_error("fixture file missing");
}
int main(){
    const auto root=fs::temp_directory_path()/("dc_hash_actions_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        for(const auto* name:{"a.bin","b.bin","c.bin","d.bin"})put(root/"copies"/name,"same data");
        put(root/"unique.bin","unique");
        put(root/"old"/"file.bin","old content");
        put(root/"outside"/"one.bin","old content");
        auto observed=scan_hashed(root);assert(observed.complete());assert_observation_only(observed);
        RetentionPolicy one;one.min_active_copies=1;
        auto manual=plan_quarantine_hashed_selected(observed,{"copies/b.bin","copies/c.bin","copies/d.bin"},one);
        assert(manual.moves.size()==3&&manual.moves.front().keepers.size()==1);
        assert(manual.name.find("Quarantine verified duplicate files")==0);
        assert(manual.warnings.front().find("candidates only")!=std::string::npos);
        assert_observation_only(observed);
        assert(execute_plan(root,manual).success);
        assert(fs::exists(root/"copies/a.bin")&&!fs::exists(root/"copies/b.bin"));
        assert(undo_latest(root).success);
        RetentionPolicy three;three.min_active_copies=3;
        auto keep3=plan_quarantine_hashed_selected(observed,{"copies/d.bin"},three);
        assert(keep3.moves.size()==1&&keep3.moves.front().keepers.size()==3);
        assert(execute_plan(root,keep3).success);assert(undo_latest(root).success);
        assert(rejected([&]{(void)plan_quarantine_hashed_selected(observed,{"copies/b.bin","copies/c.bin"},three);}));
        RetentionPolicy zero;zero.min_active_copies=0;
        assert(rejected([&]{(void)plan_clean_hashed_duplicates(observed,zero);}));
        zero.allow_zero_active=true;
        auto none=plan_quarantine_hashed_selected(observed,{"copies/a.bin","copies/b.bin","copies/c.bin","copies/d.bin"},zero);
        assert(none.moves.size()==4&&none.moves.front().keepers.empty());
        assert(execute_plan(root,none).success);assert(undo_latest(root).success);
        auto automatic=plan_clean_hashed_duplicates(observed,one);
        assert(automatic.moves.size()==4); // three copies + one old/outside candidate
        assert(automatic.cleanup_summary && automatic.cleanup_summary->duplicate_groups==2);
        assert(automatic.cleanup_summary->planned_bytes==38);
        const auto default_cleanup=plan_clean_hashed_duplicates(observed,RetentionPolicy{});
        assert(default_cleanup.cleanup_summary->groups_at_or_below_minimum==1);
        assert(default_cleanup.moves.size()==2 && default_cleanup.cleanup_summary->planned_bytes==18);
        assert(!manual.cleanup_summary);
        assert_observation_only(observed);
        auto retirement=plan_retire_hashed_directory(observed,"old",one);
        assert(retirement.quarantine.moves.size()==1&&retirement.blocked.empty());
        auto blocked=plan_retire_hashed_directory(observed,"old",RetentionPolicy{});
        assert(blocked.quarantine.moves.empty()&&blocked.blocked.size()==1);
        // Candidate evidence can be forged in a test. Default execution must
        // still reject equal size/hash but different actual byte contents.
        put(root/"collision"/"a.bin","AAAA");put(root/"collision"/"b.bin","BBBB");
        auto collision=scan_hashed(root);
        const auto a=file_index(collision,"collision/a.bin"),b=file_index(collision,"collision/b.bin");
        collision.files[b].sha256=collision.files[a].sha256;
        auto collision_plan=plan_quarantine_hashed_selected(collision,{"collision/b.bin"},one);
        assert(!execute_plan(root,collision_plan).success);
        assert(fs::exists(root/"collision/a.bin")&&fs::exists(root/"collision/b.bin"));
        assert_observation_only(collision);
        // Source metadata drift still invalidates a plan in hash-only mode.
        auto fresh=scan_hashed(root);RetentionPolicy fast=one;fast.verify_bytes_before_change=false;
        auto fast_plan=plan_quarantine_hashed_selected(fresh,{"copies/d.bin"},fast);
        assert(fast_plan.warnings.front().find("NOT be re-read")!=std::string::npos);
        assert(execute_plan(root,fast_plan).success);assert(undo_latest(root).success);
        fresh=scan_hashed(root);
        auto changed=plan_quarantine_hashed_selected(fresh,{"copies/d.bin"},fast);
        put(root/"copies/d.bin","a longer replacement");
        assert(!execute_plan(root,changed).success);assert(fs::exists(root/"copies/d.bin"));
        put(root/"copies/d.bin","same data");
        fresh=scan_hashed(root);
        auto keeper_changed=plan_quarantine_hashed_selected(fresh,{"copies/d.bin"},fast);
        const auto keeper=keeper_changed.moves.front().keepers.front();
        fs::last_write_time(root/keeper,fs::last_write_time(root/keeper)+std::chrono::seconds(5));
        assert(!execute_plan(root,keeper_changed).success);assert(fs::exists(root/"copies/d.bin"));
        // Hard-link paths obey the explicitly selected independence policy.
        put(root/"links"/"a.bin","linked");std::error_code ec;
        fs::create_hard_link(root/"links"/"a.bin",root/"links"/"alias.bin",ec);
        if(!ec){
            put(root/"links"/"separate.bin","linked");
            auto links=scan_hashed(root);RetentionPolicy two;
            assert(rejected([&]{(void)plan_quarantine_hashed_selected(links,{"links/separate.bin"},two);}));
            two.distinct_file_objects=false;
            auto allowed=plan_quarantine_hashed_selected(links,{"links/separate.bin"},two);
            assert(allowed.moves.size()==1&&allowed.moves.front().keepers.size()==2);
        }
        auto partial=scan_hashed(root);partial.errors.push_back("Access denied");
        assert(rejected([&]{(void)plan_clean_hashed_duplicates(partial,one);}));
        partial.errors.clear();partial.files.front().sha256.clear();
        assert(rejected([&]{(void)plan_clean_hashed_duplicates(partial,one);}));
        partial.files.front().sha256=std::string(64,'z');
        assert(rejected([&]{(void)plan_clean_hashed_duplicates(partial,one);}));
        fs::remove_all(root);std::cout<<"hash-only preview / policy / deferred byte verification tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";std::error_code ec;fs::remove_all(root,ec);return 1;}
}
