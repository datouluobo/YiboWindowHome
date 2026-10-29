#include "caption_style.h"
#include <algorithm>
#include <array>
#include <deque>
#include <vector>
#include <dwmapi.h>

namespace home {
namespace {
int channel(std::uint32_t p,int shift) { return (p>>shift)&255; }
int distance(COLORREF a,COLORREF b) { return abs(int(GetRValue(a))-int(GetRValue(b)))+abs(int(GetGValue(a))-int(GetGValue(b)))+abs(int(GetBValue(a))-int(GetBValue(b))); }
COLORREF blend(COLORREF a,COLORREF b,int percentage) {
    return RGB((GetRValue(a)*(100-percentage)+GetRValue(b)*percentage)/100,(GetGValue(a)*(100-percentage)+GetGValue(b)*percentage)/100,(GetBValue(a)*(100-percentage)+GetBValue(b)*percentage)/100);
}
COLORREF color(std::uint32_t p) { return RGB(channel(p,16),channel(p,8),channel(p,0)); }
struct Entry { HWND target{}; DWORD pid{}; CaptionPalette palette; ULONGLONG sampled{}; int width{},height{};UINT dpi{}; };
std::deque<Entry> entries;
bool unobstructed(HWND target,RECT area) {
    for(HWND w=GetWindow(target,GW_HWNDPREV);w;w=GetWindow(w,GW_HWNDPREV)) {
        if(!IsWindowVisible(w)||IsIconic(w))continue;
        auto ex=GetWindowLongPtrW(w,GWL_EXSTYLE);
        if((ex&(WS_EX_LAYERED|WS_EX_TRANSPARENT))==(WS_EX_LAYERED|WS_EX_TRANSPARENT))continue;
        DWORD cloaked{};DwmGetWindowAttribute(w,DWMWA_CLOAKED,&cloaked,sizeof(cloaked));
        RECT bounds{},intersection{};if(!cloaked&&GetWindowRect(w,&bounds)&&IntersectRect(&intersection,&area,&bounds))return false;
    }
    return true;
}
}
CaptionStyleStatus inspectCaptionPalette(HWND target) {
    DWORD pid{};GetWindowThreadProcessId(target,&pid);
    auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.target==target&&e.pid==pid;});
    if(it==entries.end())return {};
    auto& p=it->palette;return {p.normal,p.foreground,p.hot,unsigned((p.sampled?1:0)|(p.hotSampled?2:0)|(p.pressedSampled?4:0)),p.glyphWidth,p.stroke};
}
CaptionSample analyzeCaptionButton(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi) {
    CaptionSample result;
    if(width<4||height<4||width>512||height>512||pixels.size()!=size_t(width)*height) return result;
    std::array<int,3> median{}; std::array<int,256> histogram{};
    for(int c=0;c<3;++c) {
        histogram.fill(0);
        for(int y=1;y<height-1;++y) for(int x=1;x<width-1;++x) ++histogram[channel(pixels[size_t(y)*width+x],c*8)];
        int total{},value{}; for(;value<255;++value) { total+=histogram[value]; if(total>=(width-2)*(height-2)/2) break; } median[c]=value;
    }
    result.background=RGB(median[2],median[1],median[0]);
    int maxContrast{}; COLORREF ink{};
    int inset=std::max(1,MulDiv(2,dpi,96)),top=std::max(1,height/2-MulDiv(12,dpi,96)),bottom=std::min(height-1,height/2+MulDiv(12,dpi,96));
    for(int y=top;y<bottom;++y) for(int x=inset;x<width-inset;++x) {
        auto c=color(pixels[size_t(y)*width+x]); int contrast=distance(c,result.background);
        if(contrast>maxContrast) { maxContrast=contrast; ink=c; }
    }
    if(maxContrast<24) return result;
    RECT glyph{width,height,0,0};
    for(int y=top;y<bottom;++y) for(int x=inset;x<width-inset;++x) {
        if(distance(color(pixels[size_t(y)*width+x]),result.background)<maxContrast*3/4) continue;
        glyph.left=std::min<LONG>(glyph.left,x); glyph.top=std::min<LONG>(glyph.top,y); glyph.right=std::max<LONG>(glyph.right,x+1); glyph.bottom=std::max<LONG>(glyph.bottom,y+1);
    }
    int w=glyph.right-glyph.left,h=glyph.bottom-glyph.top;
    if(w<MulDiv(5,dpi,96)||w>MulDiv(24,dpi,96)||h<1||h>std::max(3,MulDiv(4,dpi,96))||w<h*3) return result;
    result.foreground=ink; result.glyphWidth=w; result.stroke=std::clamp(h,1,std::max(1,MulDiv(2,dpi,96))); result.valid=true;
    // Keep the actual native background, including gradients/hover corners, and
    // remove its minimize glyph before drawing our own action glyph.
    std::vector<std::uint32_t> clean(pixels.begin(),pixels.end());
    for(int y=std::max(0L,glyph.top-1);y<std::min<LONG>(height,glyph.bottom+1);++y) {
        std::array<int,3> row{};
        for(int c=0;c<3;++c) {
            histogram.fill(0); for(int x=1;x<width-1;++x) ++histogram[channel(pixels[size_t(y)*width+x],c*8)];
            int total{},v{}; for(;v<255;++v) { total+=histogram[v]; if(total>=(width-2)/2) break; } row[c]=v;
        }
        auto p=std::uint32_t(row[0]|(row[1]<<8)|(row[2]<<16));
        for(int x=std::max(0L,glyph.left-1);x<std::min<LONG>(width,glyph.right+1);++x) clean[size_t(y)*width+x]=p;
    }
    result.image.width=std::min(width,128); result.image.height=std::min(height,96);
    result.image.pixels.resize(size_t(result.image.width)*result.image.height);
    for(int y=0;y<result.image.height;++y) for(int x=0;x<result.image.width;++x) result.image.pixels[size_t(y)*result.image.width+x]=clean[size_t(y*height/result.image.height)*width+x*width/result.image.width];
    return result;
}
void updateCaptionPalette(CaptionPalette& palette,const CaptionSample& sample,CaptionInteraction interaction) {
    if(!sample.valid) return;
    if(interaction==CaptionInteraction::Normal) {
        bool hadNormal=palette.sampled;
        bool themeChanged=hadNormal&&(distance(palette.normal,sample.background)>12||distance(palette.foreground,sample.foreground)>24);
        palette.normal=sample.background; palette.foreground=sample.foreground; palette.sampled=true;
        palette.normalImage=sample.image;
        if(!hadNormal||themeChanged) {
            if(themeChanged||!palette.hotSampled) { palette.hot=blend(palette.normal,palette.foreground,8); palette.hotForeground=palette.foreground; palette.hotImage={}; palette.hotSampled=false; }
            if(themeChanged||!palette.pressedSampled) { palette.pressed=blend(palette.normal,palette.foreground,15); palette.pressedForeground=palette.foreground; palette.pressedImage={}; palette.pressedSampled=false; }
        }
    } else if(interaction==CaptionInteraction::Hot) {
        palette.hot=sample.background; palette.hotForeground=sample.foreground; palette.hotImage=sample.image; palette.hotSampled=true;
    } else { palette.pressed=sample.background; palette.pressedForeground=sample.foreground; palette.pressedImage=sample.image; palette.pressedSampled=true; }
    palette.glyphWidth=sample.glyphWidth; palette.stroke=sample.stroke;
}
CaptionPalette sampleCaptionPalette(HWND target,RECT minimize,UINT dpi) {
    DWORD pid{}; GetWindowThreadProcessId(target,&pid);
    auto it=std::find_if(entries.begin(),entries.end(),[&](const Entry& e){return e.target==target&&e.pid==pid;});
    if(it==entries.end()) { if(entries.size()>=8) entries.pop_front(); entries.push_back({target,pid,{},{}}); it=std::prev(entries.end()); it->palette.glyphWidth=MulDiv(10,dpi,96); }
    auto& entry=*it;
    int width=minimize.right-minimize.left,height=minimize.bottom-minimize.top;
    if(entry.width!=width||entry.height!=height||entry.dpi!=dpi) {
        entry.width=width;entry.height=height;entry.dpi=dpi;entry.sampled=0;
        entry.palette.hotSampled=false;entry.palette.pressedSampled=false;entry.palette.hotImage={};entry.palette.pressedImage={};
    }
    if(GetForegroundWindow()!=target||GetTickCount64()-entry.sampled<200) return entry.palette;
    entry.sampled=GetTickCount64();
    if(width<4||height<4||width>512||height>512) return entry.palette;
    POINT center{(minimize.left+minimize.right)/2,(minimize.top+minimize.bottom)/2};
    HWND hit=WindowFromPoint(center); if(!hit||GetAncestor(hit,GA_ROOT)!=target) return entry.palette;
    if(!unobstructed(target,minimize))return entry.palette;
    POINT cursor{}; GetCursorPos(&cursor);
    bool hot=PtInRect(&minimize,cursor)&&GetAncestor(WindowFromPoint(cursor),GA_ROOT)==target;
    CaptionInteraction interaction=hot?((GetAsyncKeyState(VK_LBUTTON)&0x8000)?CaptionInteraction::Pressed:CaptionInteraction::Hot):CaptionInteraction::Normal;
    HDC screen=GetDC(nullptr); if(!screen) return entry.palette;
    HDC memory=CreateCompatibleDC(screen); BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=width; info.bmiHeader.biHeight=-height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0); HGDIOBJ old{};
    if(memory&&bitmap&&pixels) {
        old=SelectObject(memory,bitmap);
        if(BitBlt(memory,0,0,width,height,screen,minimize.left,minimize.top,SRCCOPY|CAPTUREBLT)) {
            GdiFlush();
            try { auto sample=analyzeCaptionButton({static_cast<const std::uint32_t*>(pixels),size_t(width)*height},width,height,dpi); if(GetForegroundWindow()==target) updateCaptionPalette(entry.palette,sample,interaction); } catch(...) {}
        }
    }
    if(old) SelectObject(memory,old); if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory); ReleaseDC(nullptr,screen);
    return entry.palette;
}
void paintCaptionBackground(HDC dc,RECT rect,const CaptionPalette& palette,CaptionInteraction interaction) {
    const auto& image=interaction==CaptionInteraction::Hot?palette.hotImage:interaction==CaptionInteraction::Pressed?palette.pressedImage:palette.normalImage;
    if(!image.pixels.empty()) {
        BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=image.width; info.bmiHeader.biHeight=-image.height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
        StretchDIBits(dc,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,0,0,image.width,image.height,image.pixels.data(),&info,DIB_RGB_COLORS,SRCCOPY);
    } else {
        COLORREF color=interaction==CaptionInteraction::Hot?palette.hot:interaction==CaptionInteraction::Pressed?palette.pressed:palette.normal;
        HBRUSH brush=CreateSolidBrush(color); FillRect(dc,&rect,brush); DeleteObject(brush);
    }
}
void drawCaptionGlyph(HDC dc,RECT rect,const CaptionPalette& palette,CaptionGlyph glyph) {
    int size=std::clamp<int>(palette.glyphWidth,6,std::max(6L,std::min(rect.right-rect.left,rect.bottom-rect.top)-6));
    int x=(rect.left+rect.right-size)/2,y=(rect.top+rect.bottom-size)/2,edge=size-1,half=size/2;
    HPEN pen=CreatePen(PS_SOLID,palette.stroke,palette.foreground); auto oldPen=SelectObject(dc,pen); auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    auto line=[&](std::initializer_list<POINT> p){ Polyline(dc,p.begin(),static_cast<int>(p.size())); };
    if(glyph==CaptionGlyph::Return) {
        line({{x+half,y},{x,y},{x,y+edge},{x+edge,y+edge},{x+edge,y+half},{x+half,y+half}});
        line({{x+half+size/3,y+half-size/3},{x+half,y+half},{x+half+size/3,y+half+size/3}});
    } else if(glyph==CaptionGlyph::Pinned) {
        line({{x+size/4,y},{x+edge-size/4,y},{x+edge-size/4,y+half},{x+edge,y+half+size/4},{x,y+half+size/4},{x+size/4,y+half},{x+size/4,y}});
        line({{x+half,y+half+size/4},{x+half,y+edge}});
    } else if(glyph==CaptionGlyph::Success) line({{x,y+half},{x+size/3,y+edge},{x+edge,y}});
    else { line({{x+half,y},{x+half,y+half}}); SetPixelV(dc,x+half,y+edge,palette.foreground); }
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
}
}
