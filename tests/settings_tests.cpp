#include "settings.h"
#include "preview.h"
#include <commctrl.h>
#include <iostream>
#include <shellapi.h>
#include <shlobj.h>
#include <cstring>

int wmain(int argc,wchar_t** argv) {
    std::cout<<std::unitbuf;
    if(argc!=2)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_LISTVIEW_CLASSES|ICC_TAB_CLASSES};InitCommonControlsEx(&controls);
    HINSTANCE instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"YiboHome.Settings";cls.lpfnWndProc=home::settingsProc;cls.hbrBackground=GetSysColorBrush(COLOR_WINDOW);RegisterClassW(&cls);
    int failures{};auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failures;};
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    home::State state;state.offsetX=-9;state.offsetY=2;auto& app=home::ensureApplication(state,{executable,L""},L"Settings test");auto appId=app.id;app.maximized={false,100,0};
    home::WindowRule first;first.id=home::newId(L"r-");first.name=L"First rule";first.className=L"EditorMain";app.rules.push_back(first);
    auto second=first;second.id=home::newId(L"r-");second.name=L"Second rule";second.className=L"OtherClass";app.rules.push_back(second);
    home::Store store{std::filesystem::path(argv[1])/L"settings-state.json"};check(store.save(state),"isolated settings state initializes");
    HWND settings{};auto window=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);
    wchar_t windowTitle[256]{};GetWindowTextW(window,windowTitle,256);
    check(std::wstring(windowTitle)==L"风钩 · YiboWindowHome 0.9.1","settings title shows Chinese and English names and version");
    auto tab=[&](int selection){TabCtrl_SetCurSel(GetDlgItem(window,204),selection);NMHDR notification{GetDlgItem(window,204),204,TCN_SELCHANGE};SendMessageW(window,WM_NOTIFY,204,reinterpret_cast<LPARAM>(&notification));};
    auto shown=[&](int id){return (GetWindowLongPtrW(GetDlgItem(window,id),GWL_STYLE)&WS_VISIBLE)!=0;};
    check(TabCtrl_GetItemCount(GetDlgItem(window,204))==3&&shown(275)&&!shown(274),"three peer tabs keep records separate from button calibration");
    tab(1);check(shown(274)&&shown(214)&&!shown(275)&&!shown(230),"button tab exposes calibration without record or rule controls");tab(0);
    check(window&&settings==window&&ListView_GetItemCount(GetDlgItem(window,200))==3,"native settings navigation includes global default, settings and application");
    ListView_SetItemState(GetDlgItem(window,200),2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    wchar_t value[256]{};GetDlgItemTextW(window,210,value,256);check(std::wstring(value)==L"Settings test","selecting application loads editable details");
    wchar_t emptyRecordName[256]{};ListView_GetItemText(GetDlgItem(window,275),0,1,emptyRecordName,256);check(!ListView_GetCheckState(GetDlgItem(window,275),0)&&std::wstring(emptyRecordName).find(L"启用中")==std::wstring::npos,"empty default slot is not presented as an active saved record");
    GetDlgItemTextW(window,218,value,256);check(std::wstring(value)==L"-9"&&!IsWindowEnabled(GetDlgItem(window,218)),"inherited maximum offset shows actual global value instead of stale custom 100");
    GetDlgItemTextW(window,3217,value,256);check(std::wstring(value).find(L"继承全局")!=std::wstring::npos&&SendDlgItemMessageW(window,3217,BM_GETCHECK,0,0)==BST_CHECKED,"unchecked maximum offset explicitly identifies inheritance mode");
    SendDlgItemMessageW(window,214,BM_CLICK,0,0);check(SendDlgItemMessageW(window,214,BM_GETCHECK,0,0)==BST_CHECKED&&IsWindowEnabled(GetDlgItem(window,215)),"real checkbox click enables custom ordinary offset editing");
    SetDlgItemTextW(window,215,L"-18");SetDlgItemTextW(window,216,L"3");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(state.applications[0].normal.custom&&state.applications[0].normal.x==-18&&state.applications[0].normal.y==3&&!state.applications[0].maximized.custom,"native application offset controls save independent inheritance");
    SendDlgItemMessageW(window,217,BM_CLICK,0,0);SetDlgItemTextW(window,218,L"-54");SetDlgItemTextW(window,219,L"7");SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    auto modes=store.load();check(modes.applications[0].normal.x==-18&&modes.applications[0].maximized.custom&&modes.applications[0].maximized.x==-54&&modes.applications[0].maximized.y==7,"ordinary and maximized offsets persist distinct enabled values");
    SetDlgItemTextW(window,215,L"-");SendMessageW(window,WM_COMMAND,MAKEWPARAM(215,EN_UPDATE),0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(215,EN_SETFOCUS),0);
    check(IsWindowEnabled(window)&&state.applications[0].normal.x==-18,"incomplete negative input and focus notifications do not submit or interrupt editing");
    SetDlgItemTextW(window,215,L"100");wchar_t warning[512]{};GetDlgItemTextW(window,263,warning,512);
    check(std::wstring(warning).find(L"X -500")!=std::wstring::npos&&state.applications[0].normal.x==-18,"positive horizontal input reports safe range and does not overwrite saved offset");SetDlgItemTextW(window,215,L"-18");
    tab(2);ListView_SetItemState(GetDlgItem(window,230),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    GetDlgItemTextW(window,236,value,256);check(std::wstring(value)==L"EditorMain","native rule selection loads exact class condition");
    GetDlgItemTextW(window,242,value,256);bool normalInherited=std::wstring(value)==L"-18";GetDlgItemTextW(window,245,value,256);
    check(normalInherited&&std::wstring(value)==L"-54","rule inheritance displays separate effective application offsets for both states");
    SendDlgItemMessageW(window,239,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(239,CBN_SELCHANGE),0);
    check(!IsWindowEnabled(GetDlgItem(window,252))&&!IsWindowEnabled(GetDlgItem(window,255)),"exclusion rules disable both preview states");
    SendDlgItemMessageW(window,239,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(239,CBN_SELCHANGE),0);
    SendDlgItemMessageW(window,237,CB_SETCURSEL,1,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(237,CBN_SELCHANGE),0);SetDlgItemTextW(window,238,L"Report");
    SetDlgItemTextW(window,235,L"Reports");SendDlgItemMessageW(window,241,BM_SETCHECK,BST_CHECKED,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(241,BN_CLICKED),0);SetDlgItemTextW(window,242,L"-36");SetDlgItemTextW(window,243,L"1");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    auto rule=state.applications[0].rules[0];check(rule.id==first.id&&rule.titleMatch==home::TitleMatch::Contains&&rule.title==L"Report"&&rule.normal.custom&&rule.normal.x==-36,"native rule editor saves title constraint and offset without replacing scope ID");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(234,BN_CLICKED),0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(state.applications[0].rules[1].id==first.id&&state.applications[0].rules[0].id==second.id,"native priority controls persist order while retaining rule IDs");
    // Simulate a right-click recording while the same settings draft is open.
    home::Record record;record.normal={100,100,900,700};record.actual=record.normal;record.monitor={0,0,1920,1080};record.work={0,0,1920,1040};record.device=L"DISPLAY1";
    auto key=home::scopeKey(state.applications[0]);state.records[key]=record;
    auto anotherId=home::ensureApplication(state,{L"C:\\SettingsTest\\newly-recorded.exe",L""},L"Newly recorded").id;
    auto anotherKey=home::scopeKey(state.applications.back());state.records[anotherKey]=record;
    tab(0);SetDlgItemTextW(window,210,L"Renamed application");SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(state.applications.size()==2&&state.applications[0].name==L"Renamed application"&&state.applications[0].id==appId&&state.applications[1].id==anotherId&&state.records.contains(key)&&state.records.contains(anotherKey),"saving draft retains positions and auto-added application recorded during editing");
    auto loaded=store.load();check(loaded.applications.size()==2&&loaded.records.size()==2&&loaded.applications[0].rules[1].id==first.id,"native settings transaction survives process restart");
    auto additional=home::savePositionRecord(state,home::scopeKey(state.applications[0]),record,true,L"Concurrent position");
    home::activatePositionRecord(state,home::scopeKey(state.applications[0]),additional);
    SetDlgItemTextW(window,210,L"Renamed application");SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(state.records.contains(additional)&&home::activePositionKey(state,home::scopeKey(state.applications[0]))==additional&&store.load().records.contains(additional),"saving settings retains a concurrently added position and its active selection in an existing scope");
    check(ListView_GetItemCount(GetDlgItem(window,275))==2,"record tab exposes default and concurrently added named position");
    RECT dpiBounds{};GetWindowRect(window,&dpiBounds);auto oldNav=GetDlgItem(window,200),oldPositions=GetDlgItem(window,275);int oldSelection=ListView_GetNextItem(oldPositions,-1,LVNI_SELECTED);wchar_t beforeName[256]{};GetDlgItemTextW(window,280,beforeName,256);auto dpi=GetDpiForWindow(window);
    SendMessageW(window,WM_DPICHANGED,MAKELONG(dpi,dpi),reinterpret_cast<LPARAM>(&dpiBounds));wchar_t afterName[256]{};GetDlgItemTextW(window,280,afterName,256);
    check(GetDlgItem(window,200)!=oldNav&&GetDlgItem(window,275)!=oldPositions&&ListView_GetItemCount(GetDlgItem(window,200))==4&&ListView_GetItemCount(GetDlgItem(window,275))==2&&ListView_GetNextItem(GetDlgItem(window,275),-1,LVNI_SELECTED)==oldSelection&&std::wstring(beforeName)==afterName,"DPI relayout recreates measured lists while preserving rows, selection and edit text");
    auto bounds=[&](int id){RECT r{};GetWindowRect(GetDlgItem(window,id),&r);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);return r;};
    RECT client{};GetClientRect(window,&client);auto recordList=bounds(275),sideAdd=bounds(206),activate=bounds(277),add=bounds(276);
    check(recordList.bottom<=client.bottom&&activate.bottom<=recordList.top&&activate.right<=add.left&&sideAdd.bottom<=client.bottom,"record actions stay above the list and sidebar actions fit the window");
    check(ListView_GetCountPerPage(GetDlgItem(window,275))>=5,"record list has room for at least five rows at the default settings size");
    ListView_SetItemState(GetDlgItem(window,275),0,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    check(home::activePositionKey(state,home::scopeKey(state.applications[0]))==additional,"selecting a record for inspection does not switch the active restore target");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(277,BN_CLICKED),0);
    check(home::activePositionKey(state,home::scopeKey(state.applications[0]))==additional,"activation stays in the draft until save succeeds");
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(home::activePositionKey(state,home::scopeKey(state.applications[0]))==home::scopeKey(state.applications[0]),"save persists the single record selected for left-click restoration");
    ListView_SetItemState(GetDlgItem(window,275),1,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);NMITEMACTIVATE dbl{};dbl.hdr.hwndFrom=GetDlgItem(window,275);dbl.hdr.idFrom=275;dbl.hdr.code=NM_DBLCLK;dbl.iItem=1;dbl.iSubItem=1;SendMessageW(window,WM_NOTIFY,275,reinterpret_cast<LPARAM>(&dbl));
    check(shown(280),"double-clicking a named record opens the inline name editor");SetDlgItemTextW(window,280,L"阅读布局");SendMessageW(window,WM_COMMAND,MAKEWPARAM(280,EN_KILLFOCUS),reinterpret_cast<LPARAM>(GetDlgItem(window,280)));check(!shown(280),"inline name editor closes after confirming the edit");SendMessageW(window,WM_COMMAND,MAKEWPARAM(203,BN_CLICKED),0);
    check(store.load().records.at(additional).name==L"阅读布局"&&state.records.size()==3,"record rename transaction retains geometry and other record scopes");
    SetDlgItemTextW(window,270,L"Newly");check(ListView_GetItemCount(GetDlgItem(window,200))==3,"application search filters by name while retaining global defaults and settings");
    ListView_SetItemState(GetDlgItem(window,200),2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);GetDlgItemTextW(window,210,value,256);
    check(std::wstring(value)==L"Newly recorded","filtered navigation resolves application identity instead of using visible row index");
    SetDlgItemTextW(window,270,L"");ListView_SetItemState(GetDlgItem(window,200),2,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
    std::wstring files=executable;files+=L'\0';files+=L'\0';size_t size=sizeof(DROPFILES)+files.size()*sizeof(wchar_t);
    HGLOBAL drop=GlobalAlloc(GHND,size);auto data=static_cast<DROPFILES*>(GlobalLock(drop));data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;memcpy(reinterpret_cast<char*>(data)+sizeof(DROPFILES),files.data(),files.size()*sizeof(wchar_t));GlobalUnlock(drop);
    SendMessageW(window,WM_DROPFILES,reinterpret_cast<WPARAM>(drop),0);
    check(ListView_GetItemCount(GetDlgItem(window,200))==4&&state.applications.size()==2,"native file-drop entry deduplicates an existing executable");
    SendDlgItemMessageW(window,205,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(8,8));SendMessageW(window,WM_COMMAND,IDCANCEL,0);
    check(IsWindow(window)&&GetCapture()!=GetDlgItem(window,205)&&state.applications.size()==2,"dialog Escape cancels picker capture without closing settings or adding an application");
    // Duplicate addition is navigation only, so closing must not ask to save.
    SendMessageW(window,WM_CLOSE,0,0);check(!settings&&!IsWindow(window)&&!home::settingsPreviewState(),"closing clean settings destroys window and clears preview context");
    // Warm shell/control caches before checking per-window resources.
    for(int i=0;i<3;++i){auto cycle=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);DestroyWindow(cycle);}
    DWORD beforeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),beforeUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    for(int i=0;i<30;++i){auto cycle=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);DestroyWindow(cycle);}
    DWORD afterGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS),afterUser=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);
    check(afterGdi<=beforeGdi&&afterUser<=beforeUser+2,"repeated settings lifecycles release icon lists, selected icons, fonts and brushes");
    store.writable=false;auto readOnly=home::createSettings(instance,&settings,state,store,nullptr,nullptr,false);check(readOnly&&!IsWindowEnabled(GetDlgItem(readOnly,203)),"protected store disables save in settings");DestroyWindow(readOnly);
    WNDCLASSW fixture{};fixture.hInstance=instance;fixture.lpszClassName=L"PreviewPlacementFixture";fixture.lpfnWndProc=DefWindowProcW;RegisterClassW(&fixture);
    auto target=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"PreviewPlacementFixture",L"Preview state test",WS_OVERLAPPEDWINDOW,120,140,720,480,nullptr,nullptr,instance,nullptr);
    RECT original{},restored{};GetWindowRect(target,&original);home::PreviewPlacement placement;
    auto desktopRecord=home::capture(target);OffsetRect(&desktopRecord.normal,180,70);desktopRecord.actual=desktopRecord.normal;
    bool desktopStarted=placement.beginDesktop(target,desktopRecord);GetWindowRect(target,&restored);
    check(desktopStarted&&EqualRect(&restored,&desktopRecord.normal),"desktop preview temporarily moves target to saved record geometry");
    placement.restore();GetWindowRect(target,&restored);
    check(EqualRect(&restored,&original)&&!IsZoomed(target),"desktop preview restores exact prior placement without saving the temporary position");
    check(placement.begin(target,true)&&IsZoomed(target),"maximum preview really switches target into maximized state");placement.restore();GetWindowRect(target,&restored);
    check(!IsZoomed(target)&&EqualRect(&original,&restored),"maximum preview restores original ordinary state and rectangle");
    ShowWindow(target,SW_MAXIMIZE);WINDOWPLACEMENT maximized{sizeof(maximized)},after{sizeof(after)};GetWindowPlacement(target,&maximized);
    check(placement.begin(target,false)&&!IsZoomed(target),"ordinary preview really restores a maximized target temporarily");placement.restore();GetWindowPlacement(target,&after);
    check(IsZoomed(target)&&EqualRect(&maximized.rcNormalPosition,&after.rcNormalPosition),"ordinary preview returns to original maximum state and restore bounds");
    check(placement.begin(target,true)&&IsZoomed(target),"already maximized preview retains existing state");placement.restore();check(IsZoomed(target),"same-state preview does not restore a different state");
    placement.begin(target,false);DestroyWindow(target);placement.restore();check(!IsWindow(target),"preview cleanup tolerates target destruction");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
