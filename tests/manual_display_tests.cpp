#include "state.h"
#include "window_ops.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <future>
#include <thread>
#include <atomic>

namespace {
constexpr auto fixtureName=L"YiboWindowHome display test fixture";
std::string bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
std::uint64_t fingerprint(const std::string& text){std::uint64_t value=14695981039346656037ULL;for(unsigned char c:text){value^=c;value*=1099511628211ULL;}return value;}
std::string quote(const std::wstring& text){std::ostringstream out;out<<'"'<<std::hex<<std::setfill('0');for(wchar_t c:text)out<<"\\u"<<std::setw(4)<<unsigned(c);out<<'"';return out.str();}
void rect(std::ostream& out,RECT r){out<<'['<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<']';}
bool closeRect(RECT a,RECT b){return abs(a.left-b.left)<=2&&abs(a.top-b.top)<=2&&abs(a.right-b.right)<=2&&abs(a.bottom-b.bottom)<=2;}
std::int64_t overlap(RECT a,RECT b){RECT intersection{};return IntersectRect(&intersection,&a,&b)?std::int64_t(intersection.right-intersection.left)*(intersection.bottom-intersection.top):0;}
std::optional<size_t> nativeDisplay(HWND w,std::span<const home::Display> displays){MONITORINFOEXW info{};info.cbSize=sizeof(info);if(GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&info))for(size_t i=0;i<displays.size();++i)if(_wcsicmp(displays[i].device.c_str(),info.szDevice)==0)return i;return {};}
std::wstring executable(){wchar_t path[32768]{};auto size=GetModuleFileNameW(nullptr,path,32768);return home::normalizeAppPath({path,size});}
struct DpiNotifications {std::atomic<UINT> last{},count{};};
LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM p,LPARAM l){
    if(msg==WM_NCCREATE){auto creation=reinterpret_cast<CREATESTRUCTW*>(l);SetWindowLongPtrW(w,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(creation->lpCreateParams));}
    if(msg==WM_CLOSE){DestroyWindow(w);PostQuitMessage(0);return 0;}
    if(msg==WM_DPICHANGED){auto notifications=reinterpret_cast<DpiNotifications*>(GetWindowLongPtrW(w,GWLP_USERDATA));if(notifications){notifications->last=LOWORD(p);++notifications->count;}auto r=reinterpret_cast<RECT*>(l);SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);return 0;}
    return DefWindowProcW(w,msg,p,l);
}
struct Window {
    HWND handle{};
    std::thread worker;
    DpiNotifications notifications;
    explicit Window(const home::Display& display){
        static bool registered=[] {WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=procedure;c.lpszClassName=L"YiboHome.ManualDisplayTest";return RegisterClassW(&c)!=0;}();
        if(!registered)return;
        std::promise<HWND> ready;auto future=ready.get_future();
        worker=std::thread([&,display]{
            auto work=display.work;int width=std::min<LONG>(MulDiv(640,display.dpi,96),work.right-work.left),height=std::min<LONG>(MulDiv(420,display.dpi,96),work.bottom-work.top);
            auto w=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"YiboHome.ManualDisplayTest",fixtureName,WS_OVERLAPPEDWINDOW,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),&notifications);
            if(w){notifications.last=GetDpiForWindow(w);ShowWindow(w,SW_SHOWNOACTIVATE);if(!IsWindowVisible(w))ShowWindow(w,SW_SHOWNOACTIVATE);}ready.set_value(w);
            MSG msg{};while(w&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(IsWindow(w))DestroyWindow(w);
        });
        handle=future.get();
    }
    ~Window(){if(handle)PostMessageW(handle,WM_CLOSE,0,0);if(worker.joinable())worker.join();}
};
struct Result {
    int code{2};std::wstring phase,reason,sessionId;
    std::vector<home::Display> displays;
    bool hasRecord{},hasPlan{},unchanged{},foregroundUnchanged{},attempted{};
    home::Record saved;home::RestorePlan plan;
    home::RestoreStatus status{home::RestoreStatus::Failed};
    RECT before{},actual{},ordinary{};UINT beforeDpi{},actualDpi{};bool maximized{};
    std::uint64_t recordFingerprint{};
    std::optional<size_t> nativeIndex;bool equalOverlapTie{};
    UINT lastNotifiedDpi{},dpiChanges{};bool spanning{};
};
void print(const Result& r){
    std::cout<<std::boolalpha<<"{\"ReportSchema\":1,\"Phase\":"<<quote(r.phase)<<",\"ExitCode\":"<<r.code<<",\"Pass\":"<<(r.code==0)<<",\"Reason\":"<<quote(r.reason)<<",\"SessionId\":"<<quote(r.sessionId)<<",\"UptimeMs\":"<<GetTickCount64()<<",\"DisplayCount\":"<<r.displays.size()<<",\"Displays\":[";
    bool comma=false;for(auto& d:r.displays){if(comma)std::cout<<',';comma=true;std::cout<<"{\"Device\":"<<quote(d.device)<<",\"Identity\":"<<quote(d.identity)<<",\"Monitor\":";rect(std::cout,d.monitor);std::cout<<",\"Work\":";rect(std::cout,d.work);std::cout<<",\"Dpi\":"<<d.dpi<<",\"Primary\":"<<d.primary<<'}';}
    std::cout<<"],\"HasRecord\":"<<r.hasRecord<<",\"RecordFingerprint\":\""<<r.recordFingerprint<<"\",\"RecordFileUnchanged\":"<<r.unchanged<<",\"ForegroundUnchanged\":"<<r.foregroundUnchanged;
    if(r.hasRecord){std::cout<<",\"SavedDpi\":"<<r.saved.dpi<<",\"SavedMaximized\":"<<r.saved.maximized<<",\"SavedNormal\":";rect(std::cout,r.saved.normal);}
    if(r.hasPlan){std::cout<<",\"EnvironmentChanged\":"<<r.plan.environmentChanged<<",\"Fallback\":"<<r.plan.fallback<<",\"SelectedDisplay\":"<<r.plan.displayIndex<<",\"PlannedNormal\":";rect(std::cout,r.plan.normal);}
    std::cout<<",\"EqualOverlapTie\":"<<r.equalOverlapTie<<",\"NativeDisplayIndex\":";if(r.nativeIndex)std::cout<<*r.nativeIndex;else std::cout<<"null";
    std::cout<<",\"NativeMonitorDpi\":";if(r.nativeIndex)std::cout<<r.displays[*r.nativeIndex].dpi;else std::cout<<"null";
    std::cout<<",\"LastNotifiedDpi\":"<<r.lastNotifiedDpi<<",\"DpiChangeCount\":"<<r.dpiChanges<<",\"Spanning\":"<<r.spanning<<",\"SavedDpiBasis\":\"anchor-monitor\"";
    std::cout<<",\"RestoreAttempted\":"<<r.attempted<<",\"RestoreStatus\":";if(r.attempted)std::cout<<int(r.status);else std::cout<<"null";
    std::cout<<",\"BeforeDpi\":"<<r.beforeDpi<<",\"ActualDpi\":"<<r.actualDpi<<",\"Maximized\":"<<r.maximized<<",\"BeforeBounds\":";rect(std::cout,r.before);std::cout<<",\"ActualBounds\":";rect(std::cout,r.actual);std::cout<<",\"OrdinaryAfterRestore\":";rect(std::cout,r.ordinary);std::cout<<"}\n";
}
bool fixtureState(const home::State& s){return s.applications.size()==1&&s.records.size()==1&&s.applications[0].name==fixtureName&&s.applications[0].path==executable()&&s.records.contains(home::scopeKey(s.applications[0]));}
Result capture(const std::filesystem::path& directory,size_t screen,const std::wstring& mode){
    Result r;r.phase=L"capture-"+mode;r.displays=home::collectDisplays();HWND foreground=GetForegroundWindow();
    if(screen>=r.displays.size()||(mode!=L"normal"&&mode!=L"maximized"&&mode!=L"spanning"&&mode!=L"spanning-biased"&&mode!=L"workarea")){r.reason=L"Invalid screen or capture mode";return r;}
    home::Store store{directory/L"display-record.json"};
    if(std::filesystem::exists(store.path)){auto old=store.load();if(!store.writable||!fixtureState(old)){r.reason=L"Existing file is not a writable display-test fixture; preserved";return r;}}
    Window w(r.displays[screen]);if(!w.handle){r.reason=L"Cannot create test window";return r;}
    if(mode==L"maximized"&&!home::boundedShowWindow(w.handle,SW_MAXIMIZE)){r.reason=L"Cannot maximize test window";return r;}
    if(mode==L"workarea"){auto bounds=r.displays[screen].work;if(!home::boundedWindowPos(w.handle,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE|SWP_NOZORDER)){r.reason=L"Cannot create full-work-area ordinary fixture";return r;}}
    if(mode==L"spanning"||mode==L"spanning-biased"){
        if(r.displays.size()<2){r.code=3;r.reason=L"Spanning capture requires two displays";return r;}
        RECT bounds=r.displays[0].work;for(auto& d:r.displays){bounds.left=std::min(bounds.left,d.work.left);bounds.top=std::min(bounds.top,d.work.top);bounds.right=std::max(bounds.right,d.work.right);bounds.bottom=std::max(bounds.bottom,d.work.bottom);}InflateRect(&bounds,-100,-100);
        if(mode==L"spanning-biased"){
            auto target=r.displays[screen].work;LONG dx=target.left+target.right-bounds.left-bounds.right,dy=target.top+target.bottom-bounds.top-bounds.bottom;
            if(abs(dx)>=abs(dy)){if(dx<0)bounds.right-=64;else bounds.left+=64;}else{if(dy<0)bounds.bottom-=64;else bounds.top+=64;}
            auto selectedArea=overlap(bounds,r.displays[screen].monitor);bool unique=selectedArea>0;
            for(size_t i=0;i<r.displays.size();++i)if(i!=screen&&overlap(bounds,r.displays[i].monitor)>=selectedArea)unique=false;
            if(!unique){r.code=3;r.reason=L"Cannot create an unambiguous spanning fixture on the selected display";return r;}
        }
        if(!home::boundedWindowPos(w.handle,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE|SWP_NOZORDER)){r.reason=L"Cannot create spanning geometry";return r;}
    }
    home::State state;auto& app=home::ensureApplication(state,{executable(),L""},fixtureName);state.records[home::scopeKey(app)]=home::capture(w.handle);
    if(!store.save(state)){r.reason=L"Cannot save isolated test record";return r;}
    r.saved=state.records.begin()->second;r.hasRecord=true;r.sessionId=app.id;r.recordFingerprint=fingerprint(bytes(store.path));r.foregroundUnchanged=GetForegroundWindow()==foreground;GetWindowRect(w.handle,&r.actual);r.actualDpi=GetDpiForWindow(w.handle);r.maximized=IsZoomed(w.handle)!=FALSE;r.nativeIndex=nativeDisplay(w.handle,r.displays);
    r.lastNotifiedDpi=w.notifications.last;r.dpiChanges=w.notifications.count;
    auto savedDisplay=std::find_if(r.displays.begin(),r.displays.end(),[&](const home::Display& d){return d.device==r.saved.device;});
    size_t intersected{},equalCount{};bool dpiFromIntersected=false;auto savedArea=savedDisplay==r.displays.end()?0:overlap(r.actual,savedDisplay->monitor);
    for(auto& d:r.displays){auto area=overlap(r.actual,d.monitor);if(area>0){++intersected;if(d.dpi==r.actualDpi)dpiFromIntersected=true;}if(savedArea>0&&area==savedArea)++equalCount;}
    r.spanning=intersected>1;r.equalOverlapTie=equalCount>1;
    bool consistent=r.nativeIndex&&savedDisplay!=r.displays.end()&&r.saved.dpi==savedDisplay->dpi&&r.actualDpi==r.lastNotifiedDpi&&dpiFromIntersected;
    r.code=r.foregroundUnchanged&&consistent?0:1;r.reason=r.code?L"Capture anchor/DPI or foreground mismatch":L"Isolated fixture captured with native monitor/DPI agreement";return r;
}
bool changedWorkArea(const home::Record& old,const home::Display& now){
    return old.monitor.right-old.monitor.left!=now.monitor.right-now.monitor.left||old.monitor.bottom-old.monitor.top!=now.monitor.bottom-now.monitor.top
        ||old.work.left-old.monitor.left!=now.work.left-now.monitor.left||old.work.top-old.monitor.top!=now.work.top-now.monitor.top
        ||old.monitor.right-old.work.right!=now.monitor.right-now.work.right||old.monitor.bottom-old.work.bottom!=now.monitor.bottom-now.work.bottom;
}
Result restore(const std::filesystem::path& directory,size_t screen,const std::wstring& phase){
    Result r;r.phase=phase;r.displays=home::collectDisplays();HWND foreground=GetForegroundWindow();home::Store store{directory/L"display-record.json"};auto before=bytes(store.path);auto state=store.load();
    if(screen>=r.displays.size()||!store.writable||!fixtureState(state)){r.reason=L"Missing valid isolated capture or screen index";return r;}
    r.hasRecord=true;r.saved=state.records.begin()->second;r.sessionId=state.applications[0].id;r.recordFingerprint=fingerprint(before);r.unchanged=true;
    bool same=home::sameTopology(r.saved.topology,r.displays),has100=false,has150or200=false,negative=false;
    for(auto& d:r.displays){has100=has100||d.dpi==96;has150or200=has150or200||d.dpi==144||d.dpi==192;negative=negative||d.monitor.left<0||d.monitor.top<0;}
    auto notReady=[&](const wchar_t* reason){r.code=3;r.reason=reason;r.unchanged=bytes(store.path)==before;r.foregroundUnchanged=GetForegroundWindow()==foreground;return r;};
    if(phase==L"baseline"&&!same)return notReady(L"Baseline requires the captured topology");
    if((phase==L"changed"||phase==L"mixed"||phase==L"negative"||phase==L"resolution")&&same)return notReady(L"No actual topology change detected");
    if(phase==L"mixed"&&!(has100&&has150or200))return notReady(L"Acceptance requires a 100-percent display plus a 150- or 200-percent display");
    if(phase==L"negative"&&!negative)return notReady(L"Current topology has no left or upper negative coordinates");
    if(phase==L"disconnected"&&r.displays.size()>=r.saved.topology.size())return notReady(L"No active display was removed");
    if(phase==L"reconnected"){
        std::ifstream checkpoint(directory/L"disconnected.checkpoint");std::uint64_t hash{};size_t savedCount{},detachedCount{};std::string extra;
        if(!(checkpoint>>hash>>savedCount>>detachedCount)||checkpoint>>extra||hash!=r.recordFingerprint||savedCount!=r.saved.topology.size()||detachedCount>=savedCount||!same)return notReady(L"Reconnect requires a successful matching disconnect checkpoint and original topology");
    }
    if(phase!=L"baseline"&&phase!=L"changed"&&phase!=L"mixed"&&phase!=L"negative"&&phase!=L"disconnected"&&phase!=L"reconnected"&&phase!=L"resolution"){r.reason=L"Unknown phase";return r;}
    Window w(r.displays[screen]);if(!w.handle){r.reason=L"Cannot create target fixture";return r;}
    GetWindowRect(w.handle,&r.before);r.beforeDpi=GetDpiForWindow(w.handle);auto plan=home::planRestore(r.saved,r.displays,r.before);if(!plan){r.reason=L"Cannot plan restore";return r;}r.plan=*plan;r.hasPlan=true;
    if(phase==L"disconnected"&&!r.plan.fallback)return notReady(L"Removed display was not the saved anchor; no disconnect fallback was exercised");
    if(phase==L"mixed"&&r.beforeDpi==r.displays[r.plan.displayIndex].dpi)return notReady(L"Choose the other-DPI screen as current target to exercise a real DPI transition");
    if(phase==L"mixed"){
        UINT destination=r.displays[r.plan.displayIndex].dpi;
        if(!((r.beforeDpi==96&&(destination==144||destination==192))||(destination==96&&(r.beforeDpi==144||r.beforeDpi==192))))return notReady(L"Target must actually cross between the 100-percent and 150- or 200-percent displays");
    }
    if(phase==L"negative"&&(r.plan.fallback||(r.displays[r.plan.displayIndex].monitor.left>=0&&r.displays[r.plan.displayIndex].monitor.top>=0)))return notReady(L"Saved anchor must remain present and occupy the left or upper negative-coordinate display");
    if(phase==L"resolution"&&r.plan.fallback)return notReady(L"Resolution test requires the original saved display to remain present");
    if(phase==L"resolution"&&!changedWorkArea(r.saved,r.displays[r.plan.displayIndex]))return notReady(L"Neither resolution nor relative work-area insets changed on the restored display");
    r.attempted=true;r.status=home::restoreDetailed(w.handle,r.saved);GetWindowRect(w.handle,&r.actual);r.actualDpi=GetDpiForWindow(w.handle);r.maximized=IsZoomed(w.handle)!=FALSE;r.ordinary=r.actual;r.nativeIndex=nativeDisplay(w.handle,r.displays);
    auto plannedOverlap=overlap(r.actual,r.displays[r.plan.displayIndex].monitor);size_t equalCount{};for(auto& d:r.displays)if(plannedOverlap>0&&overlap(r.actual,d.monitor)==plannedOverlap)++equalCount;r.equalOverlapTie=equalCount>1;
    bool nativeIsPlan=r.nativeIndex&&*r.nativeIndex==r.plan.displayIndex;
    bool allowedTie=r.nativeIndex&&!r.saved.maximized&&!r.plan.environmentChanged&&r.equalOverlapTie&&overlap(r.actual,r.displays[*r.nativeIndex].monitor)==plannedOverlap;
    r.lastNotifiedDpi=w.notifications.last;r.dpiChanges=w.notifications.count;size_t intersected{};bool dpiFromIntersectedDisplay=false;
    for(auto& d:r.displays)if(overlap(r.actual,d.monitor)>0){++intersected;if(d.dpi==r.actualDpi)dpiFromIntersectedDisplay=true;}
    r.spanning=intersected>1;
    // A straddling HWND can retain its previous WM_DPICHANGED scale even when
    // MonitorFromWindow selects another overlapping monitor. Physical geometry
    // remains strict; use the native notification to verify the HWND's scale.
    bool dpiValid=r.nativeIndex&&r.actualDpi==r.lastNotifiedDpi&&(nativeIsPlan||allowedTie)
        &&((r.spanning&&!r.saved.maximized)?dpiFromIntersectedDisplay:r.actualDpi==r.displays[*r.nativeIndex].dpi);
    bool geometry=closeRect(r.actual,r.plan.normal);
    if(r.saved.maximized){bool unmaximized=home::boundedShowWindow(w.handle,SW_RESTORE);GetWindowRect(w.handle,&r.ordinary);geometry=r.maximized&&home::displayForRect(r.actual,r.displays)==std::optional<size_t>(r.plan.displayIndex)&&unmaximized&&closeRect(r.ordinary,r.plan.normal);}
    auto expected=r.plan.environmentChanged||r.plan.fallback||r.plan.visibilityCorrected?home::RestoreStatus::Adapted:home::RestoreStatus::Restored;
    r.unchanged=bytes(store.path)==before;r.foregroundUnchanged=GetForegroundWindow()==foreground;
    bool pass=r.status==expected&&geometry&&dpiValid&&r.unchanged&&r.foregroundUnchanged;
    r.code=pass?0:1;r.reason=pass?L"Actual restore matches expected geometry, state, DPI and immutable record":L"Actual result differs from expected geometry, state, DPI or record integrity";
    if(pass&&phase==L"disconnected"){std::ofstream checkpoint(directory/L"disconnected.checkpoint");checkpoint<<r.recordFingerprint<<' '<<r.saved.topology.size()<<' '<<r.displays.size();if(!checkpoint){r.code=1;r.reason=L"Restore passed but disconnect checkpoint could not be written";}}
    return r;
}
int selfTest(const std::filesystem::path& directory){
    int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    std::filesystem::create_directories(directory);auto normal=capture(directory,0,L"normal");check(normal.code==0,"physical-test harness captures only its own isolated fixture");
    auto base=restore(directory,0,L"baseline");check(base.code==0&&base.unchanged,"physical-test baseline restores exact geometry without rewriting capture");
    check(restore(directory,0,L"changed").code==3,"unchanged desktop cannot be labeled a changed-environment pass");
    check(restore(directory,0,L"mixed").code==3,"unchanged desktop cannot be labeled a mixed-DPI pass");
    check(restore(directory,0,L"negative").code==3,"unchanged desktop cannot be labeled a negative-layout transition pass");
    check(restore(directory,0,L"disconnected").code==3,"unchanged desktop cannot be labeled a physical disconnect pass");
    check(restore(directory,0,L"reconnected").code==3,"reconnect requires a real successful disconnect checkpoint");
    check(restore(directory,0,L"resolution").code==3,"unchanged desktop cannot be labeled a resolution-change pass");
    {std::ofstream forged(directory/L"disconnected.checkpoint");forged<<0<<' '<<2<<' '<<1;}
    check(restore(directory,0,L"reconnected").code==3,"stale or unrelated disconnect checkpoint is rejected");
    auto maximum=capture(directory,0,L"maximized");base=restore(directory,0,L"baseline");check(maximum.code==0&&base.code==0&&base.maximized,"physical-test harness validates maximum state and ordinary restore bounds");
    if(normal.displays.size()>1){auto span=capture(directory,0,L"spanning");base=restore(directory,1,L"baseline");check(span.code==0&&base.code==0,"physical-test harness restores a real spanning rectangle");
        span=capture(directory,1,L"spanning");check(span.code==0&&span.actualDpi==span.lastNotifiedDpi,"equal-area capture distinguishes anchor monitor DPI from notified window DPI");
        for(size_t i=0;i<2;++i){auto biased=capture(directory,i,L"spanning-biased");base=restore(directory,i==0?1:0,L"baseline");check(biased.code==0&&base.code==0&&!base.equalOverlapTie,"unambiguous spanning geometry preserves exact bounds and notified HWND DPI");}
        for(size_t i=0;i<2;++i){auto full=capture(directory,i,L"workarea");base=restore(directory,i==0?1:0,L"baseline");check(full.code==0&&base.code==0&&!base.maximized&&base.actualDpi==full.saved.dpi,"full-work-area ordinary restore reaches destination DPI from the other display");}
    }
    else std::cout<<"NOT_RUN spanning harness baseline requires two displays\n";
    auto missing=restore(directory/home::newId(L"missing-"),0,L"baseline");check(missing.code==2,"missing capture is an error instead of a passing test");
    auto invalid=directory/L"invalid";std::filesystem::create_directories(invalid);auto path=invalid/L"display-record.json";{std::ofstream out(path);out<<"broken fixture";}
    check(capture(invalid,0,L"normal").code==2&&bytes(path)=="broken fixture","capture preserves unrecognized existing files without overwrite");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
}
int wmain(int argc,wchar_t** argv){
    try {
        if(argc==3&&std::wstring(argv[1])==L"--self-test")return selfTest(argv[2]);
        if(argc!=5){std::cerr<<"Usage: --capture|--restore <isolated-session-directory> <screen-index> <mode-or-phase>\n";return 2;}
        size_t consumed{};auto screen=std::stoull(argv[3],&consumed);if(consumed!=std::wstring(argv[3]).size())return 2;
        Result r;if(std::wstring(argv[1])==L"--capture")r=capture(argv[2],size_t(screen),argv[4]);else if(std::wstring(argv[1])==L"--restore")r=restore(argv[2],size_t(screen),argv[4]);else return 2;print(r);return r.code;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
