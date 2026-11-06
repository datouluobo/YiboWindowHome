#include "state.h"
#include "preview.h"
#include "window_ops.h"
#include "emulator_layout.h"
#include <atomic>
#include <future>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

namespace {
std::atomic_bool stall{},minimum{},stallPosition{};
std::atomic_int resizeStage{};
std::atomic_bool constrainNativeResize{};
HANDLE releaseMessage{};
LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM p,LPARAM l){
    if(msg==WM_ENTERSIZEMOVE)resizeStage=1;
    if(msg==WM_SIZING&&resizeStage==1&&p==WMSZ_BOTTOMRIGHT&&l){RECT actual{};GetWindowRect(w,&actual);if(EqualRect(&actual,reinterpret_cast<RECT*>(l)))resizeStage=2;}
    if(msg==WM_EXITSIZEMOVE&&resizeStage==2)resizeStage=3;
    if(msg==WM_SIZING&&constrainNativeResize&&l){auto rect=reinterpret_cast<RECT*>(l);rect->right=rect->left+520;rect->bottom=rect->top+360;return TRUE;}
    if(msg==WM_NULL&&stall)WaitForSingleObject(releaseMessage,5000);
    if(msg==WM_WINDOWPOSCHANGING&&stallPosition)WaitForSingleObject(releaseMessage,5000);
    if(msg==WM_GETMINMAXINFO&&minimum){auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={900,650};return 0;}
    if(msg==WM_CLOSE){DestroyWindow(w);PostQuitMessage(0);return 0;}
    return DefWindowProcW(w,msg,p,l);
}
bool closeRect(RECT a,RECT b){return abs(a.left-b.left)<=2&&abs(a.top-b.top)<=2&&abs(a.right-b.right)<=2&&abs(a.bottom-b.bottom)<=2;}
class Child {
    PROCESS_INFORMATION process_{};
public:
    HWND window{};
    bool start(const std::filesystem::path& signal,const std::filesystem::path& framework={},const std::wstring& mode=L""){
        wchar_t ownExe[32768]{};GetModuleFileNameW(nullptr,ownExe,32768);auto exe=framework.empty()?std::filesystem::path(ownExe):framework;
        std::wstring command=L"\""+exe.wstring()+L"\" "+(framework.empty()?L"--fixture":mode)+L" \""+signal.wstring()+L"\"";
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process_))return false;
        auto until=GetTickCount64()+10000;
        do {std::ifstream input(signal);UINT_PTR handle{};DWORD pid{};if(input>>handle>>pid){window=reinterpret_cast<HWND>(handle);DWORD actual{};GetWindowThreadProcessId(window,&actual);if(pid==process_.dwProcessId&&actual==pid)return true;}Sleep(10);}while(GetTickCount64()<until&&WaitForSingleObject(process_.hProcess,0)==WAIT_TIMEOUT);
        return false;
    }
    bool close(){
        if(!process_.hProcess)return true;if(window)PostMessageW(window,WM_CLOSE,0,0);bool exited=WaitForSingleObject(process_.hProcess,2000)==WAIT_OBJECT_0;
        if(!exited){TerminateProcess(process_.hProcess,91);WaitForSingleObject(process_.hProcess,1000);}CloseHandle(process_.hThread);CloseHandle(process_.hProcess);process_={};window=nullptr;return exited;
    }
    ~Child(){close();}
};
int fixture(const std::filesystem::path& signal){
    auto displays=home::collectDisplays();if(displays.empty())return 2;auto work=displays[0].work;
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YiboHome.NativeCoreChild";c.lpfnWndProc=procedure;RegisterClassW(&c);
    auto w=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"风钩 · 进程测试",WS_OVERLAPPEDWINDOW,work.left+100,work.top+100,640,420,nullptr,nullptr,c.hInstance,nullptr);if(!w)return 3;
    {std::ofstream out(signal);out<<reinterpret_cast<UINT_PTR>(w)<<' '<<GetCurrentProcessId();}
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==5&&(std::wstring(argv[1])==L"--emulator-probe"||std::wstring(argv[1])==L"--emulator-roundtrip"||std::wstring(argv[1])==L"--native-placement"||std::wstring(argv[1])==L"--native-sizing"||std::wstring(argv[1])==L"--native-drag")) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HWND root=reinterpret_cast<HWND>(std::stoull(argv[2]));home::Store store;store.path=argv[3];auto state=store.load();
        auto policy=home::resolveWindow(state,home::describeWindow(root));auto record=home::activePositionRecord(state,policy.recordKey);
        std::ofstream log(argv[4]);if(!record)return 2;
        int sidebar=home::embeddedSidebarInset(root);log<<"sidebar-before="<<sidebar<<'\n';
        RECT before{},after{};GetWindowRect(root,&before);UINT oldDpi=GetDpiForWindow(root);
        auto result=home::RestoreStatus::Failed;
        if(std::wstring(argv[1])==L"--emulator-roundtrip") {
            RECT smaller=before;smaller.right=smaller.left+MulDiv(before.right-before.left,85,100);smaller.bottom=smaller.top+MulDiv(before.bottom-before.top,85,100);
            home::boundedNativeResize(root,smaller);Sleep(200);RECT changed{};GetWindowRect(root,&changed);log<<"smaller="<<changed.right-changed.left<<','<<changed.bottom-changed.top<<'\n';
        }
        if(std::wstring(argv[1])==L"--native-drag") {
            SetForegroundWindow(root);Sleep(150);
            POINT cursor{};GetCursorPos(&cursor);RECT bounds{};GetWindowRect(root,&bounds);
            POINT start{bounds.right-4,(bounds.top+bounds.bottom)/2};
            DWORD_PTR hit{};SendMessageTimeoutW(root,WM_NCHITTEST,0,MAKELPARAM(start.x,start.y),SMTO_ABORTIFHUNG,200,&hit);
            log<<"drag-hit="<<hit<<" foreground="<<(GetForegroundWindow()==root)<<" reachable="<<(GetAncestor(WindowFromPoint(start),GA_ROOT)==root)<<'\n';log.flush();
            if(GetForegroundWindow()==root&&GetAncestor(WindowFromPoint(start),GA_ROOT)==root) {
                SetCursorPos(start.x,start.y);INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;SendInput(1,&input,sizeof(input));Sleep(120);
                SetCursorPos(start.x+12,start.y);Sleep(200);input.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&input,sizeof(input));Sleep(300);SetCursorPos(cursor.x,cursor.y);result=home::RestoreStatus::Limited;
            }
        } else if(std::wstring(argv[1])==L"--native-sizing") {
            RECT desired=record->normal;DWORD_PTR reply{};
            auto send=[&](UINT m,WPARAM w=0,LPARAM l=0){return SendMessageTimeoutW(root,m,w,l,SMTO_BLOCK|SMTO_ABORTIFHUNG,200,&reply)!=0;};
            send(WM_ENTERSIZEMOVE);
            home::boundedWindowPos(root,nullptr,desired.left,desired.top,desired.right-desired.left,desired.bottom-desired.top,SWP_NOZORDER|SWP_NOACTIVATE);
            send(WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&desired));
            log<<"adjusted="<<desired.left<<','<<desired.top<<','<<desired.right<<','<<desired.bottom<<'\n';
            home::boundedWindowPos(root,nullptr,desired.left,desired.top,desired.right-desired.left,desired.bottom-desired.top,SWP_NOZORDER|SWP_NOACTIVATE);
            send(WM_EXITSIZEMOVE);Sleep(500);result=home::RestoreStatus::Limited;
        } else if(std::wstring(argv[1])==L"--native-placement") {
            WINDOWPLACEMENT placement{sizeof(placement)};GetWindowPlacement(root,&placement);placement.rcNormalPosition=record->normal;placement.showCmd=SW_SHOWNOACTIVATE;
            if(home::boundedWindowPlacement(root,placement))result=home::RestoreStatus::Restored;
            Sleep(500);
        } else {SetForegroundWindow(root);Sleep(100);result=home::restoreDetailed(root,*record);}
        GetWindowRect(root,&after);log<<"restore="<<int(result)<<" foreground="<<(GetForegroundWindow()==root)<<" record="<<record->normal.top<<','<<record->normal.bottom<<'\n';
        bool preserved=before.right-before.left==after.right-after.left&&before.bottom-before.top==after.bottom-after.top;
        log<<"size-preserved="<<preserved<<" same-dpi="<<(oldDpi==GetDpiForWindow(root))<<'\n';
        for(int pass=0;pass<7;++pass) {
            if(pass)Sleep(300);
            log<<"pass="<<pass<<" sidebar="<<home::embeddedSidebarInset(root)<<'\n';
            EnumChildWindows(root,[](HWND child,LPARAM data)->BOOL{
                auto& log=*reinterpret_cast<std::ofstream*>(data);wchar_t cls[128]{};GetClassNameW(child,cls,128);
                RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,GetParent(child),reinterpret_cast<POINT*>(&r),2);
                DWORD pid{};GetWindowThreadProcessId(child,&pid);
                log<<reinterpret_cast<UINT_PTR>(child)<<" pid="<<pid<<" parent="<<reinterpret_cast<UINT_PTR>(GetParent(child))<<" root="<<reinterpret_cast<UINT_PTR>(GetAncestor(child,GA_ROOT))<<" visible="<<IsWindowVisible(child)<<" rect="<<r.left<<','<<r.top<<','<<r.right<<','<<r.bottom<<'\n';return TRUE;
            },reinterpret_cast<LPARAM>(&log));log.flush();
        }
        RECT visible{};MONITORINFO monitor{sizeof(monitor)};
        if(home::visibleWindowFrame(root,visible)&&GetMonitorInfoW(MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST),&monitor))log<<"visible-bottom="<<visible.bottom<<" work-bottom="<<monitor.rcWork.bottom<<" gap="<<monitor.rcWork.bottom-visible.bottom<<'\n';
        RECT content{};HWND shell=home::embeddedShell(root);
        if(shell&&GetWindowRect(shell,&content)&&GetMonitorInfoW(MonitorFromWindow(root,MONITOR_DEFAULTTONEAREST),&monitor))log<<"content-bottom="<<content.bottom<<" work-bottom="<<monitor.rcWork.bottom<<" content-gap="<<monitor.rcWork.bottom-content.bottom<<'\n';
        if(std::wstring(argv[1])==L"--emulator-probe"||std::wstring(argv[1])==L"--emulator-roundtrip") {
            RECT focused{},inactive{};GetWindowRect(root,&focused);
            HWND focusFixture=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"",WS_POPUP,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(focusFixture){ShowWindow(focusFixture,SW_SHOWNOACTIVATE);SetForegroundWindow(focusFixture);Sleep(120);GetWindowRect(root,&inactive);
                RECT inactiveContent{};GetWindowRect(home::embeddedShell(root),&inactiveContent);
                log<<"inactive="<<(GetForegroundWindow()!=root)<<" position-stable="<<EqualRect(&focused,&inactive)<<" top="<<inactive.top<<" inactive-content-gap="<<monitor.rcWork.bottom-inactiveContent.bottom<<'\n';
                SetForegroundWindow(root);DestroyWindow(focusFixture);}
        }
        return result==home::RestoreStatus::Failed?1:0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--fixture")return fixture(argv[2]);
    std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
    auto displays=home::collectDisplays();if(displays.empty()){std::cout<<"FAIL no live displays\n";return 1;}
    HWND previousForeground=GetForegroundWindow();auto work=displays[0].work;
    auto traceForeground=[](const char* label){auto f=GetForegroundWindow();DWORD pid{};if(f)GetWindowThreadProcessId(f,&pid);std::cout<<"Foreground "<<label<<" tick="<<GetTickCount()<<" hwnd="<<reinterpret_cast<UINT_PTR>(f)<<" pid="<<pid<<'\n';};
    traceForeground("at-start");
    releaseMessage=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::promise<HWND> windowReady;auto future=windowReady.get_future();
    std::thread worker([&]{
        WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YiboHome.NativeCoreTest";c.lpfnWndProc=procedure;RegisterClassW(&c);
        auto w=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"风钩 · API 测试",WS_OVERLAPPEDWINDOW,work.left+100,work.top+100,640,420,nullptr,nullptr,c.hInstance,nullptr);
        if(w){ShowWindow(w,SW_SHOWNOACTIVATE);if(!IsWindowVisible(w))ShowWindow(w,SW_SHOWNOACTIVATE);}windowReady.set_value(w);MSG msg{};while(w&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(IsWindow(w))DestroyWindow(w);
    });
    HWND w=future.get();check(w!=nullptr,"responsive worker-thread native fixture creates without activating");
    if(!w){worker.join();CloseHandle(releaseMessage);return 1;}
    check(home::boundedCommitResize(w)&&resizeStage==3,"embedded renderer resize sends begin, actual sizing rectangle, then end");
    check(!home::boundedCommitResize(nullptr),"embedded renderer resize rejects invalid windows");
    check(home::bottomEdgeCorrection({2555,0,3300,1374},{2558,0,3297,1365},{2560,0,5120,1368},144)==3,"recorded bottom contact compensates visible frame gap");
    check(home::bottomEdgeCorrection({2555,0,3300,1374},{2560,1,3297,1366},{2560,0,5120,1368},144)==2,"content edge excludes the two-pixel emulator bottom border");
    check(home::bottomEdgeCorrection({2555,0,3300,1350},{2558,0,3297,1347},{2560,0,5120,1368},144)==0,"intentional recorded bottom gap is preserved");
    check(home::bottomEdgeCorrection({2555,0,3300,1374},{2558,0,3297,1370},{2560,0,5120,1368},144)==0,"bottom alignment does not move windows already reaching the taskbar");
    check(home::bottomEdgeCorrection({2555,0,3300,1374},{2558,0,3297,1200},{2560,0,5120,1368},144)==0,"large application size restrictions do not become a large position jump");
    {
        RECT original{},actual{};GetWindowRect(w,&original);RECT requested=original;requested.right=requested.left+600;requested.bottom=requested.top+400;
        constrainNativeResize=true;resizeStage=0;bool resized=home::boundedNativeResize(w,requested);constrainNativeResize=false;GetWindowRect(w,&actual);
        check(resized&&resizeStage==3&&actual.right-actual.left==520&&actual.bottom-actual.top==360,"native restore applies application sizing constraints before ending resize");
        home::boundedWindowPos(w,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    {
        WNDCLASSW qt{};qt.hInstance=GetModuleHandleW(nullptr);qt.lpszClassName=L"QtRendererFixture";qt.lpfnWndProc=DefWindowProcW;RegisterClassW(&qt);
        auto sub=qt;sub.lpszClassName=L"subWin";RegisterClassW(&sub);
        HWND viewport=CreateWindowExW(0,qt.lpszClassName,L"",WS_CHILD|WS_VISIBLE,0,40,400,300,w,nullptr,qt.hInstance,nullptr);
        HWND renderHost=CreateWindowExW(0,qt.lpszClassName,L"",WS_CHILD|WS_VISIBLE,30,60,500,400,viewport,nullptr,qt.hInstance,nullptr);
        HWND renderer=CreateWindowExW(0,sub.lpszClassName,L"",WS_CHILD|WS_VISIBLE,20,50,600,450,renderHost,nullptr,qt.hInstance,nullptr);
        RECT expected{},hostBounds{},renderBounds{},viewportBefore{},viewportAfter{};GetWindowRect(viewport,&viewportBefore);
        bool synced=home::syncEmbeddedRenderer(w);GetWindowRect(viewport,&expected);GetWindowRect(renderHost,&hostBounds);GetWindowRect(renderer,&renderBounds);GetWindowRect(viewport,&viewportAfter);
        check(viewport&&renderHost&&renderer&&synced&&EqualRect(&expected,&hostBounds)&&EqualRect(&expected,&renderBounds)&&EqualRect(&viewportBefore,&viewportAfter),"renderer and host fit viewport without moving caption or sidebar boundary");
        check(home::syncEmbeddedRenderer(w),"aligned renderer synchronization is idempotent");
        RECT rootClient{};GetClientRect(w,&rootClient);
        SetWindowPos(viewport,nullptr,0,40,rootClient.right-46,300,SWP_NOZORDER|SWP_NOACTIVATE);
        check(home::embeddedSidebarInset(w)==46,"expanded sidebar width is measured from shell boundary");
        SetWindowPos(viewport,nullptr,0,40,rootClient.right,300,SWP_NOZORDER|SWP_NOACTIVATE);
        check(home::embeddedSidebarInset(w)==0,"collapsed sidebar reserves no content width");
        resizeStage=0;
        bool layout=home::syncEmbeddedLayout(w,46);RECT fitted{};GetWindowRect(viewport,&fitted);MapWindowPoints(nullptr,w,reinterpret_cast<POINT*>(&fitted),2);
        check(layout&&fitted.left==0&&fitted.top==0&&fitted.right==rootClient.right-46&&fitted.bottom==rootClient.bottom,"client shell fills bottom while preserving expanded sidebar width");
        check(resizeStage==0,"layout synchronization sends no interactive sizing messages to outer wrapper");
        check(home::embeddedLayoutReady(w,46),"read-only layout validation accepts aligned native layout");
        SetWindowPos(renderHost,nullptr,20,30,200,160,SWP_NOZORDER|SWP_NOACTIVATE);
        RECT unaligned{},observed{};GetWindowRect(renderHost,&unaligned);
        bool ready=home::embeddedLayoutReady(w,46);GetWindowRect(renderHost,&observed);
        check(!ready&&EqualRect(&unaligned,&observed),"stale renderer is reported without changing its geometry");
        DestroyWindow(renderer);DestroyWindow(renderHost);DestroyWindow(viewport);
    }
    if(argc==2&&std::wstring(argv[1])==L"--layout-only") {
        PostMessageW(w,WM_CLOSE,0,0);worker.join();CloseHandle(releaseMessage);
        std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
    }
    auto saved=home::capture(w);RECT before{},after{};GetWindowRect(w,&before);
    check(!saved.topology.empty()&&!saved.device.empty()&&saved.dpi>=48&&closeRect(saved.normal,before),"live capture includes exact ordinary bounds and topology");
    SetWindowPos(w,nullptr,work.left+240,work.top+200,800,500,SWP_NOACTIVATE|SWP_NOZORDER);
    auto result=home::restoreDetailed(w,saved);GetWindowRect(w,&after);
    check(result==home::RestoreStatus::Restored&&closeRect(after,saved.normal),"cross-thread native restore returns to saved physical bounds within two pixels");
    auto topStyle=WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST;HWND top=CreateWindowExW(topStyle,L"YiboHome.NativeCoreTest",L"",WS_OVERLAPPEDWINDOW,work.left+24,work.top+24,240,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    bool topCreated=top&&!(GetWindowLongPtrW(top,GWL_EXSTYLE)&WS_EX_TOPMOST)==false;auto topSaved=topCreated?home::capture(top):home::Record{};auto topStatus=topCreated?home::restoreDetailed(top,topSaved):home::RestoreStatus::Failed;auto topAfter=topCreated?GetWindowLongPtrW(top,GWL_EXSTYLE):0;
    check(topCreated&&!IsWindowVisible(top)&&topSaved.maximized==false&&(topAfter&WS_EX_TOPMOST)!=0&&topStatus==home::RestoreStatus::Restored,"native restore preserves existing hidden topmost state");
    if(top)DestroyWindow(top);
    check((GetWindowLongPtrW(w,GWL_EXSTYLE)&WS_EX_TOPMOST)==0,"native restore preserves ordinary z-order policy");
    auto frozen=saved;frozen.normal={0,0,0,0};GetWindowRect(w,&before);result=home::restoreDetailed(w,frozen);GetWindowRect(w,&after);
    check(result==home::RestoreStatus::Failed&&closeRect(before,after),"invalid saved rectangle fails before mutating target");
    frozen=saved;frozen.dpi=0;result=home::restoreDetailed(w,frozen);
    check(result==home::RestoreStatus::Failed,"invalid saved DPI is rejected by native restore");
    auto immutable=saved;minimum=true;result=home::restoreDetailed(w,saved);GetWindowRect(w,&after);minimum=false;
    check(result==home::RestoreStatus::Limited&&after.right-after.left>=900&&after.bottom-after.top>=650,"application minimum size returns Limited rather than exact success");
    minimum=true;auto minimumMaximum=saved;minimumMaximum.maximized=true;auto maximumLimited=home::restoreDetailed(w,minimumMaximum);minimum=false;
    check(maximumLimited==home::RestoreStatus::Limited,"maximum restore reports Limited when ordinary restore bounds exceed the saved rectangle");
    check(closeRect(saved.normal,immutable.normal)&&home::sameTopology(saved.topology,immutable.topology),"limited restore cannot overwrite saved geometry or topology");
    home::restoreDetailed(w,saved);
    traceForeground("before-live-display-loop");
    bool allOrdinary=true,allMax=true,allRestoreBounds=true;
    for(auto& screen:displays){
        RECT rect{screen.work.left+100,screen.work.top+100,screen.work.left+740,screen.work.top+520};
        SetWindowPos(w,nullptr,rect.left,rect.top,640,420,SWP_NOACTIVATE|SWP_NOZORDER);auto onScreen=home::capture(w);
        SetWindowPos(w,nullptr,work.left+50,work.top+50,700,450,SWP_NOACTIVATE|SWP_NOZORDER);result=home::restoreDetailed(w,onScreen);GetWindowRect(w,&after);
        allOrdinary=allOrdinary&&result==home::RestoreStatus::Restored&&closeRect(after,onScreen.normal);
        onScreen.maximized=true;result=home::restoreDetailed(w,onScreen);GetWindowRect(w,&after);
        allMax=allMax&&result==home::RestoreStatus::Restored&&IsZoomed(w)&&home::displayForRect(after,displays)==home::displayForRect(onScreen.normal,displays);
        ShowWindow(w,SW_RESTORE);GetWindowRect(w,&after);allRestoreBounds=allRestoreBounds&&closeRect(after,onScreen.normal);
    }
    check(allOrdinary,"ordinary geometry restores independently onto every live display");
    check(allMax,"maximized restore selects each intended live display");
    check(allRestoreBounds,"manual unmaximize retains each display's saved ordinary bounds");
    traceForeground("after-live-display-loop");
    std::cout<<"Live display count: "<<displays.size()<<'\n';
    if(displays.size()>1){
        RECT spanning=displays[0].work;for(auto& screen:displays){spanning.left=std::min(spanning.left,screen.work.left);spanning.top=std::min(spanning.top,screen.work.top);spanning.right=std::max(spanning.right,screen.work.right);spanning.bottom=std::max(spanning.bottom,screen.work.bottom);}InflateRect(&spanning,-100,-100);
        home::boundedWindowPos(w,nullptr,spanning.left,spanning.top,spanning.right-spanning.left,spanning.bottom-spanning.top,SWP_NOACTIVATE|SWP_NOZORDER);auto cross=home::capture(w);size_t intersections{};for(auto& screen:displays){RECT intersection{};if(IntersectRect(&intersection,&cross.normal,&screen.monitor))++intersections;}
        home::boundedWindowPos(w,nullptr,work.left+100,work.top+100,640,420,SWP_NOACTIVATE|SWP_NOZORDER);result=home::restoreDetailed(w,cross);GetWindowRect(w,&after);
        check(intersections>1&&result==home::RestoreStatus::Restored&&closeRect(after,cross.normal),"live spanning window restores exact saved rectangle across multiple displays");
    }else std::cout<<"NOT_RUN live spanning window requires two displays\n";
    GetWindowRect(w,&before);stall=true;auto start=std::chrono::steady_clock::now();result=home::restoreDetailed(w,saved);
    traceForeground("after-hung-probe");
    auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stall=false;SetEvent(releaseMessage);
    DWORD_PTR reply{};SendMessageTimeoutW(w,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&reply);GetWindowRect(w,&after);
    check(result==home::RestoreStatus::Failed&&elapsed<500&&closeRect(before,after),"unresponsive target fails within bounded query time without moving");std::cout<<"Hung target query: "<<elapsed<<" ms\n";
    ResetEvent(releaseMessage);stall=true;home::PreviewPlacement preview;start=std::chrono::steady_clock::now();bool begun=preview.begin(w,true);
    elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stall=false;SetEvent(releaseMessage);SendMessageTimeoutW(w,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,1000,&reply);
    check(!begun&&elapsed<500,"unresponsive target cannot start preview or hang the caller");
    result=home::restoreDetailed(w,saved);check(result==home::RestoreStatus::Restored,"responsive target recovers after a timed-out operation");
    SetWindowPos(w,nullptr,work.left+250,work.top+150,700,460,SWP_NOACTIVATE|SWP_NOZORDER);
    ResetEvent(releaseMessage);stallPosition=true;
    std::thread watchdog([]{std::this_thread::sleep_for(std::chrono::milliseconds(500));SetEvent(releaseMessage);});
    start=std::chrono::steady_clock::now();result=home::restoreDetailed(w,saved);elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stallPosition=false;watchdog.join();
    check(elapsed<300&&result==home::RestoreStatus::Failed,"target hanging during position change cannot block caller after initial responsive probe");std::cout<<"Position-change hang: "<<elapsed<<" ms\n";
    home::restoreDetailed(w,saved);
    ResetEvent(releaseMessage);stallPosition=true;
    std::thread showWatchdog([]{std::this_thread::sleep_for(std::chrono::milliseconds(800));SetEvent(releaseMessage);});
    start=std::chrono::steady_clock::now();begun=preview.begin(w,true);elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stallPosition=false;showWatchdog.join();
    check(!begun&&elapsed<500,"preview maximize remains bounded when target hangs during state change");std::cout<<"Preview state-change hang: "<<elapsed<<" ms\n";home::restoreDetailed(w,saved);
    ResetEvent(releaseMessage);stallPosition=true;
    std::thread zWatchdog([]{std::this_thread::sleep_for(std::chrono::milliseconds(500));SetEvent(releaseMessage);});
    start=std::chrono::steady_clock::now();bool topped=home::boundedWindowPos(w,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stallPosition=false;zWatchdog.join();
    check(!topped&&elapsed<300,"topmost operation remains bounded when target layout hangs");
    traceForeground("after-hang-cases");
    SetWindowPos(w,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    HWND loopForeground=GetForegroundWindow();
    bool repeated=true;for(int i=0;i<200;++i){
        SetWindowPos(w,nullptr,work.left+200+i%80,work.top+160,700,460,SWP_NOACTIVATE|SWP_NOZORDER);
        if(GetForegroundWindow()!=loopForeground){std::cout<<"Foreground loop iteration="<<i<<" stage=move\n";traceForeground("loop-change");loopForeground=GetForegroundWindow();}
        auto restored=home::restoreDetailed(w,saved);
        if(GetForegroundWindow()!=loopForeground){std::cout<<"Foreground loop iteration="<<i<<" stage=restore\n";traceForeground("loop-change");loopForeground=GetForegroundWindow();}
        GetWindowRect(w,&after);repeated=repeated&&restored==home::RestoreStatus::Restored&&closeRect(after,saved.normal);
    }
    check(repeated,"200 asynchronous cross-thread native restores complete without false success or geometry drift");
    traceForeground("after-native-fixture");
    auto directory=argc>=2?std::filesystem::path(argv[1]):std::filesystem::path(L"build/native-core-tests");std::filesystem::create_directories(directory);
    auto suffix=std::to_wstring(GetCurrentProcessId());Child a,b;
    bool started=a.start(directory/(L"child-a-"+suffix+L".txt"))&&b.start(directory/(L"child-b-"+suffix+L".txt"));
    check(started,"two isolated external application processes create independent native windows");
    if(started){
        auto key=home::appKey(a.window);check(!key.empty()&&key==home::appKey(b.window),"independent application instances resolve to the same executable identity");
        auto recordA=home::capture(a.window);RECT sourceBefore{},sourceAfter{},destination{};GetWindowRect(a.window,&sourceBefore);
        result=home::restoreDetailed(b.window,recordA);GetWindowRect(a.window,&sourceAfter);GetWindowRect(b.window,&destination);
        check(result==home::RestoreStatus::Restored&&closeRect(destination,recordA.normal)&&closeRect(sourceBefore,sourceAfter),"instance B restores A's shared record without moving A");
        home::boundedWindowPos(b.window,nullptr,work.left+310,work.top+210,720,480,SWP_NOACTIVATE|SWP_NOZORDER);auto recordB=home::capture(b.window);GetWindowRect(b.window,&sourceBefore);
        result=home::restoreDetailed(a.window,recordB);GetWindowRect(a.window,&destination);GetWindowRect(b.window,&sourceAfter);
        check(result==home::RestoreStatus::Restored&&closeRect(destination,recordB.normal)&&closeRect(sourceBefore,sourceAfter),"new record from B restores A without moving B");
        check(a.close()&&a.start(directory/(L"child-reopened-"+suffix+L".txt")),"target application closes and reopens as a fresh process");
        result=home::restoreDetailed(a.window,recordB);GetWindowRect(a.window,&destination);
        check(result==home::RestoreStatus::Restored&&closeRect(destination,recordB.normal),"fresh application process restores shared record after previous instance exits");
        check(a.close()&&b.close(),"all external fixtures shut down cleanly");
    }
    if(argc>=3){
        traceForeground("after-child-fixtures");
        for(auto mode:{L"wpf",L"winforms",L"fixed"}){
            Child managed;auto label=std::wstring(mode);bool ready=managed.start(directory/(L"managed-"+label+L"-"+suffix+L".txt"),argv[2],mode);
            std::string prefix=label==L"wpf"?"WPF ":label==L"fixed"?"fixed WinForms ":"WinForms ";check(ready,(prefix+"external fixture initializes").c_str());if(!ready)continue;
            auto description=home::describeWindow(managed.window);check(!description.identity.path.empty()&&!description.className.empty()&&description.systemSafe,(prefix+"real process identity and HWND class resolve").c_str());
            std::cout<<prefix<<"window awareness="<<int(GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(managed.window)))<<'\n';
            auto original=home::capture(managed.window);auto operationStart=std::chrono::steady_clock::now();bool prepared=home::boundedWindowPos(managed.window,nullptr,work.left+260,work.top+180,700,480,SWP_NOACTIVATE|SWP_NOZORDER);auto prepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-operationStart).count();
            operationStart=std::chrono::steady_clock::now();result=home::restoreDetailed(managed.window,original);auto restoreMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-operationStart).count();GetWindowRect(managed.window,&after);
            std::cout<<prefix<<"prepared="<<prepared<<" prepare-ms="<<prepareMs<<" ordinary-status="<<int(result)<<" restore-ms="<<restoreMs<<" actual="<<after.left<<','<<after.top<<','<<after.right<<','<<after.bottom<<'\n';
            check(prepared&&result==home::RestoreStatus::Restored&&closeRect(after,original.normal),(prefix+"ordinary restore preserves measured outer rectangle").c_str());
            if(label==L"fixed"){
                auto larger=original;larger.normal.right=larger.normal.left+1000;larger.normal.bottom=larger.normal.top+750;result=home::restoreDetailed(managed.window,larger);GetWindowRect(managed.window,&after);
                check(result==home::RestoreStatus::Limited&&closeRect(after,original.normal),(prefix+"size constraint reports Limited without overriding fixed dimensions").c_str());
            }else{
                traceForeground((prefix+"before-maximum").c_str());auto maximum=original;maximum.maximized=true;result=home::restoreDetailed(managed.window,maximum);traceForeground((prefix+"after-maximum").c_str());bool maximized=IsZoomed(managed.window)!=FALSE;home::boundedShowWindow(managed.window,SW_RESTORE);GetWindowRect(managed.window,&after);
                std::cout<<prefix<<"maximum status="<<int(result)<<" zoomed="<<maximized<<" ordinary="<<original.normal.left<<','<<original.normal.top<<','<<original.normal.right<<','<<original.normal.bottom<<" returned="<<after.left<<','<<after.top<<','<<after.right<<','<<after.bottom<<'\n';
                if(!closeRect(after,original.normal)){Sleep(100);RECT settled{};GetWindowRect(managed.window,&settled);std::cout<<prefix<<"100ms settled="<<settled.left<<','<<settled.top<<','<<settled.right<<','<<settled.bottom<<" zoomed="<<IsZoomed(managed.window)<<'\n';}
                check(result==home::RestoreStatus::Restored&&maximized&&closeRect(after,original.normal),(prefix+"maximize and unmaximize retain ordinary bounds").c_str());
            }
            if(label!=L"fixed"&&displays.size()>1&&displays[0].dpi!=displays[1].dpi){
                bool ordinaryMixed=true,maximumMixed=true;
                for(size_t i=0;i<2;++i){auto& destination=displays[i];auto& other=displays[i==0?1:0];int width=MulDiv(640,destination.dpi,96),height=MulDiv(420,destination.dpi,96);
                    bool placed=home::boundedWindowPos(managed.window,nullptr,destination.work.left+100,destination.work.top+100,width,height,SWP_NOACTIVATE|SWP_NOZORDER);auto atDestination=home::capture(managed.window);
                    bool moved=home::boundedWindowPos(managed.window,nullptr,other.work.left+100,other.work.top+100,640,420,SWP_NOACTIVATE|SWP_NOZORDER);UINT fromDpi=GetDpiForWindow(managed.window);
                    auto ordinaryStatus=home::restoreDetailed(managed.window,atDestination);GetWindowRect(managed.window,&after);bool exact=closeRect(after,atDestination.normal);
                    UINT restoredDpi=GetDpiForWindow(managed.window);
                    ordinaryMixed=ordinaryMixed&&placed&&moved&&fromDpi!=destination.dpi&&restoredDpi==destination.dpi&&ordinaryStatus==home::RestoreStatus::Restored&&exact;
                    atDestination.maximized=true;home::boundedWindowPos(managed.window,nullptr,other.work.left+100,other.work.top+100,640,420,SWP_NOACTIVATE|SWP_NOZORDER);auto maximumStatus=home::restoreDetailed(managed.window,atDestination);bool wasMaximized=IsZoomed(managed.window)!=FALSE;
                    WINDOWPLACEMENT placedNormal{sizeof(placedNormal)};GetWindowPlacement(managed.window,&placedNormal);RECT expectedNormal=placedNormal.rcNormalPosition;if(!(GetWindowLongPtrW(managed.window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW))OffsetRect(&expectedNormal,destination.work.left-destination.monitor.left,destination.work.top-destination.monitor.top);
                    bool returned=home::boundedShowWindow(managed.window,SW_RESTORE);GetWindowRect(managed.window,&after);bool exactNormal=closeRect(after,atDestination.normal);
                    if(!exactNormal){std::cout<<prefix<<"normal diagnostic expected="<<atDestination.normal.left<<','<<atDestination.normal.top<<','<<atDestination.normal.right<<','<<atDestination.normal.bottom<<" WP="<<expectedNormal.left<<','<<expectedNormal.top<<','<<expectedNormal.right<<','<<expectedNormal.bottom<<" immediate="<<after.left<<','<<after.top<<','<<after.right<<','<<after.bottom<<'\n';Sleep(100);RECT settled{};GetWindowRect(managed.window,&settled);std::cout<<prefix<<"normal after100="<<settled.left<<','<<settled.top<<','<<settled.right<<','<<settled.bottom<<'\n';}
                    maximumMixed=maximumMixed&&maximumStatus==home::RestoreStatus::Restored&&wasMaximized&&returned&&exactNormal;
                    std::cout<<prefix<<"mixed screen="<<i<<" placed="<<placed<<" moved="<<moved<<" from-dpi="<<fromDpi<<" restored-dpi="<<restoredDpi<<" destination-dpi="<<destination.dpi<<" ordinary-status="<<int(ordinaryStatus)<<" exact="<<exact<<" maximum-status="<<int(maximumStatus)<<" normal-exact="<<exactNormal<<'\n';
                }
                check(ordinaryMixed,(prefix+"mixed-DPI ordinary restore succeeds in both display directions").c_str());
                check(maximumMixed,(prefix+"mixed-DPI maximum restore retains correct ordinary bounds in both directions").c_str());
            }
            check(managed.close(),(prefix+"fixture exits without leftover process").c_str());traceForeground((prefix+"after-close").c_str());
        }
    }else std::cout<<"NOT_RUN WPF/WinForms fixture executable not supplied\n";
    traceForeground("at-end");check(GetForegroundWindow()==previousForeground,"native API checks leave foreground application unchanged");
    PostMessageW(w,WM_CLOSE,0,0);worker.join();
    check(home::restoreDetailed(w,saved)==home::RestoreStatus::Failed&&!preview.begin(w,true),"destroyed target fails restore and preview safely");
    CloseHandle(releaseMessage);std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
