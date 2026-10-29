#include "caption_style.h"
#include <algorithm>
#include <iostream>

home::CaptionSample sample(std::uint32_t background,std::uint32_t ink,bool corners=false) {
    std::vector<std::uint32_t> pixels(40*50,background);
    for(int x=14;x<26;++x) pixels[25*40+x]=ink;
    if(corners) pixels[0]=0xf0f3f9;
    return home::analyzeCaptionButton(pixels,40,50,120);
}
int main() {
    int failures{}; auto check=[&](bool ok,const char* label) { std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n'; if(!ok) ++failures; };
    auto normal=sample(0xf0f3f9,0x25292d);
    check(normal.valid&&normal.background==RGB(240,243,249)&&normal.foreground==RGB(37,41,45),"native background and foreground colors are sampled");
    check(normal.glyphWidth==12&&normal.stroke==1,"native glyph extent and stroke are sampled");
    check(normal.image.pixels[25*40+20]==0xf0f3f9,"native minimize glyph is removed from background image");
    auto hot=sample(0xdce0e7,0x25292d,true);
    check(hot.image.pixels[0]==0xf0f3f9&&hot.image.pixels[25*40+20]==0xdce0e7,"native hover corners and fill are preserved");
    home::CaptionPalette palette; home::updateCaptionPalette(palette,normal,home::CaptionInteraction::Normal);
    check(palette.sampled&&palette.normalImage.width==40,"normal state retains measured background");
    home::updateCaptionPalette(palette,hot,home::CaptionInteraction::Hot);
    check(palette.hotSampled&&palette.hot==RGB(220,224,231)&&palette.normal==normal.background,"native hover state is learned independently");
    auto dark=sample(0x202020,0xeeeeee); home::updateCaptionPalette(palette,dark,home::CaptionInteraction::Normal);
    check(palette.foreground==RGB(238,238,238)&&!palette.hotSampled&&palette.hotImage.pixels.empty(),"theme change updates glyph color and invalidates learned hover");
    home::CaptionPalette initial; home::updateCaptionPalette(initial,hot,home::CaptionInteraction::Hot); home::updateCaptionPalette(initial,normal,home::CaptionInteraction::Normal);
    check(initial.hotSampled&&initial.hot==hot.background,"first normal sample preserves already observed hover state");
    std::vector<std::uint32_t> blank(40*50,0xf0f3f9);
    check(!home::analyzeCaptionButton(blank,40,50,120).valid,"blank or occluded button does not change palette");
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen); BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=40; info.bmiHeader.biHeight=-50; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bytes{}; HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bytes,nullptr,0); auto old=SelectObject(dc,bitmap); RECT r{0,0,40,50};
    home::paintCaptionBackground(dc,r,initial,home::CaptionInteraction::Hot); GdiFlush();
    check(static_cast<std::uint32_t*>(bytes)[25*40+20]==0xdce0e7,"hover painting uses actual learned background");
    home::drawCaptionGlyph(dc,r,initial,home::CaptionGlyph::Return); GdiFlush();
    DWORD before=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    for(int i=0;i<200;++i) { home::paintCaptionBackground(dc,r,initial,home::CaptionInteraction::Normal); home::drawCaptionGlyph(dc,r,initial,home::CaptionGlyph::Return); }
    GdiFlush();
    DWORD after=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
    std::cout<<"Paint GDI objects "<<before<<" -> "<<after<<'\n';
    check(after<=before,"repeated glyph painting releases GDI resources");
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr,screen);
    std::cout<<"Failures: "<<failures<<'\n'; return failures?1:0;
}
