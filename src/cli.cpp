#include "core.hpp"
#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
using namespace dc;
static void usage(){std::cout<<"DiskCurator " DISKCURATOR_VERSION "\n"
  "Find duplicate files, compare backups and organize folders.\n"
  "  dc_cli --help | --version\n  Options may precede operands. Use -- before a path beginning with --.\n"
  "  dc_cli inventory ROOT  (read-only metadata totals, no index; exit 4 for partial results)\n"
  "  dc_cli scan ROOT\n  dc_cli hashes ROOT\n  dc_cli duplicates ROOT\n  dc_cli folders ROOT\n"
  "  dc_cli compare ROOT RELATIVE_A RELATIVE_B\n  dc_cli pair ROOT RELATIVE_A RELATIVE_B\n  dc_cli relations ROOT\n"
  "  dc_cli similar ROOT [MAX_HAMMING]\n  dc_cli overlaps ROOT [MAX_RESULTS]\n  dc_cli safety ROOT\n"
  "  dc_cli rename ROOT REL_FOLDER PREFIX [--apply --yes]\n"
  "  dc_cli archive ROOT type|date [--apply --yes]\n"
  "  dc_cli suggest ROOT\n  dc_cli merge ROOT SOURCE_DIR DEST_DIR [--apply --yes]\n"
  "  dc_cli structure ROOT [NEW_TOP_LEVEL_FOLDER] [--apply --yes]\n"
  "  dc_cli retire ROOT OLD_FOLDER [--keep 0..32] [--allow-zero] [--count-hardlinks] [--trust-scan-hash] [--apply --yes]\n"
  "  dc_cli clean ROOT [--keep 0..32] [--allow-zero] [--count-hardlinks] [--trust-scan-hash] [--apply --yes]\n  dc_cli quarantine-selected ROOT PATH [PATH...] [--keep 0..32] [--allow-zero] [--count-hardlinks] [--trust-scan-hash] [--apply --yes]\n  dc_cli undo ROOT\n  dc_cli history ROOT\n  dc_cli recover ROOT\n";
}
namespace {
struct CommandSpec {
    size_t minimum, maximum;
    bool apply = false;
    bool retention = false;
    bool whole_root_verification = false;
};

const std::map<std::string, CommandSpec> commands = {
    {"inventory", {0,0}}, {"scan", {0,0}}, {"hashes", {0,0}}, {"history", {0,0}}, {"recover", {0,0}}, {"undo", {0,0}},
    {"duplicates", {0,0,false,false,true}}, {"folders", {0,0,false,false,true}},
    {"compare", {2,2}}, {"pair", {2,2}}, {"similar", {0,1}},
    {"relations", {0,0,false,false,true}}, {"suggest", {0,0,false,false,true}},
    {"overlaps", {0,1,false,false,true}}, {"safety", {0,0,false,false,true}},
    {"rename", {2,2,true}}, {"archive", {1,1,true}}, {"merge", {2,2,true}},
    {"structure", {0,1,true,false,true}},
    {"clean", {0,0,true,true,true}}, {"retire", {1,1,true,true,true}},
    {"quarantine-selected", {1,std::numeric_limits<size_t>::max(),true,true,true}}
};

unsigned parse_number(const std::string& text, unsigned maximum, const char* label) {
    unsigned value = 0;
    const auto end = text.data() + text.size();
    const auto result = std::from_chars(text.data(), end, value);
    if (result.ec != std::errc{} || result.ptr != end || value > maximum)
        throw std::runtime_error(std::string(label) + " requires an integer in 0.." + std::to_string(maximum));
    return value;
}

int run_inventory(const fs::path& root) {
    double next_update = 5;
    const auto progress = [&](const ScanProgress& state) {
        if (state.elapsed_seconds < next_update) return;
        next_update = state.elapsed_seconds + 5;
        std::cerr << "Inventory progress: Seconds=" << static_cast<U64>(state.elapsed_seconds)
                  << " Files=" << state.files_discovered << " Bytes=" << state.total_bytes
                  << " ScanErrors=" << state.errors << '\n';
    };
    const auto summary = scan_inventory(root, progress);
    const auto errors = summary.error_count();
    std::cout << "Status=" << (errors ? "partial" : "complete")
              << " Files=" << summary.file_count << " Folders=" << summary.directory_count
              << " Bytes=" << summary.bytes << " ScanErrors=" << errors
              << " DirectoryErrors=" << summary.directory_errors
              << " ReparseSkipped=" << summary.reparse_skipped
              << " OtherErrors=" << summary.other_errors << '\n';
    return errors ? 4 : 0;
}

struct Arguments {
    std::vector<std::string> operands;
    RetentionPolicy retention;
    bool apply = false;
};

Arguments parse_arguments(const std::vector<std::string>& input, const CommandSpec& spec) {
    Arguments parsed;
    std::set<std::string> options;
    bool literal = false;
    for (size_t i = 3; i < input.size(); ++i) {
        const auto& token = input[i];
        if (!literal && token == "--") { literal = true; continue; }
        if (literal || token.rfind("--", 0) != 0) { parsed.operands.push_back(token); continue; }
        if (!options.insert(token).second) throw std::runtime_error("Repeated option: " + token);
        if (spec.apply && (token == "--apply" || token == "--yes")) continue;
        if (!spec.retention) throw std::runtime_error("Unsupported option: " + token);
        if (token == "--keep" && i+1 < input.size()) {
            parsed.retention.min_active_copies = parse_number(input[++i], 32, "--keep");
        } else if (token == "--count-hardlinks") parsed.retention.distinct_file_objects = false;
        else if (token == "--allow-zero") parsed.retention.allow_zero_active = true;
        else if (token == "--trust-scan-hash") parsed.retention.verify_bytes_before_change = false;
        else throw std::runtime_error("Invalid or incomplete option: " + token);
    }
    if (parsed.operands.size() < spec.minimum || parsed.operands.size() > spec.maximum)
        throw std::runtime_error("Incorrect argument count for " + input[1]);
    parsed.apply = options.count("--apply") != 0;
    if (parsed.apply != (options.count("--yes") != 0))
        throw std::runtime_error("Execution requires both --apply and --yes");
    if (parsed.retention.min_active_copies == 0 && !parsed.retention.allow_zero_active)
        throw std::runtime_error("--keep 0 requires --allow-zero");
    return parsed;
}
}

static int exec(const std::vector<std::string>& input) {
 if (input.size()==2 && input[1]=="--version") { std::cout << DISKCURATOR_VERSION << "\n"; return 0; }
 if (input.size()==2 && input[1]=="--help") { usage(); return 0; }
 if (input.size()<3) { usage(); return 1; }
 const auto& cmd=input[1];
 const auto spec=commands.find(cmd);
 if (spec==commands.end()) throw std::runtime_error("Unknown command: " + cmd);
 const auto args=parse_arguments(input, spec->second);
 const auto& operands=args.operands;
 const auto& retention=args.retention;
 const auto apply=args.apply;
 const auto root=from_utf8(input[2]);
 if (root.empty()) throw std::runtime_error("A scan root is required");
 if (cmd=="inventory") return run_inventory(root);
 if (cmd=="archive" && operands[0]!="type" && operands[0]!="date")
     throw std::runtime_error("Archive mode must be type or date");
 unsigned threshold=7, limit=200;
 if (cmd=="similar" && !operands.empty()) threshold=parse_number(operands[0],64,"MAX_HAMMING");
 if (cmd=="overlaps" && !operands.empty()) limit=parse_number(operands[0],std::numeric_limits<unsigned>::max(),"MAX_RESULTS");
 if(cmd=="history"){for(auto&h:history(root))std::cout<<h.transaction<<" "<<h.status<<" "<<h.label<<" ("<<h.items<<")\n";return 0;}
 if (cmd=="recover") {
     const auto reports = recover_incomplete(root);
     bool requires_manual_recovery = false;
     for (const auto& report : reports) {
         std::cout << report << "\n";
         if (report.rfind("MANUAL RECOVERY NEEDED", 0) == 0) requires_manual_recovery = true;
     }
     return requires_manual_recovery ? 3 : 0;
 }
 if(cmd=="undo"){auto r=undo_latest(root);std::cout<<r.message<<"\n";return r.success?0:2;}
 auto inv=scan_hashed(root);std::cout<<"Files="<<inv.files.size()<<" Folders="<<inv.dirs.size()<<" Bytes="<<human(inv.bytes)<<" ScanErrors="<<inv.errors.size()<<"\n";
 if (!inv.index_error.empty()) std::cerr << "Index not saved; scan results available in memory: " << inv.index_error << "\n";
 for (const auto& error : inv.errors) std::cerr << "Scan warning: " << error << "\n";
 if (!inv.complete() && spec->second.apply) return 4;
 if(cmd=="hashes"){for(const auto& f:inv.files)std::cout<<f.sha256<<"\t"<<f.bytes<<"\t"<<utf8(f.rel)<<"\n";}
 else if(cmd=="scan") {
    std::vector<File> largest(std::min<size_t>(30, inv.files.size()));
    std::partial_sort_copy(inv.files.begin(), inv.files.end(), largest.begin(), largest.end(),
        [](const File& left, const File& right) {
            if (left.bytes != right.bytes) return left.bytes > right.bytes;
            return left.rel < right.rel;
        });
    for (const auto& file : largest)
        std::cout << human(file.bytes) << " " << utf8(file.rel) << "\n";
 }
 else {
 Duplicates d;
 if (spec->second.whole_root_verification) {
     d=deduplicate(inv);
     std::cout<<"ReadErrors="<<d.errors.size()<<" DuplicateGroups="<<d.groups.size()<<" Redundant="<<human(d.wasted)<<"\n";
     for (const auto& error : d.errors) std::cerr << "Verification warning: " << error << "\n";
     if (!d.errors.empty()) return 4;
 }
 if(cmd=="duplicates"){for(auto&g:d.groups){std::cout<<"GROUP\n";for(auto i:g)std::cout<<"  "<<utf8(inv.files[i].rel)<<"\n";}}
 else if(cmd=="folders"){for(auto&g:duplicate_dirs(inv)){std::cout<<"GROUP bytes="<<g.bytes<<"\n";for(auto&p:g.paths)std::cout<<"  "<<utf8(p)<<"\n";}}
 else if(cmd=="compare"){auto c=evaluate_hashed_directory_pair(inv,from_utf8(operands[0]),from_utf8(operands[1])).comparison;
  std::cout<<"A_subset_B="<<c.left_in_right<<" B_subset_A="<<c.right_in_left<<" ContentOverlap="<<c.content_overlap<<"\n";
  for(auto&r:c.rows)std::cout<<r.state<<" "<<utf8(r.rel)<<"\n";}
 else if(cmd=="pair"){

    auto report=evaluate_hashed_directory_pair(inv,from_utf8(operands[0]),from_utf8(operands[1]));
    auto ratio=[](const std::optional<double>& v){return v?std::to_string(*v):std::string("N/A");};
    std::cout<<"PAIR shared="<<report.shared_file_occurrences
             <<" distinct="<<report.shared_distinct_contents
             <<" coverage_min="<<ratio(report.smaller_coverage)
             <<" jaccard="<<ratio(report.multiset_jaccard)
             <<" same_relative="<<report.same_relative_files
             <<" conflicts="<<report.same_path_different_content
             <<" only_left="<<report.left_only_files<<" only_right="<<report.right_only_files
             <<" exact_tree="<<report.exact_tree
             <<" merge_allowed="<<report.disjoint_for_merge<<"\n";
    for(const auto& row:report.comparison.rows)
        std::cout<<row.state<<" "<<utf8(row.rel)<<"\n";
 }
 else if(cmd=="overlaps"){

    auto out=find_overlapping_dirs(inv,limit);
    std::cout<<"OVERLAP_PAIRS="<<out.total_candidates<<" SHOWN="<<out.pairs.size()<<" TRUNCATED="<<out.truncated<<"\n";
    for(const auto& r:out.pairs)
        std::cout<<utf8(r.left)<<" <-> "<<utf8(r.right)<<" common="<<r.shared
                 <<" types="<<r.distinct_shared<<" coverage="<<r.smaller_coverage
                 <<" jaccard="<<r.multiset_jaccard<<"\n";
 }
 else if(cmd=="relations"){for(auto&r:directory_relations(inv))std::cout<<utf8(r.left)<<" <-> "<<utf8(r.right)<<" "<<r.relation<<" ("<<r.matched<<" shared)\n";}
 else if(cmd=="suggest"){
    const auto candidates=recommend_merges(inv);
    std::cout<<"Merge candidates: "<<candidates.size()<<" (read-only; no changes performed)\n";
    for(const auto& s:candidates){
        std::cout<<"CONFIDENCE "<<s.confidence<<" "<<utf8(s.source)<<" -> "<<utf8(s.destination)
                 <<" overlap="<<s.shared_contents<<"/"<<std::min(s.evidence_source_files,s.evidence_destination_files)
                 <<" matched_relative="<<s.same_relative_files<<" source_only="<<s.source_only
                 <<" destination_only="<<s.destination_only<<" collisions="<<s.changed_at_same_path
                 <<" actionable="<<s.useful<<"\n";
    }
 }
 else if(cmd=="structure"){
    fs::path base=fs::path(L"整理结果");
    if(!operands.empty())base=from_utf8(operands[0]);
    auto suggestion=suggest_structure(inv,base);
    std::cout<<"PROPOSED OUTPUT "<<utf8(suggestion.output_base)<<" (read only)\n";
    for(const auto& r:suggestion.rules)
        std::cout<<"MAP DIRECTORY "<<utf8(r.source)<<" -> "<<utf8(suggestion.output_base/r.target)
                 <<" ("<<r.files<<" files; "<<human(r.bytes)<<") ["<<r.reason<<"]\n";
    auto proposal=plan_structure(inv,suggestion);
    std::cout<<"STRUCTURE PLAN "<<proposal.copies.size()<<" file copies, "
             <<proposal.mappings.size()<<" old-to-new mappings, "
             <<proposal.renamed_conflicts<<" version conflicts, "<<human(proposal.bytes_to_copy)<<" copied\n";
    for(const auto& m:proposal.mappings)
        std::cout<<m.action<<" "<<utf8(m.from)<<" -> "<<utf8(m.to)<<"\n";
    if(apply){
        auto r=execute_structure_plan(inv.root,proposal);std::cout<<r.message<<"\n";return r.success?0:3;}
    std::cout<<"DRY RUN; original folders preserved. Edit mappings in Win32 GUI before applying.\n";
 }
 else if(cmd=="safety"){for(auto&[k,v]:backup_safety(inv,d)){std::cout<<k<<": ";for(auto&s:v)std::cout<<s<<" ";std::cout<<"\n";}}
 else if(cmd=="merge"){

    auto p=plan_hashed_directory_merge(inv,from_utf8(operands[0]),from_utf8(operands[1]));
    std::cout<<"Merge: "<<p.copies.size()<<" new copies, "<<p.directories.size()<<" folders, "
             <<p.identical<<" identical, "<<p.conflicts<<" name collisions\n";
    for(const auto& c:p.copies)std::cout<<"COPY "<<utf8(c.from)<<" -> "<<utf8(c.to)<<"\n";
    for(const auto& d:p.directories)std::cout<<"MKDIR "<<utf8(d)<<"\n";
    if(apply){
        auto r=execute_merge_plan(inv.root,p);std::cout<<r.message<<"\n";return r.success?0:3;}
    std::cout<<"DRY RUN only (use --apply --yes to confirm)\n";
 }
 else if(cmd=="similar"){auto r=similar_photos(inv,threshold);std::cout<<"Decoded="<<r.photos.size()<<" SimilarPairs="<<r.pairs.size()<<" ImageErrors="<<r.errors.size()<<"\n";
  for(auto&p:r.pairs)std::cout<<p.distance<<" "<<utf8(p.a)<<" ~ "<<utf8(p.b)<<"\n";}
 else {Plan p;
   if(cmd=="clean")p=plan_clean_duplicates(inv,d,retention);
   else if(cmd=="quarantine-selected"){
      std::vector<fs::path> selected;
      for (const auto& path : operands) selected.push_back(from_utf8(path));
      p=plan_quarantine_selected(inv,d,selected,retention);
   }
   else if(cmd=="retire"){

      auto proposal=plan_retire_directory(inv,d,from_utf8(operands[0]),retention);
      p=std::move(proposal.quarantine);
      std::cout<<"RETIRE "<<utf8(proposal.source_folder)<<" eligible="<<p.moves.size()
               <<" blocked="<<proposal.blocked.size()
               <<" staged_bytes="<<proposal.staged_bytes<<"\n";
      for(const auto& blocked:proposal.blocked)
          std::cout<<"BLOCKED insufficient external keepers: "<<utf8(blocked)<<"\n";
      std::cout<<"RETENTION_POLICY min_active="<<retention.min_active_copies<<" independent_objects="<<retention.distinct_file_objects<<" full_byte_recheck="<<retention.verify_bytes_before_change<<"\n";
      std::cout<<"WARNING: quarantine does not free disk space; no permanent delete\n";
   }
   else if(cmd=="rename"){RenameOptions opt;opt.dir=from_utf8(operands[0]);opt.prefix=from_utf8(operands[1]).wstring();p=plan_rename(inv,opt);}
   else if(cmd=="archive"){ArchiveOptions opt;opt.mode=operands[0]=="date"?ArchiveMode::ByYearMonth:ArchiveMode::ByType;p=plan_archive(inv,opt);}
   else throw std::runtime_error("Unknown command");
   for(auto&m:p.moves)std::cout<<utf8(m.from)<<" -> "<<utf8(m.to)<<"\n";
   std::cout<<p.moves.size()<<" moves; "<<(apply?"apply requested":"DRY RUN")<<"\n";
   if(apply){auto r=execute_plan(inv.root,p);std::cout<<r.message<<"\n";return r.success?0:3;}
 }
 }
 return inv.errors.empty()?0:4;
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<std::string> a;for(int i=0;i<argc;++i)a.push_back(utf8(std::filesystem::path(argv[i])));
 try{return exec(a);}catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<"\n";return 2;}}
#else
int main(int argc,char** argv){std::vector<std::string>a(argv,argv+argc);try{return exec(a);}catch(const std::exception&e){std::cerr<<"Error: "<<e.what()<<"\n";return 2;}}
#endif
