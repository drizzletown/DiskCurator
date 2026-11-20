#include "core.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>

using namespace dc;
namespace {
std::string to_hex(const std::string& s){
    constexpr char chars[]="0123456789abcdef";
    std::string out;out.reserve(s.size()*2);
    for(unsigned char x:s){out.push_back(chars[x>>4]);out.push_back(chars[x&15]);}
    return out;
}
void put(const fs::path& path,const std::string& value){
    fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);
    if(!f)throw std::runtime_error("Failed to create test fixture");
    f<<value;
}
std::string get(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Fixture {
    fs::path root;
    explicit Fixture(const std::string& name){
        root=fs::temp_directory_path()/("dc_recovery_"+name+"_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root);
        put(root/".DiskCurator-System"/"owner.txt","DiskCurator Native workspace v1\n");
    }
    ~Fixture(){std::error_code ec;fs::remove_all(root,ec);}
    void journal(const std::string& id,const std::vector<std::pair<fs::path,fs::path>>& moves,const std::string& state){
        std::string body="LABEL "+to_hex("simulated crash")+"\n";
        for(size_t i=0;i<moves.size();++i){
            auto stage=fs::path(".DiskCurator-System")/"stage"/from_utf8(id)/from_utf8(std::to_string(i));
            body+="MOVE "+to_hex(utf8(moves[i].first))+" "+to_hex(utf8(moves[i].second))+" "+to_hex(utf8(stage))+"\n";
        }
        body+="PENDING\n";
        if(state=="STAGEDALL")body+="STAGEDALL\n";
        put(root/".DiskCurator-System"/"journal"/(id+".log"),body);
    }
    fs::path stage(const std::string& id,int n)const{
        return root/".DiskCurator-System"/"stage"/from_utf8(id)/from_utf8(std::to_string(n));
    }
};
}
int main(){
    try{
#ifdef _WIN32
        {
            Fixture t("extended_root");
            t.root=extended_path(t.root);
            const auto source_dir=fs::path(std::wstring(110,L'a'))/std::wstring(110,L'b');
            const auto original=source_dir/L"original.txt";
            const auto destination=source_dir/L"renamed.txt";
            assert((t.root/original).native().size()>260);
            put(t.root/destination,"retained bytes");
            t.journal("old_move",{{original,destination}},"STAGEDALL");
            const auto log=t.root/L".DiskCurator-System"/L"journal"/L"old_move.log";
            std::ofstream(log,std::ios::binary|std::ios::app)<<"DONE\n";
            assert(undo_latest(t.root).success);
            assert(get(t.root/original)=="retained bytes"&&!fs::exists(t.root/destination));
            assert(get(log).find("UNDONE\n")!=std::string::npos);
        }
#endif
        {
            Fixture t("partially_staged");
            put(t.root/"photos/a.txt","A");put(t.root/"photos/b.txt","B");
            fs::create_directories(t.stage("tx_pending",0).parent_path());
            fs::rename(t.root/"photos/a.txt",t.stage("tx_pending",0));
            t.journal("tx_pending",{{"photos/a.txt","other/a.txt"},{"photos/b.txt","other/b.txt"}},"PENDING");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("Recovered")!=std::string::npos);
            assert(get(t.root/"photos/a.txt")=="A"&&get(t.root/"photos/b.txt")=="B");
            assert(!fs::exists(t.stage("tx_pending",0)));
            assert(recover_incomplete(t.root).empty());
        }
        {
            Fixture t("partially_committed");
            put(t.stage("tx_staged",0),"A");put(t.stage("tx_staged",1),"B");
            fs::create_directories(t.root/"dest");
            fs::rename(t.stage("tx_staged",0),t.root/"dest/a.txt");
            t.journal("tx_staged",{{"photos/a.txt","dest/a.txt"},{"photos/b.txt","dest/b.txt"}},"STAGEDALL");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("Recovered")!=std::string::npos);
            assert(get(t.root/"photos/a.txt")=="A"&&get(t.root/"photos/b.txt")=="B");
            assert(!fs::exists(t.root/"dest/a.txt"));
        }
        {
            Fixture t("conflict");
            put(t.stage("tx_conflict",0),"stage copy");put(t.root/"dest/a.txt","other file");
            t.journal("tx_conflict",{{"photos/a.txt","dest/a.txt"}},"STAGEDALL");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
            assert(get(t.stage("tx_conflict",0))=="stage copy");
            assert(get(t.root/"dest/a.txt")=="other file");
        }
        {
            Fixture t("forged_path");
            t.journal("tx_forged",{{"../outside.txt","dest/a.txt"}},"PENDING");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
            assert(!history(t.root).empty());
            auto blocked=execute_plan(t.root,Plan{"test",{{"file","next"}}, {}});
            assert(!blocked.success);
        }
        {
            Fixture t("forged_stage");
            const std::string body="LABEL "+to_hex("forged")+"\nMOVE "+to_hex("a.txt")+" "+to_hex("b.txt")+" "+to_hex("../../outside")+"\nPENDING\n";
            put(t.root/".DiskCurator-System"/"journal"/"tx_bad.log",body);
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
        }
        {
            // A forged journal must not remove DiskCurator's owner marker.
            Fixture t("forged_workspace_source");
            put(t.stage("tx_owner",0),"tampered");
            t.journal("tx_owner",{{".DiskCurator-System/owner.txt","photos/owner.txt"}},"STAGEDALL");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
            assert(get(t.root/".DiskCurator-System"/"owner.txt")=="DiskCurator Native workspace v1\n");
            assert(get(t.stage("tx_owner",0))=="tampered");
        }
        {
            // No transaction is allowed to replace its own journal/owner state.
            Fixture t("forged_workspace_destination");
            put(t.stage("tx_target",0),"tampered");
            t.journal("tx_target",{{"photos/owner.txt",".DiskCurator-System/owner.txt"}},"STAGEDALL");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
            assert(get(t.root/".DiskCurator-System"/"owner.txt")=="DiskCurator Native workspace v1\n");
        }
        {
            Fixture t("missing_data");
            t.journal("tx_missing",{{"photos/a.txt","dest/a.txt"}},"STAGEDALL");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
        }
        {
            Fixture t("unknown_record");
            t.journal("tx_unknown",{{"a.txt","b.txt"}},"PENDING");
            std::ofstream out(t.root/".DiskCurator-System"/"journal"/"tx_unknown.log",std::ios::app);
            out<<"GARBAGE\n";out.close();
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
            assert(!execute_plan(t.root,Plan{"blocked",{{"a.txt","b.txt"}}, {}}).success);
        }
        {
            Fixture t("missing_state");
            put(t.root/".DiskCurator-System"/"journal"/"tx_truncated.log",
                "LABEL "+to_hex("truncated")+"\nMOVE "+to_hex("a.txt")+" "+to_hex("b.txt")+" "+
                to_hex(".DiskCurator-System/stage/tx_truncated/0")+"\n");
            auto r=recover_incomplete(t.root);
            assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
        }
        {
            // rc3 on Windows wrote owner.txt in CRT text mode (CRLF), then
            // verified it in binary mode. Existing rc3 user workspaces must
            // remain usable without deleting journals or quarantine data.
            Fixture t("windows_crlf_owner");
            put(t.root/".DiskCurator-System"/"owner.txt",
                "DiskCurator Native workspace v1\r\n");
            assert(recover_incomplete(t.root).empty());
            assert(history(t.root).empty());
        }
        {
            // Accept exactly LF or CRLF. A correct first line followed by
            // appended data is *not* a valid owner marker.
            Fixture t("trailing_owner_payload");
            put(t.root/".DiskCurator-System"/"owner.txt",
                "DiskCurator Native workspace v1\r\nFORGED\n");
            bool blocked=false;
            try{(void)recover_incomplete(t.root);}catch(const std::exception&){blocked=true;}
            assert(blocked);
        }
        {
            Fixture t("trailing_owner_bytes");
            put(t.root/".DiskCurator-System"/"owner.txt",
                "DiskCurator Native workspace v1\n ");
            bool blocked=false;
            try{(void)recover_incomplete(t.root);}catch(const std::exception&){blocked=true;}
            assert(blocked);
        }
        {
            Fixture t("bad_marker");
            put(t.root/".DiskCurator-System"/"owner.txt","Someone else owns this folder\n");
            bool blocked=false;
            try{(void)recover_incomplete(t.root);}catch(const std::exception&){blocked=true;}
            assert(blocked);
        }
        {
            Fixture t("linked_journal");
            auto outside=t.root.parent_path()/(t.root.filename().string()+"_outside");
            fs::create_directories(outside);
            std::error_code ec;
            fs::create_directory_symlink(outside,t.root/".DiskCurator-System"/"journal",ec);
            if(!ec){
                bool blocked=false;
                try{(void)recover_incomplete(t.root);}catch(const std::exception&){blocked=true;}
                assert(blocked);
            }
            fs::remove_all(outside,ec);
        }
        {
            Fixture t("linked_log");
            auto outside=t.root.parent_path()/(t.root.filename().string()+"_outside.log");
            put(outside,"UNTRUSTED\n");
            std::error_code ec;
            fs::create_symlink(outside,t.root/".DiskCurator-System"/"journal"/"tx_linked.log",ec);
            if(!ec){
                auto r=recover_incomplete(t.root);
                assert(r.size()==1&&r[0].find("MANUAL RECOVERY NEEDED")!=std::string::npos);
                assert(get(outside)=="UNTRUSTED\n");
            }
            fs::remove(outside,ec);
        }
        {
            Fixture t("linked_stage");
            auto outside=t.root.parent_path()/(t.root.filename().string()+"_stage");
            fs::create_directories(outside);
            std::error_code ec;
            fs::create_directory_symlink(outside,t.root/".DiskCurator-System"/"stage",ec);
            if(!ec){
                bool blocked=false;
                try{(void)recover_incomplete(t.root);}catch(const std::exception&){blocked=true;}
                assert(blocked);
            }
            fs::remove_all(outside,ec);
        }
        {
            Fixture t("pending_missing_source");
            put(t.root/"other.txt","keep");
            t.journal("lost",{{"missing.txt","dest.txt"}},"PENDING");
            const auto report=recover_incomplete(t.root);
            assert(report.size()==1&&report.front().find("MANUAL")==0);
            assert(history(t.root).front().status=="RECOVERY_FAILED");
            assert(!execute_plan(t.root,Plan{"rename",{{"other.txt","next.txt"}},{}}).success);
            assert(get(t.root/"other.txt")=="keep");
        }
        {
            // A crash after one original in a two-file swap was restored.
            Fixture t("rollback_interrupted");
            t.journal("swap",{{"a","b"},{"b","a"}},"STAGEDALL");
            const auto log=t.root/".DiskCurator-System"/"journal"/"swap.log";
            std::ofstream(log,std::ios::app)<<"RESTORING\n";
            put(t.root/"b","original B");
            put(t.stage("swap",0),"original A");
            const auto report=recover_incomplete(t.root);
            assert(report.size()==1&&report.front().find("Recovered")==0);
            assert(get(t.root/"a")=="original A"&&get(t.root/"b")=="original B");
            assert(recover_incomplete(t.root).empty());
            assert(history(t.root).front().status=="ROLLBACK");
        }
        {
            // Simulate losing just the last UNDONE marker after reverse DONE.
            Fixture t("undo_marker");
            put(t.root/"a","A");put(t.root/"b","B");
            const auto applied=execute_plan(t.root,Plan{"swap",{{"a","b"},{"b","a"}},{}});
            assert(applied.success&&undo_latest(t.root).success);
            const auto log=t.root/".DiskCurator-System"/"journal"/(applied.transaction+".log");
            const auto content=get(log);
            assert(content.size()>=7&&content.substr(content.size()-7)=="UNDONE\n");
            fs::resize_file(log,content.size()-7);
            const auto report=recover_incomplete(t.root);
            assert(report.size()==1&&report.front().find("Recovered completed Undo")==0);
            assert(get(t.root/"a")=="A"&&get(t.root/"b")=="B");
            assert(!undo_latest(t.root).success);
            assert(recover_incomplete(t.root).empty());
        }
        {
            Fixture t("literal_transaction_token");
            put(t.root/"before","content");
            const auto renamed=execute_plan(t.root,Plan{"rename",{{"before","__TX__/__TX__"}},{}});
            assert(renamed.success&&get(t.root/"__TX__"/"__TX__")=="content");
            assert(undo_latest(t.root).success&&get(t.root/"before")=="content");
        }
        std::cout<<"RECOVERY FAULT INJECTION TESTS PASSED\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"RECOVERY TEST ERROR: "<<e.what()<<"\n";return 2;}
}
