#pragma once
#include <windows.h>
namespace home {
struct Record;
// Temporarily switch the preview target's state, then restore the exact native
// placement. The saved placement stays in Windows' own coordinate convention.
class PreviewPlacement {
public:
    PreviewPlacement()=default;
    PreviewPlacement(const PreviewPlacement&)=delete;
    PreviewPlacement& operator=(const PreviewPlacement&)=delete;
    ~PreviewPlacement() { restore(); }
    bool begin(HWND window,bool maximized);
    // Preview a saved desktop position without modifying any persistent record.
    bool beginDesktop(HWND window,const Record& record);
    void restore();
private:
    HWND window_{};
    DWORD pid_{};
    WINDOWPLACEMENT saved_{sizeof(saved_)};
    bool changed_{};
};
}
