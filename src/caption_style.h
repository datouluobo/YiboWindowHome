#pragma once
#include <windows.h>
#include <cstdint>
#include <span>
#include <vector>

namespace home {
struct CaptionBackground { int width{},height{}; std::vector<std::uint32_t> pixels; };
struct CaptionSample {
    COLORREF background{},foreground{};
    int glyphWidth{},stroke{1};
    bool valid{};
    CaptionBackground image;
};
struct CaptionPalette {
    COLORREF normal{RGB(240,240,240)},foreground{RGB(32,32,32)},hot{RGB(220,220,220)},pressed{RGB(205,205,205)};
    COLORREF hotForeground{RGB(32,32,32)},pressedForeground{RGB(32,32,32)};
    int glyphWidth{10},stroke{1};
    bool sampled{},hotSampled{},pressedSampled{};
    CaptionBackground normalImage,hotImage,pressedImage;
};
struct CaptionStyleStatus { COLORREF normal{},foreground{},hot{};unsigned flags{};int glyphWidth{},stroke{}; };
CaptionStyleStatus inspectCaptionPalette(HWND target);
enum class CaptionInteraction { Normal,Hot,Pressed };
enum class CaptionGlyph { Return,Pinned,Success,Error };
CaptionSample analyzeCaptionButton(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi);
void updateCaptionPalette(CaptionPalette& palette,const CaptionSample& sample,CaptionInteraction interaction);
CaptionPalette sampleCaptionPalette(HWND target,RECT minimize,UINT dpi);
void drawCaptionGlyph(HDC dc,RECT rect,const CaptionPalette& palette,CaptionGlyph glyph);
void paintCaptionBackground(HDC dc,RECT rect,const CaptionPalette& palette,CaptionInteraction interaction);
}
