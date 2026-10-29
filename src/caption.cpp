#include "caption.h"
#include "visual_caption.h"
#include <dwmapi.h>
#include <algorithm>

namespace home {
CaptionMeasurement measureCaption(HWND window) {
    CaptionMeasurement m;
    m.dpi=GetDpiForWindow(window); if(!m.dpi) m.dpi=96;
    m.fallbackWidth=MulDiv(46,m.dpi,96);
    GetWindowRect(window,&m.window); m.visible=m.window;
    RECT frame{}; if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_EXTENDED_FRAME_BOUNDS,&frame,sizeof(frame)))&&frame.right>frame.left&&frame.bottom>frame.top) m.visible=frame;
    if(IsZoomed(window)) { MONITORINFO monitor{sizeof(monitor)}; if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor)) {RECT clipped{};if(IntersectRect(&clipped,&m.visible,&monitor.rcWork))m.visible=clipped;} }
    RECT client{}; GetClientRect(window,&client); POINT origin{}; ClientToScreen(window,&origin);
    m.client={origin.x,origin.y,origin.x+client.right,origin.y+client.bottom};
    TITLEBARINFOEX info{}; info.cbSize=sizeof(info); DWORD_PTR result{};
    if(SendMessageTimeoutW(window,WM_GETTITLEBARINFOEX,0,reinterpret_cast<LPARAM>(&info),SMTO_ABORTIFHUNG|SMTO_BLOCK,40,&result)) {
        auto r=info.rgrect[2];
        m.hasMinimize=!(info.rgstate[2]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_UNAVAILABLE))&&r.right>r.left&&r.bottom>r.top&&r.left>=m.window.left&&r.right<=m.window.right&&r.top>=m.window.top&&r.bottom<=m.window.bottom;
        if(m.hasMinimize) m.minimize=r;
    }
    RECT controls{};
    auto style=GetWindowLongPtrW(window,GWL_STYLE);
    if(!m.hasMinimize && (style&(WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU))==(WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_SYSMENU)
        && SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CAPTION_BUTTON_BOUNDS,&controls,sizeof(controls)))
        && controls.right>controls.left && controls.bottom>controls.top && controls.left>=0 && controls.right<=m.window.right-m.window.left) {
        int width=MulDiv(controls.right-controls.left,1,3);
        if(width>=MulDiv(16,m.dpi,96)&&width<=MulDiv(96,m.dpi,96)) {
            m.fallbackWidth=width; m.controlsLeft=m.window.left+controls.left; m.hasCaptionGroup=true;
        }
    }
    // Full-client custom frames often expose their large content viewport as a
    // descendant HWND. Its top edge gives the actual caption's bottom boundary.
    if(!m.hasMinimize && m.client.top-m.visible.top<MulDiv(12,m.dpi,96)) {
        struct Search { CaptionMeasurement* m; int remaining{256}; } search{&m};
        EnumChildWindows(window,[](HWND child,LPARAM p)->BOOL {
            auto& s=*reinterpret_cast<Search*>(p); if(--s.remaining<0) return FALSE;
            if(!IsWindowVisible(child)) return TRUE;
            RECT r{}; if(!GetWindowRect(child,&r)) return TRUE;
            auto& m=*s.m;
            LONG width=std::min(r.right,m.client.right)-std::max(r.left,m.client.left);
            LONG gap=r.top-m.client.top;
            if(width*10>=(m.client.right-m.client.left)*8 && abs(r.bottom-m.client.bottom)<=MulDiv(8,m.dpi,96) && gap>=MulDiv(16,m.dpi,96) && gap<=MulDiv(64,m.dpi,96) && r.bottom-r.top>(m.client.bottom-m.client.top)/2) {
                if(!m.hasViewport||r.top<m.viewportTop) { m.hasViewport=true; m.viewportTop=r.top; }
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&search));
    }
    m.fallbackHeight=GetSystemMetricsForDpi(SM_CYCAPTION,m.dpi);
    if(m.fallbackHeight<1) m.fallbackHeight=MulDiv(32,m.dpi,96);
    if(isChromiumWindow(window)) {
        m.requiresVisualWidth=true;m.hasMinimize=false;m.hasCaptionGroup=false;
        auto measured=measureChromiumMinimize(window,m.visible,m.dpi);
        if(measured){m.visualMinimize=*measured;m.hasVisualMinimize=true;m.hasMeasuredControlRow=true;}
    } else if(!m.hasMinimize && !m.hasCaptionGroup && isAndrowsWindow(window)) {
        m.requiresVisualWidth=true;
        if(m.hasViewport) {
            auto measured=measureAndrowsMinimize(window,layoutCaption(m).title,m.dpi);
            if(measured) { m.visualMinimize=*measured; m.hasVisualMinimize=true; }
        }
    }
    return m;
}
CaptionLayout layoutCaption(const CaptionMeasurement& m,int offsetX,int offsetY) {
    CaptionLayout result; auto& title=result.title;
    title={m.visible.left,m.visible.top,m.visible.right,m.visible.top+m.fallbackHeight};
    result.source=CaptionSource::SystemMetrics;
    if(m.hasVisualMinimize&&m.hasMeasuredControlRow) {
        title.top=m.visualMinimize.top;title.bottom=m.visualMinimize.bottom;result.source=CaptionSource::MeasuredControls;
    } else if(m.hasMinimize) {
        title.top=std::max(m.visible.top,m.minimize.top); title.bottom=m.minimize.bottom;
        result.source=CaptionSource::Buttons;
    } else if(m.hasViewport) {
        title.top=std::max(m.visible.top,m.client.top); title.bottom=m.viewportTop;
        result.source=CaptionSource::ChildViewport;
    } else {
        LONG height=m.client.top-m.visible.top;
        if(height>=MulDiv(16,m.dpi,96)&&height<=MulDiv(96,m.dpi,96)) { title.bottom=m.client.top; result.source=CaptionSource::NonClient; }
    }
    int height=std::max(1L,title.bottom-title.top);
    int width=m.hasMinimize?m.minimize.right-m.minimize.left:m.hasVisualMinimize?m.visualMinimize.right-m.visualMinimize.left:std::max(1,m.fallbackWidth);
    result.widthSource=m.hasMinimize?CaptionWidthSource::MinimizeButton:m.hasVisualMinimize?CaptionWidthSource::MeasuredCaption:m.hasCaptionGroup?CaptionWidthSource::CaptionGroup:m.requiresVisualWidth?CaptionWidthSource::Unavailable:CaptionWidthSource::SystemDefault;
    int anchor=m.hasMinimize?m.minimize.left:m.hasVisualMinimize?m.visualMinimize.left:m.hasCaptionGroup?m.controlsLeft:m.visible.right-3*width;
    int x=anchor-width+MulDiv(offsetX,m.dpi,96);
    // Keep a manual horizontal adjustment from entering caption controls.
    x=std::min(x,anchor-width);
    int y=title.top+MulDiv(offsetY,m.dpi,96);
    result.button={x,y,x+width,y+height};
    return result;
}
}
