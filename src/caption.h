#pragma once
#include <windows.h>

namespace home {
enum class CaptionSource { Buttons, ChildViewport, NonClient, SystemMetrics, MeasuredControls };
enum class CaptionWidthSource { MinimizeButton, CaptionGroup, SystemDefault, MeasuredCaption, Unavailable };
struct CaptionMeasurement {
    RECT window{}, visible{}, client{}, minimize{};
    LONG viewportTop{};
    bool hasMinimize{}, hasViewport{};
    UINT dpi{96};
    int fallbackHeight{32};
    int fallbackWidth{46};
    LONG controlsLeft{};
    bool hasCaptionGroup{};
    RECT visualMinimize{};
    bool requiresVisualWidth{},hasVisualMinimize{};
    bool hasMeasuredControlRow{};
};
struct CaptionLayout {
    RECT title{}, button{};
    CaptionSource source{};
    CaptionWidthSource widthSource{};
};
CaptionMeasurement measureCaption(HWND window);
CaptionLayout layoutCaption(const CaptionMeasurement& measurement,int offsetX=0,int offsetY=0);
}
