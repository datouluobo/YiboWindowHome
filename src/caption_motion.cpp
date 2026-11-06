#include "caption_motion.h"

namespace home {
void CaptionMotion::remember(HWND window,DWORD pid,LONG_PTR style,bool maximized,const CaptionMeasurement& measured) {
    window_=window;pid_=pid;style_=style;maximized_=maximized;measured_=measured;
}
bool CaptionMotion::follow(HWND window,DWORD pid,LONG_PTR style,bool maximized,UINT dpi,RECT bounds,CaptionMeasurement& result) const {
    // Custom controls need not move with the outer right edge (for example an
    // emulator sidebar can extend the frame without moving its caption group).
    if(measured_.requiresVisualWidth&&bounds.right-bounds.left!=measured_.window.right-measured_.window.left)return false;
    if(!window_||window!=window_||pid!=pid_||style!=style_||maximized!=maximized_||dpi!=measured_.dpi
        ||bounds.right<=bounds.left||bounds.bottom<=bounds.top||layoutCaption(measured_).widthSource==CaptionWidthSource::Unavailable)return false;
    result=measured_;
    LONG dx=bounds.left-measured_.window.left,dy=bounds.top-measured_.window.top;
    LONG right=bounds.right-measured_.window.right,bottom=bounds.bottom-measured_.window.bottom;
    result.window=bounds;
    auto frame=[&](RECT& r){r.left+=dx;r.right+=right;r.top+=dy;r.bottom+=bottom;};
    frame(result.visible);frame(result.client);
    OffsetRect(&result.minimize,right,dy);OffsetRect(&result.visualMinimize,right,dy);
    result.controlsLeft+=right;result.viewportTop+=dy;
    return true;
}
}
