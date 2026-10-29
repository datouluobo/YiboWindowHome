#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace home {
// BGRA pixels of a caption-only crop; coordinates returned are crop-relative.
std::optional<RECT> detectAndrowsMinimize(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi,std::vector<RECT>* glyphs=nullptr);
bool isAndrowsWindow(HWND window);
std::optional<RECT> measureAndrowsMinimize(HWND window,RECT title,UINT dpi);
std::optional<RECT> detectCaptionControls(std::span<const std::uint32_t> pixels,int width,int height,UINT dpi,int expectedButtons=3);
bool isChromiumWindow(HWND window);
std::optional<RECT> measureChromiumMinimize(HWND window,RECT frame,UINT dpi);
}
