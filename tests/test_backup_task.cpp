#include "backup_task.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace dc;
static void put(const fs::path& path,const char* data){
    fs::create_directories(path.parent_path());
    std::ofstream out(path,std::ios::binary);out<<data;
    if(!out)throw std::runtime_error("fixture write failed");
}
static const DiffRow& find_row(const BackupComparison& comparison,const fs::path& path){
    const auto found=std::find_if(comparison.rows.begin(),comparison.rows.end(),
        [&](const auto& row){return row.rel==path;});
    if(found==comparison.rows.end())throw std::runtime_error("missing fixture row: "+utf8(path));
    return *found;
}
int main(){
    const auto root=fs::temp_directory_path()/("dc_backup_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto source=root/"originals"/"first",target=root/"archive"/"second";
    try{
        put(source/"common","stable");put(target/"common","stable");
        put(source/"name","newer");put(target/"name","older");
        put(source/"only-source","left");put(target/"only-target","right");
        fs::create_directories(source/"empty");fs::create_directories(target/"other-empty");
        put(root/"unrelated"/"no_index","external");
        bool saw_first=false,saw_second=false;
        auto result=compare_backups(source,target,{},nullptr,[&](const ScanProgress& state){
            assert(state.phase!=ScanPhase::SavingIndex);
            if(state.root_index==1&&state.root_count==2)saw_first=true;
            if(state.root_index==2&&state.root_count==2)saw_second=true;
        });
        assert(saw_first&&saw_second&&result.errors.empty()&&result.rows.size()==6);
        assert(find_row(result,"common").state=="IDENTICAL");
        assert(find_row(result,"name").state=="CHANGED");
        assert(find_row(result,"only-source").state=="ONLY_SOURCE");
        assert(find_row(result,"only-target").state=="ONLY_TARGET");
        assert(find_row(result,"empty").state=="ONLY_SOURCE_DIR");
        assert(find_row(result,"other-empty").state=="ONLY_TARGET_DIR");
        assert(fs::equivalent(root,result.merge_inventory.root)&&result.merge_inventory.files.size()==6);
        auto plan=plan_hashed_directory_merge(result.merge_inventory,result.source_relative,result.target_relative,
                                               {},nullptr,{},MergeMode::MissingOnly);
        assert(plan.copies.size()==1&&plan.directories.size()==1&&plan.identical==1&&plan.skipped_conflicts==1);
        assert(!fs::exists(root/".DiskCurator-System")&&!fs::exists(target/"only-source"));
        auto full_merge=plan_hashed_directory_merge(result.merge_inventory,result.source_relative,result.target_relative);
        for(const auto& copy:full_merge.copies)assert(copy.expected_sha256.size()==64);
        const auto version_stamp=fs::last_write_time(source/"name");
        put(source/"name","other");fs::last_write_time(source/"name",version_stamp);
        assert(!execute_merge_plan(result.merge_inventory.root,full_merge).success);
        assert(!fs::exists(target/"name (from first)")&&!fs::exists(target/"only-source"));
        put(source/"name","newer");fs::last_write_time(source/"name",version_stamp);
        const auto initial_stamp=fs::last_write_time(source/"only-source");
        put(source/"only-source","edit");fs::last_write_time(source/"only-source",initial_stamp);
        assert(!execute_merge_plan(result.merge_inventory.root,plan).success);
        assert(!fs::exists(target/"only-source")&&!fs::exists(target/"empty"));
        put(source/"only-source","left");fs::last_write_time(source/"only-source",initial_stamp);
        assert(execute_merge_plan(result.merge_inventory.root,plan).success);
        assert(fs::is_directory(target/"empty")&&fs::exists(target/"only-source")&&fs::exists(source/"only-source"));
        assert(!fs::exists(target/"name (from first)"));
        auto updated=compare_backups(source,target);assert(find_row(updated,"name").state=="CHANGED");
        assert(find_row(updated,"only-source").state=="IDENTICAL");

        auto incomplete=compare_backups(source,root/"offline");
        assert(!incomplete.errors.empty()&&incomplete.merge_inventory.root.empty());
        assert(find_row(incomplete,"only-source").state=="UNKNOWN");
        put(source/"wrong-kind","file");fs::create_directories(target/"wrong-kind");
        auto collision=compare_backups(source,target);
        assert(find_row(collision,"wrong-kind").state=="TYPE_CONFLICT"&&collision.merge_inventory.root.empty());
        auto nested=compare_backups(source,source/"empty");
        assert(nested.merge_inventory.root.empty());
        bool deleted=false;
        auto changed=compare_backups(source,target,{},nullptr,[&](const ScanProgress& state){
            if(!deleted&&state.root_index==2&&state.phase==ScanPhase::Complete){
                deleted=fs::remove(target/"common");
            }
        });
        assert(deleted&&!changed.errors.empty()&&find_row(changed,"common").state=="UNKNOWN");
        std::atomic_bool cancel{true};bool canceled=false;
        try{compare_backups(source,target,{},&cancel);}catch(const std::runtime_error&){canceled=true;}
        assert(canceled);
        bool same_rejected=false;
        try{compare_backups(source,source/".");}catch(const std::runtime_error&){same_rejected=true;}
        assert(same_rejected);
        fs::remove_all(root);std::cout<<"Backup task comparisons passed\n";
    }catch(...){fs::remove_all(root);throw;}
}
