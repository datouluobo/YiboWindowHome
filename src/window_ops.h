#pragma once
#include <windows.h>
#include <algorithm>

namespace home {
inline constexpr UINT WindowOperationTimeoutMs=200;
// Some embedded renderers update their viewport only during WM_SIZING.
// Supply the actual frame rectangle and balance the interactive notifications.
inline bool boundedCommitResize(HWND window,int sidebarInset=0) {
    RECT bounds{};if(!GetWindowRect(window,&bounds))return false;
    bounds.right-=std::max(0,sidebarInset);
    if(bounds.right<=bounds.left)return false;
    DWORD_PTR result{};
    auto send=[&](UINT message,WPARAM w=0,LPARAM l=0){
        return SendMessageTimeoutW(window,message,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&result)!=0;
    };
    if(!send(WM_ENTERSIZEMOVE))return false;
    bool sized=send(WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&bounds));
    bool ended=send(WM_EXITSIZEMOVE);
    return sized&&ended;
}
// Do not attach input queues. Foreign-thread layout callbacks can become stuck
// after the initial responsiveness probe; enqueue the operation and bound the
// acknowledgement instead of calling their layout synchronously.
inline bool windowOperationReady(HWND window,UINT timeout=WindowOperationTimeoutMs) {
    if(!IsWindow(window))return false;
    if(GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId())return true;
    Sleep(1);
    DWORD_PTR response{};
    return SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,timeout,&response)!=0;
}
inline bool boundedWindowPos(HWND window,HWND after,int x,int y,int width,int height,UINT flags) {
    RECT before{};if(!GetWindowRect(window,&before))return false;
    bool originalTop=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
    bool expectedTop=(flags&SWP_NOZORDER)?originalTop:after==HWND_TOPMOST?true:after==HWND_NOTOPMOST?false:originalTop;
    if(!SetWindowPos(window,after,x,y,width,height,flags|SWP_ASYNCWINDOWPOS))return false;
    if(GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId())return true;
    // WM_NULL can overtake the posted asynchronous position request. Observe
    // actual geometry/DPI settling before a following maximize reads placement.
    ULONGLONG until=GetTickCount64()+WindowOperationTimeoutMs,stableSince{};
    RECT previous=before;UINT previousDpi=GetDpiForWindow(window);bool responsive=false;
    do {
        auto now=GetTickCount64();if(now>=until)break;
        if(!windowOperationReady(window,UINT(std::min<ULONGLONG>(WindowOperationTimeoutMs,until-now))))return false;
        responsive=true;RECT current{};if(!GetWindowRect(window,&current))return false;UINT dpi=GetDpiForWindow(window);
        bool top=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0;
        bool exact=((flags&SWP_NOMOVE)||(current.left==x&&current.top==y))&&((flags&SWP_NOSIZE)||(current.right-current.left==width&&current.bottom-current.top==height));
        bool changed=!EqualRect(&before,&current);
        bool eligible=top==expectedTop&&(exact||changed);
        now=GetTickCount64();
        if(eligible&&EqualRect(&previous,&current)&&dpi==previousDpi){if(!stableSince)stableSince=now;if(now-stableSince>=20)return true;}
        else stableSince=0;
        previous=current;previousDpi=dpi;Sleep(1);
    }while(GetTickCount64()<until);
    // A responsive fixed-size application may reject every requested dimension.
    // Let the caller inspect the unchanged rectangle and report Limited.
    if(responsive&&((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0)==expectedTop)return true;
    SetLastError(ERROR_TIMEOUT);return false;
}
inline bool boundedShowWindow(HWND window,int command) {
    auto until=GetTickCount64()+WindowOperationTimeoutMs;
    if(GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId())ShowWindow(window,command);
    else if(!ShowWindowAsync(window,command))return false;
    if(!windowOperationReady(window))return false;
    do {
        bool ready=command==SW_MAXIMIZE?IsZoomed(window)!=FALSE:!IsZoomed(window)&&!IsIconic(window);
        if(ready)return true;
        Sleep(1);
    }while(IsWindow(window)&&GetTickCount64()<until);
    return false;
}
inline bool boundedNativeResize(HWND window,RECT requested) {
    DWORD_PTR response{};
    auto send=[&](UINT message,WPARAM w=0,LPARAM l=0){return SendMessageTimeoutW(window,message,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&response)!=0;};
    if(!send(WM_ENTERSIZEMOVE))return false;
    auto place=[&](RECT r){return boundedWindowPos(window,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);};
    bool ok=place(requested);
    if(ok)ok=send(WM_SIZING,WMSZ_BOTTOMRIGHT,reinterpret_cast<LPARAM>(&requested));
    // WM_SIZING is an in/out request: the application can constrain its frame
    // to match the embedded engine. Apply that result before ending the loop.
    if(ok)ok=requested.right>requested.left&&requested.bottom>requested.top&&
        requested.right-requested.left<=100000&&requested.bottom-requested.top<=100000;
    if(ok)ok=place(requested);
    bool ended=send(WM_EXITSIZEMOVE);
    return ok&&ended;
}
inline int bottomEdgeCorrection(RECT requested,RECT visible,RECT work,UINT dpi) {
    int tolerance=MulDiv(12,dpi?dpi:96,96);
    // An outer frame that reaches past the work area was recorded against the
    // taskbar. A record ending above it must keep its intentional gap.
    if(requested.bottom<work.bottom||requested.bottom-work.bottom>tolerance)return 0;
    int gap=work.bottom-visible.bottom;
    return gap>0&&gap<=tolerance?gap:0;
}
inline bool visibleWindowFrame(HWND window,RECT& visible) {
    using FrameQuery=HRESULT(WINAPI*)(HWND,DWORD,PVOID,DWORD);
    static auto query=[]()->FrameQuery {
        HMODULE module=LoadLibraryExW(L"dwmapi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        return module?reinterpret_cast<FrameQuery>(GetProcAddress(module,"DwmGetWindowAttribute")):nullptr;
    }();
    return query&&SUCCEEDED(query(window,9,&visible,sizeof(visible)));
}
inline bool boundedAlignBottomEdge(HWND window,RECT requested,RECT work,const RECT* contentFrame=nullptr) {
    RECT visible{},actual{};
    if(contentFrame)visible=*contentFrame;
    else if(!visibleWindowFrame(window,visible))return true;
    int dy=bottomEdgeCorrection(requested,visible,work,GetDpiForWindow(window));
    if(!dy)return true;
    if(!GetWindowRect(window,&actual))return false;
    return boundedWindowPos(window,nullptr,actual.left,actual.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
}
inline bool boundedWindowPlacement(HWND window,WINDOWPLACEMENT placement) {
    placement.flags|=WPF_ASYNCWINDOWPLACEMENT;
    return SetWindowPlacement(window,&placement)!=FALSE&&windowOperationReady(window);
}
}
