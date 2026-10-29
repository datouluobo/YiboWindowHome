#include "state.h"
#include "caption.h"
#include "caption_motion.h"
#include "window_ops.h"
#include "caption_style.h"
#include "settings.h"
#include "resource.h"
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wtsapi32.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
constexpr UINT Refresh=WM_APP+1, Tray=WM_APP+2;
constexpr int SettingsCommand=100, PauseCommand=101, ExitCommand=102;
HINSTANCE instance;
HWND host{},overlay{},settings{},tooltip{},pressedTarget{};
DWORD targetPid{},pressedPid{};
HWND target{};
bool paused{};
bool sessionNotificationsRegistered{};
UINT pressedButton{};
std::wstring targetKey;
std::wstring activeRecordKey;
std::wstring pressedRecordKey;
home::State state;
home::Store store;
std::atomic_bool refreshPending{};
std::atomic_bool fullRefreshPending{};
home::CaptionMotion captionMotion;
ULONGLONG trackingUntil{};
std::vector<HWINEVENTHOOK> hooks;
UINT taskbarCreated{};
home::CaptionPalette captionPalette;
bool hovered{};
ULONGLONG feedbackUntil{};
LONG captionBottom{};
enum class VisibilityReason { Initial,Paused,Menu,InvalidTarget,NoIdentity,Occluded,PlacementFailed,Visible,NoCaptionMeasurement,RuleExcluded };
VisibilityReason visibilityReason{};

void notify(const wchar_t* message,bool error=false) {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=host; data.uID=1; data.uFlags=NIF_INFO;
    wcscpy_s(data.szInfoTitle,L"风钩 · 技术原型"); wcsncpy_s(data.szInfo,message,_TRUNCATE); data.dwInfoFlags=error?NIIF_WARNING:NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY,&data);
    feedbackUntil=GetTickCount64()+1400;
    SetWindowTextW(overlay,error?L"!":L"✓"); InvalidateRect(overlay,nullptr,TRUE); SetTimer(host,2,1400,nullptr);
}
void trayIcon() {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=host; data.uID=1; data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;
    data.uCallbackMessage=Tray; data.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON)); wcscpy_s(data.szTip,L"风钩 · 左键恢复启用记录 / 右键更新默认 / Ctrl+右键新增 / 中键置顶");
    Shell_NotifyIconW(NIM_ADD,&data);
}
bool validTarget(HWND window,DWORD expectedPid=0) {
    if(!window || !IsWindow(window)||!IsWindowVisible(window)||IsIconic(window)||!IsWindowEnabled(window)) return false;
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    if(pid==GetCurrentProcessId() || (expectedPid&&pid!=expectedPid)) return false;
    wchar_t cls[256]{};GetClassNameW(window,cls,256);
    if(GetAncestor(window,GA_ROOT)!=window||wcscmp(cls,L"Progman")==0||wcscmp(cls,L"WorkerW")==0||wcscmp(cls,L"Shell_TrayWnd")==0||wcscmp(cls,L"Shell_SecondaryTrayWnd")==0)return false;
    DWORD cloaked{}; if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked)))&&cloaked) return false;
    return true;
}
void requestRefresh() { fullRefreshPending=true; if(!refreshPending.exchange(true)) PostMessageW(host,Refresh,0,0); }
void requestTrackingRefresh() { if(!refreshPending.exchange(true)) PostMessageW(host,Refresh,0,0); }
void trackBurst() { trackingUntil=GetTickCount64()+160;SetTimer(host,4,16,nullptr); }
void hideOverlay() { if(tooltip)SendMessageW(tooltip,TTM_POP,0,0);hovered=false;ShowWindow(overlay,SW_HIDE); }
bool hasOpenMenu(HWND window) {
    if(!window) return false;
    GUITHREADINFO info{sizeof(info)};
    DWORD thread=GetWindowThreadProcessId(window,nullptr);
    return thread&&GetGUIThreadInfo(thread,&info)&&(info.flags&(GUI_INMENUMODE|GUI_POPUPMENUMODE|GUI_SYSTEMMENUMODE));
}
void resetTarget() {
    captionMotion.clear();KillTimer(host,4);
    if(GetCapture()==overlay) ReleaseCapture();
    target=nullptr; targetPid=0; targetKey.clear(); pressedTarget=nullptr; pressedPid=0; pressedButton=0; hovered=false; feedbackUntil=0;
    KillTimer(host,2); hideOverlay();
}
void CALLBACK eventHook(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD) {
    if(event>=EVENT_SYSTEM_MENUSTART&&event<=EVENT_SYSTEM_MENUPOPUPEND) { requestRefresh(); return; }
    if(event==EVENT_SYSTEM_FOREGROUND) { trackBurst(); trackingUntil=GetTickCount64()+500;requestRefresh(); return; }
    if((window==target||window==GetForegroundWindow())&&(event==EVENT_OBJECT_LOCATIONCHANGE||event==EVENT_SYSTEM_MOVESIZESTART)) {
        if(object==OBJID_WINDOW||object==OBJID_CLIENT) { trackBurst();requestTrackingRefresh(); }return;
    }
    if(event==EVENT_SYSTEM_FOREGROUND||((window==target||window==GetForegroundWindow())&&object==OBJID_WINDOW)) requestRefresh();
}
void refresh(bool fast=false) {
    refreshPending=false;
    HWND next=GetForegroundWindow();
    bool menu=hasOpenMenu(next);
    if(paused||menu||!validTarget(next)) { visibilityReason=paused?VisibilityReason::Paused:menu?VisibilityReason::Menu:VisibilityReason::InvalidTarget; hideOverlay(); return; }
    DWORD nextPid{};GetWindowThreadProcessId(next,&nextPid);
    if(next!=target||nextPid!=targetPid) {
        if(tooltip)SendMessageW(tooltip,TTM_POP,0,0);
        target=next; hovered=false; feedbackUntil=0;captionPalette={};captionMotion.clear();targetPid=nextPid; targetKey=home::appKey(target);
    }
    if(targetKey.empty()) { visibilityReason=VisibilityReason::NoIdentity; hideOverlay(); return; }
    const auto& effective=home::settingsPreviewState()?*home::settingsPreviewState():state;
    auto policy=home::resolveWindow(effective,home::describeWindow(target));activeRecordKey=policy.recordKey;
    if(!policy.allowed){visibilityReason=VisibilityReason::RuleExcluded;hideOverlay();return;}
    home::CaptionMeasurement measurement;RECT bounds{};GetWindowRect(target,&bounds);
    auto frameStyle=GetWindowLongPtrW(target,GWL_STYLE);bool maximized=IsZoomed(target)!=FALSE;
    bool followed=fast&&captionMotion.follow(target,targetPid,frameStyle,maximized,GetDpiForWindow(target),bounds,measurement);
    if(!followed) {
        measurement=home::measureCaption(target);
        captionMotion.remember(target,targetPid,frameStyle,maximized,measurement);
    }
    auto caption=home::layoutCaption(measurement,policy.x,policy.y);
    if(caption.widthSource==home::CaptionWidthSource::Unavailable) { visibilityReason=VisibilityReason::NoCaptionMeasurement; hideOverlay(); return; }
    captionBottom=caption.title.bottom;
    RECT desired=caption.button;
    RECT minimize=measurement.hasMinimize?measurement.minimize:measurement.hasVisualMinimize?measurement.visualMinimize:RECT{measurement.hasCaptionGroup?measurement.controlsLeft:measurement.visible.right-3*measurement.fallbackWidth,caption.title.top,0,caption.title.bottom};
    if(minimize.right<=minimize.left) minimize.right=minimize.left+measurement.fallbackWidth;
    // When a higher sibling overlaps the button, hide until it is unobstructed.
    for(HWND w=GetWindow(target,GW_HWNDPREV);w;w=GetWindow(w,GW_HWNDPREV)) {
        if(w==overlay||w==host||!IsWindowVisible(w)||IsIconic(w)) continue;
        auto ex=GetWindowLongPtrW(w,GWL_EXSTYLE);
        // Layered pass-through surfaces (e.g. cursor visualizers) do not obstruct input.
        if((ex&(WS_EX_LAYERED|WS_EX_TRANSPARENT))==(WS_EX_LAYERED|WS_EX_TRANSPARENT)) continue;
        RECT r{},intersection{}; DWORD cloaked{}; DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        if(!cloaked&&GetWindowRect(w,&r)&&IntersectRect(&intersection,&desired,&r)) { visibilityReason=VisibilityReason::Occluded; hideOverlay(); return; }
    }
    // The no-activate overlay needs its own topmost surface to stay above a foreign
    // foreground window. Foreground/menu/occlusion checks constrain its lifetime.
    // This changes only our surface; the target's topmost state remains independent.
    bool top=(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    visibilityReason=SetWindowPos(overlay,HWND_TOPMOST,desired.left,desired.top,desired.right-desired.left,desired.bottom-desired.top,SWP_NOACTIVATE|SWP_SHOWWINDOW)?VisibilityReason::Visible:VisibilityReason::PlacementFailed;
    if(!GetCapture()&&GetTickCount64()>=feedbackUntil) SetWindowTextW(overlay,top?L"●":L"↶");
    InvalidateRect(overlay,nullptr,FALSE);
    // Show/move/paint first. Sampling foreign pixels must not hold up following.
    UpdateWindow(overlay);
    if(!followed) {
        captionPalette=home::sampleCaptionPalette(target,minimize,measurement.dpi);
        InvalidateRect(overlay,nullptr,FALSE);
    }
}
bool operate(HWND window,DWORD pid,UINT button,const std::wstring& expectedScope,bool createNew=false) {
    if(paused||home::settingsPreviewState())return false;
    if(!validTarget(window,pid)||window!=GetForegroundWindow()) return false;
    auto key=home::appKey(window); if(key.empty()||key!=targetKey) return false;
    auto description=home::describeWindow(window);auto policy=home::resolveWindow(state,description);
    if(!policy.allowed||policy.recordKey!=expectedScope){requestRefresh();return false;}
    if(button==WM_RBUTTONDOWN) {
        try {
            auto next=state; home::ensureApplication(next,description.identity);
            auto scope=home::resolveWindow(next,description);home::savePositionRecord(next,scope.recordKey,home::capture(window),createNew);
            if(!store.save(next)) { notify(L"记录写入失败，原记录保持有效。",true); return false; }
            state=std::move(next);activeRecordKey=scope.recordKey;notify(createNew?L"已新增位置记录。":L"已更新默认位置记录。");requestRefresh();return true;
        } catch(...) { notify(L"无法读取目标窗口位置。",true); return false; }
    }
    if(button==WM_LBUTTONDOWN) {
        auto saved=home::activePositionRecord(state,policy.recordKey); if(!saved) { notify(L"请先右键记录位置，或启用一条已有记录。",true); return false; }
        auto result=home::restoreDetailed(window,*saved);
        const wchar_t* message=result==home::RestoreStatus::Restored?L"已恢复当前窗口。":result==home::RestoreStatus::Adapted?L"显示环境已变化，已按当前屏幕恢复。":result==home::RestoreStatus::Limited?L"已按应用限制调整，请检查窗口位置与尺寸。":L"窗口恢复失败，请检查目标窗口状态。";
        notify(message,result==home::RestoreStatus::Failed);return result!=home::RestoreStatus::Failed;
    }
    if(button==WM_MBUTTONDOWN) {
        bool top=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
        bool ok=home::boundedWindowPos(window,top?HWND_NOTOPMOST:HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        bool actual=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
        ok=ok&&(actual!=top); notify(ok?(actual?L"当前窗口已置顶。":L"已取消当前窗口置顶。"):L"置顶操作失败。",!ok); requestRefresh(); return ok;
    }
    return false;
}
LRESULT CALLBACK overlayProc(HWND window,UINT msg,WPARAM w,LPARAM l) {
    static bool pressedNewRecord{};
    if(tooltip && msg>=WM_MOUSEFIRST&&msg<=WM_MOUSELAST) { MSG event{window,msg,w,l}; SendMessageW(tooltip,TTM_RELAYEVENT,0,reinterpret_cast<LPARAM>(&event)); }
    switch(msg) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NOTIFY:
        if(reinterpret_cast<NMHDR*>(l)->hwndFrom==tooltip&&reinterpret_cast<NMHDR*>(l)->code==TTN_SHOW) {
            RECT button{},tip{};GetWindowRect(window,&button);GetWindowRect(tooltip,&tip);
            MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            int x=std::clamp<LONG>(button.left,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-(tip.right-tip.left)));
            int y=std::max(captionBottom,button.bottom)+MulDiv(4,GetDpiForWindow(window),96);
            SetWindowPos(tooltip,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);return TRUE;
        }break;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        RECT r{}; GetClientRect(window,&r); POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; bool inside=PtInRect(&r,p)!=FALSE;
        if(hovered!=inside) { hovered=inside; InvalidateRect(window,nullptr,FALSE); }
        TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0}; TrackMouseEvent(&track); return 0;
    }
    case WM_MOUSELEAVE: hovered=false; InvalidateRect(window,nullptr,FALSE); return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
        if(!pressedButton) { pressedNewRecord=msg==WM_RBUTTONDOWN&&(w&MK_CONTROL)!=0;pressedButton=msg; pressedTarget=target; pressedPid=targetPid;pressedRecordKey=activeRecordKey; SetCapture(window); InvalidateRect(window,nullptr,FALSE); } return 0;
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        UINT expected=msg==WM_LBUTTONUP?WM_LBUTTONDOWN:msg==WM_RBUTTONUP?WM_RBUTTONDOWN:WM_MBUTTONDOWN;
        UINT held=pressedButton;bool createNew=pressedNewRecord;pressedNewRecord=false; HWND heldTarget=pressedTarget; DWORD heldPid=pressedPid;auto heldScope=pressedRecordKey;pressedRecordKey.clear(); pressedButton=0; ReleaseCapture();
        POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; RECT r{}; GetClientRect(window,&r);
        if(held==expected&&PtInRect(&r,p)&&target==heldTarget) operate(heldTarget,heldPid,held,heldScope,createNew);
        return 0;
    }
    case WM_CAPTURECHANGED: pressedButton=0;pressedNewRecord=false;pressedRecordKey.clear(); InvalidateRect(window,nullptr,FALSE); return 0;
    case WM_CONTEXTMENU: return 0;
    case WM_DPICHANGED: requestRefresh(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{}; HDC dc=BeginPaint(window,&ps); RECT r{}; GetClientRect(window,&r);
        auto interaction=hovered?(pressedButton?home::CaptionInteraction::Pressed:home::CaptionInteraction::Hot):home::CaptionInteraction::Normal;
        home::paintCaptionBackground(dc,r,captionPalette,interaction);
        wchar_t text[16]; GetWindowTextW(window,text,16);
        auto glyph=text[0]==L'!'?home::CaptionGlyph::Error:text[0]==L'✓'?home::CaptionGlyph::Success:text[0]==L'●'?home::CaptionGlyph::Pinned:home::CaptionGlyph::Return;
        home::CaptionPalette drawing; drawing.foreground=captionPalette.foreground; drawing.glyphWidth=captionPalette.glyphWidth; drawing.stroke=captionPalette.stroke;
        if(interaction==home::CaptionInteraction::Hot) drawing.foreground=captionPalette.hotForeground;
        if(interaction==home::CaptionInteraction::Pressed) drawing.foreground=captionPalette.pressedForeground;
        home::drawCaptionGlyph(dc,r,drawing,glyph);
        EndPaint(window,&ps); return 0;
    }
    }
    return DefWindowProcW(window,msg,w,l);
}
void showSettings() {
    if(settings) { ShowWindow(settings,SW_RESTORE); SetForegroundWindow(settings); return; }
    home::createSettings(instance,&settings,state,store,requestRefresh,target);
}
LRESULT CALLBACK hostProc(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(msg==taskbarCreated&&taskbarCreated) { trayIcon(); return 0; }
    switch(msg) {
    case WM_APP+3: return static_cast<LRESULT>(visibilityReason);
    case WM_APP+4: return reinterpret_cast<LRESULT>(target);
    case WM_APP+5: return captionPalette.normal;
    case WM_APP+6: return captionPalette.foreground;
    case WM_APP+7: return captionPalette.hot;
    case WM_APP+8: return (captionPalette.sampled?1:0)|(captionPalette.hotSampled?2:0)|(captionPalette.pressedSampled?4:0);
    case WM_APP+9: return hovered;
    case WM_APP+10: {
        auto p=home::inspectCaptionPalette(reinterpret_cast<HWND>(w));
        switch(l){case 0:return p.normal;case 1:return p.foreground;case 2:return p.hot;case 3:return p.flags;case 4:return p.glyphWidth;case 5:return p.stroke;}return 0;
    }
    case Refresh: refresh(!fullRefreshPending.exchange(false)); return 0;
    case WM_WTSSESSION_CHANGE:
        if(w==WTS_SESSION_LOCK||w==WTS_SESSION_UNLOCK) {
            resetTarget();
            if(w==WTS_SESSION_UNLOCK) { requestRefresh(); SetTimer(window,3,250,nullptr); }
        } return 0;
    case WM_DISPLAYCHANGE: case WM_SETTINGCHANGE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: requestRefresh(); return 0;
    case WM_TIMER:
        if(w==4) {
            if(GetTickCount64()>=trackingUntil) { KillTimer(window,4);requestRefresh(); }
            else requestTrackingRefresh();
            return 0;
        }
        if(w==2) { KillTimer(window,2); feedbackUntil=0; SetWindowTextW(overlay,(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)?L"●":L"↶"); }
        if(w==3) KillTimer(window,3);
        requestRefresh(); return 0;
    case Tray:
        if(l==WM_LBUTTONDBLCLK) showSettings();
        if(l==WM_RBUTTONUP) {
            HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,SettingsCommand,L"设置…"); AppendMenuW(menu,MF_STRING,PauseCommand,paused?L"恢复按钮":L"暂停按钮"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,ExitCommand,L"退出");
            POINT p{}; GetCursorPos(&p); SetForegroundWindow(window); int cmd=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr); DestroyMenu(menu); PostMessageW(window,WM_NULL,0,0);
            if(cmd==SettingsCommand) showSettings(); else if(cmd==PauseCommand) { paused=!paused; requestRefresh(); } else if(cmd==ExitCommand) DestroyWindow(window);
        } return 0;
    case WM_DESTROY: {
        if(sessionNotificationsRegistered) { WTSUnRegisterSessionNotification(window); sessionNotificationsRegistered=false; }
        for(auto hook:hooks) UnhookWinEvent(hook); hooks.clear();
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=window; data.uID=1; Shell_NotifyIconW(NIM_DELETE,&data);
        if(settings) DestroyWindow(settings); if(tooltip) DestroyWindow(tooltip); if(overlay) DestroyWindow(overlay);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(window,msg,w,l);
}
void registerClass(const wchar_t* name,WNDPROC proc) {
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance=instance; cls.lpszClassName=name; cls.lpfnWndProc=proc; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(IDI_APP_ICON)); cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(IDI_APP_ICON),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)); cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassExW(&cls);
}
HWND createOverlay() {
    return CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"YiboHome.Overlay",L"↶",WS_POPUP,0,0,26,26,nullptr,nullptr,instance,nullptr);
}
void pump(DWORD milliseconds) {
    ULONGLONG until=GetTickCount64()+milliseconds;
    do { MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); } Sleep(10); } while(GetTickCount64()<until);
}
int fixture() {
    registerClass(L"YiboHome.Fixture",[](HWND w,UINT m,WPARAM a,LPARAM b)->LRESULT {
        if(m==WM_APP+50) { SetWindowLongPtrW(w,GWLP_USERDATA,a); return 0; }
        if(m==WM_GETTITLEBARINFOEX&&GetWindowLongPtrW(w,GWLP_USERDATA)) return 0;
        if(m==WM_APP+51) {
            RECT r{}; GetWindowRect(w,&r); HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,1,L"菜单状态验证");
            TrackPopupMenu(menu,TPM_RETURNCMD,r.left+30,r.top+35,0,w,nullptr); DestroyMenu(menu); return 0;
        }
        if(m==WM_APP+52) { EndMenu(); return 0; }
        if(m==WM_APP+53) { if(a)SetPropW(w,L"YiboHome.MinimumWidth",reinterpret_cast<HANDLE>(900));else RemovePropW(w,L"YiboHome.MinimumWidth");return 0; }
        if(m==WM_GETMINMAXINFO&&GetPropW(w,L"YiboHome.MinimumWidth")) { DefWindowProcW(w,m,a,b);reinterpret_cast<MINMAXINFO*>(b)->ptMinTrackSize.x=900;return 0; }
        if(m==WM_DESTROY) { PostQuitMessage(0); return 0; } return DefWindowProcW(w,m,a,b);
    });
    HWND w=CreateWindowExW(0,L"YiboHome.Fixture",L"风钩 · 独立验证窗口",WS_OVERLAPPEDWINDOW,120,120,720,480,nullptr,nullptr,instance,nullptr);
    ShowWindow(w,SW_SHOW); MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); } return 0;
}
std::filesystem::path executable() { wchar_t path[32768]; GetModuleFileNameW(nullptr,path,32768); return path; }
int selfTest(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory); std::ofstream report(directory/L"results.txt"); int failures{};
    auto check=[&](bool ok,const char* label) { report<<(ok?"PASS ":"FAIL ")<<label<<'\n'; report.flush(); if(!ok) ++failures; };
    std::vector<PROCESS_INFORMATION> processes; std::vector<HWND> windows;
    for(int i=0;i<2;++i) {
        std::wstring command=L"\""+executable().wstring()+L"\" --fixture"; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
        if(!CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&si,&pi)) { check(false,"launch fixture"); return 1; }
        processes.push_back(pi); WaitForInputIdle(pi.hProcess,5000);
        struct Search { DWORD pid; HWND window{}; } search{pi.dwProcessId};
        for(int tries=0;tries<100&&!search.window;++tries) {
            EnumWindows([](HWND w,LPARAM p)->BOOL { auto& s=*reinterpret_cast<Search*>(p); DWORD pid{}; GetWindowThreadProcessId(w,&pid); wchar_t cls[64]; GetClassNameW(w,cls,64); if(pid==s.pid&&wcscmp(cls,L"YiboHome.Fixture")==0) s.window=w; return TRUE; },reinterpret_cast<LPARAM>(&search));
            if(!search.window) Sleep(50);
        }
        windows.push_back(search.window);
    }
    auto a=windows[0],b=windows[1]; check(a&&b,"two independent fixture processes");
    if(a&&b) {
        SetWindowPos(a,nullptr,150,160,640,420,SWP_NOZORDER|SWP_NOACTIVATE); pump(100);
        auto saved=home::capture(a); auto key=home::appKey(a); check(!key.empty()&&key==home::appKey(b),"same exe identity across processes");
        home::State initial; initial.offsetX=-7; initial.offsetY=3; initial.records[key]=saved;
        home::Store testStore{directory/L"prototype-state.json"}; check(testStore.save(initial),"atomic first write");
        home::Store restarted{testStore.path}; auto loaded=restarted.load();
        check(loaded.records.size()==1&&loaded.offsetX==-7&&loaded.records.begin()->second.dpi==saved.dpi,"JSON reload preserves records and offsets");
        check(!saved.topology.empty()&&home::sameTopology(saved.topology,loaded.records.begin()->second.topology)&&EqualRect(&saved.actual,&saved.normal),"capture and durable reload retain complete active display topology");
        SetWindowPos(b,nullptr,450,300,800,600,SWP_NOZORDER|SWP_NOACTIVATE); check(home::restore(b,saved),"restore second instance from shared record");
        RECT ar{},br{}; GetWindowRect(a,&ar); GetWindowRect(b,&br); check(EqualRect(&ar,&br),"restore current window only; source remains unchanged");
        if(saved.topology.size()>1) {
            auto origin=home::displayForRect(saved.normal,saved.topology);size_t other=*origin==0?1:0;auto work=saved.topology[other].work;
            SetWindowPos(b,nullptr,work.left+100,work.top+100,640,420,SWP_NOZORDER|SWP_NOACTIVATE);pump(100);
            auto result=home::restoreDetailed(b,saved);GetWindowRect(b,&br);
            check(result==home::RestoreStatus::Restored&&EqualRect(&br,&saved.normal),"live second-screen target restores exact original screen and bounds");
            auto& first=saved.topology[*origin];auto& second=saved.topology[other];
            LONG boundary=first.monitor.right==second.monitor.left?first.monitor.right:second.monitor.right==first.monitor.left?first.monitor.left:0;
            LONG top=std::max(first.work.top,second.work.top)+100;
            if(boundary&&top+420<std::min(first.work.bottom,second.work.bottom)) {
                SetWindowPos(b,nullptr,boundary-200,top,640,420,SWP_NOZORDER|SWP_NOACTIVATE);pump(100);auto cross=home::capture(b);
                SetWindowPos(b,nullptr,work.left+100,work.top+100,700,450,SWP_NOZORDER|SWP_NOACTIVATE);
                result=home::restoreDetailed(b,cross);GetWindowRect(b,&br);
                check(result==home::RestoreStatus::Restored&&EqualRect(&br,&cross.normal),"live unchanged two-screen layout preserves a spanning window");
                home::restore(b,saved);
            }
        }
        SendMessageW(b,WM_APP+53,TRUE,0);auto limited=home::restoreDetailed(b,saved);GetWindowRect(b,&br);
        check(limited==home::RestoreStatus::Limited&&br.right-br.left>=900,"application minimum size is reported as adjusted rather than exact restore");
        SendMessageW(b,WM_APP+53,FALSE,0);home::restore(b,saved);
        check(SetWindowPos(b,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE)&&(GetWindowLongPtrW(b,GWL_EXSTYLE)&WS_EX_TOPMOST),"enable topmost");
        check(home::restore(b,saved)&&(GetWindowLongPtrW(b,GWL_EXSTYLE)&WS_EX_TOPMOST),"restore preserves topmost");
        SetWindowPos(b,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        ShowWindow(a,SW_MAXIMIZE); pump(100); auto maximized=home::capture(a); check(maximized.maximized,"capture maximized state");
        check(home::restore(b,maximized)&&IsZoomed(b),"restore maximized state");
        ShowWindow(b,SW_RESTORE); pump(100); GetWindowRect(b,&br); check(abs(br.left-saved.normal.left)<=2&&abs(br.top-saved.normal.top)<=2&&abs((br.right-br.left)-(saved.normal.right-saved.normal.left))<=2,"maximized restore retains normal bounds");
        check(testStore.save(initial)&&std::filesystem::exists(testStore.path.wstring()+L".bak"),"replacement retains backup");
        // Corruption and read-only failure use isolated test data only.
        { std::ofstream corrupt(testStore.path); corrupt<<"{broken"; }
        home::Store corrupt{testStore.path}; auto fallback=corrupt.load(); check(!corrupt.writable&&fallback.records.size()==1&&!corrupt.save(initial),"corrupt state recovers backup and prevents overwrite");
        home::Store blocked{directory}; check(!blocked.save(initial),"failed commit is reported");
        MONITORINFOEXW fake{}; fake.cbSize=sizeof(fake); wcscpy_s(fake.szDevice,L"simulated-disconnected-fallback"); fake.rcWork={-1920,-1080,0,0}; fake.rcMonitor=fake.rcWork;
        auto mapped=home::restoreRect(saved,fake,144); check(mapped.left>=-1920&&mapped.top>=-1080&&mapped.right<=0&&mapped.bottom<=0,"negative-coordinate fallback and DPI mapping (simulation)");
        check(mapped.right-mapped.left==std::min(MulDiv(saved.normal.right-saved.normal.left,144,saved.dpi),1920),"DIP size conversion (simulation)");
        SetForegroundWindow(b); pump(100); HWND front=GetForegroundWindow();
        HWND noActivate=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"YiboHome.Overlay",L"↶",WS_POPUP,200,200,26,26,nullptr,nullptr,instance,nullptr);
        ShowWindow(noActivate,SW_SHOWNOACTIVATE); pump(100); check(GetForegroundWindow()==front&&SendMessageW(noActivate,WM_MOUSEACTIVATE,0,0)==MA_NOACTIVATE,"overlay show and mouse-activation contract preserve foreground"); DestroyWindow(noActivate);
        // Exercise the actual tracker and input handlers against our fixture only.
        registerClass(L"YiboHome.Host",hostProc);
        host=CreateWindowExW(WS_EX_TOOLWINDOW,L"YiboHome.Host",L"风钩集成测试事件窗口",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
        overlay=createOverlay();
        store.path=directory/L"interaction-state.json"; state={};
        SetForegroundWindow(b); pump(150);
        report<<"Fixture focus needed: "<<reinterpret_cast<UINT_PTR>(b)<<'\n'; report.flush();
        for(int i=0;i<600&&GetForegroundWindow()!=b;++i) pump(50);
        if(GetForegroundWindow()!=b) {
            check(false,"fixture foreground unavailable; input handlers not exercised");
            DestroyWindow(host); host=nullptr; overlay=nullptr;
            for(auto window:windows) if(window) PostMessageW(window,WM_CLOSE,0,0);
            for(auto pi:processes) { WaitForSingleObject(pi.hProcess,5000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
            report<<"Failures: "<<failures<<'\n'; return 1;
        }
        refresh();
        check(GetForegroundWindow()==b&&target==b&&IsWindowVisible(overlay),"foreground tracker displays overlay on fixture");
        eventHook(nullptr,EVENT_SYSTEM_MENUSTART,b,OBJID_WINDOW,0,0,0); refresh();
        check(IsWindowVisible(overlay),"stale menu-start event cannot latch global overlay visibility");
        PostMessageW(b,WM_APP+51,0,0);
        for(int i=0;i<30&&!hasOpenMenu(b);++i) pump(20);
        refresh(); check(hasOpenMenu(b)&&!IsWindowVisible(overlay),"actual native menu hides the overlay");
        PostMessageW(b,WM_CANCELMODE,0,0); PostMessageW(b,WM_APP+52,0,0);
        for(int i=0;i<30&&hasOpenMenu(b);++i) pump(20);
        refresh(); check(!hasOpenMenu(b)&&IsWindowVisible(overlay),"menu close restores overlay without relying on end event");
        SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_LOCK,0);
        check(!IsWindowVisible(overlay)&&!target&&!pressedButton,"session lock releases target and hides overlay");
        SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0); refresh();
        check(target==b&&IsWindowVisible(overlay),"session unlock rebinds and restores overlay");
        paused=true; SendMessageW(host,WM_WTSSESSION_CHANGE,WTS_SESSION_UNLOCK,0); refresh();
        check(paused&&!IsWindowVisible(overlay),"session unlock preserves explicit user pause");
        paused=false; refresh();
        RECT beforeMove{},afterMove{}; GetWindowRect(overlay,&beforeMove);
        SetWindowPos(b,nullptr,220,240,660,440,SWP_NOZORDER|SWP_NOACTIVATE); pump(50); refresh(); GetWindowRect(overlay,&afterMove);
        check(!EqualRect(&beforeMove,&afterMove),"overlay follows target movement");
        auto handlerClick=[&](UINT down,UINT up) { if(target!=b||GetForegroundWindow()!=b) return; SendMessageW(overlay,down,0,MAKELPARAM(8,8)); SendMessageW(overlay,up,0,MAKELPARAM(8,8)); };
        handlerClick(WM_RBUTTONDOWN,WM_RBUTTONUP);
        check(state.records.size()==1&&GetForegroundWindow()==b,"right-button handler persists record without changing foreground");
        if(state.applications.empty()||state.records.empty()) {
            // Input may be declined after a foreground change. Do not index a
            // missing application or leave fixture child processes running.
            check(false,"record prerequisite unavailable; remaining input checks stopped safely");
            DestroyWindow(host);host=nullptr;overlay=nullptr;
            for(auto fixtureWindow:windows)if(fixtureWindow)PostMessageW(fixtureWindow,WM_CLOSE,0,0);
            for(auto pi:processes){WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}
            report<<"Failures: "<<failures<<'\n';return 1;
        }
        RECT recorded{}; GetWindowRect(b,&recorded);
        SetWindowPos(b,nullptr,350,320,730,480,SWP_NOZORDER|SWP_NOACTIVATE); refresh(); handlerClick(WM_LBUTTONDOWN,WM_LBUTTONUP); GetWindowRect(b,&br);
        check(EqualRect(&recorded,&br)&&GetForegroundWindow()==b,"left-button handler restores target and preserves foreground");
        {
            auto originalState=state;auto scope=home::scopeKey(state.applications[0]);
            SetWindowPos(b,nullptr,520,260,740,480,SWP_NOZORDER|SWP_NOACTIVATE);pump(100);refresh();RECT additionalBounds{};GetWindowRect(b,&additionalBounds);
            SendMessageW(overlay,WM_RBUTTONDOWN,MK_CONTROL,MAKELPARAM(8,8));SendMessageW(overlay,WM_RBUTTONUP,MK_CONTROL,MAKELPARAM(8,8));
            auto entries=home::positionEntries(state,scope);auto additional=entries.size()>1?entries[1].key:L"";
            check(entries.size()==2&&EqualRect(&state.records.at(scope).normal,&recorded)&&home::activePositionKey(state,scope)==scope,"Ctrl-right handler appends a position without replacing default or switching an existing active record");
            bool selected=home::activatePositionRecord(state,scope,additional);bool selectionSaved=store.save(state);
            SetWindowPos(b,nullptr,660,320,600,400,SWP_NOZORDER|SWP_NOACTIVATE);pump(100);refresh();handlerClick(WM_LBUTTONDOWN,WM_LBUTTONUP);GetWindowRect(b,&br);
            check(selected&&selectionSaved&&EqualRect(&br,&additionalBounds)&&GetForegroundWindow()==b,"left handler restores the enabled named position without changing focus");
            SetWindowPos(b,nullptr,390,210,690,460,SWP_NOZORDER|SWP_NOACTIVATE);pump(100);refresh();handlerClick(WM_RBUTTONDOWN,WM_RBUTTONUP);
            check(state.records.size()==2&&home::activePositionKey(state,scope)==additional&&EqualRect(&state.records.at(additional).normal,&additionalBounds),"right handler updates only default while named position remains active");
            state=originalState;check(store.save(state),"isolated multi-position handler test restores its baseline state");refresh();handlerClick(WM_LBUTTONDOWN,WM_LBUTTONUP);
        }
        handlerClick(WM_MBUTTONDOWN,WM_MBUTTONUP); check((GetWindowLongPtrW(b,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"middle-button handler enables topmost");
        handlerClick(WM_MBUTTONDOWN,WM_MBUTTONUP); check((GetWindowLongPtrW(b,GWL_EXSTYLE)&WS_EX_TOPMOST)==0,"middle-button handler disables topmost");
        state.applications[0].enabled=false;refresh();
        check(!IsWindowVisible(overlay)&&state.records.size()==1,"application disable hides overlay while retaining its record");
        state.applications[0].enabled=true;refresh();check(IsWindowVisible(overlay),"application re-enable restores overlay");
        home::WindowRule include;include.id=home::newId(L"r-");include.name=L"Fixture main";include.className=L"YiboHome.Fixture";include.titleMatch=home::TitleMatch::Contains;include.title=L"独立验证";
        state.applications[0].rules.push_back(include);refresh();handlerClick(WM_RBUTTONDOWN,WM_RBUTTONUP);
        check(state.records.size()==2&&state.records.contains(home::scopeKey(state.applications[0],include.id)),"matching window rule records into its independent scope");
        registerClass(L"YiboHome.Settings",home::settingsProc);
        RECT ruleBefore{},ruleAfter{};GetWindowRect(overlay,&ruleBefore);
        auto rulePreview=home::createSettings(instance,&settings,state,store,requestRefresh,b,false);
        ListView_SetItemState(GetDlgItem(rulePreview,200),1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        TabCtrl_SetCurSel(GetDlgItem(rulePreview,204),2);NMHDR ruleTab{GetDlgItem(rulePreview,204),204,TCN_SELCHANGE};SendMessageW(rulePreview,WM_NOTIFY,204,reinterpret_cast<LPARAM>(&ruleTab));
        ListView_SetItemState(GetDlgItem(rulePreview,230),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        SendDlgItemMessageW(rulePreview,241,BM_SETCHECK,BST_CHECKED,0);SendMessageW(rulePreview,WM_COMMAND,MAKEWPARAM(241,BN_CLICKED),0);SetDlgItemTextW(rulePreview,242,L"-32");
        SendMessageW(rulePreview,WM_COMMAND,MAKEWPARAM(252,BN_CLICKED),0);pump(100);refresh();GetWindowRect(overlay,&ruleAfter);
        check(home::settingsPreviewState()&&home::resolveWindow(*home::settingsPreviewState(),home::describeWindow(b)).scopeId==include.id&&ruleAfter.left==ruleBefore.left+MulDiv(-32,GetDpiForWindow(b),96)&&!state.applications[0].rules[0].normal.custom,"rule preview targets its matching scope without saving draft offsets");
        DestroyWindow(rulePreview);refresh();
        auto exclusion=include;exclusion.id=home::newId(L"r-");exclusion.name=L"Excluded fixture";exclusion.exclude=true;state.applications[0].rules.push_back(exclusion);refresh();
        check(!IsWindowVisible(overlay)&&state.records.size()==2,"lower priority exclusion overrides inclusion without erasing scopes");
        state.applications[0].rules.back().active=false;refresh();
        auto originalDefault=state.records.at(home::scopeKey(state.applications[0])).normal;
        SendMessageW(overlay,WM_RBUTTONDOWN,0,MAKELPARAM(8,8));SendMessageW(b,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"临时标题"));refresh();SendMessageW(overlay,WM_RBUTTONUP,0,MAKELPARAM(8,8));
        check(state.records.size()==2&&EqualRect(&originalDefault,&state.records.at(home::scopeKey(state.applications[0])).normal),"title change between press and release cannot write to another scope");
        SendMessageW(b,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"风钩 · 独立验证窗口"));state.applications[0].rules[0].active=false;refresh();
        RECT baselineButton{};GetWindowRect(overlay,&baselineButton);state.applications[0].normal={true,-12,3};refresh();RECT shifted{};GetWindowRect(overlay,&shifted);
        check(shifted.left==baselineButton.left+MulDiv(-12,GetDpiForWindow(b),96)&&shifted.top==baselineButton.top+MulDiv(3,GetDpiForWindow(b),96),"tracker applies per-application DIP offsets");
        state.applications[0].normal.custom=false;refresh();
        registerClass(L"YiboHome.Settings",home::settingsProc);
        auto previewWindow=home::createSettings(instance,&settings,state,store,requestRefresh,b,false);
        ListView_SetItemState(GetDlgItem(previewWindow,200),1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        SendDlgItemMessageW(previewWindow,214,BM_SETCHECK,BST_CHECKED,0);SendMessageW(previewWindow,WM_COMMAND,MAKEWPARAM(214,BN_CLICKED),0);SetDlgItemTextW(previewWindow,215,L"-24");
        SendMessageW(previewWindow,WM_COMMAND,MAKEWPARAM(252,BN_CLICKED),0);pump(100);refresh();GetWindowRect(overlay,&shifted);
        check(home::settingsPreviewState()&&shifted.left==baselineButton.left+MulDiv(-24,GetDpiForWindow(b),96)&&!state.applications[0].normal.custom,"live preview applies draft offset without saving application configuration");
        auto previewCount=state.records.size();handlerClick(WM_RBUTTONDOWN,WM_RBUTTONUP);
        check(state.records.size()==previewCount,"live preview suspends record operations");
        DestroyWindow(previewWindow);refresh();GetWindowRect(overlay,&shifted);
        check(!home::settingsPreviewState()&&!settings&&EqualRect(&shifted,&baselineButton),"closing preview releases draft and restores persisted geometry");
        previewWindow=home::createSettings(instance,&settings,state,store,requestRefresh,b,false);
        ListView_SetItemState(GetDlgItem(previewWindow,200),1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        SendMessageW(previewWindow,WM_COMMAND,MAKEWPARAM(252,BN_CLICKED),0);pump(5200);
        check(!home::settingsPreviewState()&&IsWindow(settings)&&!IsIconic(settings),"five-second preview expires and returns to settings automatically");
        SetForegroundWindow(b);pump(50);DestroyWindow(previewWindow);refresh();
        auto retained=state.records.at(home::scopeKey(state.applications[0])).normal;
        SetWindowPos(b,nullptr,310,270,760,500,SWP_NOZORDER|SWP_NOACTIVATE);paused=true;refresh();handlerClick(WM_RBUTTONDOWN,WM_RBUTTONUP);
        check(GetForegroundWindow()==b&&EqualRect(&retained,&state.records.at(home::scopeKey(state.applications[0])).normal),"paused tool rejects pending input without rewriting application record");
        paused=false;refresh();
        SendMessageW(b,WM_APP+50,TRUE,0); GetWindowRect(b,&br);
        registerClass(L"YiboHome.ContentFixture",DefWindowProcW);
        HWND content=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"YiboHome.ContentFixture",L"content",WS_POPUP,br.left,br.top+40,br.right-br.left,br.bottom-br.top-40,b,nullptr,instance,nullptr);
        ShowWindow(content,SW_SHOWNOACTIVATE); refresh();
        RECT button{}; GetWindowRect(overlay,&button);
        check(IsWindowVisible(overlay)&&button.bottom<=br.top+40,"fallback button fits above separate 40-pixel content surface");
        HWND transparent=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_LAYERED|WS_EX_TRANSPARENT,L"YiboHome.ContentFixture",L"pass-through",WS_POPUP,br.left,br.top,br.right-br.left,br.bottom-br.top,nullptr,nullptr,instance,nullptr);
        SetLayeredWindowAttributes(transparent,0,1,LWA_ALPHA); ShowWindow(transparent,SW_SHOWNOACTIVATE); refresh();
        check(IsWindowVisible(overlay),"layered pass-through surface does not suppress button");
        check(!(GetWindowLongPtrW(b,GWL_EXSTYLE)&WS_EX_TOPMOST),"overlay topmost surface does not promote target window");
        GetWindowRect(overlay,&button); POINT center{(button.left+button.right)/2,(button.top+button.bottom)/2};
        check(WindowFromPoint(center)==overlay,"button center is reachable in desktop hit testing");
        DestroyWindow(transparent);
        SetWindowPos(content,HWND_TOPMOST,br.left,br.top,0,0,SWP_NOSIZE|SWP_NOACTIVATE); refresh();
        check(!IsWindowVisible(overlay),"opaque surface over caption still suppresses button");
        DestroyWindow(content); SendMessageW(b,WM_APP+50,FALSE,0);
        ShowWindow(b,SW_MINIMIZE); refresh(); check(target!=b||!IsWindowVisible(overlay),"minimized target no longer owns the visible overlay");
        DestroyWindow(host); host=nullptr; overlay=nullptr;
    }
    for(auto window:windows) if(window) PostMessageW(window,WM_CLOSE,0,0);
    for(auto pi:processes) { WaitForSingleObject(pi.hProcess,5000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    registerClass(L"YiboHome.Settings",home::settingsProc);
    auto settingsCycle=[] {
        HWND w=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);
        if(!w) return false;
        SendMessageW(w,WM_CLOSE,0,0);
        return settings==nullptr&&!IsWindow(w);
    };
    check(settingsCycle(),"settings close destroys window and owned font");
    // Let destruction notifications drain and warm the native control/theme caches.
    pump(80);
    for(int i=0;i<5;++i) settingsCycle();
    DWORD gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userBefore=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    bool cycles=true; for(int i=0;i<100;++i) cycles=settingsCycle()&&cycles;
    DWORD gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userAfter=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int i=0;i<100;++i) cycles=settingsCycle()&&cycles;
    DWORD gdiFinal=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),userFinal=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    report<<"Settings resources at 0/100/200 cycles: GDI "<<gdiBefore<<" / "<<gdiAfter<<" / "<<gdiFinal<<"; USER "<<userBefore<<" / "<<userAfter<<" / "<<userFinal<<'\n';
    // USER counts include native input/theme bookkeeping with small asynchronous variation.
    // Keep that allowance explicit; per-window leakage would grow by hundreds here.
    check(cycles&&gdiAfter<=gdiBefore&&gdiFinal<=gdiBefore&&userAfter<=userBefore+2&&userFinal<=userBefore+2,"200 settings cycles: GDI stable, USER within two objects of warmed baseline");
    report<<"Failures: "<<failures<<'\n'; return failures?1:0;
}
}
int WINAPI wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int) {
    instance=h; INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES}; InitCommonControlsEx(&common);
    int argc{}; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    std::wstring mode=argc>1?argv[1]:L""; std::filesystem::path testDirectory=argc>2?argv[2]:L"test-state"; LocalFree(argv);
    if(mode==L"--fixture") return fixture();
    if(mode==L"--settings-fixture") {
        std::filesystem::create_directories(testDirectory);store.path=testDirectory/L"settings-fixture.json";
        if(std::filesystem::exists(store.path))state=store.load();
        else {
            auto& app=home::ensureApplication(state,{executable().wstring(),L""},L"验证应用");app.normal={true,-12,0};
            home::WindowRule rule;rule.id=home::newId(L"r-");rule.name=L"主窗口";rule.className=L"YiboHome.Fixture";app.rules.push_back(rule);
            rule.id=home::newId(L"r-");rule.name=L"排除临时对话框";rule.className=L"#32770";rule.exclude=true;app.rules.push_back(rule);
            store.save(state);
        }
        registerClass(L"YiboHome.Settings",home::settingsProc);home::createSettings(instance,&settings,state,store,nullptr);
        SetWindowTextW(settings,L"风钩 · 设置（隔离验证）");
        MSG msg{};while(IsWindow(settings)&&GetMessageW(&msg,nullptr,0,0)>0){if(!IsDialogMessageW(settings,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}return 0;
    }
    registerClass(L"YiboHome.Overlay",overlayProc);
    if(mode==L"--self-test") return selfTest(testDirectory);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\YiboWindowHome.Prototype.v1");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) { HWND other=FindWindowW(L"YiboHome.Host",nullptr); if(other) PostMessageW(other,Tray,0,WM_LBUTTONDBLCLK); CloseHandle(mutex); return 0; }
    PWSTR roaming{}; if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming))) { CloseHandle(mutex); return 1; }
    store.path=std::filesystem::path(roaming)/L"YiboWindowHome"/L"prototype-state.json"; CoTaskMemFree(roaming);
    try { state=store.load(); } catch(...) { store.writable=false; store.warning=L"无法读取原型数据目录，本次会话禁止写入。"; }
    registerClass(L"YiboHome.Host",hostProc); registerClass(L"YiboHome.Settings",home::settingsProc);
    host=CreateWindowExW(WS_EX_TOOLWINDOW,L"YiboHome.Host",L"风钩事件窗口",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    overlay=createOverlay();
    if(!host||!overlay) { CloseHandle(mutex); return 1; }
    sessionNotificationsRegistered=WTSRegisterSessionNotification(host,NOTIFY_FOR_THIS_SESSION)!=FALSE;
    tooltip=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP,0,0,0,0,overlay,nullptr,instance,nullptr);
    TOOLINFOW tool{sizeof(tool)}; tool.uFlags=TTF_IDISHWND; tool.hwnd=overlay; tool.uId=reinterpret_cast<UINT_PTR>(overlay); tool.lpszText=const_cast<PWSTR>(L"左键恢复启用记录 · 右键更新默认\nCtrl + 右键新增记录 · 中键置顶"); SendMessageW(tooltip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated"); trayIcon();
    for(auto range:std::vector<std::pair<DWORD,DWORD>>{{EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND},{EVENT_OBJECT_DESTROY,EVENT_OBJECT_HIDE},{EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE},{EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE},{EVENT_SYSTEM_MENUSTART,EVENT_SYSTEM_MENUPOPUPEND},{EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZEEND}}) {
        auto hook=SetWinEventHook(range.first,range.second,nullptr,eventHook,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if(hook) hooks.push_back(hook);
    }
    if(hooks.size()!=6) notify(L"部分窗口事件监听失败，将使用每秒一次的校验。",true);
    SetTimer(host,1,1000,nullptr); requestRefresh();
    if(mode==L"--settings") showSettings();
    if(!store.warning.empty()) notify(store.warning.c_str(),true);
    MSG msg{}; int result{};
    while((result=GetMessageW(&msg,nullptr,0,0))>0) { if(!settings||!IsDialogMessageW(settings,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    CloseHandle(mutex); return result<0?1:0;
}
