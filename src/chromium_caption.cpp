#include "visual_caption.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>

namespace home {
std::optional<RECT> detectCaptionControls(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi,int expectedButtons) {
    if(width<1||height<1||width>1024||height>256||pixels.size()!=size_t(width)*height||(expectedButtons!=3&&expectedButtons!=4)) return {};
    std::vector<unsigned char> mask(pixels.size());
    std::array<int,256> histogram{};
    auto gray=[](std::uint32_t p){return (int(p&255)+int((p>>8)&255)+int((p>>16)&255))/3;};
    for(int y=0;y<height;++y) {
        histogram.fill(0); for(int x=0;x<width;++x) ++histogram[gray(pixels[size_t(y)*width+x])];
        int count{},background{}; for(;background<255;++background) { count+=histogram[background]; if(count>=width/2) break; }
        for(int x=0;x<width;++x) { auto p=pixels[size_t(y)*width+x]; int a=p&255,b=(p>>8)&255,c=(p>>16)&255; if(std::max({a,b,c})-std::min({a,b,c})<45&&abs(gray(p)-background)>=24) mask[size_t(y)*width+x]=1; }
    }
    auto ink=mask;
    std::vector<RECT> components; std::vector<int> queue;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        int start=y*width+x; if(!mask[start]) continue;
        RECT r{x,y,x+1,y+1}; queue.clear(); queue.push_back(start); mask[start]=0;
        for(size_t i=0;i<queue.size();++i) {
            int xx=queue[i]%width,yy=queue[i]/width; r.left=std::min<LONG>(r.left,xx); r.right=std::max<LONG>(r.right,xx+1); r.top=std::min<LONG>(r.top,yy); r.bottom=std::max<LONG>(r.bottom,yy+1);
            for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) { int nx=xx+dx,ny=yy+dy; if(nx<0||nx>=width||ny<0||ny>=height) continue; int index=ny*width+nx; if(mask[index]) {mask[index]=0;queue.push_back(index);} }
        }
        if(r.right-r.left>=MulDiv(4,dpi,96)&&r.right-r.left<=MulDiv(24,dpi,96)&&r.bottom-r.top<=MulDiv(24,dpi,96)) components.push_back(r);
    }
    std::sort(components.begin(),components.end(),[](RECT a,RECT b){return a.left<b.left;});
    auto hasInk=[&](int x,int y) { int radius=std::max(1,MulDiv(1,dpi,96)); for(int yy=std::max(0,y-radius);yy<=std::min(height-1,y+radius);++yy) for(int xx=std::max(0,x-radius);xx<=std::min(width-1,x+radius);++xx) if(ink[size_t(yy)*width+xx]) return true; return false; };
    auto closeShape=[&](RECT r) { int w=r.right-r.left-1,h=r.bottom-r.top-1; if(w<3||h<3||w*2<h||h*2<w) return false; for(int n:{1,2,3}) {int x=w*n/4,y=h*n/4; if(!hasInk(r.left+x,r.top+y)||!hasInk(r.right-1-x,r.top+y)) return false;} return true; };
    std::optional<RECT> best;
    for(auto line:components) {
        int lw=line.right-line.left,lh=line.bottom-line.top;
        if(lw<MulDiv(6,dpi,96)||lh>std::max(2,MulDiv(3,dpi,96))||lw<lh*3) continue;
        int cy=line.top+line.bottom,tolerance=std::max(2,MulDiv(2,dpi,96));
        std::vector<RECT> row;
        for(auto r:components) {
            if(r.left<line.left||abs((r.top+r.bottom)-cy)>2*tolerance) continue;
            if(!row.empty()&&r.left<row.back().right) {auto& g=row.back(); g.left=std::min(g.left,r.left);g.right=std::max(g.right,r.right);g.top=std::min(g.top,r.top);g.bottom=std::max(g.bottom,r.bottom);}
            else row.push_back(r);
        }
        if(row.size()<size_t(expectedButtons)) continue;
        auto a=row[0],b=row[1],c=row[2],last=row[expectedButtons-1];
        if(b.bottom-b.top<MulDiv(5,dpi,96)||c.bottom-c.top<MulDiv(5,dpi,96)||!closeShape(c)) continue;
        int ca=a.left+a.right,cb=b.left+b.right,cc=c.left+c.right;
        int spacing=(cc-ca+2)/4;
        if(expectedButtons==4) { std::array<int,3> ds{cb-ca,cc-cb,last.left+last.right-cc};std::sort(ds.begin(),ds.end());spacing=(ds[1]+1)/2; }
        if(spacing<MulDiv(16,dpi,96)||spacing>MulDiv(80,dpi,96)) continue;
        if(abs(cb-ca-2*spacing)>2*tolerance||abs(cc-cb-2*spacing)>2*tolerance) continue;
        int trailing=width-(last.left+last.right+1)/2;
        if(trailing<spacing/4||trailing>spacing+MulDiv(12,dpi,96)) continue;
        int baseline=std::max({a.top+a.bottom,b.top+b.bottom,c.top+c.bottom});
        int cellHeight=(baseline+1)&~1;
        if(cellHeight<MulDiv(20,dpi,96)||cellHeight>MulDiv(64,dpi,96)||cellHeight>height+2) continue;
        int left=(ca-spacing+1)/2;
        RECT result{left,0,left+spacing,cellHeight};
        if(!best||result.bottom<best->bottom) best=result;
    }
    return best;
}
bool isChromiumWindow(HWND window) { wchar_t cls[128]{}; GetClassNameW(window,cls,128); return wcsncmp(cls,L"Chrome_WidgetWin_",16)==0; }
std::optional<RECT> measureChromiumMinimize(HWND window,RECT frame,UINT dpi) {
    struct Cache { HWND window{};DWORD pid{};int width{},height{};UINT dpi{};ULONGLONG sampled{};std::optional<RECT> rect; };
    static Cache cache; DWORD pid{};GetWindowThreadProcessId(window,&pid);int fw=frame.right-frame.left,fh=frame.bottom-frame.top;
    bool same=cache.window==window&&cache.pid==pid&&cache.width==fw&&cache.height==fh&&cache.dpi==dpi;
    if(!same) cache={window,pid,fw,fh,dpi,0,{}};
    auto absolute=[&]()->std::optional<RECT>{if(!cache.rect)return {};auto r=*cache.rect;OffsetRect(&r,frame.left,frame.top);return r;};
    if(GetForegroundWindow()!=window||(same&&GetTickCount64()-cache.sampled<(cache.rect?1000:50))) return absolute();
    cache.sampled=GetTickCount64();int width=std::min({fw,MulDiv(360,dpi,96),1024}),height=std::min({fh,MulDiv(56,dpi,96),256});if(width<1||height<1)return {};
    HDC screen=GetDC(nullptr);if(!screen)return {};HDC memory=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels{};
    HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);HGDIOBJ old{};std::optional<RECT> measured;
    if(memory&&bitmap&&pixels) {old=SelectObject(memory,bitmap);if(BitBlt(memory,0,0,width,height,screen,frame.right-width,frame.top,SRCCOPY|CAPTUREBLT)){GdiFlush();try{measured=detectCaptionControls({static_cast<const std::uint32_t*>(pixels),size_t(width)*height},width,height,dpi);}catch(...){}}}
    if(old)SelectObject(memory,old);if(memory)DeleteDC(memory);if(bitmap)DeleteObject(bitmap);ReleaseDC(nullptr,screen);
    if(measured&&GetForegroundWindow()==window) {
        OffsetRect(&*measured,fw-width,0);POINT p{frame.left+(measured->left+measured->right)/2,frame.top+(measured->top+measured->bottom)/2};
        if(GetAncestor(WindowFromPoint(p),GA_ROOT)==window) cache.rect=measured;
    }
    return absolute();
}
}
