#include "core.hpp"
#include <cassert>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;

static void write_file(const fs::path& p, const std::string& data){
    fs::create_directories(p.parent_path());
    std::ofstream out(p,std::ios::binary);out<<data;
    if(!out)throw std::runtime_error("cannot create fixture");
}
static bool fails(const std::function<void()>& f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    const auto base=fs::temp_directory_path()/fs::path("dc_scope_")/
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto a=base/"volume_A",b=base/"volume_B";
    try{
        write_file(a/"photos"/"1.jpg","same bytes");
        write_file(b/"backup"/"2.jpg","same bytes");
        write_file(a/"unique.txt","different bytes");
        fs::create_directories(b/"empty");
        // A count-only scan returns the same totals without retaining paths or making a cache.
        const auto counts_root=base/"counts";
        write_file(counts_root/"x"/"data.txt","abc");
        fs::create_directories(counts_root/"empty");
        write_file(counts_root/".DiskCurator-System"/"owner.txt","reserved");
        write_file(counts_root/".DiskCurator-System"/"internal.bin","not user data");
        std::error_code link_error;
        fs::create_directory_symlink(counts_root,counts_root/"cycle",link_error);
        dc::ScanProgress last;
        const auto counts=dc::scan_inventory(counts_root,[&](const dc::ScanProgress& p){last=p;});
        std::atomic_bool stop{false};
        assert(fails([&]{dc::scan_inventory(counts_root,[&](const auto&){stop=true;},&stop);}));
        const auto detailed=dc::scan(counts_root);
        assert(counts.file_count==1 && counts.directory_count==3 && counts.bytes==3);
        assert(counts.file_count==detailed.files.size() && counts.directory_count==detailed.dirs.size());
        assert(counts.reparse_skipped==(link_error?0U:1U) && counts.error_count()==detailed.errors.size());
        assert(last.totals_known && last.files_discovered==1 && last.total_bytes==3 && last.bytes_read==0);
        assert(last.errors==counts.error_count() && counts.directory_errors==0 && counts.other_errors==0);
        // Browsing sizes needs only directory metadata, also for nested/empty folders.
        write_file(counts_root/"x"/"nested"/"large.bin",std::string(18,'x'));
        write_file(counts_root/"loose.txt","loose");
        write_file(counts_root/"x"/"Organized"/"existing","already archived");
        const auto space=dc::scan_space(counts_root,[&](const dc::ScanProgress& p){
            assert(p.bytes_read==0 && p.phase==dc::ScanPhase::Enumerating);
            last=p;
        });
        const auto& metadata=space.inventory;
        assert(space.children.size()==metadata.dirs.size() && metadata.dirs.size()==5);
        assert(metadata.errors.size()==counts.error_count());
        assert(last.totals_known && last.files_discovered==4 && last.total_bytes==42);
        assert(!metadata.index_persisted && !fs::exists(counts_root/"Organized"));
        for(const auto& file:metadata.files){
            assert(file.sha256.empty() && file.content==-1);
            assert(file.mtime==fs::last_write_time(counts_root/file.rel));
        }
        const auto& top=space.children.at(fs::path{});
        assert(top.size()==3);
        assert(top[0].directory && metadata.dirs.at(top[0].index).rel=="x");
        assert(metadata.dirs.at(top[0].index).bytes==37 && metadata.dirs.at(top[0].index).count==3);
        assert(!top[1].directory && metadata.files.at(top[1].index).rel=="loose.txt");
        assert(top[2].directory && metadata.dirs.at(top[2].index).rel=="empty");
        assert(space.children.at("empty").empty());
        const auto& nested=space.children.at(fs::path("x")/"nested");
        assert(nested.size()==1 && !nested[0].directory && metadata.files.at(nested[0].index).bytes==18);
        stop=false;
        assert(fails([&]{dc::scan_space(counts_root,[&](const auto&){stop=true;},&stop);}));
        dc::ArchiveOptions archive;
        archive.source="x";archive.destination=archive.source/"Organized";
        const auto scoped=dc::plan_archive(metadata,archive);
        assert(scoped.moves.size()==2); // Loose sibling and existing destination are excluded.
        for(const auto& move:scoped.moves)assert(dc::is_inside(move.to.lexically_relative(archive.destination)));
        archive.recursive=false;
        const auto direct=dc::plan_archive(metadata,archive);
        assert(direct.moves.size()==1 && direct.moves[0].from==fs::path("x")/"data.txt");
        archive.destination="empty";
        assert(fails([&]{dc::plan_archive(metadata,archive);}));
        archive.source="unknown";archive.destination="unknown/sorted";
        assert(fails([&]{dc::plan_archive(metadata,archive);}));
        // A GUI archive copies only the selected folder. Existing unrelated files
        // stay put, name collisions keep both versions and repeating does not recopy.
        const auto archive_root=base/"archive";
        write_file(archive_root/"left"/"a.txt","content A");
        write_file(archive_root/"left"/"sub"/"a.txt","content B");
        write_file(archive_root/"right"/"a.txt","untouched");
        archive={};archive.source="left";archive.destination="left/organized";
        const auto before_copy=dc::scan_space(archive_root);
        const auto copy=dc::plan_archive_copy(before_copy.inventory,archive);
        assert(copy.archive_source==archive.source && copy.copies.size()==2 && copy.renamed_conflicts==1);
        assert(!fs::exists(archive_root/archive.destination));
        const auto stamp=fs::last_write_time(archive_root/"left"/"a.txt");
        write_file(archive_root/"left"/"a.txt","changed A");
        fs::last_write_time(archive_root/"left"/"a.txt",stamp);
        assert(!dc::execute_structure_plan(archive_root,copy).success);
        assert(!fs::exists(archive_root/archive.destination));
        write_file(archive_root/"left"/"a.txt","content A");
        fs::last_write_time(archive_root/"left"/"a.txt",stamp);
        assert(dc::execute_structure_plan(archive_root,copy).success);
        for(const auto& task:copy.copies){
            assert(fs::exists(archive_root/task.from) && fs::exists(archive_root/task.to));
            assert(task.from.begin()->string()=="left");
        }
        const auto records=dc::history(archive_root);
        assert(!records.empty() && records.front().copy_mode && records.front().status=="DONE");
        assert(!dc::undo_latest(archive_root).success);
        const auto repeat=dc::plan_archive_copy(dc::scan_space(archive_root).inventory,archive);
        assert(repeat.copies.empty() && repeat.directories.empty() && repeat.existing_skips.size()==2);
        assert(dc::execute_structure_plan(archive_root,repeat).success);
        auto report=dc::scan_roots_hashed({a,b});
        assert(report.volumes.size()==2);
        assert(report.total_files==3);
        assert(report.total_bytes==10+10+15);
        assert(report.complete());
        assert(report.hash_candidates.size()==1);
        assert(report.hash_candidates.front().members.size()==2);
        assert(report.hash_candidates.front().sha256.size()==64);
        assert(!fs::exists(a/".DiskCurator-System") && !fs::exists(b/".DiskCurator-System"));
        assert(report.volumes[0].root==fs::absolute(a));
        assert(report.volumes[1].root==fs::absolute(b));
        assert(fails([&]{dc::scan_roots_hashed({a,a});}));
        assert(fails([&]{dc::scan_roots_hashed({a,a/"photos"});}));
        assert(fails([&]{dc::scan_roots_hashed({});}));
        std::atomic_bool cancelled{true};
        assert(fails([&]{dc::scan_roots_hashed({a,b},{},&cancelled);}));
        auto partial=dc::scan_roots_hashed({a,base/"not-present",b});
        assert(partial.volumes.size()==2);
        assert(!partial.complete());
        assert(partial.errors.size()==1);
        assert(partial.total_files==3);
        // Content equality assertions require the original byte verifier.
        // Hash candidates are deliberately kept read-only across volume roots.
        fs::remove_all(base);
        std::cout<<"scope tests passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";fs::remove_all(base);return 1;}
}
