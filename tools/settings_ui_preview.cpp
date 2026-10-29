#include "settings.h"
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>

// Isolated, non-elevated host for visual QA of the production settings window.
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    int argc{};auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);auto directory=std::filesystem::path(argc>1?argv[1]:L"build/ui-preview");int page=argc>2?_wtoi(argv[2]):0;
    int width=argc>3?_wtoi(argv[3]):1180,height=argc>4?_wtoi(argv[4]):850;LocalFree(argv);
    home::Store store{directory/L"preview-state.json"};home::State state;
    if(std::filesystem::exists(store.path))state=store.load();
    else {
        wchar_t local[32768]{},windows[32768]{};GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);GetWindowsDirectoryW(windows,32768);
        auto chrome=std::filesystem::path(local)/L"Google/Chrome/Application/chrome.exe";
        auto& app=home::ensureApplication(state,{chrome.wstring(),L""},L"Google Chrome");auto scope=home::scopeKey(app);
        home::WindowRule rule;rule.id=home::newId(L"r-");rule.name=L"浏览器主窗口";rule.className=L"Chrome_WidgetWin_1";app.rules.push_back(rule);
        auto displays=home::collectDisplays();if(displays.empty())return 2;auto d=std::find_if(displays.begin(),displays.end(),[](const home::Display& item){return item.primary;});if(d==displays.end())d=displays.begin();
        home::Record record;record.monitor=d->monitor;record.work=d->work;record.dpi=d->dpi;record.device=d->device;record.displayIdentity=d->identity;record.topology=displays;
        LONG w=std::min(1280L,d->work.right-d->work.left-100),h=std::min(800L,d->work.bottom-d->work.top-100);record.normal={d->work.left+60,d->work.top+60,d->work.left+60+w,d->work.top+60+h};record.actual=record.normal;
        record.savedAt=L"2026-10-29 08:30:00 UTC";home::savePositionRecord(state,scope,record,false);
        OffsetRect(&record.normal,40,30);record.actual=record.normal;auto reading=home::savePositionRecord(state,scope,record,true,L"阅读布局");home::activatePositionRecord(state,scope,reading);
        if(displays.size()>1){auto& second=displays[d==displays.begin()?1:0];record.monitor=second.monitor;record.work=second.work;record.device=second.device;record.displayIdentity=second.identity;record.dpi=second.dpi;record.normal={second.work.left+40,second.work.top+40,second.work.left+40+std::min(1100L,second.work.right-second.work.left-80),second.work.top+40+std::min(720L,second.work.bottom-second.work.top-80)};record.actual=record.normal;}
        home::savePositionRecord(state,scope,record,true,L"副屏参考");
        home::ensureApplication(state,{(std::filesystem::path(local)/L"Programs/Microsoft VS Code/Code.exe").wstring(),L""},L"Visual Studio Code");
        home::ensureApplication(state,{(std::filesystem::path(windows)/L"explorer.exe").wstring(),L""},L"文件资源管理器");
        if(!store.save(state))return 3;
    }
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"YiboHome.Settings";cls.lpfnWndProc=home::settingsProc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
    HWND settings{};auto window=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);if(!window)return 4;
    UINT dpi=GetDpiForWindow(window);RECT size{0,0,MulDiv(width,dpi,96),MulDiv(height,dpi,96)};AdjustWindowRectExForDpi(&size,DWORD(GetWindowLongPtrW(window,GWL_STYLE)),FALSE,0,dpi);
    SetWindowPos(window,nullptr,40,40,size.right-size.left,size.bottom-size.top,SWP_NOZORDER|SWP_NOACTIVATE);
    auto tabs=GetDlgItem(window,204);TabCtrl_SetCurSel(tabs,std::clamp(page,0,2));NMHDR change{tabs,204,TCN_SELCHANGE};SendMessageW(window,WM_NOTIFY,204,reinterpret_cast<LPARAM>(&change));
    SetWindowTextW(window,L"风钩 · UI 验证");ShowWindow(window,SW_SHOW);if(!IsWindowVisible(window))ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
    MSG message{};while(IsWindow(window)&&GetMessageW(&message,nullptr,0,0)>0)if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}return 0;
}
