#pragma once
#include "window_ops.h"
#include <cwchar>

namespace home {
inline HWND embeddedShell(HWND root) {
    struct Search {HWND root,shell{};} search{root};
    EnumChildWindows(root,[](HWND child,LPARAM p)->BOOL{
        auto& s=*reinterpret_cast<Search*>(p);wchar_t cls[128]{};GetClassNameW(child,cls,128);
        if(wcscmp(cls,L"subWin")!=0)return TRUE;
        HWND shell=GetParent(child);while(shell&&GetParent(shell)!=s.root)shell=GetParent(shell);
        if(shell){GetClassNameW(shell,cls,128);DWORD a{},b{};GetWindowThreadProcessId(shell,&a);GetWindowThreadProcessId(s.root,&b);if(a==b&&wcsncmp(cls,L"Qt",2)==0)s.shell=shell;}
        return FALSE;
    },reinterpret_cast<LPARAM>(&search));return search.shell;
}
inline bool finishNativeVerticalFit(HWND root,RECT requested,RECT work) {
    UINT dpi=GetDpiForWindow(root);if(!dpi)dpi=96;int limit=MulDiv(12,dpi,96);
    if(abs(requested.top-work.top)>limit||requested.bottom<work.bottom||requested.bottom-work.bottom>limit)return true;
    Sleep(100);
    RECT content{},frame{};HWND shell=embeddedShell(root);
    if(!shell||!GetWindowRect(shell,&content)||!visibleWindowFrame(root,frame))return true;
    int gap=work.bottom-content.bottom;
    if(gap<=0||gap>limit)return true;
    if(GetForegroundWindow()!=root||(GetAsyncKeyState(VK_LBUTTON)&0x8000)||(GetAsyncKeyState(VK_RBUTTON)&0x8000)||(GetAsyncKeyState(VK_MBUTTON)&0x8000)||
        (GetAsyncKeyState(VK_SHIFT)&0x8000)||(GetAsyncKeyState(VK_CONTROL)&0x8000)||(GetAsyncKeyState(VK_MENU)&0x8000))return true;
    POINT cursor{},start{frame.right-1,(frame.top+frame.bottom)/2};if(!GetCursorPos(&cursor))return false;
    DWORD_PTR hit{};
    if(GetAncestor(WindowFromPoint(start),GA_ROOT)!=root||!SendMessageTimeoutW(root,WM_NCHITTEST,0,MAKELPARAM(start.x,start.y),SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&hit)||hit!=HTRIGHT)return true;
    // Androws' client layout commits its aspect ratio through the real edge
    // interaction, including engine input coordinates. Keep this small and
    // scoped to a foreground, idle-input window; never resize child surfaces.
    if(!SetCursorPos(start.x,start.y))return false;
    INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_LEFTDOWN;
    if(SendInput(1,&input,sizeof(input))!=1){SetCursorPos(cursor.x,cursor.y);return false;}
    Sleep(120);
    bool captured=false;auto until=GetTickCount64()+150;
    do {GUITHREADINFO gui{sizeof(gui)};if(GetGUIThreadInfo(GetWindowThreadProcessId(root,nullptr),&gui)&&
        ((gui.flags&GUI_INMOVESIZE)||(gui.hwndCapture&&GetAncestor(gui.hwndCapture,GA_ROOT)==root))){captured=true;break;}Sleep(5);}while(GetTickCount64()<until&&GetForegroundWindow()==root);
    POINT end=start;bool moved=false;
    if(captured&&GetForegroundWindow()==root){end.x+=MulDiv(8,dpi,96);moved=SetCursorPos(end.x,end.y)!=FALSE;if(moved){Sleep(200);}}
    input.mi.dwFlags=MOUSEEVENTF_LEFTUP;bool released=SendInput(1,&input,sizeof(input))==1;
    if(!released)return false;
    // Wait for the target to consume button-up before restoring the cursor;
    // otherwise its resize loop can interpret that restoration as another drag.
    bool ended=false;until=GetTickCount64()+600;
    do {GUITHREADINFO gui{sizeof(gui)};
        if(GetGUIThreadInfo(GetWindowThreadProcessId(root,nullptr),&gui)&&!(gui.flags&GUI_INMOVESIZE)&&!gui.hwndCapture){ended=true;break;}
        Sleep(5);
    }while(GetTickCount64()<until);
    POINT now{};GetCursorPos(&now);
    if(ended&&((now.x==end.x&&now.y==end.y)||(now.x==start.x&&now.y==start.y)))SetCursorPos(cursor.x,cursor.y);
    if(!ended)return false;
    until=GetTickCount64()+300;
    do {RECT settled{};if(GetWindowRect(shell,&settled)&&settled.bottom>=work.bottom)break;Sleep(5);}while(GetTickCount64()<until);
    return true;
}
inline int embeddedSidebarInset(HWND root) {
    struct Search {HWND root;int inset{};} search{root};
    EnumChildWindows(root,[](HWND child,LPARAM p)->BOOL {
        auto& s=*reinterpret_cast<Search*>(p);wchar_t cls[128]{};GetClassNameW(child,cls,128);
        if(wcscmp(cls,L"subWin")!=0)return TRUE;
        HWND shell=GetParent(child);
        while(shell&&GetParent(shell)!=s.root)shell=GetParent(shell);
        if(!shell)return FALSE;
        RECT client{},bounds{};GetClientRect(s.root,&client);GetWindowRect(shell,&bounds);
        MapWindowPoints(nullptr,s.root,reinterpret_cast<POINT*>(&bounds),2);
        int gap=client.right-bounds.right;UINT dpi=GetDpiForWindow(s.root);if(!dpi)dpi=96;
        if(gap>=MulDiv(16,dpi,96)&&gap<=MulDiv(96,dpi,96))s.inset=gap;
        return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    return search.inset;
}
// Androws' subWin renderer sits inside a Qt host and a Qt viewport. Only
// these last two surfaces fill their parents; the viewport owns the caption
// and sidebar exclusions and must retain the emulator's own layout.
inline bool syncEmbeddedRenderer(HWND root) {
    struct Context { HWND root; bool ok{true}; int remaining{256}; } context{root};
    EnumChildWindows(root,[](HWND child,LPARAM data)->BOOL {
        auto& c=*reinterpret_cast<Context*>(data);if(--c.remaining<0)return FALSE;
        wchar_t cls[128]{};GetClassNameW(child,cls,128);
        if(wcscmp(cls,L"subWin")!=0||!IsWindowVisible(child))return TRUE;
        HWND host=GetParent(child),viewport=GetParent(host);
        if(!host||!viewport||viewport==c.root||GetAncestor(child,GA_ROOT)!=c.root)return TRUE;
        // The Android renderer is owned by another emulator process. Verify
        // its Qt containers belong to the target, rather than rejecting the
        // embedded renderer because it has a different PID.
        DWORD rootPid{},hostPid{},viewportPid{};
        GetWindowThreadProcessId(c.root,&rootPid);GetWindowThreadProcessId(host,&hostPid);GetWindowThreadProcessId(viewport,&viewportPid);
        if(rootPid!=hostPid||rootPid!=viewportPid)return TRUE;
        GetClassNameW(host,cls,128);if(wcsncmp(cls,L"Qt",2)!=0)return TRUE;
        GetClassNameW(viewport,cls,128);if(wcsncmp(cls,L"Qt",2)!=0)return TRUE;
        for(HWND surface : {host,child}) {
            HWND parent=GetParent(surface);RECT client{};
            if(!GetClientRect(parent,&client)||client.right<=0||client.bottom<=0){c.ok=false;break;}
            RECT current{};GetWindowRect(surface,&current);MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&current),2);
            if(EqualRect(&current,&client))continue;
            if(!SetWindowPos(surface,nullptr,0,0,client.right,client.bottom,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)
                ||!windowOperationReady(surface)){c.ok=false;break;}
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    return context.ok;
}
inline bool syncEmbeddedLayout(HWND root,int sidebarInset) {
    HWND shell=embeddedShell(root);if(!shell)return true;
    RECT client{};if(!GetClientRect(root,&client))return false;
    client.right-=std::max(0,sidebarInset);if(client.right<=0||client.bottom<=0)return false;
    RECT current{};GetWindowRect(shell,&current);MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&current),2);
    if(!EqualRect(&current,&client)) {
        if(!SetWindowPos(shell,nullptr,0,0,client.right,client.bottom,SWP_NOZORDER|SWP_NOACTIVATE|SWP_ASYNCWINDOWPOS)||!windowOperationReady(shell))return false;
    }
    return syncEmbeddedRenderer(root);
}
inline bool embeddedLayoutReady(HWND root,int sidebarInset) {
    HWND shell=embeddedShell(root);if(!shell)return true;
    RECT client{},actual{};if(!GetClientRect(root,&client)||!GetWindowRect(shell,&actual))return false;
    client.right-=std::max(0,sidebarInset);MapWindowPoints(nullptr,root,reinterpret_cast<POINT*>(&actual),2);
    auto close=[](RECT a,RECT b){return abs(a.left-b.left)<=2&&abs(a.top-b.top)<=2&&abs(a.right-b.right)<=2&&abs(a.bottom-b.bottom)<=2;};
    if(!close(client,actual))return false;
    struct Check {bool ready{true};} check;
    EnumChildWindows(root,[](HWND child,LPARAM p)->BOOL{
        wchar_t cls[128]{};GetClassNameW(child,cls,128);if(wcscmp(cls,L"subWin")!=0||!IsWindowVisible(child))return TRUE;
        for(HWND surface : {GetParent(child),child}) {
            RECT expected{},r{};HWND parent=GetParent(surface);
            if(!GetClientRect(parent,&expected)||!GetWindowRect(surface,&r)){reinterpret_cast<Check*>(p)->ready=false;return FALSE;}
            MapWindowPoints(nullptr,parent,reinterpret_cast<POINT*>(&r),2);
            if(abs(r.left)>2||abs(r.top)>2||abs(r.right-expected.right)>2||abs(r.bottom-expected.bottom)>2){reinterpret_cast<Check*>(p)->ready=false;return FALSE;}
        }return TRUE;
    },reinterpret_cast<LPARAM>(&check));return check.ready;
}
}
