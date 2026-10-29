#include <windows.h>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>
int wmain(){
    UINT32 pathsCount{},modesCount{};
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathsCount,&modesCount)!=ERROR_SUCCESS||pathsCount>256||modesCount>1024)return 2;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathsCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modesCount);
    auto status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathsCount,paths.data(),&modesCount,modes.data(),nullptr);
    if(status!=ERROR_SUCCESS){std::cout<<"query-error="<<status<<'\n';return 2;}
    std::map<std::tuple<LONG,DWORD,UINT32>,unsigned> sources;
    for(UINT32 i=0;i<pathsCount;++i){auto& s=paths[i].sourceInfo;sources[{s.adapterId.HighPart,s.adapterId.LowPart,s.id}]++;}
    unsigned maxTargets{};for(auto& [source,count]:sources)maxTargets=std::max(maxTargets,count);
    bool mirrored=maxTargets>=2;
    std::cout<<"active-paths="<<pathsCount<<" sources="<<sources.size()<<" max-targets-per-source="<<maxTargets<<" mirrored="<<(mirrored?1:0)<<'\n';
    return mirrored?0:3;
}
