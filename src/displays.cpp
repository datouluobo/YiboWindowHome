#include "displays.h"
#include <shellscalingapi.h>
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>

namespace home {
namespace {
std::wstring normalized(std::wstring text) {
    std::transform(text.begin(),text.end(),text.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return text;
}
std::map<std::wstring,std::wstring> displayIdentities() {
    // Query only active paths; a topology change can invalidate buffer sizes.
    for(int attempt=0;attempt<3;++attempt) {
        UINT32 pathCount{},modeCount{};
        constexpr UINT32 flags=QDC_ONLY_ACTIVE_PATHS;
        if(GetDisplayConfigBufferSizes(flags,&pathCount,&modeCount)!=ERROR_SUCCESS||pathCount>256||modeCount>1024)return {};
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        LONG result=QueryDisplayConfig(flags,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
        if(result==ERROR_INSUFFICIENT_BUFFER)continue;
        if(result!=ERROR_SUCCESS)return {};
        std::map<std::wstring,std::wstring> names;
        for(UINT32 i=0;i<pathCount;++i) {
            auto& path=paths[i];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS)continue;
            auto key=normalized(source.viewGdiDeviceName);
            bool known=DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS&&target.monitorDevicePath[0];
            // Clone paths have several physical targets for one desktop view.
            // Preserve that uncertainty rather than choosing an arbitrary one.
            if(names.contains(key))names[key].clear();
            else names.emplace(key,known?normalized(target.monitorDevicePath):L"");
        }
        return names;
    }
    return {};
}
std::int64_t area(RECT a,RECT b) {
    RECT intersection{};return IntersectRect(&intersection,&a,&b)?std::int64_t(intersection.right-intersection.left)*(intersection.bottom-intersection.top):0;
}
bool sameIdentity(const Display& a,const Display& b) {
    if(!a.identity.empty()||!b.identity.empty())return !a.identity.empty()&&!b.identity.empty()&&_wcsicmp(a.identity.c_str(),b.identity.c_str())==0;
    return !a.device.empty()&&_wcsicmp(a.device.c_str(),b.device.c_str())==0;
}
}
std::vector<Display> collectDisplays() {
    std::vector<Display> result;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        auto& displays=*reinterpret_cast<std::vector<Display>*>(data);
        if(displays.size()>=64)return FALSE;
        MONITORINFOEXW info{};info.cbSize=sizeof(info);
        if(GetMonitorInfoW(monitor,&info)) {
            UINT x=96,y=96;GetDpiForMonitor(monitor,MDT_EFFECTIVE_DPI,&x,&y);
            displays.push_back({info.szDevice,L"",info.rcMonitor,info.rcWork,x,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    auto identities=displayIdentities();
    for(auto& display:result)if(auto i=identities.find(normalized(display.device));i!=identities.end())display.identity=i->second;
    return result;
}
std::optional<size_t> displayForRect(RECT rect,std::span<const Display> displays) {
    if(displays.empty())return {};
    if(rect.right<=rect.left||rect.bottom<=rect.top) {
        for(size_t i=0;i<displays.size();++i)if(displays[i].primary)return i;
        return 0;
    }
    size_t best{};std::int64_t largest=-1,nearest=INT64_MAX;
    for(size_t i=0;i<displays.size();++i) {
        auto bounds=displays[i].monitor;
        auto overlap=area(rect,bounds);
        std::int64_t dx=std::max({0L,bounds.left-rect.right,rect.left-bounds.right});
        std::int64_t dy=std::max({0L,bounds.top-rect.bottom,rect.top-bounds.bottom});
        auto distance=dx*dx+dy*dy;
        if(overlap>largest||(overlap==largest&&distance<nearest)) {best=i;largest=overlap;nearest=distance;}
    }
    return best;
}
bool sameTopology(std::span<const Display> saved,std::span<const Display> current) {
    if(saved.empty()||saved.size()!=current.size())return false;
    std::vector<bool> used(current.size());
    for(auto& a:saved) {
        std::optional<size_t> match;
        for(size_t i=0;i<current.size();++i)if(sameIdentity(a,current[i])) {if(match)return false;match=i;}
        if(!match||used[*match])return false;
        auto& b=current[*match];
        if(!EqualRect(&a.monitor,&b.monitor)||!EqualRect(&a.work,&b.work)||a.dpi!=b.dpi||a.primary!=b.primary)return false;
        used[*match]=true;
    }
    return true;
}
bool hasReachableCaption(RECT rect,std::span<const Display> displays) {
    if(rect.right<=rect.left||rect.bottom<=rect.top)return false;
    for(auto& d:displays) {
        LONG w=std::min<LONG>(rect.right-rect.left,MulDiv(80,d.dpi,96));
        LONG h=std::min<LONG>(rect.bottom-rect.top,MulDiv(16,d.dpi,96));
        RECT caption=rect,intersection{};caption.bottom=std::min(rect.bottom,rect.top+MulDiv(32,d.dpi,96));
        if(IntersectRect(&intersection,&caption,&d.work)&&intersection.right-intersection.left>=w&&intersection.bottom-intersection.top>=h)return true;
    }
    return false;
}
RECT makeCaptionReachable(RECT rect,const Display& d) {
    LONG width=rect.right-rect.left,height=rect.bottom-rect.top;
    LONG visibleWidth=std::min({width,d.work.right-d.work.left,LONG(MulDiv(80,d.dpi,96))});
    LONG visibleHeight=std::min({height,d.work.bottom-d.work.top,LONG(MulDiv(16,d.dpi,96))});
    LONG x=std::clamp(rect.left,d.work.left+visibleWidth-width,d.work.right-visibleWidth);
    LONG y=std::clamp(rect.top,d.work.top,d.work.bottom-visibleHeight);
    return {x,y,x+width,y+height};
}
}
