#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dc;
static void put(const fs::path& p,const std::string& s){
    fs::create_directories(p.parent_path());std::ofstream out(p,std::ios::binary);
    out<<s;if(!out)throw std::runtime_error("Cannot create fixture");
}
static std::string read(const fs::path& p){
    std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
static std::pair<Inventory,Duplicates> prepare(const fs::path& root){
    Inventory inv=scan(root);auto dup=deduplicate(inv);
    if(!inv.complete()||!dup.errors.empty())throw std::runtime_error("Scan/content verification failed");
    return {std::move(inv),std::move(dup)};
}
static bool fails(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    const auto root=fs::temp_directory_path()/("dc_workflow_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try{
        const auto old=fs::path(L"Backup_2022"), recent=fs::path(L"Backup_2024"),output=fs::path(L"整理结果");
        put(root/old/"shared.jpg","SAME-IMAGE");
        put(root/old/"old_only.jpg","UNIQUE-OLD");
        put(root/old/"collision.jpg","OLD-EDIT");
        put(root/recent/"shared.jpg","SAME-IMAGE");
        put(root/recent/"collision.jpg","NEW-EDIT");
        put(root/recent/"fresh.jpg","NEW-ONLY");
        auto inventory_result=prepare(root);
        auto& inv=inventory_result.first;
        auto& dup=inventory_result.second;
        const auto advice=suggest_structure(inv);
        assert(!fs::exists(root/output));
        auto edited=advice;
        for(auto& r:edited.rules)if(r.source==old||r.source==recent)
            r.target=fs::path(L"照片");
        edited.file_rules.push_back({old/"shared.jpg",fs::path(L"相机档案")/"handpicked.jpg"});
        // Explicit override is honored instead of silently reusing a different path.
        auto proposal=plan_structure(inv,edited);
        bool found=false;
        for(const auto& m:proposal.mappings)if(m.from==old/"shared.jpg"){
            assert(m.to==output/L"相机档案"/"handpicked.jpg");found=true;
        }
        assert(found);
        assert(proposal.mappings.size()==6);
        assert(!fs::exists(root/output)); // analysis must be read-only
        auto traversal=edited;traversal.file_rules[0].target="../escape.jpg";
        assert(fails([&]{(void)plan_structure(inv,traversal);}));
        auto nonexistent=edited;nonexistent.file_rules[0].source="missing.jpg";
        assert(fails([&]{(void)plan_structure(inv,nonexistent);}));
        auto duplicate_target=edited;
        duplicate_target.file_rules.push_back({recent/"shared.jpg",fs::path(L"相机档案")/"handpicked.jpg"});
        assert(fails([&]{(void)plan_structure(inv,duplicate_target);}));
        auto invalid_win_name=edited;
        invalid_win_name.file_rules[0].target=fs::path(L"相机档案")/L"CON.backup.txt";
        assert(fails([&]{(void)plan_structure(inv,invalid_win_name);}));
        assert(fails([&]{(void)plan_retire_directory(inv,dup,fs::path{});}));
        auto file_into_folder=edited;
        file_into_folder.file_rules[0].target=L"照片";
        assert(fails([&]{(void)plan_structure(inv,file_into_folder);}));
        // Stale preview must refuse even if changes only appear as new files.
        put(root/old/"after_preview.jpg","ADDED");
        assert(!execute_structure_plan(root,proposal).success);
        assert(!fs::exists(root/output));
        fs::remove(root/old/"after_preview.jpg");
        const auto applied=execute_structure_plan(root,proposal);
        if(!applied.success)throw std::runtime_error("Migration failed: "+applied.message);
        assert(read(root/output/L"相机档案"/"handpicked.jpg")=="SAME-IMAGE");
        assert(read(root/old/"old_only.jpg")=="UNIQUE-OLD");
        // All source bytes must still exist after non-destructive migration.
        for(const auto& m:proposal.mappings){
            assert(fs::is_regular_file(root/m.from)&&fs::is_regular_file(root/m.to));
            assert(read(root/m.from)==read(root/m.to));
        }
        auto post_result=prepare(root);
        auto& post=post_result.first;
        auto& postdup=post_result.second;
        auto collision_edit=suggest_structure(post);
        for(auto& rule:collision_edit.rules)if(rule.source==old||rule.source==recent)rule.target=L"照片";
        collision_edit.file_rules.push_back({old/"collision.jpg",fs::path(L"相机档案")/"handpicked.jpg"});
        // User-selected exact target already exists but has DIFFERENT content.
        // It must not be replaced, auto-renamed, or silently reassigned.
        assert(fails([&]{(void)plan_structure(post,collision_edit);}));
        assert(read(root/output/L"相机档案"/"handpicked.jpg")=="SAME-IMAGE");
        auto retirement=plan_retire_directory(post,postdup,old);
        assert(retirement.quarantine.moves.size()==1); // shared: two outside (recent + output)
        assert(retirement.blocked.size()==2); // other two versions have only one outside
        assert(retirement.quarantine.moves.front().from==old/"shared.jpg");
        assert(retirement.staged_bytes==std::string("SAME-IMAGE").size());
        // A same-size changed source with restored mtime must fail when its
        // bytes no longer match the two protected keepers.
        const auto retire_source=root/old/"shared.jpg";
        const auto before_time=fs::last_write_time(retire_source);
        put(retire_source,"FAKE-IMAGE");
        fs::last_write_time(retire_source,before_time);
        assert(!execute_plan(root,retirement.quarantine).success);
        assert(read(root/old/"shared.jpg")=="FAKE-IMAGE");
        put(retire_source,"SAME-IMAGE");fs::last_write_time(retire_source,before_time);
        // Cannot retire the whole old directory, only safely isolated files.
        const auto quarantined=execute_plan(root,retirement.quarantine);
        if(!quarantined.success)throw std::runtime_error("Retirement quarantine failed: "+quarantined.message);
        assert(!fs::exists(root/old/"shared.jpg"));
        assert(fs::exists(root/old/"old_only.jpg")&&fs::exists(root/old/"collision.jpg"));
        assert(read(root/recent/"shared.jpg")=="SAME-IMAGE");
        assert(read(root/output/L"相机档案"/"handpicked.jpg")=="SAME-IMAGE");
        auto [again,again_dup]=prepare(root);
        auto deny=plan_retire_directory(again,again_dup,recent);
        // Only one outside keeper (new output) would remain if recent/shared was moved.
        bool blocked_shared=false;
        for(const auto& p:deny.blocked)if(p==recent/"shared.jpg")blocked_shared=true;
        assert(blocked_shared);
        assert(undo_latest(root).success);
        assert(read(root/old/"shared.jpg")=="SAME-IMAGE");
        // Two hardlink names to the same inode do NOT qualify as two keepers.
        auto hardroot=root/"hardlink_case";
        put(hardroot/"old"/"item.bin","BIN");
        put(hardroot/"copies"/"a.bin","BIN");
        fs::create_hard_link(hardroot/"copies"/"a.bin",hardroot/"copies"/"b.bin");
        auto [hard,hd]=prepare(hardroot);
        auto protect=plan_retire_directory(hard,hd,"old");
        assert(protect.quarantine.moves.empty());
        assert(protect.blocked.size()==1);
        assert(fs::exists(hardroot/"old"/"item.bin"));
        std::error_code ec;fs::remove_all(root,ec);
        std::cout<<"INTEGRATED FILE-EDIT / MIGRATION / TWO-KEEPER RETIREMENT PASSED\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"WORKFLOW FAIL: "<<e.what()<<" root="<<utf8(root)<<"\n";
        return 2;
    }
}
