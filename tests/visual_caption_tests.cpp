#include "visual_caption.h"
#include <algorithm>
#include <iostream>

struct Image {
    int width,height,spacing; UINT dpi;
    std::vector<std::uint32_t> pixels;
    Image(int s,UINT d,bool dark=false,bool hover=false):width(s*7),height(MulDiv(40,d,96)),spacing(s),dpi(d),pixels(size_t(width)*height,dark?0x202020:0xf0f3f9) {
        int center=width-s/2-3*s,cy=height/2;
        int r=std::max(4,MulDiv(5,d,96)); auto ink=dark?0xeeeeee:0x202020;
        if(hover) for(int y=0;y<height;++y) for(int x=center-s/2;x<center+s/2;++x) point(x,y,dark?0x404040:0xd0d0d0);
        for(int x=center-r;x<center+r;++x) point(x,cy,ink);
        center+=s; for(int i=-r;i<r;++i) { point(center+i,cy-r,ink); point(center+i,cy+r-1,ink); point(center-r,cy+i,ink); point(center+r-1,cy+i,ink); }
        center+=s; for(int i=-r;i<=r;++i) { point(center+i,cy+i,ink); point(center+i,cy-i,ink); }
        center+=s; for(int i=-r;i<r;++i) { point(center+i,cy-r,ink); point(center+i,cy+r-1,ink); point(center-r,cy+i,ink); point(center+r-1,cy+i,ink); }
        for(int i=-r+2;i<r-2;++i) point(center-2,cy+i,ink);
        // Existing Wind Hook surface and text-like noise to the left of the group.
        for(int y=0;y<height;++y) for(int x=0;x<s;++x) point(x,y,0x1b5b77);
    }
    void point(int x,int y,std::uint32_t c) { if(x>=0&&x<width&&y>=0&&y<height) pixels[size_t(y)*width+x]=c; }
    std::optional<RECT> detect() { return home::detectAndrowsMinimize(pixels,width,height,dpi); }
};
int main() {
    int failures{}; auto check=[&](bool ok,const char* label) { std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n'; if(!ok) ++failures; };
    for(auto [spacing,dpi]: {std::pair{32,96u},std::pair{40,120u},std::pair{48,144u},std::pair{43,120u},std::pair{128,384u}}) {
        Image image(spacing,dpi); auto r=image.detect();
        check(r&&r->right-r->left==spacing&&r->top==0&&r->bottom==image.height,"measured icon spacing supplies cell width and caption height");
    }
    Image dark(40,120,true); check(dark.detect().has_value(),"dark caption detection");
    Image activeSidebar(40,120);auto activeOriginal=activeSidebar.pixels;
    for(int y=0;y<activeSidebar.height;++y)for(int x=activeSidebar.width-40;x<activeSidebar.width;++x)
        if(activeOriginal[size_t(y)*activeSidebar.width+x]==0x202020)activeSidebar.point(x,y,0x0088ff);
    check(activeSidebar.detect().has_value(),"blue active sidebar toggle preserves control detection");
    for(int extra : {40,60,80}) {
        Image sidebar(40,120); auto expected=sidebar.detect();
        int oldWidth=sidebar.width; auto original=sidebar.pixels;
        sidebar.width+=extra; sidebar.pixels.assign(size_t(sidebar.width)*sidebar.height,0xf0f3f9);
        for(int y=0;y<sidebar.height;++y)
            std::copy_n(original.begin()+size_t(y)*oldWidth,oldWidth,sidebar.pixels.begin()+size_t(y)*sidebar.width);
        auto actual=sidebar.detect();
        check(expected&&actual&&EqualRect(&*expected,&*actual),"expanded sidebar preserves measured caption button position");
    }
    Image hover(40,120,false,true); auto hovered=hover.detect(); check(hovered&&hovered->right-hovered->left==40,"hover background preserves measured width");
    Image blank(40,120); std::fill(blank.pixels.begin(),blank.pixels.end(),0xf0f3f9); check(!blank.detect(),"blank caption does not produce a measurement");
    Image covered(40,120); int c=covered.width-covered.spacing/2-3*covered.spacing;
    for(int y=0;y<covered.height;++y) for(int x=c-15;x<c+15;++x) covered.point(x,y,0x1b5b77);
    check(!covered.detect(),"covered minimize glyph is rejected");
    Image missing(40,120); for(int y=0;y<missing.height;++y) for(int x=missing.width-40;x<missing.width;++x) missing.point(x,y,0xf0f3f9);
    check(!missing.detect(),"incomplete control group is rejected");
    Image irregular(40,120); auto original=irregular.pixels; int maxCenter=irregular.width-irregular.spacing/2-2*irregular.spacing;
    for(int y=0;y<irregular.height;++y) for(int x=maxCenter-18;x<maxCenter+18;++x) irregular.point(x,y,0xf0f3f9);
    for(int y=0;y<irregular.height;++y) for(int x=maxCenter-18;x<maxCenter+18;++x) if(original[size_t(y)*irregular.width+x]!=0xf0f3f9) irregular.point(x+9,y,original[size_t(y)*irregular.width+x]);
    check(!irregular.detect(),"inconsistent control spacing is rejected");
    check(!home::detectAndrowsMinimize({},100,50,120),"invalid capture is rejected");
    std::cout<<"Failures: "<<failures<<'\n'; return failures?1:0;
}
