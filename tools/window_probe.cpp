#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "caption.h"
#include <windows.h>
#include <dwmapi.h>
#include <iostream>
#include <string>

int wmain(int argc,wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc!=2) return 2;
    HWND window=reinterpret_cast<HWND>(std::stoull(argv[1]));
    HWND foreground=GetForegroundWindow(); DWORD foregroundPid{}; GetWindowThreadProcessId(foreground,&foregroundPid);
    HANDLE foregroundProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,foregroundPid);
    if(foregroundProcess) { wchar_t path[32768]{}; DWORD size=32768; if(QueryFullProcessImageNameW(foregroundProcess,0,path,&size)) { auto name=wcsrchr(path,L'\\'); std::wcout<<L"foregroundProcess="<<(name?name+1:path)<<L"\n"; } CloseHandle(foregroundProcess); }
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    wchar_t cls[256]{}; GetClassNameW(window,cls,256);
    auto style=GetWindowLongPtrW(window,GWL_STYLE),ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
    // Optional read-only diagnostic; not used by application behavior.
    auto getBand=reinterpret_cast<BOOL(WINAPI*)(HWND,PDWORD)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowBand"));
    DWORD bandId{}; if(getBand&&getBand(window,&bandId)) std::wcout<<L"targetBand="<<bandId<<L"\n";
    DWORD cloaked{}; auto cloakResult=DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
    RECT r{}; GetWindowRect(window,&r);
    auto geometry=home::layoutCaption(home::measureCaption(window));
    std::wcout<<L"captionSource="<<static_cast<int>(geometry.source)<<L" (0 buttons,1 viewport,2 nonclient,3 metrics,4 measured-controls) widthSource="<<static_cast<int>(geometry.widthSource)<<L" (0 minimize,1 group,2 default,3 measured-caption,4 unavailable) title="<<geometry.title.left<<L","<<geometry.title.top<<L","<<geometry.title.right<<L","<<geometry.title.bottom<<L" expectedButton="<<geometry.button.left<<L","<<geometry.button.top<<L","<<geometry.button.right<<L","<<geometry.button.bottom<<L"\n";
    RECT client{}; GetClientRect(window,&client); POINT origin{}; ClientToScreen(window,&origin);
    RECT frame{},caption{}; DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame));
    auto capResult=DwmGetWindowAttribute(window,DWMWA_CAPTION_BUTTON_BOUNDS,&caption,sizeof(caption));
    std::wcout<<L"clientOrigin="<<origin.x<<L","<<origin.y<<L" clientSize="<<client.right<<L","<<client.bottom<<L" frame="<<frame.left<<L","<<frame.top<<L","<<frame.right<<L","<<frame.bottom<<L" dwmCaption="<<capResult<<L":"<<caption.left<<L","<<caption.top<<L","<<caption.right<<L","<<caption.bottom<<L"\n";
    for(int offset=0;offset<=100;offset+=10) {
        DWORD_PTR hit{}; bool ok=SendMessageTimeoutW(window,WM_NCHITTEST,0,MAKELPARAM(r.left+(r.right-r.left)/2,r.top+offset),SMTO_ABORTIFHUNG|SMTO_BLOCK,20,&hit)!=0;
        std::wcout<<L"hit("<<offset<<L")="<<ok<<L":"<<static_cast<LRESULT>(hit)<<L" ";
    } std::wcout<<L"\n";
    EnumWindows([](HWND w,LPARAM p)->BOOL {
        DWORD actual{}; GetWindowThreadProcessId(w,&actual); if(actual!=static_cast<DWORD>(p)||!IsWindowVisible(w)) return TRUE;
        RECT a{}; GetWindowRect(w,&a); wchar_t c[256]{}; GetClassNameW(w,c,256);
        std::wcout<<L"appSurface="<<reinterpret_cast<UINT_PTR>(w)<<L" class="<<c<<L" rect="<<a.left<<L","<<a.top<<L","<<a.right<<L","<<a.bottom<<L"\n";
        EnumChildWindows(w,[](HWND child,LPARAM)->BOOL { RECT b{}; GetWindowRect(child,&b); wchar_t cc[256]{}; GetClassNameW(child,cc,256); std::wcout<<L" child="<<reinterpret_cast<UINT_PTR>(child)<<L" class="<<cc<<L" rect="<<b.left<<L","<<b.top<<L","<<b.right<<L","<<b.bottom<<L"\n"; return TRUE; },0); return TRUE;
    },pid);
    std::wcout<<L"hwnd="<<reinterpret_cast<UINT_PTR>(window)<<L" pid="<<pid<<L" class="<<cls
      <<L" style=0x"<<std::hex<<style<<L" exstyle=0x"<<ex<<std::dec
      <<L" caption="<<!!(style&WS_CAPTION)<<L" tool="<<!!(ex&WS_EX_TOOLWINDOW)
      <<L" visible="<<IsWindowVisible(window)<<L" iconic="<<IsIconic(window)<<L" enabled="<<IsWindowEnabled(window)
      <<L" cloakResult="<<cloakResult<<L" cloaked="<<cloaked
      <<L" rect="<<r.left<<L","<<r.top<<L","<<r.right<<L","<<r.bottom<<L"\n";
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) std::wcout<<L"OpenProcess error="<<GetLastError()<<L"\n";
    else {
        wchar_t path[32768]; DWORD length=32768;
        if(QueryFullProcessImageNameW(process,0,path,&length)) std::wcout<<L"path="<<path<<L"\n";
        else std::wcout<<L"QueryFullProcessImageName error="<<GetLastError()<<L"\n";
        HANDLE token{};
        if(OpenProcessToken(process,TOKEN_QUERY,&token)) {
            DWORD uiAccess{},bytes{}; GetTokenInformation(token,TokenUIAccess,&uiAccess,sizeof(uiAccess),&bytes);
            TOKEN_ELEVATION elevated{}; GetTokenInformation(token,TokenElevation,&elevated,sizeof(elevated),&bytes);
            std::wcout<<L"uiAccess="<<uiAccess<<L" elevated="<<elevated.TokenIsElevated<<L"\n"; CloseHandle(token);
        } else std::wcout<<L"OpenProcessToken error="<<GetLastError()<<L"\n";
        CloseHandle(process);
    }
    TITLEBARINFOEX info{}; info.cbSize=sizeof(info); DWORD_PTR result{};
    auto ok=SendMessageTimeoutW(window,WM_GETTITLEBARINFOEX,0,reinterpret_cast<LPARAM>(&info),SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result);
    std::wcout<<L"titlebarQuery="<<ok<<L" minState="<<info.rgstate[2]<<L" minRect="<<info.rgrect[2].left<<L","<<info.rgrect[2].top<<L","<<info.rgrect[2].right<<L","<<info.rgrect[2].bottom<<L"\n";
    UINT dpi=GetDpiForWindow(window); if(!dpi) dpi=96;
    RECT desired=geometry.button;
    std::wcout<<L"foreground="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<L" dpi="<<dpi<<L" buttonProposal="<<desired.left<<L","<<desired.top<<L","<<desired.right<<L","<<desired.bottom<<L"\n";
    for(HWND above=GetWindow(window,GW_HWNDPREV);above;above=GetWindow(above,GW_HWNDPREV)) {
        RECT a{},intersection{}; if(!IsWindowVisible(above)||IsIconic(above)||!GetWindowRect(above,&a)||!IntersectRect(&intersection,&desired,&a)) continue;
        DWORD apid{}; GetWindowThreadProcessId(above,&apid); wchar_t ac[256]{}; GetClassNameW(above,ac,256); DWORD cloak{}; DwmGetWindowAttribute(above,DWMWA_CLOAKED,&cloak,sizeof(cloak));
        std::wcout<<L"overlap hwnd="<<reinterpret_cast<UINT_PTR>(above)<<L" pid="<<apid<<L" class="<<ac<<L" owner="<<reinterpret_cast<UINT_PTR>(GetWindow(above,GW_OWNER))<<L" ex=0x"<<std::hex<<GetWindowLongPtrW(above,GWL_EXSTYLE)<<std::dec<<L" cloaked="<<cloak<<L" rect="<<a.left<<L","<<a.top<<L","<<a.right<<L","<<a.bottom<<L"\n";
    }
    EnumWindows([](HWND w,LPARAM)->BOOL {
        wchar_t c[256]{}; GetClassNameW(w,c,256); if(wcscmp(c,L"YiboHome.Overlay")!=0) return TRUE;
        RECT a{}; GetWindowRect(w,&a);
        auto getBand=reinterpret_cast<BOOL(WINAPI*)(HWND,PDWORD)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetWindowBand")); DWORD bandId{}; if(getBand&&getBand(w,&bandId)) std::wcout<<L"overlayBand="<<bandId<<L"\n";
        std::wcout<<L"prototypeOverlay hwnd="<<reinterpret_cast<UINT_PTR>(w)<<L" visible="<<IsWindowVisible(w)<<L" ex=0x"<<std::hex<<GetWindowLongPtrW(w,GWL_EXSTYLE)<<std::dec<<L" rect="<<a.left<<L","<<a.top<<L","<<a.right<<L","<<a.bottom<<L"\n";
        POINT center{(a.left+a.right)/2,(a.top+a.bottom)/2}; auto hit=WindowFromPoint(center); wchar_t hitClass[256]{}; GetClassNameW(hit,hitClass,256);
        std::wcout<<L"buttonCenterHit hwnd="<<reinterpret_cast<UINT_PTR>(hit)<<L" class="<<hitClass<<L"\n";
        return TRUE;
    },0);
    if(auto host=FindWindowW(L"YiboHome.Host",nullptr)) {
        DWORD_PTR reason{},target{};
        SendMessageTimeoutW(host,WM_APP+3,0,0,SMTO_ABORTIFHUNG,200,&reason);
        SendMessageTimeoutW(host,WM_APP+4,0,0,SMTO_ABORTIFHUNG,200,&target);
        std::wcout<<L"prototypeStatus="<<reason<<L" (0 initial,1 paused,2 menu,3 invalid,4 identity,5 overlap,6 placement,7 visible,8 measurement pending) target="<<target<<L"\n";
        DWORD_PTR normal{},ink{},hot{},flags{},hovered{};
        SendMessageTimeoutW(host,WM_APP+5,0,0,SMTO_ABORTIFHUNG,200,&normal); SendMessageTimeoutW(host,WM_APP+6,0,0,SMTO_ABORTIFHUNG,200,&ink); SendMessageTimeoutW(host,WM_APP+7,0,0,SMTO_ABORTIFHUNG,200,&hot); SendMessageTimeoutW(host,WM_APP+8,0,0,SMTO_ABORTIFHUNG,200,&flags); SendMessageTimeoutW(host,WM_APP+9,0,0,SMTO_ABORTIFHUNG,200,&hovered);
        std::wcout<<L"palette normalRGB="<<GetRValue(normal)<<L","<<GetGValue(normal)<<L","<<GetBValue(normal)<<L" inkRGB="<<GetRValue(ink)<<L","<<GetGValue(ink)<<L","<<GetBValue(ink)<<L" hotRGB="<<GetRValue(hot)<<L","<<GetGValue(hot)<<L","<<GetBValue(hot)<<L" sampleFlags="<<flags<<L" hovered="<<hovered<<L"\n";
        DWORD_PTR cached[6]{};for(int i=0;i<6;++i)SendMessageTimeoutW(host,WM_APP+10,reinterpret_cast<WPARAM>(window),i,SMTO_ABORTIFHUNG,200,&cached[i]);
        std::wcout<<L"cachedTargetStyle flags="<<cached[3]<<L" normalRGB="<<GetRValue(cached[0])<<L","<<GetGValue(cached[0])<<L","<<GetBValue(cached[0])<<L" inkRGB="<<GetRValue(cached[1])<<L","<<GetGValue(cached[1])<<L","<<GetBValue(cached[1])<<L" hotRGB="<<GetRValue(cached[2])<<L","<<GetGValue(cached[2])<<L","<<GetBValue(cached[2])<<L" glyph="<<cached[4]<<L" stroke="<<cached[5]<<L"\n";
    }
}
