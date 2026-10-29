#include "state.h"
#include <algorithm>
#include <cmath>

namespace home {
namespace {
RECT mappedRect(const Record& r,const Display& d) {
    const auto& old=r.normal;const auto& work=d.work;
    int width=std::clamp(MulDiv(old.right-old.left,d.dpi,r.dpi),1,int(work.right-work.left));
    int height=std::clamp(MulDiv(old.bottom-old.top,d.dpi,r.dpi),1,int(work.bottom-work.top));
    double rx=std::clamp(double(old.left-r.work.left)/std::max(1L,r.work.right-r.work.left-(old.right-old.left)),0.0,1.0);
    double ry=std::clamp(double(old.top-r.work.top)/std::max(1L,r.work.bottom-r.work.top-(old.bottom-old.top)),0.0,1.0);
    LONG x=work.left+LONG(std::lround(rx*(work.right-work.left-width)));
    LONG y=work.top+LONG(std::lround(ry*(work.bottom-work.top-height)));
    return {x,y,x+width,y+height};
}
std::optional<size_t> anchor(const Record& r,std::span<const Display> displays) {
    std::optional<size_t> match;
    for(size_t i=0;i<displays.size();++i) {
        bool equal=!r.displayIdentity.empty()?_wcsicmp(r.displayIdentity.c_str(),displays[i].identity.c_str())==0:
            !r.device.empty()&&_wcsicmp(r.device.c_str(),displays[i].device.c_str())==0;
        if(equal) {if(match)return {};match=i;}
    }
    return match;
}
bool spansDisplays(RECT rect,std::span<const Display> displays) {
    int count{};for(auto& d:displays) {RECT intersection{};if(IntersectRect(&intersection,&rect,&d.monitor)&&++count>1)return true;}return false;
}
}
RECT restoreRect(const Record& saved,const MONITORINFOEXW& info,UINT dpi) {
    if(saved.device==info.szDevice&&EqualRect(&saved.work,&info.rcWork)&&EqualRect(&saved.monitor,&info.rcMonitor)&&saved.dpi==dpi)return saved.normal;
    return mappedRect(saved,Display{info.szDevice,L"",info.rcMonitor,info.rcWork,dpi});
}
std::optional<RestorePlan> planRestore(const Record& saved,std::span<const Display> displays,RECT currentWindow) {
    if(displays.empty()||saved.normal.right<=saved.normal.left||saved.normal.bottom<=saved.normal.top||saved.dpi<48||saved.dpi>960)return {};
    auto match=anchor(saved,displays);
    auto selected=match?match:displayForRect(currentWindow,displays);
    if(!selected)return {};
    RestorePlan plan;plan.displayIndex=*selected;plan.fallback=!match;
    plan.environmentChanged=!sameTopology(saved.topology,displays);
    auto& display=displays[*selected];
    // Missing or ambiguous anchor identities never reuse coordinates just because
    // a DISPLAY number has been assigned to a different monitor.
    plan.normal=!plan.environmentChanged&&match?saved.normal:mappedRect(saved,display);
    plan.crossScreenReduced=plan.environmentChanged&&spansDisplays(saved.normal,saved.topology);
    if(!hasReachableCaption(plan.normal,displays)) {
        plan.normal=makeCaptionReachable(plan.normal,display);plan.visibilityCorrected=true;
    }
    return plan;
}
std::optional<DesktopPreview> buildDesktopPreview(const Record* record,std::span<const Display> displays,RECT currentWindow,RECT viewport) {
    if(displays.empty()||displays.size()>64||viewport.right<=viewport.left||viewport.bottom<=viewport.top)return {};
    DesktopPreview result;result.desktopBounds=displays.front().monitor;
    for(auto& d:displays) {
        auto valid=[](RECT r){return r.left>=-100000&&r.top>=-100000&&r.right<=100000&&r.bottom<=100000&&r.right>r.left&&r.bottom>r.top;};
        if(!valid(d.monitor)||!valid(d.work)||d.dpi<48||d.dpi>960)return {};
        result.desktopBounds.left=std::min(result.desktopBounds.left,d.monitor.left);
        result.desktopBounds.top=std::min(result.desktopBounds.top,d.monitor.top);
        result.desktopBounds.right=std::max(result.desktopBounds.right,d.monitor.right);
        result.desktopBounds.bottom=std::max(result.desktopBounds.bottom,d.monitor.bottom);
    }
    const auto& bounds=result.desktopBounds;
    result.scale=std::min(double(viewport.right-viewport.left)/(bounds.right-bounds.left),double(viewport.bottom-viewport.top)/(bounds.bottom-bounds.top));
    double x=viewport.left+((viewport.right-viewport.left)-(bounds.right-bounds.left)*result.scale)/2;
    double y=viewport.top+((viewport.bottom-viewport.top)-(bounds.bottom-bounds.top)*result.scale)/2;
    auto project=[&](RECT rect)->PreviewBounds {return {x+(rect.left-bounds.left)*result.scale,y+(rect.top-bounds.top)*result.scale,x+(rect.right-bounds.left)*result.scale,y+(rect.bottom-bounds.top)*result.scale};};
    for(auto& display:displays)result.displays.push_back({display,project(display.monitor),project(display.work)});
    if(record) {
        result.plan=planRestore(*record,displays,currentWindow);if(!result.plan)return {};
        result.windowBounds=record->maximized?displays[result.plan->displayIndex].work:result.plan->normal;
        result.window=project(result.windowBounds);
    }
    return result;
}
std::optional<DesktopPreview> currentDesktopPreview(const Record* record,RECT currentWindow,RECT viewport) {
    auto displays=collectDisplays();return buildDesktopPreview(record,displays,currentWindow,viewport);
}
}
