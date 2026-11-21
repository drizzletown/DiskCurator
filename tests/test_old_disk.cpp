#include "old_disk.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
using namespace dc;
namespace {
struct Fixture {
    fs::path root=fs::temp_directory_path()/("dc_old_disk_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Fixture(){std::error_code ec;fs::remove_all(root,ec);}
    void write(const fs::path& rel,const std::string& body)const {
        fs::create_directories((root/rel).parent_path());
        std::ofstream out(root/rel,std::ios::binary);assert(out);out<<body;assert(out);
    }
    Inventory read()const{return scan_hashed(root,{},nullptr,{},false);}
};
const OldDiskPair* find(const OldDiskOverview& o,const fs::path& a,const fs::path& b){
    for(const auto& pair:o.versions)if((pair.left==a&&pair.right==b)||(pair.left==b&&pair.right==a))return &pair;
    return nullptr;
}
}
int main(){
    Fixture disk;
    const fs::path a="Alpha",b="2019-transfer";
    for(const auto& root:{a,b}){
        disk.write(root/"first"/"budget","final budget");
        disk.write(root/"second"/"ledger","retained ledger");
    }
    disk.write(a/"memo","old memo");disk.write(b/"sub"/"renamed","old memo");
    disk.write(a/"report","draft");disk.write(b/"report","edited");
    disk.write(a/"only-old","unused");disk.write(b/"new-work","addition");
    const auto metadata=scan_space(disk.root);
    const auto clues=describe_old_disk_folders(metadata.inventory);
    assert(clues.complete&&clues.versions.empty()&&clues.duplicate_groups==0);
    assert(clues.folder_clues.at(a).categories.at("Other")==5);
    assert(clues.folder_clues.at(a).earliest<=clues.folder_clues.at(a).latest);
    auto inv=disk.read();const auto scan=inspect_old_disk(inv);
    assert(scan.complete && scan.unread_files==0 && !scan.discovery_limited);
    assert(scan.largest_folders.size()==2&&scan.largest_folders[0].bytes>=scan.largest_folders[1].bytes);
    assert(scan.duplicate_groups==3&&scan.duplicate_files==6);
    assert(scan.redundant_bytes==std::string("final budgetretained ledgerold memo").size());
    const auto* pair=find(scan,a,b);assert(pair && !pair->same_tree);
    assert(pair->shared_contents==3 && pair->coverage_percent==60);
    assert(pair->left_only_paths==2&&pair->right_only_paths==2);
    assert(pair->same_path_files==2&&pair->conflicting_paths==1);
    assert(!fs::exists(disk.root/".DiskCurator-System"));

    // Every member changed both name and location. The original directory
    // names carry no evidence of a relationship.
    Fixture moved;
    for(size_t i=0;i<6;++i){
        const auto n=std::to_string(i);
        moved.write(fs::path("yellow")/("scan"+n),n);
        moved.write(fs::path("transfer")/("draft"+n),n);
    }
    auto relocated=inspect_old_disk(moved.read());pair=find(relocated,"yellow","transfer");
    assert(pair&&pair->shared_contents==6&&pair->same_path_files==0&&!pair->same_tree);

    // Matching file lists alone cannot establish equal trees: empty folders
    // can be present on one side only.
    Fixture trees;
    for(const auto* dir:{"D1","D2"}){
        trees.write(fs::path(dir)/"x","one");trees.write(fs::path(dir)/"y","two");
        fs::create_directories(trees.root/dir/"empty");
    }
    auto equal=inspect_old_disk(trees.read());pair=find(equal,"D1","D2");assert(pair&&pair->same_tree);
    fs::create_directory(trees.root/"D2"/"different empty");
    auto different=inspect_old_disk(trees.read());pair=find(different,"D1","D2");assert(pair&&!pair->same_tree);

    Fixture templates;
    for(const auto* dir:{"client","unrelated"}){
        templates.write(fs::path(dir)/"LICENSE.txt","standard license");
        templates.write(fs::path(dir)/".gitignore","standard ignore");
        templates.write(fs::path(dir)/"notes",dir);
    }
    auto boilerplate=inspect_old_disk(templates.read());
    assert(boilerplate.duplicate_groups==2&&boilerplate.versions.empty());

    Fixture noise;
    for(int i=0;i<20;++i){
        const auto dir=fs::path("unrelated-"+std::to_string(i));
        noise.write(dir/"empty","");noise.write(dir/"desktop.ini","same system file");
        noise.write(dir/"setup.txt","common preface");noise.write(dir/"terms.txt","common terms");
        noise.write(dir/"actual",std::to_string(i));
    }
    auto no_pairs=inspect_old_disk(noise.read());
    assert(no_pairs.versions.empty()&&no_pairs.duplicate_groups==4);
    Fixture repeated;
    for(int version=0;version<18;++version){
        const auto dir=fs::path("version"+std::to_string(version));
        for(int f=0;f<6;++f)repeated.write(dir/("entry"+std::to_string(f)),std::to_string(f));
        repeated.write(dir/"work",std::to_string(version)+" new version");
    }
    auto historical=inspect_old_disk(repeated.read());
    assert(!historical.versions.empty()&&historical.versions.front().shared_contents==6);
    assert(historical.versions.front().conflicting_paths==1&&historical.discovery_limited);
    // Many unrelated folders sharing the same template should cost roughly
    // one pass over the inventory, and cannot crowd out real backup families.
    Inventory large;
    for(size_t i=0;i<1600;++i){
        auto name=fs::path("dir"+std::to_string(i));
        large.dirs.push_back({name,100,21});
        large.files.push_back({name/"template",3,{},-1,"shared-template"});
        for(int j=0;j<20;++j){
            const auto id=std::to_string(i)+"_"+std::to_string(j);
            large.files.push_back({name/id,5,{},-1,id});
        }
    }
    auto mostly_unique=inspect_old_disk(large);
    assert(mostly_unique.complete&&mostly_unique.versions.empty());
    assert(mostly_unique.duplicate_groups==1&&mostly_unique.duplicate_files==1600);
    assert(mostly_unique.largest_folders.size()==8);
#ifdef _WIN32
    Fixture casing;
    casing.write("source/NOTES.txt","one");casing.write("copy/Notes.TXT","one");
    auto variants=inspect_old_disk(casing.read());pair=find(variants,"source","copy");
    assert(pair&&pair->same_path_files==1&&pair->same_tree);
#endif
    std::atomic_bool stopped{true};
    bool canceled=false;try{(void)inspect_old_disk(inv,&stopped);}catch(const std::runtime_error&){canceled=true;}
    assert(canceled);
    // Positive matches survive an inaccessible sibling; users must re-read
    // the pair to find out whether either disk actually lacks a file.
    auto assert_partial_pair=[&](const OldDiskOverview& partial){
        const auto* candidate=find(partial,a,b);
        assert(!partial.complete&&candidate&&candidate->shared_contents>=2);
        assert(!candidate->same_tree&&!candidate->coverage_percent&&!candidate->same_path_files);
        assert(!candidate->conflicting_paths&&!candidate->left_only_paths&&!candidate->right_only_paths);
    };
    auto failed=inv;failed.errors.push_back("unrelated sibling unreadable");
    auto unknown=inspect_old_disk(failed);assert(unknown.duplicate_groups==3);assert_partial_pair(unknown);
    const auto unread=std::find_if(inv.files.begin(),inv.files.end(),[&](const File& f){return f.rel==a/"memo";});
    assert(unread!=inv.files.end());unread->sha256.clear();
    unknown=inspect_old_disk(inv);assert(unknown.unread_files==1);assert_partial_pair(unknown);
    assert(find(unknown,a,b)->shared_contents==2);
    std::cout<<"Old disk discovery passed\n";
}
