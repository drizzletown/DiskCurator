#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace dc;
static void put(const fs::path& p,const std::string& bytes){fs::create_directories(p.parent_path());std::ofstream o(p,std::ios::binary);o<<bytes;if(!o)throw std::runtime_error("Cannot make fixture");}
static std::string read(const fs::path& p){std::ifstream i(p,std::ios::binary);return {std::istreambuf_iterator<char>(i),{}};}
static bool fails(const std::function<void()>& fn){try{fn();return false;}catch(const std::exception&){return true;}}
static Inventory prepare(const fs::path& root){auto i=scan(root);auto d=deduplicate(i);if(!i.complete()||!d.errors.empty())throw std::runtime_error("Fixture scan failed");return i;}
int main(){
  const auto root=fs::temp_directory_path()/ ("dc_structure_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try{
    put(root/fs::u8path(u8"2022相机备份")/"DCIM"/"IMG_1.jpg","SAME IMAGE");
    put(root/fs::u8path(u8"2022相机备份")/"DCIM"/"IMG_2.jpg","OLD VERSION");
    put(root/fs::u8path(u8"2024相机备份")/"DCIM"/"IMG_1.jpg","SAME IMAGE");
    put(root/fs::u8path(u8"2024相机备份")/"DCIM"/"IMG_2.jpg","NEW VERSION");
    put(root/fs::u8path(u8"2024相机备份")/"DCIM"/"IMG_3.jpg","NEW EXTRA");
    put(root/fs::u8path(u8"课程")/fs::u8path(u8"线代.pdf"),"DOCUMENT");
    put(root/"loose.txt","ROOTFILE");
    fs::create_directories(root/fs::u8path(u8"2022相机备份")/"Empty");
    auto inv=prepare(root);
    auto prop=suggest_structure(inv);
    assert(prop.rules.size()==4);
    // The proposal does not write or create the output folder.
    assert(!fs::exists(root/fs::u8path(u8"整理结果")));
    bool changed=false;
    for(auto& r:prop.rules)if(r.source==fs::u8path(u8"2024相机备份")){
        r.target=fs::u8path(u8"01_照片与视频/相机合集");changed=true;
    }else if(r.source==fs::u8path(u8"2022相机备份")){
        r.target=fs::u8path(u8"01_照片与视频/相机合集");
    }
    assert(changed);
    auto plan=plan_structure(inv,prop);
    // 7 original files, one duplicated image; one of two filename collisions
    assert(plan.mappings.size()==7);
    assert(plan.copies.size()==6);
    assert(plan.renamed_conflicts==1);
    assert(plan.mappings.front().to!=fs::path{});
    assert(!fs::exists(root/fs::u8path(u8"整理结果")));
    assert(fs::exists(root/fs::u8path(u8"2022相机备份")/"DCIM"/"IMG_2.jpg"));
    // Bad edit must fail without changing anything.
    auto bad=prop;bad.rules.front().target="../../outside";
    assert(fails([&]{(void)plan_structure(inv,bad);}));
    auto missing=prop;missing.rules.pop_back();
    assert(fails([&]{(void)plan_structure(inv,missing);}));
    // Stale plans fail before creating output/workspace.
    put(root/fs::u8path(u8"课程")/"additional.pdf","NEW SINCE PREVIEW");
    auto stale=execute_structure_plan(root,plan);
    assert(!stale.success&&!fs::exists(root/fs::u8path(u8"整理结果")));
    fs::remove(root/fs::u8path(u8"课程")/"additional.pdf");
    // A source can be changed in-place and its original timestamp restored.
    auto original_file=root/fs::u8path(u8"2022相机备份")/"DCIM"/"IMG_2.jpg";
    auto original_time=fs::last_write_time(original_file);
    put(original_file,"EVIL VERSION"); // same byte length as "OLD VERSION"
    fs::last_write_time(original_file,original_time);
    auto stealth=execute_structure_plan(root,plan);
    assert(!stealth.success&&!fs::exists(root/fs::u8path(u8"整理结果")));
    put(original_file,"OLD VERSION");fs::last_write_time(original_file,original_time);
    auto good=execute_structure_plan(root,plan);
    if(!good.success)throw std::runtime_error(good.message);
    assert(read(root/fs::u8path(u8"整理结果")/fs::u8path(u8"01_照片与视频")/fs::u8path(u8"相机合集")/"DCIM"/"IMG_1.jpg")=="SAME IMAGE");
    auto new_version=root/fs::u8path(u8"整理结果")/fs::u8path(u8"01_照片与视频")/fs::u8path(u8"相机合集")/"DCIM"/"IMG_2.jpg";
    assert(fs::exists(new_version));
    size_t version_count=0;
    for(auto& x:fs::directory_iterator(new_version.parent_path()))if(x.path().filename().wstring().find(L"IMG_2")!=std::wstring::npos)++version_count;
    assert(version_count==2);
    assert(fs::exists(root/fs::u8path(u8"整理结果")/fs::u8path(u8"01_照片与视频")/fs::u8path(u8"相机合集")/"Empty"));
    assert(fs::exists(root/fs::u8path(u8"2024相机备份")/"DCIM"/"IMG_1.jpg"));
    assert(!undo_latest(root).success); // additive copy never undone destructively
    // Interrupted additive migration: only the owned temporary stage may
    // disappear during recovery; committed target data and original survive.
    const std::string crash_id="structure_recovery_fixture";
    const auto crash_stage=fs::path(".DiskCurator-System")/"stage"/crash_id/"0";
    put(root/crash_stage,"UNCOMMITTED");
    auto encode=[](const std::string& text){const char* hexchars="0123456789abcdef";
        std::string out;for(unsigned char c:text){out+=hexchars[c>>4];out+=hexchars[c&15];}return out;};
    const std::string crash_log="LABEL "+encode("Merge directories: Structure reorganization -> "+utf8(fs::path(L"整理结果")))+"\n"+
        "COPY "+encode(utf8(fs::u8path(u8"2022相机备份/DCIM/IMG_2.jpg")))+" "+
        encode(utf8(fs::u8path(u8"整理结果/01_照片与视频/相机合集/DCIM/IMG_2.jpg")))+" "+
        encode(utf8(crash_stage))+"\nPENDING\nSTAGEDALL\n";
    put(root/".DiskCurator-System"/"journal"/(crash_id+".log"),crash_log);
    const auto crash_recovery=recover_incomplete(root);
    assert(crash_recovery.size()==1);
    assert(!fs::exists(root/crash_stage));
    assert(read(root/fs::u8path(u8"2022相机备份")/"DCIM"/"IMG_2.jpg")=="OLD VERSION");
    assert(read(root/fs::u8path(u8"整理结果")/fs::u8path(u8"01_照片与视频")/fs::u8path(u8"相机合集")/"DCIM"/"IMG_2.jpg")=="OLD VERSION");
    auto repeat=prepare(root);
    auto reprop=suggest_structure(repeat);
    for(auto& r:reprop.rules)if(r.source==fs::u8path(u8"2024相机备份")||r.source==fs::u8path(u8"2022相机备份"))
        r.target=fs::u8path(u8"01_照片与视频/相机合集");
    auto second=plan_structure(repeat,reprop);
    assert(second.copies.empty());
    assert(execute_structure_plan(root,second).success);
    // Verify an altered pre-existing output file cannot be silently reused.
    put(root/fs::u8path(u8"整理结果")/fs::u8path(u8"01_照片与视频")/fs::u8path(u8"相机合集")/"DCIM"/"IMG_1.jpg","TAMPERED");
    auto current=prepare(root);auto next=suggest_structure(current);
    for(auto& r:next.rules)if(r.source==fs::u8path(u8"2024相机备份")||r.source==fs::u8path(u8"2022相机备份"))
        r.target=fs::u8path(u8"01_照片与视频/相机合集");
    auto followup=plan_structure(current,next);
    // Must preserve both versions instead of replacing the tampered target.
    bool original_retained=false;
    for(const auto& m:followup.mappings)if(m.from==fs::u8path(u8"2022相机备份/DCIM/IMG_1.jpg")){
        original_retained=true;assert(m.to!=fs::u8path(u8"整理结果/01_照片与视频/相机合集/DCIM/IMG_1.jpg"));
    }
    assert(original_retained);
    // Do not silently exclude a pre-existing ordinary folder named as output.
    const auto unrelated=root/"untrusted";
    put(unrelated/fs::u8path(u8"照片备份")/"img.jpg","ORIGINAL");
    put(unrelated/fs::u8path(u8"整理结果")/"private.txt","USER INFORMATION");
    auto untrusted=prepare(unrelated);
    auto untrusted_proposal=suggest_structure(untrusted);
    assert(fails([&]{(void)plan_structure(untrusted,untrusted_proposal);}));
    assert(read(unrelated/fs::u8path(u8"整理结果")/"private.txt")=="USER INFORMATION");
    // Incomplete scan must not produce a recommendation.
    auto incomplete=current;incomplete.errors.push_back("not readable");
    assert(fails([&]{(void)suggest_structure(incomplete);}));
    // A high-confidence backup relation must produce a shared suggested
    // destination, while still requiring user confirmation before writing.
    const auto stronger=root/"strongevidence";
    for(int i=0;i<6;++i){
        const auto file="IMG"+std::to_string(i)+".jpg";
        put(stronger/"backup_old"/file,"SAME"+std::to_string(i));
        put(stronger/"backup_new"/file,"SAME"+std::to_string(i));
    }
    put(stronger/"backup_new"/"latest.jpg","ONLY NEW");
    auto check=prepare(stronger);
    const auto recommended=suggest_structure(check);
    fs::path first,second_target;size_t grouped=0;
    for(const auto& r:recommended.rules){
        if(r.source=="backup_old"){first=r.target;++grouped;}
        if(r.source=="backup_new"){second_target=r.target;++grouped;}
    }
    assert(grouped==2&&first==second_target);
    assert(!fs::exists(stronger/fs::u8path(u8"整理结果")));
    // Hundreds of top-level folders, nested files, threshold-boundary mixed
    // types and empty folders exercise one-pass category aggregation. This
    // inventory is entirely in memory: proposals must not read file bytes.
    Inventory many;many.root=root/"synthetic-no-files";many.dirs.push_back({{},0,0});
    constexpr size_t top_level_count=640;
    int content=0;
    const std::vector<fs::path> buckets={
        fs::u8path(u8"01_照片与视频"),fs::u8path(u8"06_混合内容_待核对"),
        fs::u8path(u8"02_文档资料"),fs::u8path(u8"03_音频"),
        fs::u8path(u8"04_源码项目"),fs::u8path(u8"05_压缩归档"),
        fs::u8path(u8"06_混合内容_待核对"),fs::u8path(u8"07_空目录")};
    const std::vector<std::string> extensions={".JPG",".jpg",".pdf",".mp3",".cpp",".zip",".dat",""};
    std::map<fs::path,fs::path> expected;
    for(size_t d=0;d<top_level_count;++d){
        const auto dir=fs::path("source-"+std::to_string(d));const auto kind=d%8;
        expected[dir]=buckets[kind]/dir;
        if(kind==7){many.dirs.push_back({dir,0,0});continue;}
        many.dirs.push_back({dir,40,4});many.dirs.push_back({dir/"nested",40,4});
        for(size_t f=0;f<4;++f){
            auto extension=extensions[kind];
            if((kind==0&&f==3)||(kind==1&&f>=2))extension=".txt";
            many.files.push_back({dir/"nested"/("file-"+std::to_string(f)+extension),10,{},content++,{}});
        }
    }
    many.files.push_back({"loose.txt",12,{},content++,{}});
    many.files.push_back({"second.dat",7,{},content++,{}});
    auto large_proposal=suggest_structure(many,"output");
    assert(large_proposal.rules.size()==top_level_count+1);
    for(const auto& rule:large_proposal.rules){
        if(rule.source==fs::path(".")){assert(rule.files==2&&rule.bytes==19);continue;}
        assert(rule.target==expected.at(rule.source));
    }
    std::atomic_bool cancelled{true};
    assert(fails([&]{(void)suggest_structure(many,"output",&cancelled);}));
    for(const auto& warning:large_proposal.warnings)
        assert(warning.find("two-independent-copy")==std::string::npos);
    std::error_code ec;fs::remove_all(root,ec);
    std::cout<<"STRUCTURE PROPOSAL, EDIT, VERIFY, IDEMPOTENCE PASSED\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<" fixture="<<utf8(root)<<"\n";return 2;}
}
