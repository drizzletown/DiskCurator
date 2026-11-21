// Methods of the native window's old-disk workflow. Every write goes through
// the same preview, confirmation and transaction services as the quick tasks.
void old_forget_archive(){
    old_archive.reset();old_archive_root.clear();old_retire_after_refresh=false;
}
bool old_has_archive()const{return old_archive&&old_step==3&&old_archive_root==root;}
bool guided() const { return workflow==Workflow::OldDisk; }

static const wchar_t* old_step_name(unsigned step){
    static const wchar_t* const names[]={L"认识这块盘",L"核对目录版本",L"整理重复文件",
        L"归档暂时不用的资料",L"建立新目录结构",L"复核操作记录"};
    return names[step<6?step:0];
}
int old_main_view()const{
    static constexpr int pages[]={OldOverview,OldPairs,OldGroups,OldSources,OldSources,7};
    return pages[old_step];
}
const Row* old_single_row()const{
    if(ListView_GetSelectedCount(list)!=1)return nullptr;
    const auto n=ListView_GetNextItem(list,-1,LVNI_SELECTED);
    return n>=0&&size_t(n)<visible.size()?&rows[visible[size_t(n)]]:nullptr;
}
std::wstring old_caption()const{
    if(results_stale)return old_changed.empty()?L"检查操作记录后，重新盘点可继续本步骤。":L"检查本次记录后，更新清单即可继续。只重读新增、变化和此次操作涉及的文件。";
    const auto n=[](size_t x){return std::to_wstring(x);};
    if(view==9)return L"请核对下方的原位置、目标和处理方式，确认后才会写入。";
    if(view==7)return L"本次实际完成 "+n(old_writes.size())+L" 笔操作。下表是此范围的全部记录。";
    switch(view){
    case OldOverview:if(old_metadata)return n(inv.files.size())+L" 个文件 · "+n(inv.dirs.size())+L" 个目录 · 已快速盘点。下一步按内容查找备份和重复文件。";
        return n(inv.files.size())+L" 个文件 · "+n(old_overview.duplicate_groups)+L" 组重复候选 · "+n(old_overview.versions.size())+L" 对可能相关的目录。";
    case OldPairs:return L"选一对目录查看共有内容、双方独有项和冲突版本。也可以手选两个目录。";
    case 8:return L"从 "+fmt(requested_source)+L" 补到 "+fmt(requested_destination)+L"；保留来源，同名不同内容另存。";
    case OldGroups:return n(hash_duplicates.groups.size())+L" 组候选 · 每组至少留 "+n(old_keep)+L" 份独立文件。选一组查看位置。";
    case OldGroupFiles:{
        const size_t copies=hash_duplicates.groups.at(old_group).size();
        return n(copies)+L" 个位置 · 至少留 "+n(old_keep)+L" 份独立文件 · "+
            (copies<=old_keep?L"按当前规则全部保留。":L"请选择要移入隔离区的文件；执行前核对保留副本。");
    }
    case OldSources:return L"当前位置 "+fmt(root/old_folder)+L" · 双击目录进入，搜索可跨目录；Ctrl / Shift 多选。";
    case OldRules:return L"复制到 "+fmt(root/old_outputs[old_step-3])+L" · 原件保留。双击任一行可调整目标。";
    default:return L"";
    }
}
void old_navigation(bool result){
    const bool active=guided()&&result&&!busy;
    auto show=[](HWND h,bool on){ShowWindow(h,on?SW_SHOW:SW_HIDE);};
    show(old_back,active&&old_step>0&&!results_stale);
    show(old_next,active);
    show(old_resume,active&&view==OldOverview&&old_saved>0&&old_saved<=5&&!results_stale);
    show(old_extra,false);show(old_remove,false);show(old_retire,active&&old_has_archive()&&(view==7||view==OldSources));
    if(!active)return;
    const bool selecting=view==OldSources,queue=view==OldRules,pair=view==8;
    const bool completed=view==7&&old_step==5;
    std::wstring next=old_step<5?L"下一步　"+std::wstring(old_step_name(old_step+1)):L"完成并返回首页";
    SetWindowTextW(old_next,results_stale?(old_changed.empty()?L"重扫并继续此步骤":L"更新清单并继续"):next.c_str());
    if(view==OldOverview&&old_saved>0&&old_saved<=5)SetWindowTextW(old_resume,(L"继续："+std::wstring(old_step_name(old_saved))).c_str());
    show(btn_basic,false);show(btn_history,false);show(duplicate_view,false);
    show(retention_button,!results_stale&&(view==OldGroups||view==OldGroupFiles));
    const bool extra=view==OldPairs||pair||queue||(selecting&&!old_rules[old_step-3].empty())||(view==OldOverview&&!inv.complete());
    show(old_extra,!results_stale&&extra);
    SetWindowTextW(old_extra,view==OldPairs?L"手选两个目录…":pair?L"交换两个目录":queue?L"预览复制方案":selecting?L"查看已有清单":L"查看读取异常");
    show(old_remove,!results_stale&&queue);
    bool back=!results_stale&&(view!=old_main_view()||(selecting&&!old_folder.empty()));
    show(return_result,back);
    SetWindowTextW(return_result,view==9?L"返回修改":queue?L"继续选择来源":selecting?L"上一级":L"返回本步");
    const bool undo=view==7&&can_restore;
    const bool choose=(view==OldPairs&&!old_overview.versions.empty())||view==OldGroups||view==OldGroupFiles||pair||selecting||queue;
    show(context_action,undo||(!results_stale&&choose));
    SetWindowTextW(context_action,undo?L"恢复最近的移动…":pair?L"预览目录合并":view==OldPairs?L"查看所选差异":
        view==OldGroups?L"查看这组文件":view==OldGroupFiles?L"预览隔离所选":
        selecting?(old_step==3?L"加入归档清单":L"加入新目录映射"):L"修改目标位置…");
    if(completed)SetWindowTextW(old_next,L"完成并返回首页");
    if(!results_stale&&view!=9)SetWindowTextW(progress_stage,(L"第 "+std::to_wstring(old_step+1)+L" / 6 步　"+old_step_name(old_step)).c_str());
    old_selection_buttons();
}
void old_selection_buttons(){
    if(!guided()||!has_scan||busy)return;
    const auto count=ListView_GetSelectedCount(list);
    bool enabled=!results_stale;
    if(view==7)enabled=can_restore;
    else if(view==OldPairs||view==OldGroups)enabled=enabled&&old_single_row()!=nullptr&&old_single_row()->suggestion>=0;
    else if(view==8)enabled=enabled&&has_comparison&&pair_report.disjoint_for_merge;
    else if(view==OldGroupFiles)enabled=enabled&&old_group<hash_duplicates.groups.size()&&
        count>0&&size_t(count)+old_keep<=hash_duplicates.groups[old_group].size();
    else if(view==OldSources)enabled=enabled&&count>0;
    else if(view==OldRules)enabled=enabled&&count==1;
    EnableWindow(context_action,enabled);
    EnableWindow(old_remove,view==OldRules&&count>0&&!results_stale);
}
void old_display(int page){
    change_view(page,true);
}
bool old_discard(){
    if(has_pending&&MessageBoxW(hwnd,L"离开此预览会放弃尚未执行的方案。继续吗？",kProductName,MB_YESNO|MB_DEFBUTTON2)!=IDYES)return false;
    if(has_pending||!pending.name.empty()||pending_is_merge||pending_is_structure)invalidate_pending();
    return true;
}
void old_go(unsigned next){
    if(!guided()||next>5||busy||!has_scan||results_stale||!old_discard())return;
    if(old_metadata&&next>0){
        if(!need_scan())return;
        if(old_mount&&!same_drive(*old_mount)){alert(hwnd,L"磁盘已更换，请返回首页重新选择。",MB_ICONWARNING);return;}
        old_requested_step=next;start(Task::Scan);return;
    }
    if(old_step!=next)old_forget_archive();
    old_step=next;old_folder.clear();old_display(old_main_view());
    dc::save_old_disk_step(root,old_step);old_saved=old_step;
}
void old_return(){
    if(results_stale||!old_discard())return;
    if(view==OldSources&&!old_folder.empty()){old_folder=old_folder.parent_path();old_display(OldSources);}
    else if(view==9&&old_step==1)old_display(8);
    else if(view==9&&old_step==2)old_display(OldGroupFiles);
    else if(view==9&&(old_step==3||old_step==4))old_display(old_rules[old_step-3].empty()?OldSources:OldRules);
    else old_display(old_main_view());
}
void old_retention(){
    if(busy||results_stale||old_step!=2)return;
    std::vector<Prompt::Field> fields={{L"每组至少保留多少份独立文件（1 至 32）",std::to_wstring(old_keep)}};
    auto valid=[](const std::vector<Prompt::Field>& f)->std::optional<Prompt::InputError>{
        const auto& v=f[0].value;
        if(v.empty()||v.size()>2||v.find_first_not_of(L"0123456789")!=std::wstring::npos||std::stoul(v)<1||std::stoul(v)>32)
            return Prompt::InputError{0,L"请输入 1 至 32 的整数。"};
        return {};
    };
    if(!prompt(hwnd,L"此次整理的副本保留规则",fields,font,L"保存",valid))return;
    old_keep=std::stoul(fields[0].value);invalidate_pending();refresh();refresh_navigation();
}
void old_verify_pair(){
    if(!need_scan())return;
    const auto* row=old_single_row();
    if(row&&row->suggestion>=0&&size_t(row->suggestion)<old_overview.versions.size()){
        const auto& pair=old_overview.versions[size_t(row->suggestion)];
        requested_source=pair.left;requested_destination=pair.right;start(Task::Pair);
    }else do_pair_assess();
}
void old_edit_rule(){
    const auto* row=old_single_row();
    if(!row||row->suggestion<0)return;
    auto& rules=old_rules[old_step-3];
    const auto index=size_t(row->suggestion);if(index>=rules.size())return;
    auto& rule=rules[index];
    std::vector<Prompt::Field> fields={{L"在新结构中的相对位置",fmt(rule.target)}};
    if(!prompt(hwnd,L"安排 "+fmt(rule.source)+L" 的新位置",fields,font))return;
    fs::path target=fields[0].value;
    if(target.empty()||!dc::is_inside(target)){alert(hwnd,L"请填写新目录内的相对路径。",MB_ICONWARNING);return;}
    rule.target=target;old_display(OldRules);
}
void old_action(){
    if(view==7){do_undo();return;}
    if(!need_scan())return;
    if(view==OldPairs){old_verify_pair();return;}
    if(view==8&&has_comparison&&pair_report.disjoint_for_merge){start(Task::MergePlan);return;}
    if(view==OldGroups){
        const auto* row=old_single_row();if(!row||row->suggestion<0||size_t(row->suggestion)>=hash_duplicates.groups.size())return;
        old_group=size_t(row->suggestion);old_display(OldGroupFiles);return;
    }
    if(view==OldGroupFiles){
        auto chosen=selected_paths();
        if(chosen.empty()||old_group>=hash_duplicates.groups.size()||chosen.size()+old_keep>hash_duplicates.groups[old_group].size())return;
        requested_files=std::move(chosen);start(Task::QuarantinePlan);return;
    }
    if(view==OldRules){old_edit_rule();return;}
    if(view!=OldSources)return;
    auto paths=selected_paths();if(paths.empty())return;
    auto& rules=old_rules[old_step-3];
    for(const auto& path:paths){
        if(std::none_of(rules.begin(),rules.end(),[&](const auto& r){return r.source==path;}))
            rules.push_back({path,path,{}});
    }
    old_display(OldRules);
}
void old_command(int id){
    if(!guided()||!has_scan||busy)return;
    if(id==ID_OLD_RETIRE&&old_has_archive()&&(view==7||view==OldSources)){
        std::vector<Prompt::Field> f={{L"每个移出的原件至少留下多少个独立归档副本",L""}};
        f[0].choices={L"至少两份；不足则原件留在原位",L"仅一份；逐字节核对归档副本后隔离原件"};
        if(!prompt(hwnd,L"收起已归档原件",f,font,L"仅生成预览"))return;
        old_archive_keep=f[0].selection==1?1:2;
        if(results_stale){
            if(old_changed.empty())return;
            old_retire_after_refresh=true;start(Task::OldRefresh);
        }else if(need_scan())start(Task::ArchivedRetirePlan);
        return;
    }
    if(id==ID_OLD_NEXT){
        if(results_stale){if(old_changed.empty())run_scan();else start(Task::OldRefresh);return;}
        if(old_step==5){dc::save_old_disk_step(root,0);select_task(Workflow::Home);return;}
        old_go(old_step+1);return;
    }
    if(results_stale)return;
    if(id==ID_OLD_BACK&&old_step>0){old_go(old_step-1);return;}
    if(id==ID_OLD_RESUME){if(view==OldOverview&&old_saved>0)old_go(old_saved);return;}
    if(id==ID_OLD_REMOVE&&view==OldRules){
        auto& rules=old_rules[old_step-3];auto paths=selected_paths();std::set<fs::path> remove(paths.begin(),paths.end());
        rules.erase(std::remove_if(rules.begin(),rules.end(),[&](const auto& r){return remove.count(r.source);}),rules.end());
        old_display(rules.empty()?OldSources:OldRules);return;
    }
    if(id!=ID_OLD_EXTRA)return;
    if(view==OldPairs){do_pair_assess();return;}
    if(view==OldOverview){change_view(12,true);return;}
    if(view==8&&has_comparison){std::swap(requested_source,requested_destination);start(Task::Pair);return;}
    if(view==OldSources){if(!old_rules[old_step-3].empty())old_display(OldRules);return;}
    if(view!=OldRules||old_rules[old_step-3].empty()||!need_scan())return;
    auto& out=old_outputs[old_step-3];
    std::vector<Prompt::Field> fields={{L"在当前扫描根目录内建立的顶层文件夹",fmt(out)}};
    if(!prompt(hwnd,old_step==3?L"归档副本存到哪里":L"新目录结构建在哪里",fields,font,L"生成复制预览"))return;
    if(!dc::valid_file_name(fields[0].value)){alert(hwnd,L"请输入一个有效的文件夹名。",MB_ICONWARNING);return;}
    out=fields[0].value;start(Task::SelectedCopyPlan);
}

static std::wstring old_folder_types(const dc::FolderClues& clues){
    static const std::map<std::string,std::wstring> labels={{"Documents",L"文档"},{"Pictures",L"图片"},
        {"Videos",L"视频"},{"Audio",L"音频"},{"Archives",L"压缩包"},{"Code",L"代码"},{"Other",L"其他"}};
    std::vector<std::pair<std::string,size_t>> ranked(clues.categories.begin(),clues.categories.end());
    std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){
        return a.second!=b.second?a.second>b.second:a.first<b.first;
    });
    std::wstring text;
    for(size_t i=0;i<std::min<size_t>(2,ranked.size());++i){
        if(i)text+=L" · ";
        const auto label=labels.find(ranked[i].first);
        text+=(label==labels.end()?L"其他":label->second)+std::wstring(L" ")+std::to_wstring(ranked[i].second);
    }
    return text;
}
static unsigned old_file_year(const std::optional<fs::file_time_type>& time){
    if(!time)return 0;
    ULARGE_INTEGER stamp{};stamp.QuadPart=time->time_since_epoch().count();
    const FILETIME filetime{stamp.LowPart,stamp.HighPart};SYSTEMTIME utc{};
    return FileTimeToSystemTime(&filetime,&utc)?utc.wYear:0;
}
bool old_rows(){
    const auto n=[](size_t x){return std::to_wstring(x);};
    if(view==OldOverview){
        rows.push_back({L"此次盘点",fmt(root),bytes(inv.bytes),n(inv.files.size())+L" 个文件，"+n(inv.dirs.size())+L" 个目录"});
        if(old_metadata){
            rows.push_back({L"目录版本和重复文件",L"待分析文件内容",L"",L"下一步再读取所选范围内的文件"});
        }else{
        rows.push_back({L"可能是不同时期的备份",n(old_overview.versions.size())+L" 对目录",L"",L"下一步可比较两个版本，保留双方独有资料"});
        rows.push_back({L"相同内容的文件",n(old_overview.duplicate_groups)+L" 组，涉及 "+n(old_overview.duplicate_files)+L" 份文件",L"",L"默认每组保留两份独立文件"});
        }
        for(const auto& folder:old_overview.largest_folders){
            const auto& clues=old_overview.folder_clues.at(folder.rel);
            std::wstring details=n(folder.count)+L" 个文件";
            const auto first=old_file_year(clues.earliest),last=old_file_year(clues.latest);
            if(first&&last){
                details+=L" · 修改于 "+n(first);
                if(first!=last)details+=L"–"+n(last);
                details+=L" 年";
            }
            rows.push_back({fmt(folder.rel),old_folder_types(clues),bytes(folder.bytes),details,folder.bytes,folder.rel});
        }
        if(!inv.complete())rows.push_back({L"部分资料未读取",n(inv.errors.size())+L" 项读取异常",L"",L"可以手工选两个目录，在所选范围重新核对"});
        return true;
    }
    if(view==OldPairs){
        for(size_t i=0;i<old_overview.versions.size();++i){
            const auto& p=old_overview.versions[i];
            if(!old_overview.complete){
                rows.push_back({fmt(p.left),fmt(p.right),n(p.shared_contents)+L" 种已读内容重合",
                    L"已读部分有共同内容，须重新核对这两个目录。",p.shared_contents,{},static_cast<int>(i)});
                continue;
            }
            rows.push_back({fmt(p.left),fmt(p.right),n(p.shared_contents)+L" 种内容重合",
                (p.same_tree?std::wstring(L"疑似整套相同"):L"较小侧覆盖 "+n(p.coverage_percent)+L"%")+
                L" · 左独有 "+n(p.left_only_paths)+L"，右独有 "+n(p.right_only_paths)+L"，同路径不同 "+n(p.conflicting_paths),
                p.shared_contents,{},static_cast<int>(i)});
        }
        if(rows.empty())rows.push_back({L"尚无目录候选",inv.complete()?L"没有找到足够的内容线索":L"盘点不完整，暂不推断版本关系",L"",L"点“手选两个目录”可以自行核对"});
        if(old_overview.discovery_limited)rows.push_back({L"候选显示有限",L"手工选择也能比较指定目录",L"",L""});
        return true;
    }
    if(view==8&&has_comparison){
        const auto& comparison=pair_report.comparison;
        rows.push_back({L"来源 → 合并目标",fmt(comparison.left)+L" → "+fmt(comparison.right),L"",L"保留双方已有文件，仅向目标补充内容"});
        rows.push_back({L"逐项核对",L"来源独有 "+n(comparison.only_left)+L" 项，目标独有 "+n(comparison.only_right)+L" 项",
            n(comparison.identical)+L" 项相同",n(comparison.changed)+L" 项同路径不同；项目数含子目录"});
        const auto query=low(get_text(search));size_t matched=0;
        for(const auto& d:comparison.rows){
            const auto label=d.state=="ONLY LEFT"?L"来源独有":d.state=="ONLY RIGHT"?L"目标独有":L"同路径不同";
            if(!query.empty()&&low(fmt(d.rel)).find(query)==std::wstring::npos&&std::wstring(label).find(query)==std::wstring::npos)continue;
            if(++matched<=5000)rows.push_back({label,fmt(d.rel),bytes(d.left),L"目标 "+bytes(d.right),d.left+d.right});
        }
        if(matched>5000)rows.push_back({L"显示前 5000 项",L"可搜索全部差异",L"",L""});
        return true;
    }
    if(view==OldGroups){
        const auto query=low(get_text(search));size_t matched=0;source_filtered=true;
        for(size_t i=0;i<hash_duplicates.groups.size();++i){
            const auto& group=hash_duplicates.groups[i];if(group.empty())continue;
            const auto& first=inv.files[group.front()];
            if(!query.empty()&&std::none_of(group.begin(),group.end(),[&](size_t idx){return low(fmt(inv.files[idx].rel)).find(query)!=std::wstring::npos;}))continue;
            if(++matched<=5000)rows.push_back({first.rel.filename().wstring(),fmt(first.rel),n(group.size())+L" 份",
                group.size()<=old_keep?L"全部保留，仍可查看位置":L"选这组，再选需要隔离的位置",first.bytes,{},static_cast<int>(i)});
        }
        if(matched>5000)rows.push_back({L"显示前 5000 组",L"可搜索所有候选路径",L"",L""});
        if(!matched&&query.empty())rows.push_back({L"没有发现重复候选",L"可以继续归档或建立新目录结构",L"",L""});
        return true;
    }
    if(view==OldGroupFiles){
        if(old_group>=hash_duplicates.groups.size())return true;
        const auto query=low(get_text(search));size_t matched=0;source_filtered=true;
        for(auto i:hash_duplicates.groups[old_group]){
            const auto& f=inv.files[i];if(!query.empty()&&low(fmt(f.rel)).find(query)==std::wstring::npos)continue;
            if(++matched>5000)continue;
            rows.push_back({f.rel.filename().wstring(),fmt(f.rel),bytes(f.bytes),L"未选中的独立副本可用于保留核验",f.bytes,f.rel});
        }
        if(matched>5000)rows.push_back({L"显示前 5000 份",L"搜索路径可定位这组中的其他文件",L"",L""});
        return true;
    }
    if(view==OldSources){
        const auto query=low(get_text(search));size_t matched=0;source_filtered=true;
        auto add=[&](const fs::path& rel,bool dir,dc::U64 size){
            if(rel.empty()||(query.empty()?rel.parent_path()!=old_folder:low(fmt(rel)).find(query)==std::wstring::npos))return;
            if(++matched>5000)return;
            rows.push_back({rel.filename().wstring(),fmt(rel),bytes(size),dir?L"文件夹 · 双击打开":L"文件",size,rel,dir?1:0});
        };
        for(const auto& d:inv.dirs)add(d.rel,true,d.bytes);
        for(const auto& f:inv.files)add(f.rel,false,f.bytes);
        if(matched>5000)rows.push_back({L"显示前 5000 项",L"输入更具体的路径可以继续筛选",L"",L""});
        return true;
    }
    if(view==OldRules){
        const auto index=old_step-3;
        for(size_t i=0;i<old_rules[index].size();++i){
            const auto& rule=old_rules[index][i];
            rows.push_back({fmt(rule.source),fmt(old_outputs[index]/rule.target),L"复制",L"来源保留 · 双击可修改目标",0,rule.source,static_cast<int>(i)});
        }
        return true;
    }
    return false;
}
