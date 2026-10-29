#define UNICODE
#define _UNICODE
#include "caption.h"
#include "visual_caption.h"
#include <iostream>
#include <string>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HWND window=reinterpret_cast<HWND>(std::stoull(argv[1]));
    auto m=home::measureCaption(window); auto layout=home::layoutCaption(m);
    auto r=home::measureAndrowsMinimize(window,layout.title,m.dpi);
    std::cout<<"Androws="<<home::isAndrowsWindow(window)<<" dpi="<<m.dpi<<" titleHeight="<<layout.title.bottom-layout.title.top<<"\n";
    if(r) std::cout<<"measured="<<r->left<<","<<r->top<<","<<r->right<<","<<r->bottom<<" width="<<r->right-r->left<<"\n";
    else std::cout<<"measurement unavailable\n";
    return r?0:1;
}
