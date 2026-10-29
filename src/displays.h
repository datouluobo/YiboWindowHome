#pragma once
#include <windows.h>
#include <span>
#include <string>
#include <vector>
#include <optional>

namespace home {
struct Display {
    std::wstring device,identity;
    RECT monitor{},work{};
    UINT dpi{96};
    bool primary{};
};
std::vector<Display> collectDisplays();
std::optional<size_t> displayForRect(RECT rect,std::span<const Display> displays);
bool sameTopology(std::span<const Display> saved,std::span<const Display> current);
bool hasReachableCaption(RECT rect,std::span<const Display> displays);
RECT makeCaptionReachable(RECT rect,const Display& display);
}
