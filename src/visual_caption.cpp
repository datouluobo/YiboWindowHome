#include "visual_caption.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace home {
std::optional<RECT> detectAndrowsMinimize(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi,std::vector<RECT>* debugGlyphs) {
    if(width<1||height<1||pixels.size()!=size_t(width)*height||width>1024||height>256) return {};
    int middle=height/2,half=MulDiv(12,dpi,96);
    int top=std::max(1,middle-half),bottom=std::min(height-1,middle+half);
    if(bottom<=top) return {};
    std::vector<unsigned char> mask(size_t(width)*height);
    // Column medians across the caption remove hover fills and theme gradients.
    std::array<int,256> histogram{};
    std::vector<int> background(width);
    auto gray=[](std::uint32_t p) { return (int(p&255)+int((p>>8)&255)+int((p>>16)&255))/3; };
    for(int x=0;x<width;++x) {
        histogram.fill(0); for(int y=1;y<height-1;++y) ++histogram[gray(pixels[size_t(y)*width+x])];
        int total{},value{}; for(;value<255;++value) { total+=histogram[value]; if(total>=std::max(1,(height-2)/2)) break; } background[x]=value;
    }
    for(int y=top;y<bottom;++y) {
        for(int x=0;x<width;++x) {
            auto p=pixels[size_t(y)*width+x];
            // The sidebar toggle turns blue when expanded. Column background
            // subtraction still removes solid overlay fills, preserving glyphs.
            if(std::abs(gray(p)-background[x])>=24) mask[size_t(y)*width+x]=1;
        }
    }
    std::vector<RECT> components; std::vector<int> queue;
    for(int y=top;y<bottom;++y) for(int x=0;x<width;++x) {
        int start=y*width+x; if(!mask[start]) continue;
        RECT r{x,y,x+1,y+1}; queue.clear(); queue.push_back(start); mask[start]=0;
        for(size_t i=0;i<queue.size();++i) {
            int xx=queue[i]%width,yy=queue[i]/width;
            r.left=std::min<LONG>(r.left,xx); r.right=std::max<LONG>(r.right,xx+1); r.top=std::min<LONG>(r.top,yy); r.bottom=std::max<LONG>(r.bottom,yy+1);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                int nx=xx+dx,ny=yy+dy;
                if(nx<0||nx>=width||ny<top||ny>=bottom) continue;
                int index=ny*width+nx; if(mask[index]) { mask[index]=0; queue.push_back(index); }
            }
        }
        if(r.right-r.left>=MulDiv(4,dpi,96)&&r.right-r.left<=MulDiv(24,dpi,96)&&r.bottom-r.top<=MulDiv(24,dpi,96)) components.push_back(r);
    }
    std::sort(components.begin(),components.end(),[](RECT a,RECT b){return a.left<b.left;});
    // The sidebar icon can contain disconnected strokes; merge overlapping x spans.
    std::vector<RECT> glyphs;
    for(auto r:components) {
        if(!glyphs.empty()&&r.left<glyphs.back().right) { auto& g=glyphs.back(); g.right=std::max(g.right,r.right); g.top=std::min(g.top,r.top); g.bottom=std::max(g.bottom,r.bottom); }
        else glyphs.push_back(r);
    }
    if(debugGlyphs) *debugGlyphs=glyphs;
    std::optional<RECT> detected;
    for(size_t i=0;i+3<glyphs.size();++i) {
        auto a=glyphs[i],b=glyphs[i+1],c=glyphs[i+2],d=glyphs[i+3];
        int aw=a.right-a.left,ah=a.bottom-a.top;
        if(aw<MulDiv(6,dpi,96)||ah>std::max(2,MulDiv(3,dpi,96))||aw<ah*3) continue;
        if(b.bottom-b.top<MulDiv(5,dpi,96)||c.bottom-c.top<MulDiv(5,dpi,96)||d.bottom-d.top<MulDiv(5,dpi,96)) continue;
        int ca=a.left+a.right,cb=b.left+b.right,cc=c.left+c.right,cd=d.left+d.right;
        std::array<int,3> distances{cb-ca,cc-cb,cd-cc}; std::sort(distances.begin(),distances.end());
        int spacing=(distances[1]+1)/2,tolerance=std::max(2,MulDiv(2,dpi,96));
        if(spacing<MulDiv(16,dpi,96)||spacing>MulDiv(80,dpi,96)) continue;
        if(std::abs((cc-cb)-2*spacing)>2*tolerance||std::abs((cd-cc)-2*spacing)>2*tolerance) continue;
        if(std::abs((b.top+b.bottom)-(a.top+a.bottom))>2*tolerance||std::abs((c.top+c.bottom)-(a.top+a.bottom))>2*tolerance) continue;
        int trailing=width-(cd+1)/2;
        // An expanded emulator toolbar extends the frame beyond the caption's
        // sidebar toggle. Keep measuring the four glyphs, allowing that extra
        // strip without treating it as part of a caption button.
        if(trailing<spacing/3||trailing>spacing+MulDiv(64,dpi,96)) continue;
        int left=(ca-spacing)/2;
        if(left<0) continue;
        RECT result{left,0,left+spacing,height};
        if(detected) return {}; // Ambiguous captions must not acquire a guessed width.
        detected=result;
    }
    return detected;
}
bool isAndrowsWindow(HWND window) {
    wchar_t cls[128]{}; GetClassNameW(window,cls,128);
    if(wcsncmp(cls,L"Qt",2)!=0) return false;
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); if(!process) return false;
    wchar_t path[32768]{}; DWORD size=32768; bool ok=QueryFullProcessImageNameW(process,0,path,&size)!=FALSE; CloseHandle(process);
    if(!ok) return false;
    auto name=wcsrchr(path,L'\\'); return _wcsicmp(name?name+1:path,L"Androws.exe")==0;
}
std::optional<RECT> measureAndrowsMinimize(HWND window,RECT title,UINT dpi) {
    struct Cache { HWND window{}; DWORD pid{}; int titleWidth{},titleHeight{}; UINT dpi{}; ULONGLONG sampled{}; std::optional<RECT> relative; };
    static Cache cache;
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    int width=title.right-title.left,height=title.bottom-title.top;
    bool same=cache.window==window&&cache.pid==pid&&cache.titleWidth==width&&cache.titleHeight==height&&cache.dpi==dpi;
    if(!same) cache={window,pid,width,height,dpi,0,{}};
    auto absolute=[&]() -> std::optional<RECT> { if(!cache.relative) return {}; auto r=*cache.relative; OffsetRect(&r,title.left,title.top); return r; };
    if(GetForegroundWindow()!=window) return absolute();
    if(same&&GetTickCount64()-cache.sampled<50) return absolute();
    cache.sampled=GetTickCount64();
    int cropWidth=std::min({width,MulDiv(280,dpi,96),1024});
    if(cropWidth<1||height<1||height>MulDiv(96,dpi,96)) return {};
    HDC screen=GetDC(nullptr); if(!screen) return {};
    HDC memory=CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=cropWidth; info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    bool ok=false; HGDIOBJ old{};
    if(memory&&bitmap&&pixels) { old=SelectObject(memory,bitmap); ok=BitBlt(memory,0,0,cropWidth,height,screen,title.right-cropWidth,title.top,SRCCOPY|CAPTUREBLT)!=FALSE; GdiFlush(); }
    std::optional<RECT> measured;
    if(ok&&GetForegroundWindow()==window) {
        try { measured=detectAndrowsMinimize({static_cast<const std::uint32_t*>(pixels),size_t(cropWidth)*height},cropWidth,height,dpi); } catch(...) {}
    }
    if(old) SelectObject(memory,old); if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); ReleaseDC(nullptr,screen);
    if(measured) {
        OffsetRect(&*measured,title.right-title.left-cropWidth,0);
        POINT center{title.left+(measured->left+measured->right)/2,title.top+height/2};
        HWND hit=WindowFromPoint(center);
        if(hit&&GetAncestor(hit,GA_ROOT)==window) cache.relative=measured;
        else cache.relative.reset();
    } else if(ok&&GetForegroundWindow()==window) cache.relative.reset();
    return absolute();
}
}
