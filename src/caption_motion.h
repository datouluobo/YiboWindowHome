#pragma once
#include "caption.h"

namespace home {
// One target only. Reuse control metrics during a move/resize burst; a quiet
// timer performs a fresh measurement. DPI/state/frame changes require measuring.
class CaptionMotion {
    HWND window_{};
    DWORD pid_{};
    LONG_PTR style_{};
    bool maximized_{};
    CaptionMeasurement measured_{};
public:
    void remember(HWND window,DWORD pid,LONG_PTR style,bool maximized,const CaptionMeasurement& measured);
    bool follow(HWND window,DWORD pid,LONG_PTR style,bool maximized,UINT dpi,RECT bounds,CaptionMeasurement& result) const;
    void clear() { window_=nullptr; }
};
}
