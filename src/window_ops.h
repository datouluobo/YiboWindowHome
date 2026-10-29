#pragma once
#include <windows.h>
#include <algorithm>

namespace home {
inline constexpr UINT WindowOperationTimeoutMs=200;
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
inline bool boundedWindowPlacement(HWND window,WINDOWPLACEMENT placement) {
    placement.flags|=WPF_ASYNCWINDOWPLACEMENT;
    return SetWindowPlacement(window,&placement)!=FALSE&&windowOperationReady(window);
}
}
