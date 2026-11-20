#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <iomanip>
#include <cwctype>
#include <cwchar>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "core.hpp"
#include "observations.hpp"

#ifdef _MSC_VER
#pragma comment(lib,"comctl32.lib")
#endif
namespace fs=std::filesystem;

namespace {
constexpr UINT WMU_DONE=WM_APP+43;
constexpr wchar_t kProductName[]=L"磁盘文件整理助手";
constexpr int kCompactTitleWidth=184;
enum Id { ID_ROOT=100,ID_BROWSE,ID_SCAN,ID_STOP,ID_COMPARE,ID_RENAME,ID_ARCHIVE,ID_QUARANTINE,
          ID_APPLY,ID_UNDO,ID_EXPORT,ID_PHOTO,ID_LIST=114,ID_SEARCH,ID_RELATIONS,ID_MERGE,ID_SUGGEST,ID_STRUCTURE,ID_MAP_EDIT,ID_STRUCTURE_PREVIEW,ID_STRUCTURE_REJECT,ID_RETIRE=125, ID_SCAN_SCOPE=230, ID_LENS_PICK=231, ID_BASIC_ACTIONS=240, ID_THEME_ACTIONS, ID_SMART_ACTIONS, ID_SCAN_PROGRESS, ID_PROGRESS_STAGE, ID_SELECTION_CONTEXT, ID_ALL_RESULTS, ID_PROGRESS_METRICS, ID_PROGRESS_PATH, ID_SHOW_PREVIEW, ID_SHOW_HISTORY,ID_STATUS,
          ID_CLEAR_FILTER=252,ID_WELCOME_TITLE,ID_WELCOME_DETAIL,ID_CHOOSE_FOLDER,ID_CHOOSE_DRIVES,ID_DUPLICATE_VIEW,ID_QUICK_INVENTORY };
enum class Task { Scan, ScanAll, Inventory, Verify, Pair, Suggest, Structure, Photo, Relations, ArchivePlan, MergePlan, StructurePlan, RetirementPlan, QuarantinePlan, CleanPlan, RenamePlan, Apply, MergeApply, StructureApply, Undo };
enum Command { CMD_OPEN=5100, CMD_ISOLATE_SELECTED, CMD_PAIR, CMD_EXACT, CMD_HISTORY, CMD_PREVIEW, CMD_POLICY, CMD_SELECT_ALL, CMD_SELECT_NONE };
constexpr COLORREF kNavy=RGB(15,23,42),kPage=RGB(245,248,252),kAccent=RGB(21,184,153),kText=RGB(31,41,55),kMuted=RGB(100,116,139);
const wchar_t* const scope_lenses[]={L"扫描概览",L"文件视角",L"目录视角",L"哈希副本组",L"扫描问题"};
const wchar_t* const tab_names[]={L"文件视角",L"目录视角",L"文件副本",L"目录副本",L"目录关系",L"相似照片",L"副本保障",L"操作历史",L"双目录评估",L"操作预览",L"融合建议",L"目录重组映射",L"扫描概览"};

std::wstring wutf8(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),nullptr,0);if(n<=0)return L"[invalid UTF-8]";std::wstring out(size_t(n),L'\0');MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),&out[0],n);return out;}
std::string utf8w(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);std::string out(size_t(n),'\0');if(n>0)WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),&out[0],n,nullptr,nullptr);return out;}
std::wstring low(std::wstring s){std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return wchar_t(towlower(c));});return s;}
std::wstring get_text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(size_t(n)+1,L'\0');GetWindowTextW(h,&s[0],n+1);s.resize(n);return s;}
void set_font(HWND h,HFONT f){SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE);}
HWND create(HWND parent,const wchar_t* cls,const wchar_t* label,DWORD style,int id,DWORD ex=0){return CreateWindowExW(ex,cls,label,style|WS_CHILD|WS_VISIBLE,0,0,40,24,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);}
void move(HWND h,int x,int y,int w,int height){MoveWindow(h,x,y,std::max(1,w),std::max(1,height),TRUE);}
std::wstring bytes(dc::U64 n){return wutf8(dc::human(n));}
void alert(HWND owner,const std::wstring& message,UINT style=MB_ICONINFORMATION){MessageBoxW(owner,message.c_str(),kProductName,style|MB_OK);}
std::wstring fmt(const fs::path& p){if(p.empty())return L".";return p.generic_wstring();}
// A volume selection is a frozen input contract, not a request to scan all
// currently visible drives. Volume identity is checked before and after IO.
struct DriveChoice {
    fs::path root;
    std::wstring label, guid;
    DWORD serial=0;
    UINT kind=DRIVE_UNKNOWN;
    ULONGLONG capacity=0, free_bytes=0;
};
bool read_drive(const fs::path& root,DriveChoice& out) {
    const auto wide=root.wstring();
    const UINT kind=GetDriveTypeW(wide.c_str());
    if(kind!=DRIVE_FIXED && kind!=DRIVE_REMOVABLE)return false;
    wchar_t label[MAX_PATH]{};DWORD serial=0;
    if(!GetVolumeInformationW(wide.c_str(),label,MAX_PATH,&serial,nullptr,nullptr,nullptr,0))return false;
    wchar_t guid[MAX_PATH]{};
    if(!GetVolumeNameForVolumeMountPointW(wide.c_str(),guid,MAX_PATH))guid[0]=0;
    ULARGE_INTEGER avail{},total{},free{};
    if(!GetDiskFreeSpaceExW(wide.c_str(),&avail,&total,&free))return false;
    out.root=root;out.kind=kind;out.label=label;out.guid=guid;
    out.serial=serial;out.capacity=total.QuadPart;out.free_bytes=free.QuadPart;
    return true;
}
bool same_drive(const DriveChoice& frozen) {
    DriveChoice current;
    return read_drive(frozen.root,current) &&
           current.kind==frozen.kind && current.serial==frozen.serial &&
           (frozen.guid.empty() || (!current.guid.empty() &&
            low(frozen.guid)==low(current.guid)));
}
std::vector<DriveChoice> list_drives(std::vector<std::string>& issues) {
    DWORD n=GetLogicalDriveStringsW(0,nullptr);
    if(!n)throw std::runtime_error("Cannot enumerate Windows logical drives");
    std::vector<wchar_t> data(size_t(n)+2,L'\0');
    if(!GetLogicalDriveStringsW(DWORD(data.size()),data.data()))
       throw std::runtime_error("GetLogicalDriveStringsW failed");
    std::vector<DriveChoice> result;
    for(const wchar_t* ptr=data.data();*ptr;ptr+=wcslen(ptr)+1) {
        const auto kind=GetDriveTypeW(ptr);
        if(kind!=DRIVE_FIXED&&kind!=DRIVE_REMOVABLE)continue;
        DriveChoice choice;
        if(read_drive(fs::path(ptr),choice))result.push_back(std::move(choice));
        else issues.push_back("Drive not ready: "+utf8w(ptr));
    }
    return result;
}
std::wstring drive_description(const DriveChoice& drive) {
    auto s=drive.root.wstring()+L"  "+(drive.label.empty()?L"(无卷标)":drive.label);
    s+=L"  ·  "+bytes(drive.capacity)+L"  ·  空闲 "+bytes(drive.free_bytes);
    s+=drive.kind==DRIVE_REMOVABLE?L"  ·  可移除介质":L"  ·  固定卷（也可能是 USB 硬盘）";
    return s;
}


struct Prompt {
    struct Field {
        std::wstring label, value;
        bool checkbox = false;
        std::vector<std::wstring> choices;
        int selection = 0;
        // A file rename initially selects the stem so typing preserves its extension.
        std::optional<int> initial_selection_end;
        std::optional<std::pair<int,int>> number_range;
        Field(std::wstring name, std::wstring initial, bool check = false)
            : label(std::move(name)), value(std::move(initial)), checkbox(check) {}
    };
    struct InputError {size_t field; std::wstring message;};
    using Validate = std::function<std::optional<InputError>(const std::vector<Field>&)>;
    Validate validate;
    std::wstring ok_text;
    HWND feedback = nullptr;
    bool showing_error = false;
    HWND owner=nullptr,window=nullptr,ok=nullptr,cancel=nullptr;
    std::wstring title;
    std::vector<Field> fields;
    std::vector<HWND> labels,controls;
    HFONT font=nullptr;
    int scroll=0,scroll_limit=0,viewport=0;
    bool accepted=false;
};
void layout_prompt(Prompt* p){
    if(!p->window)return;
    RECT rc{};GetClientRect(p->window,&rc);
    const int width=std::max(100,int(rc.right-rc.left));
    const int height=std::max(100,int(rc.bottom-rc.top));
    p->viewport=std::max(40,height-(p->showing_error ? 83 : 51));
    p->scroll_limit=std::max(0,int(p->fields.size())*57+8-p->viewport);
    p->scroll=std::min(p->scroll,p->scroll_limit);
    ShowScrollBar(p->window,SB_VERT,p->scroll_limit>0);
    SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;
    si.nMin=0;si.nMax=int(p->fields.size())*57+8;si.nPage=UINT(p->viewport);si.nPos=p->scroll;
    SetScrollInfo(p->window,SB_VERT,&si,TRUE);
    for(size_t i=0;i<p->controls.size();++i){
        const int y=16+int(i)*57-p->scroll;
        const bool visible=y>=0&&y+45<=p->viewport;
        move(p->labels[i],16,y,std::max(120,width-40),17);
        move(p->controls[i],16,p->fields[i].checkbox?y+5:y+19,std::max(120,width-40),p->fields[i].choices.empty()?24:145);
        ShowWindow(p->labels[i],visible&&!p->fields[i].checkbox?SW_SHOW:SW_HIDE);
        ShowWindow(p->controls[i],visible?SW_SHOW:SW_HIDE);
    }
    if(p->feedback) {
        move(p->feedback,16,height-78,width-32,30);
        ShowWindow(p->feedback,p->showing_error?SW_SHOW:SW_HIDE);
    }
    if(p->ok){move(p->ok,width-196,height-41,85,29);move(p->cancel,width-103,height-41,85,29);}
}
LRESULT CALLBACK prompt_proc(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto* p=reinterpret_cast<Prompt*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto cs=reinterpret_cast<CREATESTRUCTW*>(l);p=static_cast<Prompt*>(cs->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);p->window=h;return DefWindowProcW(h,msg,w,l);}
    if(!p)return DefWindowProcW(h,msg,w,l);
    if(msg==WM_CREATE){
        for(size_t i=0;i<p->fields.size();++i){
            const auto& field=p->fields[i];
            auto label=CreateWindowExW(0,L"STATIC",field.label.c_str(),WS_CHILD|WS_VISIBLE,0,0,40,17,h,nullptr,nullptr,nullptr);set_font(label,p->font);
            HWND ed=nullptr;
            if(!field.choices.empty()){
                ed=create(h,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,2000+int(i));
                for(const auto& choice:field.choices)SendMessageW(ed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(choice.c_str()));
                SendMessageW(ed,CB_SETCURSEL,field.selection,0);
            } else ed=field.checkbox
                ?create(h,L"BUTTON",field.label.c_str(),BS_AUTOCHECKBOX|WS_TABSTOP,2000+int(i))
                :create(h,L"EDIT",field.value.c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,2000+int(i),WS_EX_CLIENTEDGE);
            if(field.checkbox)SendMessageW(ed,BM_SETCHECK,field.value==L"0"?BST_UNCHECKED:BST_CHECKED,0);
            set_font(ed,p->font);p->labels.push_back(label);p->controls.push_back(ed);
        }
        p->feedback=create(h,L"STATIC",L"",SS_LEFT,1999);
        set_font(p->feedback,p->font);
        p->ok=create(h,L"BUTTON",p->ok_text.c_str(),BS_DEFPUSHBUTTON|WS_TABSTOP,IDOK);
        p->cancel=create(h,L"BUTTON",L"取消",WS_TABSTOP,IDCANCEL);
        set_font(p->ok,p->font);set_font(p->cancel,p->font);
        layout_prompt(p);return 0;
    }
    if(msg==WM_SIZE){layout_prompt(p);return 0;}
    if(msg==WM_VSCROLL){
        int next=p->scroll;
        switch(LOWORD(w)){
            case SB_LINEUP:next-=57;break;
            case SB_LINEDOWN:next+=57;break;
            case SB_PAGEUP:next-=p->viewport;break;
            case SB_PAGEDOWN:next+=p->viewport;break;
            case SB_THUMBTRACK:case SB_THUMBPOSITION:{SCROLLINFO si{};si.cbSize=sizeof(si);si.fMask=SIF_TRACKPOS;GetScrollInfo(h,SB_VERT,&si);next=si.nTrackPos;break;}
            case SB_TOP:next=0;break;
            case SB_BOTTOM:next=p->scroll_limit;break;
            default:break;
        }
        p->scroll=std::max(0,std::min(next,p->scroll_limit));layout_prompt(p);return 0;
    }
    if(msg==WM_MOUSEWHEEL){p->scroll=std::max(0,std::min(p->scroll-57*(GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA),p->scroll_limit));layout_prompt(p);return 0;}
    if(msg==WM_COMMAND){int id=LOWORD(w);
        if(id>=2000&&size_t(id-2000)<p->fields.size() &&
           (HIWORD(w)==EN_SETFOCUS||HIWORD(w)==BN_SETFOCUS)){
            const int y=16+(id-2000)*57;
            if(y-p->scroll<0)p->scroll=y;
            else if(y+47-p->scroll>p->viewport)p->scroll=y+47-p->viewport;
            p->scroll=std::max(0,std::min(p->scroll,p->scroll_limit));layout_prompt(p);
        }
        if(id==IDOK){
            std::optional<Prompt::InputError> error;
            for(size_t i=0;i<p->fields.size();++i){
                auto& field=p->fields[i];
                if(!field.choices.empty()){
                    field.selection=int(SendMessageW(p->controls[i],CB_GETCURSEL,0,0));
                    if(field.selection==CB_ERR&&!error)error=Prompt::InputError{i,L"请先选择"+field.label+L"。"};
                } else if(field.checkbox){
                    field.value=SendMessageW(p->controls[i],BM_GETCHECK,0,0)==BST_CHECKED?L"1":L"0";
                } else {
                    field.value=get_text(p->controls[i]);
                    if(field.number_range&&!error){
                        const auto [minimum,maximum]=*field.number_range;
                        int value=-1;
                        if(field.value.find_first_not_of(L"0123456789")==std::wstring::npos){
                            try{value=std::stoi(field.value);}catch(const std::exception&){}
                        }
                        if(value<minimum||value>maximum)
                            error=Prompt::InputError{i,L"“"+field.label+L"”请输入 "+std::to_wstring(minimum)+L" 到 "+std::to_wstring(maximum)+L" 之间的整数。"};
                    }
                }
            }
            if(!error&&p->validate)error=p->validate(p->fields);
            if(error){
                const size_t i=error->field;
                p->showing_error=true;
                SetWindowTextW(p->feedback,error->message.c_str());
                p->scroll=16+int(i)*57;
                layout_prompt(p);
                SetFocus(p->controls[i]);
                if(!p->fields[i].checkbox&&p->fields[i].choices.empty())SendMessageW(p->controls[i],EM_SETSEL,0,-1);
                MessageBeep(MB_ICONWARNING);
                return 0;
            }
            p->accepted=true;DestroyWindow(h);return 0;}
        if(id==IDCANCEL){DestroyWindow(h);return 0;}}
    if(msg==WM_CLOSE){DestroyWindow(h);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
bool prompt(HWND owner,const std::wstring& title,std::vector<Prompt::Field>& fields,HFONT font,const wchar_t* ok_text=L"确定",Prompt::Validate validate={}){
    static bool registered=false;if(!registered){WNDCLASSW wc{};wc.lpfnWndProc=prompt_proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"DCFormNative";wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;registered=true;}
    Prompt p;p.ok_text=ok_text;p.validate=std::move(validate);p.owner=owner;p.title=title;p.fields=fields;p.font=font;
    RECT r{},work{};GetWindowRect(owner,&r);MONITORINFO monitor{sizeof(MONITORINFO)};
    if(GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor))work=monitor.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
    int h=std::min(int(fields.size())*57+130,std::max(300,int(work.bottom-work.top)-70));
    int w=std::min(580,std::max(320,int(work.right-work.left)-36));
    int x=std::max(int(work.left),std::min(int(work.right)-w,int(r.left+((r.right-r.left)-w)/2)));
    int y=std::max(int(work.top),std::min(int(work.bottom)-h,int(r.top+((r.bottom-r.top)-h)/2)));
    p.window=CreateWindowExW(WS_EX_DLGMODALFRAME,L"DCFormNative",title.c_str(),WS_CAPTION|WS_SYSMENU|WS_POPUP|WS_VSCROLL|WS_CLIPCHILDREN,x,y,w,h,owner,nullptr,GetModuleHandleW(nullptr),&p);
    if(!p.window)return false;
    const HWND previous_focus=GetFocus();
    EnableWindow(owner,FALSE);ShowWindow(p.window,SW_SHOW);UpdateWindow(p.window);
    if(!p.controls.empty()){
        SetFocus(p.controls.front());
        if(p.fields.front().initial_selection_end)
            SendMessageW(p.controls.front(),EM_SETSEL,0,*p.fields.front().initial_selection_end);
    }
    MSG m{};while(IsWindow(p.window)){BOOL got=GetMessageW(&m,nullptr,0,0);if(got<=0){if(got==0)PostQuitMessage(int(m.wParam));break;}
        // Hidden rows of long forms must still be reachable from the keyboard.
        if(m.message==WM_KEYDOWN && m.wParam==VK_TAB && IsChild(p.window,GetFocus())){
            std::vector<HWND> stops=p.controls;
            stops.push_back(p.ok);stops.push_back(p.cancel);
            const auto current=std::find(stops.begin(),stops.end(),GetFocus());
            if(current!=stops.end()){
                const size_t step=(GetKeyState(VK_SHIFT)<0)?stops.size()-1:1;
                const size_t next=(size_t(current-stops.begin())+step)%stops.size();
                if(next<p.controls.size()&&!IsWindowVisible(stops[next])){
                    p.scroll=16+int(next)*57;
                    layout_prompt(&p);
                }
                SetFocus(stops[next]);
                continue;
            }
        }
        if(!IsDialogMessageW(p.window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}
    EnableWindow(owner,TRUE);SetForegroundWindow(owner);
    if(IsWindow(previous_focus)&&IsWindowVisible(previous_focus)&&IsWindowEnabled(previous_focus))SetFocus(previous_focus);
    if(p.accepted)fields=std::move(p.fields);return p.accepted;
}
struct Row {
    std::wstring a,b,c,d;dc::U64 count=0;fs::path launch;int suggestion=-1;
    Row(std::wstring name,std::wstring location,std::wstring size,std::wstring detail,dc::U64 value=0,fs::path path={},int index=-1)
      :a(std::move(name)),b(std::move(location)),c(std::move(size)),d(std::move(detail)),count(value),launch(std::move(path)),suggestion(index){}
};
struct JobResult {Task task;dc::InventorySummary inventory_summary;std::string error;dc::Inventory inventory;dc::Duplicates duplicates;std::vector<dc::DirGroup> dirs;dc::Duplicates hash_duplicates;dc::DirectoryPairReport pair;dc::OverlapListing overlaps;dc::PhotoReport photos;dc::Plan plan;dc::MergePlan merge_plan;dc::RetirementProposal retirement;dc::StructureSuggestion structure;dc::StructurePlan structure_plan;std::vector<dc::MergeSuggestion> suggestions;dc::Result result;dc::FullScan full_scan;dc::Observations observations;};
struct App {
 HWND hwnd=nullptr,root_box=nullptr,scope_pick=nullptr,lens_pick=nullptr,btn_browse=nullptr,btn_scan=nullptr,btn_stop=nullptr,list=nullptr,search=nullptr,status=nullptr;
 HWND btn_basic=nullptr,btn_theme=nullptr,btn_smart=nullptr,btn_preview=nullptr,btn_history=nullptr,btn_all_results=nullptr;
 HWND scan_bar=nullptr,progress_stage=nullptr,progress_metrics=nullptr,progress_path=nullptr,selection_context=nullptr;
 int scan_scope=0; // Only explicitly selected drives, or one manually selected folder.
 int scope_view=0;std::vector<DriveChoice> selected_drives;
 bool has_full_scan=false;dc::FullScan all_scan;dc::Observations observations;
 size_t observation_volume=0,focused_volume=0;
 bool exact_ready=false,suggestions_ready=false,structure_ready=false,restoring_selection=false,close_requested=false,results_stale=false;
 dc::Duplicates hash_duplicates;
 int deferred_command=0;std::vector<fs::path> deferred_paths;
 Task active_task=Task::Scan;
 std::mutex progress_mutex;dc::ScanProgress progress_snapshot{};std::wstring progress_message;
 ULONGLONG job_started=0,job_finished=0,last_progress_tick=0,speed_tick=0;
 dc::U64 speed_bytes=0,speed_files=0;double recent_speed=0,recent_file_rate=0;
 size_t speed_root=0;dc::ScanPhase speed_phase=dc::ScanPhase::Enumerating;
 bool progress_marquee=false,task_issue=false,scope_controls_open=false;
 HWND btn_apply=nullptr;
 HWND welcome_title=nullptr,welcome_detail=nullptr,choose_folder=nullptr,choose_disks=nullptr;
 HWND clear_filter=nullptr,duplicate_view=nullptr,btn_inventory=nullptr;
 std::optional<dc::InventorySummary> quick_result;
 HFONT font=nullptr,bold_font=nullptr,display_font=nullptr,small_font=nullptr;HBRUSH page_brush=nullptr;
 fs::path root,selected_dir;
 dc::Inventory inv;dc::Duplicates dup;std::vector<dc::DirGroup> dupdirs;dc::OverlapListing overlaps;dc::PhotoReport photos;std::vector<dc::MergeSuggestion> suggestions;dc::DirectoryPairReport pair_report;dc::StructureSuggestion structure_suggestion;dc::StructureSuggestion previous_file_edit;bool file_edit_in_progress=false;bool structure_rejected=false;bool has_comparison=false,photos_ready=false,relations_ready=false,has_scan=false;
 dc::Plan pending;dc::RetirementProposal retirement_summary;dc::MergePlan pending_merge;dc::StructurePlan pending_structure;fs::path requested_source,requested_destination,requested_retirement;bool pending_is_merge=false,pending_is_structure=false;dc::ArchiveOptions archive_request;bool has_pending=false;std::thread worker;std::atomic_bool cancel{false};bool busy=false,task_cancelable=false;
 dc::RetentionPolicy retention_policy{};
 std::vector<fs::path> requested_files;dc::RenameOptions rename_request;
 int view=0,sort_column=2,photo_distance=7;bool sort_desc=true;
 std::vector<Row> rows;std::vector<size_t> visible;
 std::set<std::wstring> saved_selection_keys;bool rebuilding_rows=false,filter_pending=false,source_filtered=false;

 fs::path user_settings_path(){
     std::wstring buf(32768,L'\0');
     DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",&buf[0],DWORD(buf.size()));
     if(n>0&&n<buf.size())return fs::path(buf.substr(0,n))/L"DiskCurator"/L"settings.ini";
     return fs::temp_directory_path()/L"DiskCurator"/L"settings.ini";
 }
 void load_retention(){
     try{
         const auto path=user_settings_path();
         int copies=GetPrivateProfileIntW(L"Retention",L"MinActiveCopies",2,path.c_str());
         if(copies>=1&&copies<=32)retention_policy.min_active_copies=unsigned(copies);
         retention_policy.distinct_file_objects=GetPrivateProfileIntW(L"Retention",L"DistinctFiles",1,path.c_str())!=0;
         retention_policy.verify_bytes_before_change=GetPrivateProfileIntW(L"Retention",L"FullByteRecheck",1,path.c_str())!=0;
     }catch(const std::exception&){} // Disposable preference; default remains safe.
 }
 void save_retention(){
     try{
         const auto path=user_settings_path();fs::create_directories(path.parent_path());
         const auto count=std::to_wstring(retention_policy.min_active_copies);
         WritePrivateProfileStringW(L"Retention",L"MinActiveCopies",count.c_str(),path.c_str());
         WritePrivateProfileStringW(L"Retention",L"DistinctFiles",
                                    retention_policy.distinct_file_objects?L"1":L"0",path.c_str());
         WritePrivateProfileStringW(L"Retention",L"FullByteRecheck",
                                    retention_policy.verify_bytes_before_change?L"1":L"0",path.c_str());
     }catch(const std::exception&){}
 }
 bool ask_retention(const std::wstring& title){

     std::vector<Prompt::Field> fields={
         {L"至少保留的份数（0～32）",std::to_wstring(retention_policy.min_active_copies)},
         {L"只计算独立文件（硬链接算一份，推荐）",
             retention_policy.distinct_file_objects?L"1":L"0",true},
         {L"隔离前再次比较文件内容（推荐）",
             retention_policy.verify_bytes_before_change?L"1":L"0",true},
         {L"明确允许零份活动副本，仅依赖隔离区恢复（危险；必须勾选才能填0）",L"0",true}
     };
     fields[0].number_range={{0,32}};
     auto check_zero=[](const std::vector<Prompt::Field>& inputs)->std::optional<Prompt::InputError>{
         if(std::stoi(inputs[0].value)==0 && inputs[3].value!=L"1")
             return Prompt::InputError{3,L"保留 0 份需勾选同意。你也可以将保留份数改为 1 或更多。"};
         return {};
     };
     if(!prompt(hwnd,title,fields,font,L"确定",check_zero))return false;
     auto proposed=retention_policy;
     try{
         const auto count=std::stoi(fields[0].value);
         proposed.min_active_copies=static_cast<unsigned>(count);
         proposed.distinct_file_objects=fields[1].value!=L"0";
         proposed.verify_bytes_before_change=fields[2].value!=L"0";
         proposed.allow_zero_active=count==0&&fields[3].value!=L"0";
         if(!proposed.verify_bytes_before_change &&
            MessageBoxW(hwnd,L"快速验证只相信本次扫描得到的SHA-256和文件大小/修改时间。\n如文件被更改后伪装成原时间和大小，可能无法发现。\n\n确定使用快速模式？",
                L"降低文件内容复核等级",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return false;
         if(proposed.allow_zero_active &&
            MessageBoxW(hwnd,L"0份保留者意味着操作后活动目录中可以没有该内容的任何文件。\n虽然当前仅移入可撤销隔离区，但如果隔离区丢失，数据可能无法恢复。\n\n确定采用0份策略？",
                L"高风险保留策略",MB_YESNO|MB_ICONWARNING|MB_DEFBUTTON2)!=IDYES)return false;
         retention_policy=proposed;
         save_retention();
         return true;
     }catch(const std::exception& e){alert(hwnd,L"无效的副本保留设置："+wutf8(e.what()),MB_ICONWARNING);return false;}
 }
 bool showing_duplicate_files() const {
     return full_overview()?scope_view==3:has_scan&&view==2;
 }
 bool showing_files() const {
     return full_overview()?(scope_view==1||scope_view==3):has_scan&&(view==0||view==2);
 }
 bool can_verify_duplicate_files() const {
     return has_scan&&view==2&&!results_stale&&!exact_ready&&inv.complete()&&!hash_duplicates.groups.empty();
 }
 void duplicate_shortcut(){
     if(!has_scan&&!full_overview())return;
     if(can_verify_duplicate_files()){
         run_action(CMD_EXACT);
         return;
     }
     const bool back=showing_duplicate_files();
     if(full_overview()){
         SendMessageW(lens_pick,CB_SETCURSEL,back?1:3,0);
         SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(ID_LENS_PICK,CBN_SELCHANGE),(LPARAM)lens_pick);
     }else change_view(back?0:2);
     SetFocus(list);
 }
 void sync_lens(){
     if(!lens_pick)return;
     const bool scope=full_overview();
     const auto* labels=scope?scope_lenses:tab_names;
     const int count=scope?int(std::size(scope_lenses)):int(std::size(tab_names));
     if(SendMessageW(lens_pick,CB_GETCOUNT,0,0)!=count){
         SendMessageW(lens_pick,CB_RESETCONTENT,0,0);
         for(int i=0;i<count;++i)SendMessageW(lens_pick,CB_ADDSTRING,0,(LPARAM)labels[i]);
     }
     SendMessageW(lens_pick,CB_SETCURSEL,scope?scope_view:view,0);
     ShowWindow(lens_pick,(scope||has_scan)?SW_SHOW:SW_HIDE);
     const wchar_t* action=L"重复文件";
     if(showing_duplicate_files())action=can_verify_duplicate_files()?L"核验副本":L"返回文件";
     SetWindowTextW(duplicate_view,action);
 }
 std::wstring cache_warning() const {
     size_t failed=has_scan&&!inv.index_error.empty()?1:0;
     if(has_full_scan)for(size_t i=0;i<all_scan.volumes.size();++i){
         if((!has_scan||i!=focused_volume)&&!all_scan.volumes[i].index_error.empty())++failed;
     }
     return failed?L"索引未持久化（"+std::to_wstring(failed)+L" 个根）；内存结果可用":L"";
 }
 void status_text(const std::wstring& message){
     const auto warning=cache_warning();
     const auto text=warning.empty()?message:warning+L" · "+message;
     SetWindowTextW(status,text.c_str());
 }
 void reset_progress(){
     if(!scan_bar)return;
     job_finished=0;task_issue=false;quick_result.reset();
     SendMessageW(scan_bar,PBM_SETMARQUEE,FALSE,0);SendMessageW(scan_bar,PBM_SETPOS,0,0);progress_marquee=false;
     SetWindowTextW(progress_stage,L"等待扫描 · 请确认范围后开始");
     SetWindowTextW(progress_metrics,L"统计完成后显示容量、速度、已用时间和预计剩余时间。");
     SetWindowTextW(progress_path,L"仅扫描指定范围；内容核验和主题分析在结果中按需进行。");
 }
 void set_busy(bool enabled,bool can_cancel=false){
     busy=enabled;task_cancelable=enabled&&can_cancel;
     EnableWindow(btn_scan,!enabled);EnableWindow(btn_browse,!enabled);EnableWindow(root_box,!enabled&&scan_scope==1);
     EnableWindow(scope_pick,!enabled);EnableWindow(lens_pick,!enabled);EnableWindow(search,!enabled);EnableWindow(list,!enabled);EnableWindow(btn_stop,enabled&&can_cancel);
     refresh_navigation();
 }
 bool full_overview() const {return has_full_scan&&!has_scan;}
 void reset_inventory(){
     clear_analysis();
     has_scan=false;
     has_full_scan=false;
     results_stale=false;
     inv={};
     all_scan={};
     observations={};
     hash_duplicates={};
     scope_view=0;
 }
 void switch_scan_scope(int next){
     if(busy||next<0||next>1||scan_scope==next)return;
     if(has_pending && MessageBoxW(hwnd,L"切换扫描模式将放弃尚未执行的操作预览。继续吗？",
          L"切换扫描模式",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES){
         SendMessageW(scope_pick,CB_SETCURSEL,scan_scope,0);return;
     }
     reset_inventory();
     scan_scope=next;
     root.clear();
     view=12;
     EnableWindow(root_box,next==1);EnableWindow(btn_browse,TRUE);
     if(next==1)SetWindowTextW(root_box,L"");
     SetWindowTextW(btn_browse,next==0?L"选择磁盘…":L"浏览…");
     SendMessageW(scope_pick,CB_SETCURSEL,next,0);
     if(next==0){std::wstring summary;for(const auto& drive:selected_drives){if(!summary.empty())summary+=L"、";summary+=drive.root.wstring();}
         SetWindowTextW(root_box,summary.empty()?L"点击‘选择磁盘’指定本次扫描范围":summary.c_str());}
     reset_progress();

     refresh();refresh_navigation();
 }
 void choose_drives() {
     if(busy||scan_scope!=0)return;
     std::vector<std::string> issues;
     auto found=list_drives(issues);
     if(found.empty()){alert(hwnd,L"未检测到可读取的本地磁盘。请确认移动硬盘已连接。",MB_ICONWARNING);return;}
     std::vector<Prompt::Field> fields;
     for(const auto& drive:found) {
         const bool checked=std::any_of(selected_drives.begin(),selected_drives.end(),
             [&](const DriveChoice& old){return low(old.root.wstring())==low(drive.root.wstring())&&same_drive(old);});
         fields.push_back({drive_description(drive),checked?L"1":L"0",true});
     }
     if(!prompt(hwnd,L"选择本次需要扫描的磁盘（可以多选）",fields,font))return;
     std::vector<DriveChoice> chosen;
     for(size_t i=0;i<found.size();++i)if(fields[i].value==L"1")chosen.push_back(found[i]);
     if(chosen.empty()){alert(hwnd,L"没有选中磁盘。不会开始扫描。",MB_ICONINFORMATION);return;}
     if(has_pending&&MessageBoxW(hwnd,L"更改扫描范围将取消尚未执行的预览，确定吗？",
             L"更改扫描范围",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return;
     selected_drives=std::move(chosen);
     reset_inventory();
     reset_progress();
     root.clear();
     std::wstring summary;
     for(const auto& drive:selected_drives){if(!summary.empty())summary+=L"、";summary+=drive.root.wstring();}
     SetWindowTextW(root_box,summary.c_str());
     scope_view=0;
     if(!issues.empty())status_text(L"部分磁盘未就绪，未列入可选范围；本次只扫描已勾选的磁盘。");
     refresh();refresh_navigation();SetFocus(btn_scan);
 }
 void invalidate_pending(){
     if(has_pending&&!busy){
         SetWindowTextW(progress_stage,L"上次预览已失效");
         SetWindowTextW(progress_metrics,L"");
         SetWindowTextW(progress_path,L"确认扫描范围和选项后，可重新生成预览。");
     }
     has_pending=false;pending={};pending_merge={};pending_structure={};pending_is_merge=false;pending_is_structure=false;
     retirement_summary={};refresh_navigation();
 }
 void clear_analysis(){
     exact_ready=false;suggestions_ready=false;structure_ready=false;
     dup={};dupdirs.clear();suggestions.clear();overlaps={};
     photos={};photos_ready=false;relations_ready=false;has_comparison=false;pair_report={};
     structure_suggestion={};structure_rejected=false;
     has_pending=false;pending={};pending_merge={};pending_structure={};pending_is_merge=false;pending_is_structure=false;
     retirement_summary={};selected_dir.clear();deferred_command=0;deferred_paths.clear();
 }
 static dc::Duplicates hash_groups(const dc::Inventory& inventory){
     std::map<std::pair<dc::U64,std::string>,std::vector<size_t>> grouped;
     for(size_t i=0;i<inventory.files.size();++i){const auto& f=inventory.files[i];
         if(f.sha256.size()==64)grouped[{f.bytes,f.sha256}].push_back(i);}
     dc::Duplicates result;
     for(auto& entry:grouped)if(entry.second.size()>1){result.wasted+=entry.first.first*(entry.second.size()-1);result.groups.push_back(std::move(entry.second));}
     std::sort(result.groups.begin(),result.groups.end(),[&](const auto& a,const auto& b){
         return inventory.files[a[0]].bytes*(a.size()-1)>inventory.files[b[0]].bytes*(b.size()-1);});
     return result;
 }
 void focus_volume(size_t index){
     if(busy||!full_overview()||index>=all_scan.volumes.size())return;
     const auto& volume_root=all_scan.volumes[index].root;
     const auto drive=std::find_if(selected_drives.begin(),selected_drives.end(),
         [&](const DriveChoice& d){return low(d.root.wstring())==low(volume_root.wstring());});
     if(drive==selected_drives.end()||!same_drive(*drive)){
         alert(hwnd,L"磁盘已断开或卷身份发生变化，请重新选择并扫描。",MB_ICONWARNING);return;}
     clear_analysis();focused_volume=index;observation_volume=index;
     inv=std::move(all_scan.volumes[index]);root=inv.root;has_scan=true;
     hash_duplicates={};
     for(const auto& group:all_scan.hash_candidates){std::vector<size_t> local;
         for(const auto& member:group.members)if(member.volume==index)local.push_back(member.file);
         if(local.size()>1){hash_duplicates.wasted+=group.bytes*(local.size()-1);hash_duplicates.groups.push_back(std::move(local));}}
     change_view(scope_view==2?1:scope_view==3?2:0);
 }
 void return_to_all_results(){
     if(busy||!has_full_scan||!has_scan)return;
     if(has_pending&&MessageBoxW(hwnd,L"返回全部结果将放弃尚未执行的预览。继续吗？",L"返回扫描结果",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return;
     all_scan.volumes[focused_volume]=std::move(inv);has_scan=false;root.clear();clear_analysis();
     scope_view=0;SetWindowTextW(search,L"");refresh();refresh_navigation();
 }
 std::vector<fs::path> selected_paths()const{
     std::vector<fs::path> result;
     for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;)
         if(size_t(i)<visible.size()){
             const auto& row=rows[visible[size_t(i)]];
             if(!row.launch.empty()||(view==1&&row.a==L"(扫描根目录)"))result.push_back(row.launch);
         }
     return result;
 }
 void restore_paths(const std::vector<fs::path>& paths){
     restoring_selection=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
     std::set<fs::path> selected(paths.begin(),paths.end());
     for(int i=0;i<int(visible.size());++i)if(selected.count(rows[visible[size_t(i)]].launch)&&(!rows[visible[size_t(i)]].launch.empty()||(view==1&&rows[visible[size_t(i)]].a==L"(扫描根目录)")))
         ListView_SetItemState(list,i,LVIS_SELECTED,LVIS_SELECTED);
     restoring_selection=false;refresh_selection();
 }
 bool ensure_result_scope(){
     if(has_scan)return true;
     if(!full_overview())return false;
     int chosen=-1;std::vector<fs::path> paths;
     for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;){
         if(size_t(i)>=visible.size())continue;
         const auto& row=rows[visible[size_t(i)]];int v=row.suggestion-3000;
         if(v<0||size_t(v)>=all_scan.volumes.size())continue;
         if(chosen>=0&&chosen!=v){alert(hwnd,L"本次操作请选择同一个磁盘中的项目。跨磁盘结果仍可一起观察。",MB_ICONINFORMATION);return false;}
         chosen=v;paths.push_back(row.launch);
     }
     if(chosen<0&&all_scan.volumes.size()==1)chosen=0;
     if(chosen<0){alert(hwnd,L"请先在结果中选择要操作的磁盘、文件或目录。",MB_ICONINFORMATION);return false;}
     focus_volume(size_t(chosen));if(!has_scan)return false;restore_paths(paths);return true;
 }
 void refresh_selection(){
     if(!selection_context||restoring_selection)return;
     size_t count=0;dc::U64 total=0;fs::path first;
     for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;)if(size_t(i)<visible.size()){
         const auto& row=rows[visible[size_t(i)]];++count;total+=row.count;if(count==1)first=row.launch;}
     if(has_scan&&count==1){
         if(view==1||view==3)selected_dir=first;
         else if(view==0||view==2)selected_dir=first.parent_path();
     }else selected_dir.clear();
     std::wstring text=count?L"已选 "+std::to_wstring(count)+L" 项":L"选择结果后操作 · Ctrl / Shift 多选";
     if(!count&&can_verify_duplicate_files())text=L"这些文件的大小和哈希相同。点“核验副本”可逐字节比较当前范围的候选文件。";
     if(count&&(view<=3||full_overview()))text+=L" · 所选行容量 "+bytes(total);
     if(results_stale)text=L"结果已过期 · 仅供参考，请重新扫描 · "+text;
     SetWindowTextW(selection_context,text.c_str());
 }
 void refresh_navigation(){
     if(!btn_basic)return;
     sync_lens();
     const bool ready=has_scan||full_overview();
     const bool welcome=!busy&&!ready;
     const bool range_set=scan_scope==0?!selected_drives.empty():GetWindowTextLengthW(root_box)>0;
     if(range_set)scope_controls_open=true;
     for(HWND h:{scope_pick,root_box,btn_browse})ShowWindow(h,scope_controls_open?SW_SHOW:SW_HIDE);
     for(HWND h:{welcome_title,welcome_detail})ShowWindow(h,welcome?SW_SHOW:SW_HIDE);
     for(HWND h:{choose_folder,choose_disks})ShowWindow(h,welcome&&!scope_controls_open?SW_SHOW:SW_HIDE);
     if(welcome){
         SetWindowTextW(welcome_title,range_set?L"准备扫描":L"选择要检查的位置");
         const wchar_t* detail=L"从一个文件夹开始，或选择多个磁盘一起检查。扫描只读取文件。";
         if(range_set)detail=L"快速盘点只统计数量和大小。要查重复文件，点“开始扫描”。";
         else if(scope_controls_open)detail=scan_scope==1?L"在上方输入文件夹路径，或点“浏览…”选择。":L"在上方选择要扫描的磁盘。";
         if(quick_result){
             const auto& s=*quick_result;
             SetWindowTextW(welcome_title,s.error_count()?L"快速盘点结束 · 部分内容未计入":L"快速盘点完成");
             const auto counts=std::to_wstring(s.file_count)+L" 个文件 · "+std::to_wstring(s.directory_count)+L" 个目录 · "+bytes(s.bytes);
             SetWindowTextW(welcome_detail,counts.c_str());
         }else SetWindowTextW(welcome_detail,detail);
     }
     ShowWindow(btn_scan,scope_controls_open&&!busy?SW_SHOW:SW_HIDE);
     ShowWindow(btn_inventory,welcome&&range_set?SW_SHOW:SW_HIDE);
     EnableWindow(btn_inventory,welcome&&range_set);
     ShowWindow(btn_stop,busy&&task_cancelable?SW_SHOW:SW_HIDE);
     EnableWindow(btn_scan,!busy&&range_set);
     SetWindowTextW(btn_scan,ready?L"重新扫描":L"开始扫描");
     for(HWND h:{search,clear_filter,duplicate_view,btn_basic,btn_theme,btn_smart,btn_history,list,selection_context})
         ShowWindow(h,ready?SW_SHOW:SW_HIDE);
     ShowWindow(status,welcome&&!job_finished?SW_HIDE:SW_SHOW);
     for(HWND h:{progress_stage,progress_metrics})ShowWindow(h,(!welcome||job_finished)&&!quick_result?SW_SHOW:SW_HIDE);
     ShowWindow(scan_bar,busy?SW_SHOW:SW_HIDE);
     ShowWindow(progress_path,show_progress_detail()?SW_SHOW:SW_HIDE);
     EnableWindow(clear_filter,ready&&!busy&&GetWindowTextLengthW(search)>0);
     EnableWindow(duplicate_view,ready&&!busy);
     for(HWND h:{btn_basic,btn_history})EnableWindow(h,!busy&&ready);
     for(HWND h:{btn_theme,btn_smart,btn_preview})EnableWindow(h,!busy&&ready&&!results_stale);
     const bool reviewing=has_scan&&view==9;
     ShowWindow(btn_preview,ready&&!reviewing?SW_SHOW:SW_HIDE);
     EnableWindow(btn_apply,!busy&&has_pending);ShowWindow(btn_apply,reviewing&&has_pending?SW_SHOW:SW_HIDE);
     ShowWindow(btn_all_results,has_full_scan&&has_scan&&(selected_drives.size()>1||!all_scan.errors.empty())?SW_SHOW:SW_HIDE);
     refresh_selection();
     RECT client{};GetClientRect(hwnd,&client);layout(client.right,client.bottom,false);
     InvalidateRect(hwnd,nullptr,FALSE);
 }
 bool show_progress_detail() const {
     return busy||task_issue||results_stale||(has_scan&&view==9);
 }
 static std::wstring duration_text(double seconds){
     if(seconds<0)return L"估算中";
     const auto n=static_cast<unsigned long long>(seconds+0.5);
     if(n>=3600)return std::to_wstring(n/3600)+L" 小时 "+std::to_wstring((n%3600)/60)+L" 分";
     if(n>=60)return std::to_wstring(n/60)+L" 分 "+std::to_wstring(n%60)+L" 秒";
     return std::to_wstring(n)+L" 秒";
 }
 void record_progress(const dc::ScanProgress& snapshot){
     std::lock_guard<std::mutex> lock(progress_mutex);
     if(snapshot.phase!=progress_snapshot.phase||snapshot.root_index!=progress_snapshot.root_index||
        snapshot.bytes_read!=progress_snapshot.bytes_read||snapshot.files_completed!=progress_snapshot.files_completed||
        snapshot.files_discovered!=progress_snapshot.files_discovered)last_progress_tick=GetTickCount64();
     progress_snapshot=snapshot;
 }
 void record_message(const std::string& message){
     std::lock_guard<std::mutex> lock(progress_mutex);progress_message=wutf8(message);
 }
 void render_progress(){
     if(!scan_bar||!busy)return;
     dc::ScanProgress snapshot;std::wstring message;ULONGLONG updated;
     {std::lock_guard<std::mutex> lock(progress_mutex);snapshot=progress_snapshot;message=progress_message;updated=last_progress_tick;}
     const auto now=GetTickCount64();const auto elapsed=double(now-job_started)/1000.0;
     const bool scanning=active_task==Task::Scan||active_task==Task::ScanAll||active_task==Task::Inventory;
     const bool quantified=(snapshot.phase==dc::ScanPhase::Hashing||snapshot.phase==dc::ScanPhase::Verifying)&&snapshot.totals_known;
     const bool marquee=!quantified;
     if(marquee!=progress_marquee){
         auto style=GetWindowLongPtrW(scan_bar,GWL_STYLE);
         SetWindowLongPtrW(scan_bar,GWL_STYLE,marquee?style|PBS_MARQUEE:style&~PBS_MARQUEE);
         SendMessageW(scan_bar,PBM_SETMARQUEE,marquee,45);progress_marquee=marquee;
     }
     std::wstring stage;
     switch(snapshot.phase){
       case dc::ScanPhase::Enumerating:stage=scanning?L"正在只读统计文件与容量":L"准备分析";break;
       case dc::ScanPhase::Hashing:stage=L"读取文件并计算 SHA-256";break;
       case dc::ScanPhase::Verifying:stage=L"按需逐字节核验相同内容";break;
       case dc::ScanPhase::SavingIndex:stage=L"保存扫描索引";break;
       case dc::ScanPhase::Analyzing:stage=L"汇总扫描结果";break;
       default:stage=L"完成当前阶段";break;
     }
     if(active_task==Task::Inventory)stage=L"快速盘点 · 只读统计";
     if(!scanning&&active_task!=Task::Verify){
         stage=active_task==Task::Pair?L"评估所选两个目录":active_task==Task::MergePlan?L"生成所选目录的融合预览":active_task==Task::Suggest?L"分析目录融合建议":active_task==Task::Structure?L"生成目录重组映射":
             active_task==Task::Relations?L"分析相似目录":active_task==Task::Photo?L"分析相似照片":
             (active_task==Task::Apply||active_task==Task::MergeApply||active_task==Task::StructureApply||active_task==Task::Undo)?L"执行已确认的操作":L"生成操作预览";
     }
     if(snapshot.root_count>1)stage+=L" · 磁盘 "+std::to_wstring(snapshot.root_index)+L" / "+std::to_wstring(snapshot.root_count);
     if(quantified){
         double fraction=snapshot.total_bytes?std::min(1.0,double(snapshot.bytes_processed)/double(snapshot.total_bytes)):
             (snapshot.files_discovered?double(snapshot.files_completed)/double(snapshot.files_discovered):1.0);
         if(snapshot.files_completed<snapshot.files_discovered)fraction=std::min(fraction,0.999);
         SendMessageW(scan_bar,PBM_SETPOS,WPARAM(fraction*1000.0),0);
         std::wostringstream out;out<<std::fixed<<std::setprecision(1)<<fraction*100.0;stage+=L" · "+out.str()+L"%";
     }
     if(cancel.load())stage=L"正在停止 · 等待当前读取返回";
     SetWindowTextW(progress_stage,stage.c_str());
     if(snapshot.phase!=speed_phase||snapshot.root_index!=speed_root||snapshot.bytes_read<speed_bytes){
         speed_phase=snapshot.phase;speed_root=snapshot.root_index;speed_bytes=snapshot.bytes_read;speed_files=snapshot.files_completed;speed_tick=now;recent_speed=0;recent_file_rate=0;
     }else if(now>speed_tick+750){
         const double rate=double(snapshot.bytes_read-speed_bytes)*1000.0/double(now-speed_tick);
         recent_speed=recent_speed==0?rate:recent_speed*0.7+rate*0.3;
         const double file_rate=double(snapshot.files_completed-speed_files)*1000.0/double(now-speed_tick);
         recent_file_rate=recent_file_rate==0?file_rate:recent_file_rate*0.7+file_rate*0.3;
         speed_bytes=snapshot.bytes_read;speed_files=snapshot.files_completed;speed_tick=now;
     }
     std::wstring metrics;
     if(quantified){
         metrics=L"已处理 "+bytes(snapshot.bytes_processed)+L" / "+bytes(snapshot.total_bytes)+L" · 文件 "+
             std::to_wstring(snapshot.files_completed)+L" / "+std::to_wstring(snapshot.files_discovered);
         metrics+=L" · "+bytes(static_cast<dc::U64>(std::max(0.0,recent_speed)))+L"/s";
         const auto remaining_bytes=snapshot.total_bytes-std::min(snapshot.total_bytes,snapshot.bytes_processed);
         const auto remaining_files=snapshot.files_discovered-std::min(snapshot.files_discovered,snapshot.files_completed);
         double left=-1.0;
         if(elapsed>=3&&now-updated<15000){
             if(remaining_bytes&&recent_speed>1)left=double(remaining_bytes)/recent_speed;
             if(remaining_files&&recent_file_rate>0.01)left=std::max(left,double(remaining_files)/recent_file_rate);
             if(!remaining_files&&!remaining_bytes)left=0;
         }
         metrics+=(snapshot.root_count>1?L" · 本盘预计剩余 ":L" · 本阶段预计剩余 ")+duration_text(left);
     }else if(scanning&&snapshot.phase==dc::ScanPhase::Enumerating){
         metrics=L"已发现 "+std::to_wstring(snapshot.files_discovered)+L" 个文件 · 合计 "+bytes(snapshot.total_bytes);
     }else if(snapshot.phase==dc::ScanPhase::Verifying){
         metrics=L"已读取 "+bytes(snapshot.bytes_read)+L" · 已处理文件 "+std::to_wstring(snapshot.files_completed)+
             L" / "+std::to_wstring(snapshot.files_discovered)+L" · 预计剩余：总读取量尚未确定";
     }else metrics=L"此阶段总工作量未确定";
     metrics+=L" · 已用 "+duration_text(elapsed);
     if(snapshot.errors)metrics+=L" · 读取异常 "+std::to_wstring(snapshot.errors);
     SetWindowTextW(progress_metrics,metrics.c_str());
     std::wstring current=snapshot.current_file.empty()?message:L"当前："+fmt(snapshot.root/snapshot.current_file);
     if(now-updated>=15000&&quantified)current=L"连续 "+duration_text(double(now-updated)/1000.0)+L" 未收到新读取进度 · "+current;
     SetWindowTextW(progress_path,current.c_str());
 }
 size_t scan_issue_count() const {
     if(has_scan)return inv.errors.size();
     if(!full_overview())return 0;
     size_t count=all_scan.errors.size();
     for(const auto& volume:all_scan.volumes)count+=volume.errors.size();
     return count;
 }
 void finish_progress(bool success,bool stopped=false){
     task_issue=!success;
     SendMessageW(scan_bar,PBM_SETMARQUEE,FALSE,0);progress_marquee=false;
     SetWindowLongPtrW(scan_bar,GWL_STYLE,GetWindowLongPtrW(scan_bar,GWL_STYLE)&~PBS_MARQUEE);
     const bool scanning=active_task==Task::Scan||active_task==Task::ScanAll;
     const size_t errors=scanning?scan_issue_count():0;
     const wchar_t* stage=L"本次任务未完成";
     if(success){
         if(scanning)stage=errors?L"扫描结束 · 存在读取异常，结果不完整":L"扫描完成";
         else stage=active_task==Task::Verify?L"副本核验完成":L"任务完成";
     }else if(stopped)stage=(scanning||active_task==Task::Inventory)?L"已停止 · 可重新扫描":L"分析已停止 · 扫描结果已保留";
     SetWindowTextW(progress_stage,stage);
     if(success)SendMessageW(scan_bar,PBM_SETPOS,1000,0);
     std::wstring summary=L"已用 "+duration_text(double((job_finished?job_finished:GetTickCount64())-job_started)/1000.0);
     if(success&&scanning){const auto total=has_scan?inv.bytes:all_scan.total_bytes;const auto files=has_scan?inv.files.size():all_scan.total_files;
         summary+=L" · "+std::to_wstring(files)+L" 个文件 · "+bytes(total);
         if(!errors)summary+=L" · SHA-256 全内容扫描";}
     if(success&&active_task==Task::Verify)summary+=L" · "+std::to_wstring(dup.groups.size())+L" 组文件已逐字节核验为相同内容";
     if(errors)summary+=L" · "+std::to_wstring(errors)+L" 项异常";
     SetWindowTextW(progress_metrics,summary.c_str());
     SetWindowTextW(progress_path,success?L"":stopped?L"可以调整范围，然后重新尝试。":L"请查看任务提示后重试。");
     refresh_navigation();
 }
 void layout(int W,int H,bool resize_columns=true){
     const int margin=W<980?14:24;
     const int right=W-margin, width=W-2*margin, gap=8;
     const int scope_width=W<980?126:158;
     const bool ready=has_scan||full_overview();
     const int scope_x=margin+kCompactTitleWidth+gap;
     const int root_x=scope_x+scope_width+gap;
     const int action_width=busy?(task_cancelable?72:0):(ready?100:0);
     const int browse_x=right-(action_width?action_width+gap:0)-84;
     move(scope_pick,scope_x,13,scope_width,190);
     move(root_box,root_x,13,browse_x-gap-root_x,32);
     move(btn_browse,browse_x,13,84,32);
     if(ready)move(btn_scan,right-100,13,100,32);
     move(btn_stop,right-72,13,72,32);

     int y=68;
     if(!busy&&W>=980){
         const int stage_width=std::min(400,width/3);
         move(progress_stage,margin,y,stage_width,24);
         move(progress_metrics,margin+stage_width+gap,y+1,width-stage_width-gap,23);
         y+=28;
     }else{
         move(progress_stage,margin,y,width,24);y+=28;
         if(busy){move(scan_bar,margin,y,width,10);y+=16;}
         move(progress_metrics,margin,y,width,22);y+=24;
     }
     if(show_progress_detail()){move(progress_path,margin,y,width,22);y+=24;}
     y+=4;
     const int lens_width=W<980?154:182;
     const int duplicate_width=100, clear_width=84;
     const int filter_x=margin+lens_width+duplicate_width+2*gap;
     move(lens_pick,margin,y,lens_width,360);
     move(duplicate_view,margin+lens_width+gap,y,duplicate_width,32);
     move(search,filter_x,y,right-clear_width-gap-filter_x,32);
     move(clear_filter,right-clear_width,y,clear_width,32);
     y+=40;
     const int menu_width=W<980?86:110;
     move(btn_basic,margin,y,menu_width,32);
     move(btn_theme,margin+menu_width+gap,y,menu_width,32);
     move(btn_smart,margin+2*(menu_width+gap),y,menu_width,32);
     move(btn_all_results,margin+3*(menu_width+gap),y,104,32);
     move(btn_history,right-86,y,86,32);
     move(btn_preview,right-2*86-gap,y,86,32);
     move(btn_apply,right-86-gap-100,y,100,32);y+=39;
     move(selection_context,margin,y,width,23);y+=26;
     move(list,margin,y,width,std::max(80,H-y-41));
     if(resize_columns){
         const int available=width-GetSystemMetrics(SM_CXVSCROLL)-6;
         ListView_SetColumnWidth(list,0,available*21/100);
         ListView_SetColumnWidth(list,1,available*33/100);
         ListView_SetColumnWidth(list,2,available*13/100);
         ListView_SetColumnWidth(list,3,available-available*21/100-available*33/100-available*13/100);
     }
     move(status,margin,H-32,width,23);
     const int welcome_y=std::max(140,(H-132)/2);
     move(welcome_title,margin,welcome_y,width,40);
     move(welcome_detail,margin,welcome_y+45,width,28);
     move(choose_folder,W/2-150,welcome_y+88,144,38);
     move(choose_disks,W/2+6,welcome_y+88,144,38);
     if(!ready&&!busy){
         move(btn_scan,W/2-150,welcome_y+88,144,38);
         move(btn_inventory,W/2+6,welcome_y+88,144,38);
     }
 }
 static void solid(HDC dc,RECT r,COLORREF c){HBRUSH b=CreateSolidBrush(c);FillRect(dc,&r,b);DeleteObject(b);}
 static void label_at(HDC dc,HFONT face,const std::wstring& text,int x,int y,int width,int height,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){
     auto old=SelectObject(dc,face);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);RECT rc{x,y,x+width,y+height};
     DrawTextW(dc,text.c_str(),-1,&rc,flags);SelectObject(dc,old);
 }
 void paint(HDC dc){
     RECT rc{};GetClientRect(hwnd,&rc);const int W=rc.right,H=rc.bottom;
     const int margin=W<980?14:24;
     solid(dc,rc,kPage);solid(dc,RECT{0,0,W,58},kNavy);
     label_at(dc,scope_controls_open?bold_font:display_font,kProductName,margin,10,
              scope_controls_open?kCompactTitleWidth:W-2*margin,38,RGB(248,250,252));
     if(has_scan||full_overview())solid(dc,RECT{margin,H-39,W-margin,H-38},RGB(205,216,227));
 }
 void change_view(int n,bool reset_view=false){
     if(n<0||n>=int(std::size(tab_names)))return;
     if(n!=view||reset_view){
         restoring_selection=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);restoring_selection=false;
         sort_column=n==7?0:2;sort_desc=n!=5;
         if(GetWindowTextLengthW(search))SetWindowTextW(search,L"");
         if(filter_pending){KillTimer(hwnd,2);filter_pending=false;}
     }
     view=n;refresh();refresh_navigation();
     if(!busy&&!task_issue&&has_scan&&(pending_is_structure||pending_is_merge||!pending.name.empty()))show_plan_status();
     if(reset_view&&!visible.empty())ListView_EnsureVisible(list,0,FALSE);
 }
 void open_scope_selected(){
     int index=ListView_GetNextItem(list,-1,LVNI_SELECTED);if(index<0||size_t(index)>=visible.size())return;
     const Row& row=rows[visible[size_t(index)]];const int v=row.suggestion-3000;
     if(v<0||size_t(v)>=all_scan.volumes.size())return;
     reveal_in_explorer(all_scan.volumes[size_t(v)].root/row.launch);
 }
 std::wstring row_key(const Row& row)const{
     return row.a+L"\n"+row.b+L"\n"+row.launch.generic_wstring()+L"\n"+std::to_wstring(row.suggestion);
 }
 std::set<std::wstring> selection_keys()const{
     std::set<std::wstring> keys;
     for(int i=-1;(i=ListView_GetNextItem(list,i,LVNI_SELECTED))>=0;)
         if(size_t(i)<visible.size()&&visible[size_t(i)]<rows.size())keys.insert(row_key(rows[visible[size_t(i)]]));
     return keys;
 }
 void append_scan_issue(const std::string& error,const fs::path& volume){
     constexpr char reparse[]="Skipped reparse point: ";
     constexpr char directory[]="Cannot list ";
     if(error.rfind(reparse,0)==0){
         rows.push_back({L"已跳过重解析点",wutf8(error.substr(sizeof(reparse)-1)),fmt(volume),L"未访问此路径指向的内容"});
     }else if(error.rfind(directory,0)==0){
         rows.push_back({L"目录无法列出",wutf8(error.substr(sizeof(directory)-1)),fmt(volume),L"此目录中的文件未计入结果"});
     }else{
         rows.push_back({L"读取异常",wutf8(error),fmt(volume),L"此项未能完整扫描"});
     }
 }
 void refresh(){
     saved_selection_keys=selection_keys();rebuilding_rows=true;source_filtered=false;restoring_selection=true;
     ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);restoring_selection=false;rows.clear();
     if(full_overview()){
         const auto typed=low(get_text(search));
         constexpr size_t cap=5000;
         struct Point{size_t volume,index;dc::U64 size;};
         std::vector<Point> points;
         if(scope_view==0){
             rows.push_back({L"已完成所选磁盘扫描",std::to_wstring(all_scan.volumes.size())+L" 个磁盘",
                 std::to_wstring(all_scan.total_files)+L" 个文件 · "+bytes(all_scan.total_bytes),
                 L"选择文件或目录后使用操作菜单；跨磁盘写入尚不支持"});
             for(size_t v=0;v<all_scan.volumes.size();++v){const auto& volume_inventory=all_scan.volumes[v];
                 rows.push_back({L"磁盘："+fmt(volume_inventory.root),std::to_wstring(volume_inventory.files.size())+L" 个文件 · "+
                     std::to_wstring(volume_inventory.dirs.size())+L" 个目录",bytes(volume_inventory.bytes),
                     volume_inventory.complete()?L"已完成 SHA-256；从结果操作菜单发起整理":L"扫描有异常；结论不完整",
                     volume_inventory.bytes,{},int(3000+v)});
             }
             rows.push_back({L"哈希候选组",std::to_wstring(all_scan.hash_candidates.size())+L" 组",
                 L"不等同于已逐字节验证",L"切换到文件或副本组视角查看具体位置"});
         }else if(scope_view==1||scope_view==2){
             source_filtered=true;
             for(size_t v=0;v<all_scan.volumes.size();++v){
                 const auto& volume_inventory=all_scan.volumes[v];
                 if(scope_view==1){for(size_t i=0;i<volume_inventory.files.size();++i){const auto& f=volume_inventory.files[i];
                     if(!typed.empty()){
                         const auto copies=observations.files[v][i].hash_candidate_copies;
                         const auto detail=(copies?L"本次扫描同哈希候选 "+std::to_wstring(copies)+L" 份":L"SHA-256 不可用")+L" · "+wutf8(f.sha256);
                         if(low(fmt(volume_inventory.root/f.rel)).find(typed)==std::wstring::npos&&low(detail).find(typed)==std::wstring::npos&&low(bytes(f.bytes)).find(typed)==std::wstring::npos)continue;
                     }
                     points.push_back({v,i,f.bytes});
                 }}else{for(size_t i=0;i<volume_inventory.dirs.size();++i){const auto& d=volume_inventory.dirs[i];
                     if(!typed.empty()){
                         const auto copies=observations.folders[v][i].tree_candidate_copies;
                         const auto detail=std::to_wstring(d.count)+L" 文件 · "+(copies?L"本次扫描同树哈希候选 "+std::to_wstring(copies)+L" 份":L"内容校验不完整");
                         if(low(fmt(volume_inventory.root/d.rel)).find(typed)==std::wstring::npos&&low(detail).find(typed)==std::wstring::npos&&low(bytes(d.bytes)).find(typed)==std::wstring::npos)continue;
                     }
                     points.push_back({v,i,d.bytes});
                 }}
             }
             const auto total=points.size();const size_t display=std::min(cap,total);
             const auto order=[](const Point& a,const Point& b){return a.size>b.size;};
             if(total>display){std::partial_sort(points.begin(),points.begin()+display,points.end(),order);points.resize(display);}
             else std::sort(points.begin(),points.end(),order);
             for(const auto& point:points){const auto& volume_inventory=all_scan.volumes[point.volume];
                 if(scope_view==1){const auto& f=volume_inventory.files[point.index];
                     const auto copies=observations.files[point.volume][point.index].hash_candidate_copies;
                     rows.push_back({f.rel.filename().wstring(),fmt(volume_inventory.root/f.rel),bytes(f.bytes),
                         (copies?L"本次扫描同哈希候选 "+std::to_wstring(copies)+L" 份":L"SHA-256 不可用")+
                         L" · "+wutf8(f.sha256.substr(0,12)),f.bytes,f.rel,int(3000+point.volume)});
                 }else{const auto& d=volume_inventory.dirs[point.index];
                     const auto copies=observations.folders[point.volume][point.index].tree_candidate_copies;
                     rows.push_back({d.rel.empty()?fmt(volume_inventory.root):d.rel.filename().wstring(),fmt(volume_inventory.root/d.rel),bytes(d.bytes),
                         std::to_wstring(d.count)+L" 文件 · "+(copies?L"本次扫描同树哈希候选 "+std::to_wstring(copies)+L" 份":L"内容校验不完整"),
                         d.bytes,d.rel,int(3000+point.volume)});
                 }
             }
             if(total>display)rows.push_back({L"显示范围已截取",L"本视角按容量显示前 "+std::to_wstring(display)+L" / "+std::to_wstring(total),
                 L"完整索引仍保留",L"可在搜索框输入路径关键词，重新筛选全部已扫描项目"});
         }else if(scope_view==3){
             source_filtered=true;size_t shown=0,matched=0;
             for(size_t i=0;i<all_scan.hash_candidates.size();++i){const auto& group=all_scan.hash_candidates[i];
                 const auto extra=group.members.size()>1?dc::U64(group.members.size()-1):0;
                 const auto detail=L"本次扫描同哈希候选 "+std::to_wstring(group.members.size())+L" 份 · 候选逻辑冗余估计（假设留1份） "+bytes(group.bytes*extra);
                 for(const auto& member:group.members){
                     const auto& volume=all_scan.volumes[member.volume];const auto& file=volume.files[member.file];
                     if(!typed.empty()&&low(fmt(volume.root/file.rel)).find(typed)==std::wstring::npos&&
                        low(detail).find(typed)==std::wstring::npos&&wutf8(file.sha256).find(typed)==std::wstring::npos&&low(bytes(file.bytes)).find(typed)==std::wstring::npos)continue;
                     ++matched;if(shown++>=cap)continue;
                     rows.push_back({L"组 "+std::to_wstring(i+1)+L" · "+file.rel.filename().wstring(),fmt(volume.root/file.rel),bytes(file.bytes),
                         detail+L"（内容未逐字节验证）",file.bytes,file.rel,int(3000+member.volume)});
                 }
             }
             if(matched>cap)rows.push_back({L"显示范围已截取",std::to_wstring(cap)+L" / "+std::to_wstring(matched)+L" 项",L"",L"输入路径、哈希或副本数可查询全部候选"});
         }else{
             if(all_scan.errors.empty()&&all_scan.complete())rows.push_back({L"扫描完成",L"所选磁盘没有报告读取错误",L"",L"文件内容是否稳定仍受扫描时的磁盘状态影响"});
             for(const auto& volume:all_scan.volumes)if(!volume.index_error.empty())
                 rows.push_back({L"索引未持久化",fmt(volume.root),L"内存结果可用",wutf8(volume.index_error)});
             for(const auto& e:all_scan.errors)rows.push_back({L"卷访问失败",wutf8(e),L"",L"未读取此卷的数据"});
             for(const auto& volume_inventory:all_scan.volumes)for(const auto& e:volume_inventory.errors)
                 append_scan_issue(e,volume_inventory.root);
         }
         rebuild();return;
     }
     if(!has_scan){rebuild();return;}
     const auto typed=low(get_text(search));constexpr size_t result_cap=5000;
     auto matches=[&](const fs::path& path){return typed.empty()||low(fmt(path)).find(typed)!=std::wstring::npos;};
     if(view==0||view==1){
         source_filtered=true;
         std::vector<size_t> indices;const bool files=view==0;
         std::map<int,size_t> verified_copies;std::map<fs::path,size_t> verified_dirs;
         if(exact_ready){for(const auto& file:inv.files)if(file.content>=0)++verified_copies[file.content];
             for(const auto& group:dupdirs)for(const auto& path:group.paths)verified_dirs[path]=group.paths.size();}
         auto file_label=[&](size_t i){const auto& f=inv.files[i];size_t copies=0;
             if(observation_volume<observations.files.size()&&i<observations.files[observation_volume].size())copies=observations.files[observation_volume][i].hash_candidate_copies;
             return exact_ready&&f.content>=0?L"当前根精确副本 "+std::to_wstring(verified_copies[f.content]):
                 copies?L"本次扫描同哈希候选 "+std::to_wstring(copies)+L" 份":std::wstring(L"SHA-256 不可用");};
         auto dir_label=[&](size_t i){const auto& d=inv.dirs[i];size_t copies=0;
             if(observation_volume<observations.folders.size()&&i<observations.folders[observation_volume].size())copies=observations.folders[observation_volume][i].tree_candidate_copies;
             return exact_ready&&inv.complete()?L"当前根精确相同目录 "+std::to_wstring(std::max(size_t(1),verified_dirs[d.rel])):
                 copies?L"本次扫描同树哈希候选 "+std::to_wstring(copies)+L" 份":std::wstring(L"目录内容未完整读取");};
         const size_t count=files?inv.files.size():inv.dirs.size();
         for(size_t i=0;i<count;++i){
             const auto& path=files?inv.files[i].rel:inv.dirs[i].rel;
             bool match=matches(path);
             if(!match){const auto detail=files?file_label(i)+L" · SHA256 "+wutf8(inv.files[i].sha256):
                 std::to_wstring(inv.dirs[i].count)+L" 个文件 · "+dir_label(i);
                 match=low(detail).find(typed)!=std::wstring::npos||low(bytes(files?inv.files[i].bytes:inv.dirs[i].bytes)).find(typed)!=std::wstring::npos;
             }
             if(match)indices.push_back(i);
         }
         const size_t total=indices.size(),limit=std::min(total,result_cap);
         auto size=[&](size_t i){return files?inv.files[i].bytes:inv.dirs[i].bytes;};
         auto order=[&](size_t a,size_t b){return size(a)>size(b);};
         if(limit<total){std::partial_sort(indices.begin(),indices.begin()+limit,indices.end(),order);indices.resize(limit);}
         else std::sort(indices.begin(),indices.end(),order);
         for(size_t i:indices){
             if(files){const auto& f=inv.files[i];
                 rows.push_back({f.rel.filename().wstring(),fmt(f.rel),bytes(f.bytes),file_label(i)+L" · SHA256 "+wutf8(f.sha256.substr(0,12)),f.bytes,f.rel});
             }else{const auto& d=inv.dirs[i];
                 rows.push_back({d.rel.empty()?L"(扫描根目录)":d.rel.filename().wstring(),fmt(d.rel),bytes(d.bytes),std::to_wstring(d.count)+L" 个文件 · "+dir_label(i),d.bytes,d.rel});
             }
         }
         if(total>limit)rows.push_back({L"显示范围已截取",L"当前显示容量最大的 "+std::to_wstring(limit)+L" / "+std::to_wstring(total)+L" 项",L"完整索引仍保留",L"输入名称、路径、哈希或副本数可搜索全部扫描结果"});
     }
     else if(view==2){source_filtered=true;size_t k=0,shown=0,matched=0;const auto& grouped=exact_ready?dup:hash_duplicates;
         for(const auto& group:grouped.groups){++k;const auto detail=(exact_ready?L"当前根精确副本 ":L"当前根同哈希候选 ")+std::to_wstring(group.size())+L" 份";
             for(size_t i:group){const auto& f=inv.files[i];
                 if(!matches(f.rel)&&low(detail).find(typed)==std::wstring::npos&&wutf8(f.sha256).find(typed)==std::wstring::npos&&low(bytes(f.bytes)).find(typed)==std::wstring::npos)continue;
                 ++matched;if(shown++>=result_cap)continue;
                 rows.push_back({(exact_ready?L"精确副本组 ":L"哈希候选组 ")+std::to_wstring(k),fmt(f.rel),bytes(f.bytes),detail+L" · "+
                     (exact_ready?L"已逐字节核验":L"SHA-256 相同；操作时按您配置的策略验证"),f.bytes,f.rel});}}
         if(matched>result_cap)rows.push_back({L"显示范围已截取",std::to_wstring(result_cap)+L" / "+std::to_wstring(matched)+L" 项",L"",L"输入路径、哈希或副本数查找全部候选"});
     }
     else if(view==3){size_t matched=0;
         if(exact_ready){size_t group_index=0;for(const auto& group:dupdirs){++group_index;for(const auto& path:group.paths)if(matches(path)){
             if(++matched<=result_cap)rows.push_back({L"精确重复目录组 "+std::to_wstring(group_index),fmt(path),bytes(group.bytes),std::to_wstring(group.paths.size())+L" 个等价目录（当前根）",group.bytes,path});}}}
         else if(observation_volume<observations.folders.size())for(size_t i=0;i<inv.dirs.size();++i){
             const auto& d=inv.dirs[i];const size_t copies=observations.folders[observation_volume][i].tree_candidate_copies;
             if(copies>1&&matches(d.rel)&&++matched<=result_cap)rows.push_back({L"相同目录树候选",fmt(d.rel),bytes(d.bytes),std::to_wstring(copies)+L" 份 · 目录树哈希相同（本次扫描全部根）",d.bytes,d.rel});}
         if(matched>result_cap)rows.push_back({L"显示范围已截取",std::to_wstring(result_cap)+L" / "+std::to_wstring(matched)+L" 项",L"",L"输入路径关键词查找全部目录副本"});
         if(rows.empty())rows.push_back({L"没有匹配的重复目录",L"目录视角仍可查看所有目录大小与副本候选数",L"",L""});
     }
     else if(view==4){
         if(!relations_ready)rows.push_back({L"查找相似目录",L"主题操作 → 分析当前范围的相似目录",L"",L"只读分析，不要求达到智能融合推荐阈值"});
         else{
             rows.push_back({L"发现目录关系",std::to_wstring(overlaps.total_candidates)+L" 组内容重合",std::to_wstring(overlaps.pairs.size())+L" 组已显示",L"选中一组，再选择基础操作 → 评估两个目录查看差异并预览融合"});
             if(overlaps.truncated)rows.push_back({L"显示已截取",L"候选很多：按重合覆盖率排序，只显示部分结果",L"",L"可缩小扫描根目录，或手工指定任意两目录评估"});
             for(size_t i=0;i<overlaps.pairs.size();++i){const auto& r=overlaps.pairs[i];
                 rows.push_back({fmt(r.left),fmt(r.right),std::to_wstring(r.shared)+L" 个共有文件",
                     L"较小侧覆盖 "+std::to_wstring(int(r.smaller_coverage*100.0+0.5))+L"%，多重集 Jaccard "+
                     std::to_wstring(int(r.multiset_jaccard*100.0+0.5))+L"%",r.shared,{},int(i)});
             }
         }
     }
     else if(view==5){if(!photos_ready)rows.push_back({L"主题操作 → 分析当前范围的相似照片",L"Windows WIC 解码 + dHash",L"",L""});
         for(auto& p:photos.pairs)rows.push_back({fmt(p.a),fmt(p.b),std::to_wstring(p.distance)+L" / 64 位",L"候选相似照片，需人工核对",dc::U64(p.distance),p.a});
         if(photos_ready&&photos.pairs.empty())rows.push_back({L"未发现符合阈值的相似照片对",L"可尝试调大 dHash 距离阈值；仍需人工核对候选",L"",L""});
         for(auto& p:photos.photos)rows.push_back({L"图片元数据",fmt(p.rel),p.exif_date?wutf8(p.date):L"未找到 EXIF 拍摄日期",L"已读取 dHash",999,p.rel});
         for(auto& e:photos.errors)rows.push_back({L"无法解码",wutf8(e),L"",L"该文件未参加照片相似度比较",1000});}
     else if(view==6){
         rows.push_back({L"当前副本保留设置",L"至少 "+std::to_wstring(retention_policy.min_active_copies)+L" 份活动副本",L"可自行配置",
             retention_policy.verify_bytes_before_change?L"操作前再次逐字节核验":L"操作时仅核对扫描哈希与文件状态"});
         rows.push_back({L"计数范围",fmt(root),L"",L"候选：本次扫描全部根；精确：当前根；计划合格保留者：当前根且按冻结策略复核。候选按路径计数，不代表独立对象或物理盘备份"});
         std::map<int,size_t> counts;if(exact_ready)for(const auto& f:inv.files)if(f.content>=0)++counts[f.content];
         size_t matched=0;
         for(size_t i=0;i<inv.files.size();++i){const auto& f=inv.files[i];
             const auto copies=exact_ready&&f.content>=0?counts[f.content]:
                 observation_volume<observations.files.size()?observations.files[observation_volume][i].hash_candidate_copies:0;
             if(copies>1||!matches(f.rel))continue;
             if(++matched<=result_cap)rows.push_back({copies?(exact_ready?L"当前范围仅一份精确内容":L"本次扫描全部根仅一份哈希候选"):L"内容未完整读取",fmt(f.rel),bytes(f.bytes),L"操作前请检查副本保留设置",f.bytes,f.rel});
         }
         if(matched>result_cap)rows.push_back({L"显示范围已截取",std::to_wstring(result_cap)+L" / "+std::to_wstring(matched)+L" 项",L"",L"输入路径关键词继续检查"});
     }
     else if(view==7){
         for(const auto& item:dc::history(root))
             rows.push_back({wutf8(item.transaction),history_action(item.label),std::to_wstring(item.items)+L" 项",history_state(item.status),item.items});
         if(rows.empty())rows.push_back({L"当前范围还没有操作记录",fmt(root),L"",L"确认执行整理操作后，可在这里查看结果"});
     }
     else if(view==8){if(!has_comparison)rows.push_back({L"基础操作 → 评估两个目录，输入 A/B 相对路径",L"",L"",L""});
         else {rows.push_back({L"比较概要",fmt(pair_report.comparison.left)+L" ↔ "+fmt(pair_report.comparison.right),std::to_wstring(pair_report.comparison.identical)+L" 项相同",L"A⊆B="+std::wstring(pair_report.comparison.left_in_right?L"是":L"否")+L"，B⊆A="+(pair_report.comparison.right_in_left?L"是":L"否")});
             {
                 auto pct=[](const std::optional<double>& x){return x?std::to_wstring(int(*x*100.0+0.5))+L"%":L"无定义";};
                 rows.push_back({L"精确内容重合",std::to_wstring(pair_report.shared_file_occurrences)+L" 项，共 "+std::to_wstring(pair_report.shared_distinct_contents)+L" 种内容",L"较小侧覆盖 "+pct(pair_report.smaller_coverage),L"多重集 Jaccard "+pct(pair_report.multiset_jaccard)});
                 rows.push_back({L"同相对路径",std::to_wstring(pair_report.same_relative_files)+L" 项内容相同",std::to_wstring(pair_report.same_path_different_content)+L" 项内容冲突",L"相同路径比例 "+pct(pair_report.same_path_fraction)});
                 rows.push_back({L"两侧独有文件",L"仅左 "+std::to_wstring(pair_report.left_only_files),L"仅右 "+std::to_wstring(pair_report.right_only_files),L"目录融合必须人工预览确认"});
                 rows.push_back({L"目录结构",L"左侧 "+std::to_wstring(pair_report.left_dirs)+L" 个子目录（空 "+std::to_wstring(pair_report.left_empty_dirs)+L"）",L"右侧 "+std::to_wstring(pair_report.right_dirs)+L" 个子目录（空 "+std::to_wstring(pair_report.right_empty_dirs)+L"）",pair_report.exact_tree?L"完整目录树相同":L"并非完全相同"});
             }
             for(auto& r:pair_report.comparison.rows)rows.push_back({wutf8(r.state),fmt(r.rel),bytes(r.left),bytes(r.right),r.left+r.right});}}
     else if(view==9){
         const bool generated=pending_is_structure||pending_is_merge||!pending.name.empty();
         if(!generated){
             rows.push_back({L"还没有操作预览",L"在结果中选中要整理的文件或目录",L"",L"从操作菜单选择重命名、隔离、融合或重组"});
         }else{
             const auto summary=describe_preview();
             rows.push_back({summary.title,summary.location,summary.changes,summary.effect});
         }
         if(pending_is_structure){
             for(const auto& m:pending_structure.mappings)rows.push_back({fmt(m.from),fmt(m.to),bytes(m.bytes),
                 m.action=="COPY"?L"复制并验证":m.action=="REUSE_EXISTING"?L"沿用目标已有文件":L"多个来源共用这一目标文件",m.bytes,m.from});
             for(const auto& d:pending_structure.directories)rows.push_back({L"新建文件夹",fmt(d),L"",L"包含空目录"});
             add_preview_notes(pending_structure.warnings);
         }else if(pending_is_merge){
             if(pending_merge.identical)rows.push_back({L"目标已有相同内容",fmt(pending_merge.destination),
                 std::to_wstring(pending_merge.identical)+L" 个文件",L"保留原位，跳过复制"});
             for(const auto& f:pending_merge.copies)rows.push_back({fmt(f.from),fmt(f.to),bytes(f.bytes),L"复制并验证，源文件保留",f.bytes,f.from});
             for(const auto& d:pending_merge.directories)rows.push_back({L"新建文件夹",fmt(d),L"",L"包含空目录"});
             add_preview_notes(pending_merge.warnings);
         }else if(generated){
             const bool quarantine=preview_is_quarantine();
             for(const auto& m:pending.moves){
                 std::wstring detail=quarantine?L"移入本次隔离区":L"将来源移动到所示目标";
                 if(!m.keepers.empty())detail+=L" · 保留 "+std::to_wstring(m.keepers.size())+L" 份，例如 "+fmt(m.keepers.front());
                 rows.push_back({fmt(m.from),fmt(m.to),L"待执行",detail,0,m.from});
             }
             if(!retirement_summary.blocked.empty()){
                 rows.push_back({L"留在原位",fmt(retirement_summary.source_folder),
                     std::to_wstring(retirement_summary.blocked.size())+L" 项",L"所选目录外的保留副本不足"});
                 for(const auto& file:retirement_summary.blocked)
                     rows.push_back({L"跳过清退",fmt(file),L"",L"保留在原目录"});
             }
             add_preview_notes(pending.warnings);
         }
     }
     else if(view==10){
         if(!suggestions_ready)rows.push_back({L"尚未分析融合建议",L"智能操作 → 分析当前范围的目录融合建议",L"",L""});
         else if(suggestions.empty())rows.push_back({L"暂无可靠融合候选",L"没有足够的文件内容及相对路径重合证据",L"",L"这不表示没有重复；可使用基础操作 → 评估两个目录，或主题操作 → 融合两个目录"});
         for(size_t i=0;i<suggestions.size();++i){auto& s=suggestions[i];
             const wchar_t* confidence_label=s.confidence==3?L"高度匹配":s.confidence==2?L"较可能同源":L"建议人工核对";
             auto percent=std::to_wstring(int(s.overlap*100+0.5));
             auto explanation=L"同路径同内容 "+std::to_wstring(s.same_relative_files)+
                L"；源独有 "+std::to_wstring(s.source_only)+L"，目标独有 "+
                std::to_wstring(s.destination_only)+L"，同名变化 "+
                std::to_wstring(s.changed_at_same_path)+L"（待补版本 "+std::to_wstring(s.unresolved_collisions)+L"）"+
                (s.useful?L"；双击生成预览":L"；目标已覆盖来源，无需融合");
             rows.push_back({confidence_label,fmt(s.source)+L" → "+fmt(s.destination),
                std::to_wstring(s.shared_contents)+L" 项重合 / "+percent+L"%",explanation,
                dc::U64(s.shared_contents),{},int(i)});
         }
     }
     else if(view==11){
         if(structure_rejected)rows.push_back({L"已拒绝本次结构建议",L"未修改任何文件",L"",L"如需重新建议，请选择智能操作 → 生成当前范围的目录重组映射"});
         else if(!structure_ready)rows.push_back({L"尚未分析目录结构",L"智能操作 → 生成当前范围的目录重组映射",L"",L""});
         else if(structure_suggestion.rules.empty())rows.push_back({L"暂无需要迁移的内容",L"",L"",L""});
         else {
             rows.push_back({L"建议目标根目录",fmt(structure_suggestion.output_base),L"",L"仅为建议；扫描阶段没有创建该目录"});
             for(size_t i=0;i<structure_suggestion.rules.size();++i){const auto& r=structure_suggestion.rules[i];
                 rows.push_back({fmt(r.source),fmt(structure_suggestion.output_base/r.target),
                     std::to_wstring(r.files)+L" 个文件 · "+bytes(r.bytes),wutf8(r.reason)+L"；双击编辑对应关系",r.bytes,{},int(i)});
             }
             for(const auto& note:structure_suggestion.warnings)rows.push_back({L"注意事项",wutf8(note),L"",L""});
         }
     }
     else if(view==12){
         rows.push_back({L"已完成扫描",std::to_wstring(inv.files.size())+L" 个文件 · "+bytes(inv.bytes),std::to_wstring(inv.dirs.size())+L" 个目录",inv.complete()?L"全部可读取文件已计算 SHA-256":L"存在读取异常，结论可能不完整"});
         rows.push_back({L"当前根副本候选",std::to_wstring(hash_duplicates.groups.size())+L" 组同哈希内容",L"候选逻辑冗余估计（假设留1份） "+bytes(hash_duplicates.wasted),L"文件与目录视角显示位置、容量和副本数量"});
         rows.push_back({L"精确内容核验",exact_ready?L"已完成":L"按需执行",L"",L"无需等待整盘二次读取即可浏览扫描结果"});
         rows.push_back({L"目录融合与重组",L"选择目录后从主题操作或智能操作发起",L"",L"先生成可检查的方案，再确认执行"});
         for(const auto& error:inv.errors)append_scan_issue(error,inv.root);
     }
     if(view==12&&!inv.index_error.empty())rows.push_back({L"索引未持久化",fmt(inv.root),L"内存结果可用",wutf8(inv.index_error)});
     rebuild();
 }
 std::wstring empty_results_message() const {
     if(GetWindowTextLengthW(search))return L"没有匹配的结果。请换一个关键词，或清除筛选。";
     if(showing_duplicate_files())
         return has_scan&&exact_ready?L"当前范围内未发现内容相同的文件。":L"在本次已读取的文件中未发现重复候选。";
     if((has_scan&&view==0)||(full_overview()&&scope_view==1))return L"当前范围内没有可显示的文件。";
     return L"当前视图没有可显示的结果。";
 }
 void rebuild(){
     const auto keys=rebuilding_rows?saved_selection_keys:selection_keys();rebuilding_rows=false;
     restoring_selection=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
     visible.clear();auto filter=low(get_text(search));for(size_t i=0;i<rows.size();++i){auto&r=rows[i];if(source_filtered||r.a==L"显示范围已截取"||filter.empty()||low(r.a).find(filter)!=std::wstring::npos||low(r.b).find(filter)!=std::wstring::npos||low(r.c).find(filter)!=std::wstring::npos||low(r.d).find(filter)!=std::wstring::npos)visible.push_back(i);}
     // Keep a matching plan summary above the details, even after a column click.
     auto sortable=visible.begin();
     if(has_scan&&view==9&&sortable!=visible.end()&&*sortable==0)++sortable;
     auto col=sort_column;bool desc=sort_desc;std::stable_sort(sortable,visible.end(),[&](size_t a,size_t b){auto&x=rows[a],&y=rows[b];
         if(col==2&&x.count!=y.count)return desc?x.count>y.count:x.count<y.count;
         const auto&xs=col==0?x.a:col==1?x.b:col==2?x.c:x.d;const auto&ys=col==0?y.a:col==1?y.b:col==2?y.c:y.d;
         auto k=low(xs).compare(low(ys));return desc?k>0:k<0;});
     ListView_SetItemCountEx(list,int(visible.size()),LVSICF_NOSCROLL);
     std::wstring note=visible.empty()?empty_results_message():
         (filter.empty()?L"显示 ":L"筛选结果 ")+std::to_wstring(visible.size())+L" 行";
     if(full_overview())note+=L" · 所选磁盘已索引 "+std::to_wstring(all_scan.total_files)+L" 文件 · "+std::to_wstring(all_scan.hash_candidates.size())+L" 组哈希候选";
     if(has_scan){
         if(!dup.errors.empty())note=L"副本核验不完整 · "+note;
         note+=L" · "+std::to_wstring(inv.files.size())+L" 个文件，"+bytes(inv.bytes)+L" · "+std::to_wstring(exact_ready?dup.groups.size():hash_duplicates.groups.size())+
             (exact_ready?L" 组精确副本":L" 组重复候选")+L" · 当前范围 "+root.wstring();
     }
     if(const size_t issues=scan_issue_count()){
         const auto lens=full_overview()?L"扫描问题":L"扫描概览";
         note=L"扫描不完整 · "+std::to_wstring(issues)+L" 项异常，可在「"+lens+L"」查看路径 · "+note;
     }
     for(int i=0;i<int(visible.size());++i)if(keys.count(row_key(rows[visible[size_t(i)]])))ListView_SetItemState(list,i,LVIS_SELECTED,LVIS_SELECTED);
     restoring_selection=false;if(results_stale)note=L"结果已过期 · 未自动重扫 · "+note;
     if(!busy)status_text(note);
     refresh_selection();
 }
 void start(Task task){
     if(busy)return;
     if(worker.joinable())worker.join();
     cancel.store(false);active_task=task;quick_result.reset();
     const bool cancellable=task==Task::Inventory||task==Task::Scan||task==Task::ScanAll||task==Task::Verify||task==Task::Pair||task==Task::MergePlan||task==Task::Suggest||task==Task::Structure||task==Task::Photo||task==Task::Relations||task==Task::ArchivePlan||task==Task::QuarantinePlan||task==Task::CleanPlan||task==Task::RenamePlan||task==Task::RetirementPlan;
     job_started=GetTickCount64();job_finished=0;speed_tick=job_started;speed_bytes=0;speed_files=0;recent_speed=0;recent_file_rate=0;speed_root=0;
     {std::lock_guard<std::mutex> lock(progress_mutex);progress_snapshot={};progress_snapshot.root=root;
      progress_snapshot.root_index=1;progress_message.clear();last_progress_tick=job_started;}
     set_busy(true,cancellable);render_progress();
     const bool writing=task==Task::Apply||task==Task::MergeApply||task==Task::StructureApply||task==Task::Undo;
     status_text(writing?L"正在执行已确认的计划，完成前不能关闭窗口":L"后台分析或预览进行中；尚未执行文件操作");
     const auto selected_root=root;const auto frozen_drives=selected_drives;
     const bool scanning_drives=task==Task::ScanAll||(task==Task::Inventory&&scan_scope==0);
     const auto plan=pending;const auto merge=pending_merge;const auto structplan=pending_structure;
     const auto structproposal=structure_suggestion;const auto archive=archive_request;
     const auto policy=retention_policy;const auto files=requested_files;const auto rename=rename_request;
     if(task==Task::Structure||task==Task::StructurePlan||task==Task::MergePlan||task==Task::ArchivePlan||task==Task::RetirementPlan||task==Task::QuarantinePlan||task==Task::CleanPlan||task==Task::RenamePlan){
         has_pending=false;pending={};pending_merge={};pending_structure={};pending_is_merge=false;pending_is_structure=false;
     }
     auto request_source=requested_source,request_destination=requested_destination,request_retirement=requested_retirement;
     const dc::Inventory* invref=&inv;bool clean_has_scan=has_scan;int distance=photo_distance;
     if(task==Task::Scan||task==Task::ScanAll){
         reset_inventory();
         previous_file_edit={};file_edit_in_progress=false;refresh();refresh_navigation();
     }
     worker=std::thread([this,task,selected_root,plan,merge,structplan,structproposal,archive,policy,files,rename,
          request_source,request_destination,request_retirement,invref,clean_has_scan,distance,frozen_drives,scanning_drives]() mutable {
         auto result=std::make_unique<JobResult>();result->task=task;
         const auto progress=[this](const std::string& message){record_message(message);};
         const auto structured=[this](const dc::ScanProgress& snapshot){record_progress(snapshot);};
         const auto analyzing=[&](const char* message){
             dc::ScanProgress snapshot;{std::lock_guard<std::mutex> lock(progress_mutex);snapshot=progress_snapshot;}
             snapshot.phase=dc::ScanPhase::Analyzing;snapshot.current_file.clear();structured(snapshot);progress(message);
         };
         try{
             if(task==Task::Scan||task==Task::ScanAll||task==Task::Inventory){
                 std::vector<fs::path> roots;
                 if(scanning_drives){
                     if(frozen_drives.empty())throw std::runtime_error("No drives selected");
                     for(const auto& drive:frozen_drives){
                         if(!same_drive(drive))throw std::runtime_error("Selected volume changed or disconnected: "+dc::utf8(drive.root));
                         roots.push_back(drive.root);
                     }
                 }else roots.push_back(selected_root);
                 if(task==Task::Inventory){
                     auto& sum=result->inventory_summary;
                     for(size_t i=0;i<roots.size();++i){
                         if(scanning_drives&&!same_drive(frozen_drives[i]))throw std::runtime_error("Selected volume changed before inventory");
                         const auto part=dc::scan_inventory(roots[i],[&](dc::ScanProgress snapshot){
                             snapshot.root_index=i+1;snapshot.root_count=roots.size();structured(snapshot);
                         },&cancel);
                         sum.file_count+=part.file_count;sum.directory_count+=part.directory_count;
                         sum.bytes+=part.bytes;sum.directory_errors+=part.directory_errors;
                         sum.reparse_skipped+=part.reparse_skipped;sum.other_errors+=part.other_errors;
                     }
                 }else result->full_scan=dc::scan_roots_hashed(roots,progress,&cancel,structured);
                 if(scanning_drives)for(const auto& drive:frozen_drives)if(!same_drive(drive))
                     throw std::runtime_error("Selected volume changed during scan: "+dc::utf8(drive.root));
                 if(task!=Task::Inventory){
                     if(result->full_scan.volumes.empty())throw std::runtime_error(result->full_scan.errors.empty()?"No scan result":result->full_scan.errors.front());
                     analyzing("Building file and directory observations from the hash index");
                     result->observations=dc::observe_scan(result->full_scan,&cancel);
                     if(task==Task::Scan){
                         result->inventory=std::move(result->full_scan.volumes.front());
                         result->hash_duplicates=hash_groups(result->inventory);
                     }
                 }
             }else if(task==Task::Verify){
                 if(!clean_has_scan)throw std::runtime_error("Scan first");
                 result->inventory=*invref;
                 result->duplicates=dc::deduplicate(result->inventory,progress,&cancel,structured);
                 if(cancel.load())throw std::runtime_error("Operation canceled");
                 analyzing("Building exact directory groups");
                 if(result->inventory.complete()&&result->duplicates.errors.empty())result->dirs=dc::duplicate_dirs(result->inventory);
             }else if(task==Task::Pair){
                 if(!clean_has_scan)throw std::runtime_error("Scan first");
                 result->pair=dc::evaluate_hashed_directory_pair(*invref,request_source,request_destination,progress,&cancel,structured);
             }else if(task==Task::Suggest){
                 if(!clean_has_scan)throw std::runtime_error("Scan first");
                 analyzing("Analyzing directory merge candidates");result->suggestions=dc::recommend_merges(*invref,100,&cancel);
             }else if(task==Task::Structure){
                 if(!clean_has_scan)throw std::runtime_error("Scan first");
                 analyzing("Preparing directory structure mappings");
                 if(cancel.load())throw std::runtime_error("Operation canceled");
                 result->structure=dc::suggest_structure(*invref,L"整理结果",&cancel);
                 if(cancel.load())throw std::runtime_error("Operation canceled");
             }else if(task==Task::Photo){if(!clean_has_scan)throw std::runtime_error("Scan first");result->photos=dc::similar_photos(*invref,distance,progress,&cancel);}
             else if(task==Task::Relations){if(!clean_has_scan)throw std::runtime_error("Scan first");result->overlaps=dc::find_overlapping_dirs(*invref,500,&cancel);}
             else if(task==Task::ArchivePlan){if(!clean_has_scan)throw std::runtime_error("Scan first");result->plan=dc::plan_archive(*invref,archive,progress,&cancel);}
             else if(task==Task::MergePlan){if(!clean_has_scan)throw std::runtime_error("Scan first");result->merge_plan=dc::plan_hashed_directory_merge(*invref,request_source,request_destination,progress,&cancel,structured);}
             else if(task==Task::StructurePlan){if(!clean_has_scan)throw std::runtime_error("Scan first");result->structure_plan=dc::plan_structure(*invref,structproposal);}
             else if(task==Task::RetirementPlan){if(!clean_has_scan)throw std::runtime_error("Scan first");result->retirement=dc::plan_retire_hashed_directory(*invref,request_retirement,policy);if(cancel.load())throw std::runtime_error("Operation canceled");}
             else if(task==Task::QuarantinePlan){result->plan=dc::plan_quarantine_hashed_selected(*invref,files,policy);if(cancel.load())throw std::runtime_error("Operation canceled");}
             else if(task==Task::CleanPlan){result->plan=dc::plan_clean_hashed_duplicates(*invref,policy);if(cancel.load())throw std::runtime_error("Operation canceled");}
             else if(task==Task::RenamePlan){result->plan=dc::plan_rename(*invref,rename);if(cancel.load())throw std::runtime_error("Operation canceled");}
             else if(task==Task::Apply)result->result=dc::execute_plan(selected_root,plan);
             else if(task==Task::MergeApply)result->result=dc::execute_merge_plan(selected_root,merge);
             else if(task==Task::StructureApply)result->result=dc::execute_structure_plan(selected_root,structplan);
             else if(task==Task::Undo)result->result=dc::undo_latest(selected_root);
         }catch(const std::exception& e){result->error=e.what();}
         if(!PostMessageW(hwnd,WMU_DONE,0,(LPARAM)result.get()))return;
         result.release();
     });
 }
 void complete(std::unique_ptr<JobResult> result){
     job_finished=GetTickCount64();
     if(worker.joinable())worker.join();
     set_busy(false);
     if(close_requested){DestroyWindow(hwnd);return;}
     if(!result->error.empty()){
         const bool write_failed=result->task==Task::Apply||result->task==Task::MergeApply||
             result->task==Task::StructureApply||result->task==Task::Undo;
         if(write_failed){clear_analysis();results_stale=true;change_view(7);}
         finish_progress(false,cancel.load());deferred_command=0;deferred_paths.clear();
         if(write_failed){
             SetWindowTextW(progress_stage,L"操作异常终止 · 当前扫描结果已过期");
             SetWindowTextW(progress_path,L"未自动重扫。请检查操作历史；再次分析或操作前，请显式重新扫描。");
         }
         if(result->task==Task::StructurePlan&&file_edit_in_progress){
             structure_suggestion=std::move(previous_file_edit);file_edit_in_progress=false;change_view(11);
         }
         if(!cancel.load()||write_failed)alert(hwnd,L"任务未完成：\n"+wutf8(result->error)+
             (write_failed?L"\n\n文件可能已部分改变，当前结果已过期；未自动重扫。请检查历史并显式重新扫描。":L""),MB_ICONWARNING);
         status_text(write_failed?L"操作异常终止 · 结果已过期，请检查历史并重新扫描":
             cancel.load()?L"已停止本次任务":L"任务未完成，请检查提示后重试");return;
     }
     if(result->task==Task::Inventory){
         quick_result=result->inventory_summary;
         const auto& s=*quick_result;
         finish_progress(true);
         std::wstring summary=L"已用 "+duration_text(double(job_finished-job_started)/1000.0);
         if(s.error_count())summary+=L" · 目录访问异常 "+std::to_wstring(s.directory_errors)+
             L" · 已跳过重解析点 "+std::to_wstring(s.reparse_skipped)+L" · 其他异常 "+std::to_wstring(s.other_errors);
         status_text(summary+L" · 点“开始扫描”可查重复文件");
         return;
     }
     if(result->task==Task::ScanAll){
         all_scan=std::move(result->full_scan);observations=std::move(result->observations);has_full_scan=true;has_scan=false;
         root.clear();scope_view=0;view=12;
         if(all_scan.volumes.size()==1&&all_scan.errors.empty())focus_volume(0);else {refresh();refresh_navigation();}
         finish_progress(true);
     }else if(result->task==Task::Scan){
         inv=std::move(result->inventory);observations=std::move(result->observations);observation_volume=0;
         hash_duplicates=std::move(result->hash_duplicates);has_scan=true;root=inv.root;
         change_view(0);
         finish_progress(true);
     }else if(result->task==Task::Verify){
         inv=std::move(result->inventory);dup=std::move(result->duplicates);dupdirs=std::move(result->dirs);
         exact_ready=inv.complete()&&dup.errors.empty();const int command=deferred_command;
         auto paths=std::move(deferred_paths);deferred_command=0;deferred_paths.clear();
         refresh();restore_paths(paths);refresh_navigation();finish_progress(exact_ready);
         if(!exact_ready){alert(hwnd,L"内容核验存在未完成项，本次不继续生成整理方案。请检查扫描异常。",MB_ICONWARNING);return;}
         if(command)run_action(command);
         return;
     }else if(result->task==Task::Pair){
         pair_report=std::move(result->pair);has_comparison=true;
         change_view(8);finish_progress(true);present_pair_report();return;
     }else if(result->task==Task::Suggest){suggestions=std::move(result->suggestions);suggestions_ready=true;change_view(10);}
     else if(result->task==Task::Structure){structure_suggestion=std::move(result->structure);structure_ready=true;structure_rejected=false;change_view(11);}
     else if(result->task==Task::Photo){photos=std::move(result->photos);photos_ready=true;change_view(5);}
     else if(result->task==Task::Relations){overlaps=std::move(result->overlaps);relations_ready=true;change_view(4);}
     else if(result->task==Task::ArchivePlan||result->task==Task::QuarantinePlan||result->task==Task::CleanPlan||result->task==Task::RenamePlan)make_preview(std::move(result->plan));
     else if(result->task==Task::MergePlan)preview_merge(std::move(result->merge_plan));
     else if(result->task==Task::StructurePlan){file_edit_in_progress=false;previous_file_edit={};preview_structure(std::move(result->structure_plan));}
     else if(result->task==Task::RetirementPlan){retirement_summary=std::move(result->retirement);make_preview(retirement_summary.quarantine);}
     else{
         const bool applied=result->result.success;
         // A completed or interrupted mutation invalidates the entire displayed
         // snapshot. Keep it as read-only evidence; never implicitly re-read a
         // terabyte drive after moving a handful of files.
         const bool undo=result->task==Task::Undo;
         const auto summary=undo?PreviewSummary{L"撤销上一次操作",fmt(root),L"",L""}:describe_preview();
         clear_analysis();results_stale=true;change_view(7);finish_progress(applied);
         const auto stage=summary.title+(applied?L"已完成 · 结果已过期":L"未完全成功 · 结果已过期");
         SetWindowTextW(progress_stage,stage.c_str());
         if(applied){
             const auto next=summary.changes.empty()?L"要继续整理，请重新扫描。":
                 summary.changes+L"。要继续整理，请重新扫描。";
             SetWindowTextW(progress_path,next.c_str());
         }else{
             SetWindowTextW(progress_path,L"请先查看操作历史，按提示检查相关路径，再决定是否重新扫描。");
             alert(hwnd,L"请先在操作历史中核对本次记录。\n\n"+wutf8(result->result.message),MB_ICONWARNING);
         }
         return;
     }
     finish_progress(true);
     if(view==9&&(pending_is_structure||pending_is_merge||!pending.name.empty()))show_plan_status();
 }
 void show_plan_status(){
     const auto summary=describe_preview();
     const auto stage=summary.title+(!has_pending?L" · 无需执行":view==9?L" · 请核对预览":L" · 预览已就绪");
     SetWindowTextW(progress_stage,stage.c_str());
     SetWindowTextW(progress_metrics,summary.changes.c_str());
     SetWindowTextW(progress_path,view!=9?L"":has_pending?L"核对来源、目标和数量后，可选择“确认执行…”。":
         L"本次没有要修改的项目。");
 }
 void browse(){if(scan_scope==0){choose_drives();return;}
     IFileOpenDialog* raw=nullptr;
     HRESULT hr=CoCreateInstance(CLSID_FileOpenDialog,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&raw));
     auto release=[](auto* item){if(item)item->Release();};
     std::unique_ptr<IFileOpenDialog,decltype(release)> dialog(raw,release);
     FILEOPENDIALOGOPTIONS options{};
     if(SUCCEEDED(hr))hr=dialog->GetOptions(&options);
     if(SUCCEEDED(hr))hr=dialog->SetOptions(options|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM|FOS_PATHMUSTEXIST|FOS_NOCHANGEDIR);
     if(SUCCEEDED(hr)){
         dialog->SetTitle(L"选择要扫描的文件夹");
         dialog->SetOkButtonLabel(L"选择此文件夹");
         hr=dialog->Show(hwnd);
     }
     if(hr==HRESULT_FROM_WIN32(ERROR_CANCELLED))return;
     IShellItem* item=nullptr;
     if(SUCCEEDED(hr))hr=dialog->GetResult(&item);
     std::unique_ptr<IShellItem,decltype(release)> selected(item,release);
     PWSTR path=nullptr;
     if(SUCCEEDED(hr))hr=selected->GetDisplayName(SIGDN_FILESYSPATH,&path);
     if(SUCCEEDED(hr)){SetWindowTextW(root_box,path);SetFocus(btn_scan);}
     else {scope_controls_open=true;refresh_navigation();alert(hwnd,L"无法打开此文件夹。可在上方输入或粘贴完整路径。",MB_ICONWARNING);SetFocus(root_box);}
     CoTaskMemFree(path);
 }
 bool need_scan(){
     if(busy||!has_scan)return false;
     if(results_stale){alert(hwnd,L"文件操作后，当前扫描结果已过期。请显式重新扫描后再进行分析或文件操作；也可以选择较小的目录扫描。",MB_ICONINFORMATION);return false;}
     if(scan_scope==0){
         const auto match=std::find_if(selected_drives.begin(),selected_drives.end(),[&](const auto& d){return low(d.root.wstring())==low(root.wstring());});
         if(match==selected_drives.end()||!same_drive(*match)){alert(hwnd,L"磁盘已断开或卷身份改变，请重新扫描。",MB_ICONWARNING);return false;}
     }else{
         std::error_code ec;auto typed=fs::absolute(fs::path(get_text(root_box)),ec);
         if(ec||low(typed.lexically_normal().wstring())!=low(root.wstring())){
             alert(hwnd,L"扫描目录输入已改变。请先扫描新的目录，再进行操作。",MB_ICONWARNING);return false;}
     }
     return true;
 }
 void run_scan(bool quick=false){
     if(quick&&(has_scan||full_overview()||busy))return;
     if(scan_scope==0){
       if(selected_drives.empty()){choose_drives();if(selected_drives.empty())return;}
       for(const auto& drive:selected_drives)if(!same_drive(drive)){alert(hwnd,L"已选磁盘身份变化或已断开，请重新选择磁盘。",MB_ICONWARNING);return;}
       if(has_pending&&MessageBoxW(hwnd,L"重新扫描将清除当前操作预览。继续吗？",kProductName,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
       start(quick?Task::Inventory:Task::ScanAll);return;
     }
     auto p=get_text(root_box);if(p.empty()){browse();p=get_text(root_box);}if(p.empty())return;
     std::error_code ec;auto abs=fs::absolute(fs::path(p),ec);if(ec||!fs::is_directory(abs,ec)){alert(hwnd,L"选择的目录不存在，或无法访问。",MB_ICONWARNING);return;}
     if(has_pending&&MessageBoxW(hwnd,L"重新扫描将清除当前操作预览。继续吗？",kProductName,MB_YESNO|MB_ICONQUESTION)!=IDYES)return;
     root=abs.lexically_normal();start(quick?Task::Inventory:Task::Scan);
 }
 static std::wstring history_state(const std::string& state){
     static const std::map<std::string,std::wstring> labels={
         {"DONE",L"已完成"},{"UNDONE",L"已撤销"},{"ROLLBACK",L"已回滚"},
         {"PARTIAL",L"部分完成，已复制的文件保留"},{"PENDING",L"操作待恢复"},
         {"STAGEDALL",L"暂存完成，待提交或恢复"},{"RESTORING",L"等待继续恢复原名"},
         {"RECOVERY_FAILED",L"需要人工恢复，请保留现场"}
     };
     const auto found=labels.find(state);
     return found==labels.end()?wutf8(state):found->second;
 }
 static std::wstring history_action(const std::string& label){
     if(label=="Rename file")return L"重命名文件";
     if(label=="Batch rename")return L"按规则重命名";
     if(label=="Archive by file type")return L"按文件类型归档";
     if(label=="Archive by photo date")return L"按日期归档";
     for(const auto& entry:{
         std::pair<const char*,const wchar_t*>{"Merge directories: Structure reorganization -> ",L"构建整理目录  "},
         {"Merge directories: ",L"融合目录  "},{"Undo: ",L"撤销记录  "},
         {"Corrupt or unreadable journal: ",L"操作日志无法读取  "}
     }){
         const std::string prefix=entry.first;
         if(label.rfind(prefix,0)==0)return entry.second+wutf8(label.substr(prefix.size()));
     }
     const std::string prefix="Quarantine verified duplicate files";
     if(label.rfind(prefix,0)==0){
         auto detail=label.substr(prefix.size());
         const std::string suffix=" (scan-hash candidates)";
         if(detail.size()>=suffix.size()&&detail.compare(detail.size()-suffix.size(),suffix.size(),suffix)==0)
             detail.resize(detail.size()-suffix.size());
         if(detail.rfind(" from ",0)==0)return L"清退目录中的重复文件  "+wutf8(detail.substr(6));
         if(detail==" (manual selection)")return L"隔离所选重复文件";
         if(detail.empty())return L"按策略隔离重复文件";
     }
     return wutf8(label);
 }
 void add_preview_notes(const std::vector<std::string>& warnings){
     // The summary and item rows already express the plan's ordinary copy
     // and retention policy. Keep exceptional notes and unknown diagnostics.
     for(const auto& warning:warnings){
         const auto count_end=warning.find(' ');
         const std::string tail=count_end==std::string::npos?"":warning.substr(count_end);
         if(tail==" different-content name collisions: both versions will be kept"||
            tail==" name conflicts retained as independently named versions")continue;
         if(tail==" photos use file modification date, not EXIF capture date."){
             rows.push_back({L"归档日期",L"缺少拍摄日期，采用文件修改日期",wutf8(warning.substr(0,count_end))+L" 张照片",L""});
             continue;
         }
         bool described=false;
         for(const char* prefix:{"Copy-only merge. Both original backup folders", "User retention policy: ",
             "Manual duplicate quarantine, keeper minimum ", "Quarantine is reversible and ",
             "Quarantine does not free disk capacity.", "Preview uses full-scan SHA-256 candidates only.",
             "FAST MODE:", "HIGH RISK: zero active copies", "Read-only recommendation. Names and extensions",
             "Execution first builds a verified COPY", "Retiring old entries is a separate previewed action"})
             if(warning.rfind(prefix,0)==0){described=true;break;}
         if(described||warning.find("keepers remain OUTSIDE the retired folder.")!=std::string::npos)continue;
         if(warning=="No content outside the proposed output directory"){
             rows.push_back({L"无来源文件",L"整理目标以外没有可复制的文件",L"",L""});
         }else if(warning.rfind("Automatic backup grouping is incomplete: ",0)==0){
             rows.push_back({L"部分备份未能自动分组",wutf8(warning.substr(sizeof("Automatic backup grouping is incomplete: ")-1)),L"",L"请检查来源与目标路径"});
         }else rows.push_back({L"方案说明",wutf8(warning),L"",L""});
     }
 }
 bool preview_is_quarantine() const {
     return pending.name.rfind("Quarantine verified duplicate files",0)==0;
 }
 struct PreviewSummary {
     std::wstring title, location, changes, effect;
 };
 PreviewSummary describe_preview() const {
     if(pending_is_structure || pending_is_merge){
         const auto files=pending_is_structure?pending_structure.copies.size():pending_merge.copies.size();
         const auto dirs=pending_is_structure?pending_structure.directories.size():pending_merge.directories.size();
         const auto conflicts=pending_is_structure?pending_structure.renamed_conflicts:pending_merge.conflicts;
         const auto location=pending_is_structure?(root/pending_structure.output_base).wstring():
             pending_merge.source.wstring()+L" → "+pending_merge.destination.wstring();
         std::wstring changes=L"复制 "+std::to_wstring(files)+L" 个文件，新建 "+std::to_wstring(dirs)+L" 个目录";
         if(pending_is_structure)changes+=L" · "+bytes(pending_structure.bytes_to_copy);
         if(conflicts)changes+=L" · "+std::to_wstring(conflicts)+L" 处同名冲突，另存版本";
         return {pending_is_structure?L"构建整理目录":L"融合两个目录",location,changes,
             has_pending?L"源文件保留，已有文件受保护。普通撤销不会移除本次复制的文件。":L"无需新增文件或目录，可返回结果继续浏览。"};
     }
     if(preview_is_quarantine()){
         const auto& policy=pending.retention;
         const bool retire=!retirement_summary.source_folder.empty();
         std::wstring effect;
         if(policy.min_active_copies==0)effect=L"允许隔离最后一份活动副本，恢复依赖隔离区。";
         else effect=L"为每个隔离项在"+std::wstring(retire?L"所选目录外":L"当前操作根内")+L"保留至少 "+
             std::to_wstring(policy.min_active_copies)+(policy.distinct_file_objects?L" 个独立文件对象。":L" 个路径，硬链接可分别计数。");
         if(!policy.verify_bytes_before_change)effect+=L"快速模式仅核对本次扫描哈希和文件状态。";
         else if(policy.min_active_copies>0)effect+=L"执行前逐字节核对保留副本。";
         effect+=L"隔离仍占用磁盘空间。";
         const auto where=retire?(root/retirement_summary.source_folder).wstring():root.wstring();
         return {retire?L"清退所选目录的重复文件":L"隔离重复文件",where,
             L"移入隔离区 "+std::to_wstring(pending.moves.size())+L" 个文件",effect};
     }
     const bool archive=pending.name.rfind("Archive by ",0)==0;
     const auto title=archive?(pending.name=="Archive by file type"?L"按文件类型归档":L"按日期归档"):L"重命名";
     return {title,root.wstring(),L"变更 "+std::to_wstring(pending.moves.size())+L" 项",
         has_pending?L"核对来源和目标路径。完成后可在当前根查看操作历史。":
             L"当前条件没有产生路径变更，可调整命名或归档条件后重新预览。"};
 }
 void make_preview(dc::Plan plan){
     if(plan.name.rfind("Quarantine verified duplicate files from ",0)!=0)retirement_summary={};
     pending_is_merge=false;pending_is_structure=false;pending_merge={};pending_structure={};
     pending=std::move(plan);has_pending=!pending.moves.empty();
     change_view(9,true);
 }
 void preview_merge(dc::MergePlan proposal){
     pending_merge=std::move(proposal);pending={};pending_structure={};pending_is_merge=true;pending_is_structure=false;
     has_pending=!pending_merge.copies.empty()||!pending_merge.directories.empty();
     change_view(9,true);
 }
 void preview_structure(dc::StructurePlan proposal){
     pending_structure=std::move(proposal);pending={};pending_merge={};pending_is_merge=false;pending_is_structure=true;
     has_pending=!pending_structure.copies.empty()||!pending_structure.directories.empty();
     change_view(9,true);
 }
 void do_structure(){if(!need_scan())return;if(!structure_ready||structure_rejected){start(Task::Structure);return;}change_view(11);
     if(!structure_rejected)status_text(L"目录结构建议：选中一条原目录→目标目录的映射后双击编辑；确认无误，再选择智能操作 → 预览目录重组方案。");
 }
 void do_file_map_edit(const fs::path& from){
     if(!need_scan()||structure_rejected||!pending_is_structure)return;
     const auto it=std::find_if(pending_structure.mappings.begin(),pending_structure.mappings.end(),
         [&](const dc::StructureMapping& x){return low(fmt(x.from))==low(fmt(from));});
     if(it==pending_structure.mappings.end())return;
     const auto suggested=it->to.lexically_relative(structure_suggestion.output_base);
     std::vector<Prompt::Field> fields={{L"新结构中的相对文件路径（留空恢复自动映射）",fmt(suggested)}};
     if(!prompt(hwnd,L"编辑单个原文件的最终目标路径："+fmt(from),fields,font))return;
     try{
         const auto raw=fields[0].value;
         const auto target=fs::path(raw);
         if(!raw.empty()&&(target==L"."||!dc::is_inside(target)||target.empty()))
             throw std::runtime_error("File target must be relative and within the proposed output folder");
         previous_file_edit=structure_suggestion;
         file_edit_in_progress=true;
         auto& overrides=structure_suggestion.file_rules;
         overrides.erase(std::remove_if(overrides.begin(),overrides.end(),[&](const dc::StructureFileRule& x){
             return low(fmt(x.source))==low(fmt(from));}),overrides.end());
         if(!raw.empty())overrides.push_back({from,target});
         retirement_summary={};has_pending=false;pending={};pending_merge={};pending_structure={};
         pending_is_merge=false;pending_is_structure=false;EnableWindow(btn_apply,FALSE);
         start(Task::StructurePlan); // rebuild the entire plan with new explicit mapping
     }catch(const std::exception& e){alert(hwnd,wutf8(e.what()),MB_ICONWARNING);}
 }
 void do_map_edit(){if(!need_scan()||structure_rejected)return;
     if(view==9&&pending_is_structure){
         const int sel=ListView_GetNextItem(list,-1,LVNI_SELECTED);
         if(sel>=0&&size_t(sel)<visible.size()){
             const auto& row=rows[visible[size_t(sel)]];
             const auto it=std::find_if(pending_structure.mappings.begin(),pending_structure.mappings.end(),
                 [&](const dc::StructureMapping& m){return low(fmt(m.from))==low(fmt(row.launch));});
             if(!row.launch.empty()&&it!=pending_structure.mappings.end()){
                 do_file_map_edit(row.launch);return;
             }
         }
         alert(hwnd,L"请选择操作预览中的原文件映射行；双击可编辑文件级目标位置。",MB_ICONWARNING);return;
     }
     if(view!=11){change_view(11);return;}
     auto selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
     if(selected<0||size_t(selected)>=visible.size()){alert(hwnd,L"请先选中一个来源目录的映射行，再选择智能操作 → 编辑所选映射。",MB_ICONWARNING);return;}
     const int index=rows[visible[size_t(selected)]].suggestion;
     if(index<0||size_t(index)>=structure_suggestion.rules.size()){
         alert(hwnd,L"请选择具体的原目录映射行（双击也可编辑）。",MB_ICONWARNING);return;}
     auto& rule=structure_suggestion.rules[size_t(index)];
     std::vector<Prompt::Field> f={{L"建议目标根目录（单个文件夹名）",fmt(structure_suggestion.output_base)},
         {L"该来源目录在目标根目录下的相对位置",fmt(rule.target)}};
     if(!prompt(hwnd,L"编辑旧目录与新目录的对应关系",f,font))return;
     try{
         const auto output=fs::path(f[0].value),target=fs::path(f[1].value);
         if(output.empty()||output==L"."||!dc::is_inside(output)||
            std::distance(output.begin(),output.end())!=1||target.empty()||target==L"."||!dc::is_inside(target))
              throw std::runtime_error("Target must stay inside a single top-level output directory");
         structure_suggestion.output_base=output;rule.target=target;
         has_pending=false;pending={};pending_merge={};pending_structure={};pending_is_merge=false;pending_is_structure=false;
         EnableWindow(btn_apply,FALSE);refresh();
     }catch(const std::exception& e){alert(hwnd,wutf8(e.what()),MB_ICONWARNING);}
 }
 void do_structure_preview(){if(!need_scan()||structure_rejected)return;
     if(!structure_ready||structure_suggestion.rules.empty()){
         alert(hwnd,L"当前没有可执行的结构建议，请先完成扫描。",MB_ICONWARNING);return;}
     start(Task::StructurePlan);
 }
 void do_structure_reject(){if(!need_scan())return;
     if(MessageBoxW(hwnd,L"拒绝本次建议？\n不会复制、移动或删除任何文件。从智能操作 → 生成当前范围的目录重组映射可重新获得建议。",
         L"拒绝结构迁移",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
     structure_rejected=true;structure_suggestion={};has_pending=false;pending={};pending_merge={};pending_structure={};
     pending_is_merge=false;pending_is_structure=false;EnableWindow(btn_apply,FALSE);
     change_view(12);
 }
 void do_manual_quarantine(){
     if(!need_scan())return;
     if(view!=0&&view!=2){alert(hwnd,L"请在文件或副本视角中选中要隔离的文件。",MB_ICONINFORMATION);return;}
     const auto chosen=selected_paths();
     if(chosen.empty()){alert(hwnd,L"请先选择需要隔离的文件，Ctrl / Shift 可多选。",MB_ICONINFORMATION);return;}
     if(!ask_retention(L"隔离所选文件 · 设置副本保留策略"))return;
     invalidate_pending();
     requested_files=chosen;start(Task::QuarantinePlan);
 }
 void do_pair_assess(){
     if(!need_scan())return;
     fs::path a=has_comparison?pair_report.comparison.left:selected_dir;
     fs::path b=has_comparison?pair_report.comparison.right:fs::path{};
     // Two selected directory rows are accepted directly as dialog defaults.
     if(view==1||view==3||view==4){
         const auto picked=(view==1||view==3)?selected_paths():std::vector<fs::path>{};
         if(picked.size()==2){a=picked[0];b=picked[1];}
         if(view==4){
             const int pick=ListView_GetNextItem(list,-1,LVNI_SELECTED);
             if(pick>=0&&size_t(pick)<visible.size()){
                 const int i=rows[visible[size_t(pick)]].suggestion;
                 if(i>=0&&size_t(i)<overlaps.pairs.size()){
                     a=overlaps.pairs[size_t(i)].left;b=overlaps.pairs[size_t(i)].right;
                 }
             }
         }
     }
     std::vector<Prompt::Field> fields={{L"左侧目录（相对扫描根目录）",fmt(a)},
                                       {L"右侧目录（相对扫描根目录）",fmt(b)}};
     if(!prompt(hwnd,L"手工指定两个目录 · 内容与结构评估",fields,font))return;
     try{
         a=fs::path(fields[0].value);b=fs::path(fields[1].value);
         if(a==L".")a.clear();
         if(b==L".")b.clear();
         requested_source=a;requested_destination=b;start(Task::Pair);
     }catch(const std::exception& e){alert(hwnd,wutf8(e.what()),MB_ICONWARNING);}
 }
 void present_pair_report(){
     const auto pct=[](const std::optional<double>& v){return v?std::to_wstring(int(*v*100.0+0.5))+L"%":L"无定义";};
     std::wstring summary=L"内容交集："+std::to_wstring(pair_report.shared_file_occurrences)+
         L" 项，较小侧覆盖率："+pct(pair_report.smaller_coverage)+
         L"\n相同相对路径："+std::to_wstring(pair_report.same_relative_files)+
         L"，同名异内容："+std::to_wstring(pair_report.same_path_different_content)+
         L"\n完整目录树相同："+(pair_report.exact_tree?std::wstring(L"是"):std::wstring(L"否"))+
         L"\n\n详细差异已显示在结果中。是否生成左→右的目录融合预览？";
     if(!pair_report.disjoint_for_merge){alert(hwnd,summary+L"\n这两个目录有包含关系或涉及根目录，不能直接融合。",MB_ICONINFORMATION);return;}
     if(MessageBoxW(hwnd,summary.c_str(),L"双目录评估已完成",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)==IDYES)start(Task::MergePlan);
 }
 void do_merge(){if(!need_scan())return;
     fs::path source=selected_dir,destination;
     if(has_comparison){source=pair_report.comparison.left;destination=pair_report.comparison.right;}
     if(view==1||view==3){const auto picked=selected_paths();if(picked.size()==2){source=picked[0];destination=picked[1];}}
     std::vector<Prompt::Field> fields={{L"来源目录（相对扫描根目录，保留原文件）",fmt(source)},
         {L"目标目录（相对扫描根目录，保留已有内容）",destination.empty()?L"":fmt(destination)}};
     if(!prompt(hwnd,L"目录融合 · 仅分析这两个目录",fields,font))return;
     requested_source=fs::path(fields[0].value);requested_destination=fs::path(fields[1].value);
     start(Task::MergePlan);
 }
 void do_suggestion(size_t index){
     if(!need_scan()||index>=suggestions.size())return;
     const auto& rec=suggestions[index];
     const auto label=rec.confidence==3?L"高度匹配":rec.confidence==2?L"较可能同源":L"建议人工核对";
     std::wstring explanation=L"候选匹配级别："+std::wstring(label)+L"\n"+
         fmt(rec.source)+L"  →  "+fmt(rec.destination)+L"\n\n"+
         L"经过内容校验的重合文件："+std::to_wstring(rec.shared_contents)+
         L"\n相同相对路径且内容相同："+std::to_wstring(rec.same_relative_files)+
         L"\n源目录独有："+std::to_wstring(rec.source_only)+
         L"\n目标目录独有："+std::to_wstring(rec.destination_only)+
         L"\n同名但内容不同："+std::to_wstring(rec.changed_at_same_path)+
         L"\n尚未保留的冲突版本："+std::to_wstring(rec.unresolved_collisions)+
         L"\n\n目标方向基于文件数量，不代表较新备份。"+
         L"\n软件不会自动覆盖、移动或删除任何源文件。";
     if(!rec.useful){alert(hwnd,explanation+L"\n\n当前建议方向已无需融合；如果希望反向补齐，可使用‘融合目录’手动指定。");return;}
     if(MessageBoxW(hwnd,(explanation+L"\n\n是否根据这组建议生成详细融合预览？").c_str(),
                    L"确认融合建议（仅预览）",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES)return;
     requested_source=rec.source;requested_destination=rec.destination;start(Task::MergePlan);
 }
 void do_suggest(){if(!need_scan())return;if(!suggestions_ready){start(Task::Suggest);return;}
     if(view==10){const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
         if(selected>=0&&size_t(selected)<visible.size()){
             const int index=rows[visible[size_t(selected)]].suggestion;
             if(index>=0){do_suggestion(size_t(index));return;}
         }}
     change_view(10);
     if(!suggestions.empty())status_text(L"扫描发现 "+std::to_wstring(suggestions.size())+L" 组融合候选。双击所选融合建议生成预览；不会自动执行。");
 }
 void do_rename(){if(!need_scan())return;
     const auto picked=selected_paths();
     if(picked.size()!=1||(view!=0&&view!=1&&view!=2&&view!=3)){
         alert(hwnd,L"请先选择一个文件，或选择一个目录并设置该目录内的批量命名规则。",MB_ICONINFORMATION);return;}
     const bool selected_file=view==0||view==2;
     if(selected_file){
         const auto source=picked.front();
         std::vector<Prompt::Field> fields={{L"新文件名（含扩展名）",source.filename().wstring()}};
         fields.front().initial_selection_end=int(source.stem().wstring().size());
         const auto validate_name=[&](const std::vector<Prompt::Field>& input)->std::optional<Prompt::InputError>{
             const fs::path name(input[0].value);
             if(!dc::valid_file_name(name))
                 return Prompt::InputError{0,L"请输入有效的文件名，不能包含路径、保留名称或结尾的空格和点。"};
             if(low(name.wstring())==low(source.filename().wstring()))
                 return Prompt::InputError{0,L"请输入不同的文件名。当前版本不支持仅更改大小写。"};
             return {};
         };
         if(!prompt(hwnd,L"重命名文件",fields,font,L"生成预览",validate_name))return;
         invalidate_pending();
         rename_request={};
         rename_request.single_file=dc::FileRename{source,fs::path(fields[0].value)};
         start(Task::RenamePlan);
         return;
     }
     selected_dir=picked.front();
     std::vector<Prompt::Field> f={{L"当前文件夹（相对根目录，. 代表根目录）",fmt(selected_dir)},
          {L"名称筛选（* 匹配任意字符，? 匹配一个字符）",L"*"},{L"查找字符串（空表示不替换）",L""},{L"替换为",L""},
          {L"新名称前缀",L""},{L"新名称后缀",L""},{L"编号起点（0 表示不编号）",L"0"},
          {L"编号最小位数",L"3"},{L"包含文件",L"1",true},{L"包含文件夹",L"1",true}};
     f[6].number_range={{0,std::numeric_limits<int>::max()}};
     f[7].number_range={{1,255}};
     if(!prompt(hwnd,L"批量重命名",f,font,L"生成预览"))return;
     invalidate_pending();
     try{dc::RenameOptions o;o.dir=fs::path(f[0].value);if(o.dir==L".")o.dir.clear();o.pattern=f[1].value;o.find=f[2].value;o.replace=f[3].value;o.prefix=f[4].value;o.suffix=f[5].value;o.number_start=std::stoi(f[6].value);o.digits=std::stoi(f[7].value);o.include_files=f[8].value!=L"0";o.include_dirs=f[9].value!=L"0";
         rename_request=std::move(o);start(Task::RenamePlan);}catch(const std::exception&e){alert(hwnd,wutf8(e.what()),MB_ICONWARNING);}
 }
 void do_archive(){if(!need_scan())return;
     std::vector<Prompt::Field> f={{L"归档方式",L""},
         {L"归档目标子目录（在所选根目录内）",L"Organized"},
         {L"递归处理子目录",L"1",true},
         {L"缺少 EXIF 时使用文件修改时间",L"1",true}};
     f[0].choices={L"按文件类型",L"按拍摄年月"};
     auto check_target=[](const std::vector<Prompt::Field>& inputs)->std::optional<Prompt::InputError>{
         const fs::path target(inputs[1].value);
         if(target.empty()||target==L"."||!dc::is_inside(target))
             return Prompt::InputError{1,L"请输入扫描目录内的目标子目录，例如 Organized。"};
         return {};
     };
     if(!prompt(hwnd,L"归档当前目录的文件",f,font,L"生成预览",check_target))return;
     invalidate_pending();
     try{dc::ArchiveOptions o;o.mode=f[0].selection==1?dc::ArchiveMode::ByYearMonth:dc::ArchiveMode::ByType;
         o.destination=fs::path(f[1].value);o.recursive=f[2].value!=L"0";o.use_mtime_fallback=f[3].value!=L"0";
         archive_request=o;start(Task::ArchivePlan);}catch(const std::exception&e){alert(hwnd,wutf8(e.what()),MB_ICONWARNING);}
 }
 void do_retire(){if(!need_scan())return;
     std::vector<Prompt::Field> fields={{L"要清退的目录（相对扫描根目录；默认使用选中的目录）",fmt(selected_dir)}};
     if(!prompt(hwnd,L"旧目录安全清退 · 根据您选择的保留份数隔离文件",fields,font))return;
     invalidate_pending();
     requested_retirement=fs::path(fields[0].value);
     if(requested_retirement.empty()||requested_retirement==L"."||!dc::is_inside(requested_retirement)){
         alert(hwnd,L"必须选择根目录下的一个普通旧目录，不可选择整个根目录。",MB_ICONWARNING);return;
     }
     if(!ask_retention(L"历史旧目录清退 · 保留策略"))return;
     start(Task::RetirementPlan);
 }
 void do_clean(){if(!need_scan()||!ask_retention(L"当前扫描范围的多余副本 · 保留策略"))return;
     invalidate_pending();
     start(Task::CleanPlan);}
 void do_apply(){
     if(!has_pending||busy||!need_scan())return;
     if(view!=9)change_view(9,true);
     const auto summary=describe_preview();
     std::wstring question=summary.title+L"\n操作根目录  "+root.wstring()+L"\n";
     if(summary.location!=root.wstring())question+=summary.location+L"\n";
     question+=L"\n"+summary.changes+L"\n"+summary.effect;
     if(pending_is_merge||pending_is_structure){
         const auto& copies=pending_is_merge?pending_merge.copies:pending_structure.copies;
         const auto& directories=pending_is_merge?pending_merge.directories:pending_structure.directories;
         if(!copies.empty()){
             const auto& copy=copies.front();
             question+=L"\n\n复制示例\n"+copy.from.wstring()+L" → "+copy.to.wstring();
         }else if(!directories.empty()){
             question+=L"\n\n新建文件夹示例\n"+directories.front().wstring();
         }
     }else if(!pending.moves.empty()){
         const auto& move=pending.moves.front();
         question+=pending.moves.size()==1?L"\n\n路径变更\n":L"\n\n路径变更示例\n";
         question+=move.from.wstring()+L" → "+move.to.wstring();
     }
     if(GetWindowTextLengthW(search)||ListView_GetSelectedCount(list))
         question+=L"\n\n筛选和选中行只用于查看。确认后将执行上述全部变更。";
     question+=L"\n\n现在执行整份计划吗？选择“否”可返回预览。";
     if(MessageBoxW(hwnd,question.c_str(),L"确认执行操作计划",MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2)!=IDYES)return;
     if(preview_is_quarantine()&&(pending.retention.min_active_copies==0||!pending.retention.verify_bytes_before_change)){
         std::wstring risk;
         if(pending.retention.min_active_copies==0)
             risk=L"按此方案隔离后，活动目录中可能一份也不剩。请确认可以从隔离区恢复。\n";
         if(!pending.retention.verify_bytes_before_change)
             risk+=L"快速模式可能漏过扫描后大小和时间戳均未改变的内容修改。\n";
         risk+=L"\n仍要按此策略隔离吗？";
         if(MessageBoxW(hwnd,risk.c_str(),L"确认隔离风险",MB_ICONWARNING|MB_YESNO|MB_DEFBUTTON2)!=IDYES)return;
     }
     start(pending_is_structure?Task::StructureApply:pending_is_merge?Task::MergeApply:Task::Apply);
 }
 void do_undo(){if(busy||!need_scan())return;if(MessageBoxW(hwnd,L"撤销最近一次已提交的操作？\n如果随后有手工修改或文件被占用，撤销可能失败。",L"确认撤销",MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2)==IDYES)start(Task::Undo);}
 void do_photo(){if(!need_scan())return;
     std::vector<Prompt::Field> fields={{L"相似容差（0 最严格，建议 5～8）",std::to_wstring(photo_distance)}};
     fields[0].number_range={{0,16}};
     if(!prompt(hwnd,L"查找相似照片",fields,font,L"开始分析"))return;
     photo_distance=std::stoi(fields[0].value);start(Task::Photo);
 }
 void do_relations(){if(!need_scan())return;start(Task::Relations);}
 void export_csv(){if(!has_scan&&!full_overview())return;wchar_t filename[MAX_PATH]=L"文件整理报告.csv";
     OPENFILENAMEW of{};of.lStructSize=sizeof(of);of.hwndOwner=hwnd;of.lpstrFilter=L"CSV 文件 (*.csv)\0*.csv\0所有文件\0*.*\0";of.lpstrFile=filename;of.nMaxFile=MAX_PATH;of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST;of.lpstrDefExt=L"csv";
     if(!GetSaveFileNameW(&of))return;
     std::ofstream out(fs::path(filename),std::ios::binary);if(!out){alert(hwnd,L"无法写入 CSV 文件。",MB_ICONWARNING);return;}
     out<<"\xEF\xBB\xBF";out<<"\"名称/状态\",\"相对路径/内容\",\"大小/数量\",\"说明\"\r\n";
     if(results_stale)out<<dc::csv_quote("结果状态")<<","<<dc::csv_quote("扫描结果已过期")<<","<<dc::csv_quote("仅供参考")<<","<<dc::csv_quote("文件操作后未自动重新扫描")<<"\r\n";
     auto metadata=[&](const std::wstring& name,const std::wstring& value,const std::wstring& detail){
         out<<dc::csv_quote(utf8w(name))<<","<<dc::csv_quote(utf8w(value))<<",\"\","<<dc::csv_quote(utf8w(detail))<<"\r\n";
     };
     metadata(L"导出范围",L"仅当前显示结果",L"候选数按路径计数并包含自身；不是全量库存导出");
     metadata(L"计数口径",L"文件/目录候选：本次扫描全部根；副本组：当前根或总览标注范围",L"精确数和计划合格保留者仅限当前根；合格保留者须按冻结策略复核");
     if(has_scan)metadata(L"当前操作根",fmt(inv.root),L"快照非实时；候选不是已验证保留者");
     auto export_root=[&](const dc::Inventory& volume){if(volume.root.empty())return;
         metadata(L"扫描根",fmt(volume.root),volume.index_error.empty()?L"":L"索引未持久化；内存结果可用："+wutf8(volume.index_error));};
     if(has_scan)export_root(inv);
     if(has_full_scan)for(size_t i=0;i<all_scan.volumes.size();++i){
         if(!has_scan||i!=focused_volume)export_root(all_scan.volumes[i]);
     }
     for(auto i:visible){auto& r=rows[i];out<<dc::csv_quote(utf8w(r.a))<<","<<dc::csv_quote(utf8w(r.b))<<","<<dc::csv_quote(utf8w(r.c))<<","<<dc::csv_quote(utf8w(r.d))<<"\r\n";}
     out.flush();if(out)alert(hwnd,L"CSV 已导出（UTF-8 BOM，可在 Excel 中打开）。");else alert(hwnd,L"CSV 写入失败。",MB_ICONWARNING);
 }
 bool require_exact(int command){
     if(exact_ready)return true;
     if(!inv.complete()){alert(hwnd,L"当前扫描存在读取异常。请先处理异常，再进行需要完整内容判定的分析。",MB_ICONWARNING);return false;}
     deferred_command=command;deferred_paths=selected_paths();start(Task::Verify);return false;
 }
 void run_action(int command){
     if(busy)return;
     if(filter_pending){KillTimer(hwnd,2);filter_pending=false;refresh();}
     if(command==CMD_SELECT_ALL){restoring_selection=true;ListView_SetItemState(list,-1,LVIS_SELECTED,LVIS_SELECTED);restoring_selection=false;refresh_selection();return;}
     if(command==CMD_SELECT_NONE){restoring_selection=true;ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);restoring_selection=false;refresh_selection();return;}
     if(command==ID_EXPORT){export_csv();return;}
     if(command==CMD_OPEN){open_selected_location();return;}
     if(command==CMD_POLICY){ask_retention(L"副本保留与内容核验设置");return;}
     if(command==CMD_HISTORY){if(ensure_result_scope())change_view(7);return;}
     if(command==CMD_PREVIEW){if(ensure_result_scope())change_view(9);return;}
     if(results_stale){alert(hwnd,L"结果已过期，请重新扫描后再进行分析或文件操作。不会自动重读整个磁盘。",MB_ICONINFORMATION);return;}
     if(!ensure_result_scope()||!need_scan())return;
     if(command==CMD_EXACT){if(exact_ready)refresh();else {deferred_command=0;deferred_paths=selected_paths();start(Task::Verify);}return;}
     const bool needs_exact=command==ID_SUGGEST||command==ID_STRUCTURE||command==ID_RELATIONS;
     if(needs_exact&&!require_exact(command))return;
     switch(command){
       case CMD_ISOLATE_SELECTED:do_manual_quarantine();break;
       case CMD_PAIR:case ID_COMPARE:do_pair_assess();break;
       case ID_RENAME:do_rename();break;
       case ID_ARCHIVE:do_archive();break;
       case ID_QUARANTINE:do_clean();break;
       case ID_APPLY:do_apply();break;
       case ID_UNDO:do_undo();break;
       case ID_PHOTO:do_photo();break;
       case ID_RELATIONS:do_relations();break;
       case ID_MERGE:do_merge();break;
       case ID_SUGGEST:do_suggest();break;
       case ID_STRUCTURE:do_structure();break;
       case ID_MAP_EDIT:do_map_edit();break;
       case ID_STRUCTURE_PREVIEW:do_structure_preview();break;
       case ID_STRUCTURE_REJECT:do_structure_reject();break;
       case ID_RETIRE:do_retire();break;
       default:break;
     }
 }
 HMENU action_menu(int category){
     HMENU menu=CreatePopupMenu();if(!menu)return nullptr;
     const bool ready=has_scan||full_overview();
     const bool files=showing_files();
     const bool dirs=full_overview()?scope_view==2:(view==1||view==3||view==4||view==8);
     const bool selected=ListView_GetSelectedCount(list)>0;
     auto item=[&](int id,const wchar_t* text,bool enabled=true){
         const bool read_only=id==CMD_OPEN||id==CMD_SELECT_ALL||id==CMD_SELECT_NONE||id==ID_EXPORT||id==CMD_POLICY;
         AppendMenuW(menu,MF_STRING|((ready&&enabled&&(!results_stale||read_only))?MF_ENABLED:MF_GRAYED),UINT_PTR(id),text);
     };
     auto sep=[&]{AppendMenuW(menu,MF_SEPARATOR,0,nullptr);};
     if(category==0){
         item(CMD_OPEN,L"在资源管理器查看所选位置",selected&&(full_overview()||!selected_paths().empty()));
         item(CMD_ISOLATE_SELECTED,L"隔离所选文件…（先预览）",files&&selected);
         item(ID_RENAME,files?L"重命名所选文件…\tF2":L"批量重命名目录内的项目…",
              (files||(full_overview()?scope_view==2:view==1||view==3))&&ListView_GetSelectedCount(list)==1);
         item(CMD_PAIR,L"评估两个目录…",dirs);
         sep();item(CMD_SELECT_ALL,L"选择当前显示的全部行");item(CMD_SELECT_NONE,L"清除选择");
         item(ID_EXPORT,L"导出当前显示的结果 CSV…");
         sep();item(CMD_POLICY,L"副本保留与内容核验设置…");item(ID_UNDO,L"撤销最近一次操作…",has_scan);
     }else if(category==1){
         item(ID_MERGE,L"融合两个目录…（保留冲突版本）",dirs);
         item(ID_RETIRE,L"按保留策略清退选中目录…",(full_overview()?scope_view==2:view==1||view==3)&&selected);
         item(ID_QUARANTINE,L"按策略隔离当前范围的多余副本…");
         sep();item(ID_ARCHIVE,L"当前扫描范围按类型或日期归档…");item(ID_PHOTO,L"分析当前范围的相似照片…");
         sep();item(CMD_EXACT,L"精确核验当前范围的副本（按需读取）");item(ID_RELATIONS,L"分析当前范围的相似目录");
     }else{
         item(ID_SUGGEST,L"分析当前范围的目录融合建议");item(ID_STRUCTURE,L"生成当前范围的目录重组映射");
         sep();item(ID_MAP_EDIT,L"编辑所选映射…",has_scan&&(view==11||view==9));
         item(ID_STRUCTURE_PREVIEW,L"预览目录重组方案",has_scan&&structure_ready&&!structure_rejected);
         item(ID_STRUCTURE_REJECT,L"拒绝本次重组建议…",has_scan&&structure_ready&&!structure_rejected);
     }
     return menu;
 }
 void show_action_menu(int category){
     if(busy)return;
     HWND button=category==0?btn_basic:category==1?btn_theme:btn_smart;
     RECT rc{};GetWindowRect(button,&rc);HMENU menu=action_menu(category);if(!menu)return;
     const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_LEFTALIGN|TPM_TOPALIGN,rc.left,rc.bottom,0,hwnd,nullptr);
     DestroyMenu(menu);if(command)run_action(int(command));
 }
 void show_row_actions(){
     if(busy||(!has_scan&&!full_overview()))return;
     POINT at{};GetCursorPos(&at);POINT client=at;ScreenToClient(list,&client);
     LVHITTESTINFO hit{};hit.pt=client;const int index=ListView_HitTest(list,&hit);
     if(index>=0&&!(ListView_GetItemState(list,index,LVIS_SELECTED)&LVIS_SELECTED)){
         ListView_SetItemState(list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
         ListView_SetItemState(list,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
     }
     HMENU menu=CreatePopupMenu();if(!menu)return;
     AppendMenuW(menu,MF_POPUP,UINT_PTR(action_menu(0)),L"基础操作");
     AppendMenuW(menu,MF_POPUP,UINT_PTR(action_menu(1)),L"主题操作");
     AppendMenuW(menu,MF_POPUP,UINT_PTR(action_menu(2)),L"智能操作");
     AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,CMD_PREVIEW,L"查看操作预览");
     const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,at.x,at.y,0,hwnd,nullptr);
     DestroyMenu(menu);if(command)run_action(command);
 }
 void reveal_in_explorer(const fs::path& path){
     std::error_code error;
     const auto info=fs::status(path,error);
     if(fs::is_directory(info)){
         if(reinterpret_cast<INT_PTR>(ShellExecuteW(hwnd,L"explore",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL))>32)return;
     }else if(fs::is_regular_file(info)){
         PIDLIST_ABSOLUTE item=nullptr;
         if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr))){
             const HRESULT opened=SHOpenFolderAndSelectItems(item,0,nullptr,0);
             CoTaskMemFree(item);
             if(SUCCEEDED(opened))return;
         }
     }
     status_text(L"无法在资源管理器中定位所选项目，请检查路径或重新扫描。");
 }
 void open_selected_location(){
     if(full_overview()){open_scope_selected();return;}
     const auto paths=selected_paths();
     if(!paths.empty())reveal_in_explorer(root/paths.front());
 }
 void open_selected(){
     const int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
     if(selected<0||size_t(selected)>=visible.size())return;
     const auto& row=rows[visible[size_t(selected)]];
     if(!full_overview()&&!results_stale){
         if(view==9&&pending_is_structure&&!row.launch.empty()){do_file_map_edit(row.launch);return;}
         if(view==10&&row.suggestion>=0){do_suggestion(size_t(row.suggestion));return;}
         if(view==11&&row.suggestion>=0){do_map_edit();return;}
     }
     open_selected_location();
 }
 bool handle_key(const MSG& message){
     if(message.message!=WM_KEYDOWN||(message.hwnd!=hwnd&&!IsChild(hwnd,message.hwnd)))return false;
     const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
     const bool alt=(GetKeyState(VK_MENU)&0x8000)!=0;
     if(alt)return false;
     if(message.wParam==VK_TAB&&!ctrl){
         HWND current=GetFocus();
         if(!IsChild(hwnd,current))current=nullptr;
         HWND next=GetNextDlgTabItem(hwnd,current,(GetKeyState(VK_SHIFT)&0x8000)!=0);
         if(next){SetFocus(next);SendMessageW(hwnd,WM_CHANGEUISTATE,MAKEWPARAM(UIS_CLEAR,UISF_HIDEFOCUS),0);}
         return next!=nullptr;
     }
     if(!busy&&(has_scan||full_overview())&&ctrl&&message.wParam=='F'){
         SetFocus(search);SendMessageW(search,EM_SETSEL,0,-1);return true;
     }
     if(message.hwnd==search&&message.wParam==VK_ESCAPE&&GetWindowTextLengthW(search)>0){
         SetWindowTextW(search,L"");return true;
     }
     if(!ctrl&&message.wParam==VK_F2&&message.hwnd==list&&!busy&&!results_stale&&showing_files()&&ListView_GetSelectedCount(list)==1){
         run_action(ID_RENAME);return true;
     }
     if(!ctrl&&message.wParam==VK_RETURN){
         if(message.hwnd==list&&!busy){open_selected();return true;}
         wchar_t cls[16]{};
         GetClassNameW(message.hwnd,cls,16);
         if(lstrcmpiW(cls,L"Button")==0&&IsWindowEnabled(message.hwnd)){
             SendMessageW(message.hwnd,BM_CLICK,0,0);return true;
         }
     }
     return false;
 }
 void init_controls(){
     load_retention();
     font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
     bold_font=CreateFontW(-18,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
     display_font=CreateFontW(-25,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
     small_font=CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
     page_brush=CreateSolidBrush(kPage);
     scope_pick=create(hwnd,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,ID_SCAN_SCOPE);
     SendMessageW(scope_pick,CB_ADDSTRING,0,(LPARAM)L"选择磁盘扫描");SendMessageW(scope_pick,CB_ADDSTRING,0,(LPARAM)L"指定目录扫描");
     SendMessageW(scope_pick,CB_SETCURSEL,0,0);
     root_box=create(hwnd,L"EDIT",L"未选择磁盘（请点击选择磁盘…）",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,ID_ROOT,WS_EX_CLIENTEDGE);
     btn_browse=create(hwnd,L"BUTTON",L"选择磁盘…",BS_PUSHBUTTON|WS_TABSTOP,ID_BROWSE);
     btn_scan=create(hwnd,L"BUTTON",L"扫描所选磁盘",BS_DEFPUSHBUTTON|WS_TABSTOP,ID_SCAN);
     btn_inventory=create(hwnd,L"BUTTON",L"快速盘点",BS_PUSHBUTTON|WS_TABSTOP,ID_QUICK_INVENTORY);
     btn_stop=create(hwnd,L"BUTTON",L"停止",BS_PUSHBUTTON|WS_TABSTOP,ID_STOP);
     progress_stage=create(hwnd,L"STATIC",L"等待扫描 · 先指定磁盘或目录",SS_LEFT|SS_ENDELLIPSIS,ID_PROGRESS_STAGE);
     scan_bar=create(hwnd,PROGRESS_CLASSW,L"",PBS_SMOOTH,ID_SCAN_PROGRESS);
     SendMessageW(scan_bar,PBM_SETRANGE32,0,1000);SendMessageW(scan_bar,PBM_SETBARCOLOR,0,kAccent);
     progress_metrics=create(hwnd,L"STATIC",L"统计完成后显示读取容量、速度、已用时间和本阶段预计剩余时间。",SS_LEFT|SS_ENDELLIPSIS,ID_PROGRESS_METRICS);
     progress_path=create(hwnd,L"STATIC",L"不会自动扫描未选中的磁盘。",SS_LEFT|SS_PATHELLIPSIS,ID_PROGRESS_PATH);
     welcome_title=create(hwnd,L"STATIC",L"选择要检查的位置",SS_CENTER,ID_WELCOME_TITLE);
     welcome_detail=create(hwnd,L"STATIC",L"",SS_CENTER,ID_WELCOME_DETAIL);
     choose_folder=create(hwnd,L"BUTTON",L"选择文件夹…",BS_PUSHBUTTON|WS_TABSTOP,ID_CHOOSE_FOLDER);
     choose_disks=create(hwnd,L"BUTTON",L"选择磁盘…",BS_PUSHBUTTON|WS_TABSTOP,ID_CHOOSE_DRIVES);
     lens_pick=create(hwnd,WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,ID_LENS_PICK);
     duplicate_view=create(hwnd,L"BUTTON",L"重复文件",BS_PUSHBUTTON|WS_TABSTOP,ID_DUPLICATE_VIEW);
     search=create(hwnd,L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,ID_SEARCH,WS_EX_CLIENTEDGE);
     SendMessageW(search,EM_SETCUEBANNER,TRUE,(LPARAM)L"筛选名称、路径或哈希（Ctrl+F）");
     clear_filter=create(hwnd,L"BUTTON",L"清除筛选",BS_PUSHBUTTON|WS_TABSTOP,ID_CLEAR_FILTER);
     btn_basic=create(hwnd,L"BUTTON",L"基础操作 ▾",BS_PUSHBUTTON|WS_TABSTOP,ID_BASIC_ACTIONS);
     btn_theme=create(hwnd,L"BUTTON",L"主题操作 ▾",BS_PUSHBUTTON|WS_TABSTOP,ID_THEME_ACTIONS);
     btn_smart=create(hwnd,L"BUTTON",L"智能操作 ▾",BS_PUSHBUTTON|WS_TABSTOP,ID_SMART_ACTIONS);
     btn_all_results=create(hwnd,L"BUTTON",L"返回全部结果",BS_PUSHBUTTON|WS_TABSTOP,ID_ALL_RESULTS);
     btn_preview=create(hwnd,L"BUTTON",L"操作预览",BS_PUSHBUTTON|WS_TABSTOP,ID_SHOW_PREVIEW);
     btn_history=create(hwnd,L"BUTTON",L"操作历史",BS_PUSHBUTTON|WS_TABSTOP,ID_SHOW_HISTORY);
     btn_apply=create(hwnd,L"BUTTON",L"确认执行…",BS_PUSHBUTTON|WS_TABSTOP,ID_APPLY);
     selection_context=create(hwnd,L"STATIC",L"选择结果后操作 · Ctrl / Shift 多选",SS_LEFT|SS_ENDELLIPSIS,ID_SELECTION_CONTEXT);
     list=create(hwnd,WC_LISTVIEWW,L"",LVS_REPORT|LVS_OWNERDATA|LVS_SHOWSELALWAYS|WS_TABSTOP,ID_LIST,WS_EX_CLIENTEDGE);
     ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
     const wchar_t* heads[]={L"名称 / 状态",L"位置 / 对应关系",L"大小 / 数量",L"副本 / 说明"};
     for(int i=0;i<4;++i){LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;col.pszText=const_cast<wchar_t*>(heads[i]);col.cx=250;col.iSubItem=i;ListView_InsertColumn(list,i,&col);}
     status=create(hwnd,L"STATIC",L"请指定扫描范围",SS_LEFT|SS_ENDELLIPSIS,ID_STATUS);
     for(HWND h:{root_box,scope_pick,lens_pick,btn_browse,btn_scan,btn_stop,progress_metrics,progress_path,search,btn_basic,btn_theme,btn_smart,btn_all_results,btn_preview,btn_history,btn_apply,selection_context,list,status})set_font(h,font);
     set_font(welcome_title,display_font);
     for(HWND h:{welcome_detail,choose_folder,choose_disks,duplicate_view,clear_filter,btn_inventory})set_font(h,font);
     SendMessageW(root_box,EM_SETCUEBANNER,TRUE,(LPARAM)L"输入或粘贴文件夹路径");
     set_font(progress_stage,bold_font);EnableWindow(root_box,FALSE);EnableWindow(btn_stop,FALSE);
     SetTimer(hwnd,1,250,nullptr);refresh();refresh_navigation();SetFocus(choose_folder);
 }
 ~App(){cancel.store(true);if(worker.joinable())worker.join();if(font)DeleteObject(font);if(bold_font)DeleteObject(bold_font);if(display_font)DeleteObject(display_font);if(small_font)DeleteObject(small_font);if(page_brush)DeleteObject(page_brush);}
};

LRESULT CALLBACK main_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);app=static_cast<App*>(cs->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)app);app->hwnd=hwnd;return DefWindowProcW(hwnd,msg,w,l);}
    if(!app)return DefWindowProcW(hwnd,msg,w,l);
    try{
        switch(msg){
            case WM_CREATE:app->init_controls();return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);app->paint(dc);EndPaint(hwnd,&ps);return 0;}
            case WM_CTLCOLORSTATIC:{HDC dc=(HDC)w;SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(l)==app->welcome_title?kText:kMuted);return (LRESULT)app->page_brush;}
            case WM_CTLCOLORLISTBOX:case WM_CTLCOLOREDIT:{HDC dc=(HDC)w;SetBkColor(dc,RGB(255,255,255));SetTextColor(dc,kText);return (LRESULT)GetStockObject(WHITE_BRUSH);}
            case WM_SIZE:if(app->list)app->layout(LOWORD(l),HIWORD(l));return 0;
            case WM_GETMINMAXINFO:{auto* mm=(MINMAXINFO*)l;mm->ptMinTrackSize={800,580};return 0;}
            case WM_TIMER:
                if(w==1)app->render_progress();
                else if(w==2){KillTimer(hwnd,2);app->filter_pending=false;app->refresh();}
                return 0;
            case WM_COMMAND:{const int id=LOWORD(w);
                if(id==ID_SEARCH&&HIWORD(w)==EN_CHANGE){
                    app->restoring_selection=true;ListView_SetItemState(app->list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);app->restoring_selection=false;
                    app->filter_pending=true;EnableWindow(app->clear_filter,!app->busy&&GetWindowTextLengthW(app->search)>0);app->refresh_selection();SetTimer(hwnd,2,180,nullptr);return 0;}
                if(app->busy&&id!=ID_STOP)return 0;
                if(id==ID_STOP){app->cancel.store(true);EnableWindow(app->btn_stop,FALSE);app->render_progress();return 0;}
                if(id==ID_LENS_PICK&&HIWORD(w)==CBN_SELCHANGE){
                    const int selected=int(SendMessageW(app->lens_pick,CB_GETCURSEL,0,0));
                    if(selected>=0&&app->full_overview()){
                        app->restoring_selection=true;ListView_SetItemState(app->list,-1,0,LVIS_SELECTED|LVIS_FOCUSED);app->restoring_selection=false;
                        app->scope_view=selected;SetWindowTextW(app->search,L"");app->refresh();app->refresh_navigation();
                    }else if(selected>=0&&app->has_scan)app->change_view(selected);
                    return 0;
                }
                if(id==ID_SCAN_SCOPE&&HIWORD(w)==CBN_SELCHANGE){app->switch_scan_scope(int(SendMessageW(app->scope_pick,CB_GETCURSEL,0,0)));return 0;}
                if(id==ID_ROOT&&HIWORD(w)==EN_CHANGE&&app->scan_scope==1){
                    if(app->quick_result)app->reset_progress();
                    if(app->has_pending){app->invalidate_pending();app->refresh();}
                    if(!app->has_scan)app->refresh_navigation();
                    else EnableWindow(app->btn_scan,GetWindowTextLengthW(app->root_box)>0);
                    return 0;
                }
                // Focus, edit and combo notifications are not action commands.
                if(HIWORD(w)!=BN_CLICKED)return 0;
                if(id==ID_CHOOSE_FOLDER||id==ID_CHOOSE_DRIVES){
                    const int scope=id==ID_CHOOSE_FOLDER?1:0;
                    app->switch_scan_scope(scope);
                    if(app->scan_scope==scope)app->browse();
                }else if(id==ID_CLEAR_FILTER){
                    SetWindowTextW(app->search,L"");SetFocus(app->search);
                }else if(id==ID_DUPLICATE_VIEW){
                    app->duplicate_shortcut();
                }else if(id==ID_QUICK_INVENTORY)app->run_scan(true);
                else if(id==ID_BROWSE)app->browse();else if(id==ID_SCAN)app->run_scan();
                else if(id==ID_BASIC_ACTIONS)app->show_action_menu(0);
                else if(id==ID_THEME_ACTIONS)app->show_action_menu(1);
                else if(id==ID_SMART_ACTIONS)app->show_action_menu(2);
                else if(id==ID_ALL_RESULTS)app->return_to_all_results();
                else if(id==ID_SHOW_PREVIEW)app->run_action(CMD_PREVIEW);
                else if(id==ID_SHOW_HISTORY)app->run_action(CMD_HISTORY);
                else app->run_action(id);
                return 0;
            }
            case WM_NOTIFY:{auto* hdr=(NMHDR*)l;
                if(hdr->idFrom==ID_LIST){
                    if(hdr->code==LVN_GETEMPTYMARKUP){
                        auto* empty=reinterpret_cast<NMLVEMPTYMARKUP*>(l);
                        empty->dwFlags=EMF_CENTERED;
                        const auto message=app->empty_results_message();
                        lstrcpynW(empty->szMarkup,message.c_str(),int(std::size(empty->szMarkup)));
                        return TRUE;
                    }
                    if(hdr->code==NM_CUSTOMDRAW){auto* cd=reinterpret_cast<NMLVCUSTOMDRAW*>(l);
                        if(cd->nmcd.dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                        if(cd->nmcd.dwDrawStage==CDDS_ITEMPREPAINT){cd->clrTextBk=(cd->nmcd.dwItemSpec%2==0)?RGB(255,255,255):RGB(249,251,253);cd->clrText=kText;return CDRF_DODEFAULT;}
                    }
                    if(hdr->code==LVN_GETDISPINFOW){auto* di=(NMLVDISPINFOW*)l;size_t n=size_t(di->item.iItem);
                        if(n<app->visible.size()&&(di->item.mask&LVIF_TEXT)){
                            auto& row=app->rows[app->visible[n]];auto& text=di->item.iSubItem==0?row.a:di->item.iSubItem==1?row.b:di->item.iSubItem==2?row.c:row.d;
                            di->item.pszText=const_cast<wchar_t*>(text.c_str());}return 0;
                    }
                    if(hdr->code==LVN_ITEMCHANGED||hdr->code==LVN_ODSTATECHANGED){app->refresh_selection();return 0;}
                    if(hdr->code==LVN_COLUMNCLICK){auto* n=(NMLISTVIEW*)l;app->sort_desc=app->sort_column==n->iSubItem?!app->sort_desc:false;app->sort_column=n->iSubItem;app->rebuild();return 0;}
                    if(hdr->code==NM_RCLICK){app->show_row_actions();return 0;}
                    if(hdr->code==NM_DBLCLK){if(!app->busy)app->open_selected();return 0;}
                }
                return 0;
            }
            case WMU_DONE:{std::unique_ptr<JobResult> result(reinterpret_cast<JobResult*>(l));app->complete(std::move(result));return 0;}
            case WM_CLOSE:
                if(app->busy){
                    if(!app->task_cancelable){alert(hwnd,L"正在执行已确认的文件操作，请等待完成后关闭。",MB_ICONWARNING);return 0;}
                    if(MessageBoxW(hwnd,L"后台任务仍在进行。停止后退出吗？",L"退出",MB_YESNO|MB_ICONQUESTION)!=IDYES)return 0;
                    if(!app->busy){DestroyWindow(hwnd);return 0;}
                    app->close_requested=true;app->cancel.store(true);EnableWindow(app->btn_stop,FALSE);app->render_progress();return 0;
                }
                DestroyWindow(hwnd);return 0;
            case WM_DESTROY:{KillTimer(hwnd,1);KillTimer(hwnd,2);MSG pending{};
                while(PeekMessageW(&pending,hwnd,WMU_DONE,WMU_DONE,PM_REMOVE))delete reinterpret_cast<JobResult*>(pending.lParam);
                PostQuitMessage(0);return 0;}
        }
    }catch(const std::exception& e){alert(hwnd,L"发生错误：\n"+wutf8(e.what()),MB_ICONERROR);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}

}
int WINAPI wWinMain(HINSTANCE inst,HINSTANCE,LPWSTR,int show){
    SetProcessDPIAware();INITCOMMONCONTROLSEX ic{};ic.dwSize=sizeof(ic);ic.dwICC=ICC_LISTVIEW_CLASSES|ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES;InitCommonControlsEx(&ic);
    auto hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool com=SUCCEEDED(hr);
    WNDCLASSW wc{};wc.lpfnWndProc=main_proc;wc.hInstance=inst;wc.lpszClassName=L"DiskCuratorNativeWindow";wc.hbrBackground=(HBRUSH)(COLOR_BTNFACE+1);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hIcon=LoadIconW(nullptr,IDI_APPLICATION);
    if(!RegisterClassW(&wc)){if(com)CoUninitialize();return 2;}
    RECT work_area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work_area,0);
    const int initial_width=std::clamp<int>(work_area.right-work_area.left-40,800,1360);
    const int initial_height=std::clamp<int>(work_area.bottom-work_area.top-40,580,850);
    App app;HWND wnd=CreateWindowExW(0,wc.lpszClassName,kProductName,WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,initial_width,initial_height,nullptr,nullptr,inst,&app);
    if(!wnd){if(com)CoUninitialize();return 3;}
    ShowWindow(wnd,show);UpdateWindow(wnd);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){if(app.handle_key(m))continue;TranslateMessage(&m);DispatchMessageW(&m);}
    if(com)CoUninitialize();
    return int(m.wParam);
}
