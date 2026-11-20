#include "core.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dc;
static void put(const fs::path& p,const std::string& body){
    fs::create_directories(p.parent_path());
    std::ofstream o(p,std::ios::binary);if(!o)throw std::runtime_error("fixture create");o<<body;
}
static bool failed(const std::function<void()>& fn){try{fn();return false;}catch(const std::exception&){return true;}}
static const MergeSuggestion* find(const std::vector<MergeSuggestion>& xs,const fs::path& a,const fs::path& b){
    for(const auto& s:xs)if((s.source==a&&s.destination==b)||(s.source==b&&s.destination==a))return &s;
    return nullptr;
}
int main(){
    const auto root=fs::temp_directory_path()/("dc_suggest_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        // Two dated camera backups, substantial byte-verified overlap, one
        // revised filename and one extra photo in each snapshot.
        const auto a=fs::path(L"相机备份2023");const auto b=fs::path(L"相机备份2024");
        for(int i=1;i<=8;++i){auto file=fs::path("DCIM")/("IMG_"+std::to_string(i)+".jpg");
            put(root/a/file,"photo-content-"+std::to_string(i));
            put(root/b/file,"photo-content-"+std::to_string(i));}
        put(root/a/"DCIM"/"IMG_9.jpg","old-version");
        put(root/b/"DCIM"/"IMG_9.jpg","new-version");
        put(root/a/"DCIM"/"only-old.jpg","old-only");
        put(root/b/"DCIM"/"only-new.jpg","new-only");
        put(root/b/"DCIM"/"only-new2.jpg","new-only2");
        // Two completely equal trees do not need merging.
        for(const auto& d:{fs::path("equal-A"),fs::path("equal-B")}){
            put(root/d/"a.txt","identical-alpha");put(root/d/"b.txt","identical-beta");
            put(root/d/"c.txt","identical-gamma");
        }
        // Same names but different bytes are not lineage evidence.
        for(int i=1;i<=8;++i){const auto f="IMG_"+std::to_string(i)+".jpg";
            put(root/"unrelated-X"/f,"unrelated-X-"+std::to_string(i));
            put(root/"unrelated-Y"/f,"unrelated-Y-"+std::to_string(i));
        }
        // OS-generated common metadata is excluded from evidence.
        put(root/"noise-A"/"Thumbs.db","x");put(root/"noise-B"/"Thumbs.db","x");
        put(root/"noise-A"/"desktop.ini","y");put(root/"noise-B"/"desktop.ini","y");
        auto inv=scan(root);auto verified=deduplicate(inv);
        assert(inv.complete()&&verified.errors.empty());
        auto candidates=recommend_merges(inv,40);
        auto* match=find(candidates,a,b);
        assert(match!=nullptr);
        assert(match->confidence==3);
        // Also discover matching folders nested inside separate backups.
        auto* nested=find(candidates,a/"DCIM",b/"DCIM");
        assert(nested!=nullptr && nested->shared_contents==8);
        assert(match->shared_contents==8&&match->same_relative_files==8);
        assert(match->source==a&&match->destination==b);
        assert(match->source_only==1&&match->destination_only==2);
        assert(match->changed_at_same_path==1&&match->useful);
        assert(!find(candidates,"equal-A","equal-B"));
        assert(!find(candidates,"unrelated-X","unrelated-Y"));
        assert(!find(candidates,"noise-A","noise-B"));
        assert(!fs::exists(root/".DiskCurator-System")); // Suggestions are read-only.
        auto pending=plan_merge_dirs(inv,match->source,match->destination);
        assert(pending.conflicts==1&&pending.copies.size()==2);
        // Recommendation is not an execution request, regardless of confidence.
        assert(!fs::exists(root/b/"DCIM"/"only-old.jpg"));
        assert(execute_merge_plan(root,pending).success);
        auto rescan=scan(root);auto d2=deduplicate(rescan);
        auto repeat=plan_merge_dirs(rescan,a,b);
        assert(repeat.copies.empty());
        // A fully covered source can still be reported as an informational
        // candidate when its destination contains additional files.
        auto after=recommend_merges(rescan,40);
        if(auto* present=find(after,a,b))assert(!present->useful);
        assert(failed([&]{auto bad=rescan;bad.errors.push_back("unreadable");(void)recommend_merges(bad);}));
        std::atomic_bool stop{true};assert(failed([&]{(void)recommend_merges(rescan,40,&stop);}));
        std::error_code ec;fs::remove_all(root,ec);
        std::cout<<"RECOMMENDATION TESTS PASSED\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"FAIL: "<<e.what()<<" root="<<utf8(root)<<"\n";return 2;
    }
}
