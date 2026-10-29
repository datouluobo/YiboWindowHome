#include "state.h"
#include "app_files.h"
#include <shobjidl.h>
#include <iostream>
#include <fstream>

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);
    int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    home::State state;state.offsetX=-4;state.offsetY=2;
    home::WindowMatch window{{L"C:\\Apps\\editor.exe",L""},L"EditorMain",L"Report - Editor"};
    auto policy=home::resolveWindow(state,window);
    check(policy.allowed&&policy.x==-4&&policy.y==2,"unconfigured ordinary applications inherit global defaults");
    auto id=home::ensureApplication(state,window.identity).id;
    check(home::ensureApplication(state,{L"c:\\apps\\.\\EDITOR.exe",L""}).id==id&&state.applications.size()==1,"normalized case-insensitive full path deduplicates applications");
    auto& app=state.applications[0];app.normal={true,-20,3};app.maximized={true,-40,0};
    policy=home::resolveWindow(state,window);
    check(policy.allowed&&policy.appId==id&&policy.x==-20&&policy.recordKey==home::scopeKey(app),"application default has stable ID and ordinary offset");
    window.maximized=true;check(home::resolveWindow(state,window).x==-40,"maximized offset is independent of ordinary offset");window.maximized=false;
    home::WindowRule first;first.id=home::newId(L"r-");first.name=L"Reports";first.className=L"EditorMain";first.titleMatch=home::TitleMatch::Contains;first.title=L"Report";first.normal={true,-60,4};app.rules.push_back(first);
    policy=home::resolveWindow(state,window);
    check(policy.allowed&&policy.explicitRule&&policy.scopeId==first.id&&policy.x==-60,"matching class and title receive independent rule scope and override");
    window.maximized=true;check(home::resolveWindow(state,window).x==-40,"rule can inherit maximized offset from application");window.maximized=false;
    auto other=window;other.title=L"Notes - Editor";
    check(home::resolveWindow(state,other).scopeId.empty(),"unmatched title uses application default record");
    other=window;other.className=L"OtherClass";
    check(home::resolveWindow(state,other).scopeId.empty(),"class and title conditions are ANDed");
    auto second=first;second.id=home::newId(L"r-");second.name=L"Second";app.rules.push_back(second);
    check(home::resolveWindow(state,window).scopeId==first.id,"first matching enabled rule wins stable list order");
    std::swap(app.rules[0],app.rules[1]);
    check(home::resolveWindow(state,window).scopeId==second.id,"reordering rules changes winner without changing rule IDs");
    auto excluded=first;excluded.id=home::newId(L"r-");excluded.exclude=true;excluded.name=L"Excluded";app.rules.push_back(excluded);
    check(!home::resolveWindow(state,window).allowed,"any exclusion wins over higher priority inclusion");
    app.rules.back().active=false;
    check(home::resolveWindow(state,window).allowed,"inactive exclusion has no effect");
    app.enabled=false;
    check(!home::resolveWindow(state,window).allowed,"application disable wins over every window rule");app.enabled=true;
    window.defaultEligible=false;
    check(home::resolveWindow(state,window).allowed,"explicit inclusion can allow a suitable top-level tool window");
    other=window;other.title=L"Notes";
    check(!home::resolveWindow(state,other).allowed,"unmatched tool window remains filtered");
    window.systemSafe=false;
    check(!home::resolveWindow(state,window).allowed,"system safety filter cannot be overridden by rules");window.systemSafe=true;window.defaultEligible=true;
    app.rules[0].titleMatch=home::TitleMatch::Equals;app.rules[0].title=window.title;
    check(home::resolveWindow(state,window).scopeId==second.id,"title equals matches full title");
    other=window;other.title+=L" changed";
    check(home::resolveWindow(state,other).scopeId==first.id,"title equals does not match a longer title");
    auto recordKey=home::scopeKey(app);home::Record record;record.normal={100,100,900,700};record.actual=record.normal;record.monitor={0,0,1920,1080};record.work={0,0,1920,1040};record.device=L"DISPLAY1";record.savedAt=L"2026-10-27 00:00:00 UTC";
    state.records[recordKey]=record;state.records[home::scopeKey(app,first.id)]=record;state.records[home::scopeKey(app,first.id)].normal.left=200;
    check(state.records.at(recordKey).normal.left==100&&state.records.at(home::scopeKey(app,first.id)).normal.left==200,"application default and rule records are isolated");
    app.path=L"d:\\updated\\editor.exe";
    check(home::scopeKey(app)==recordKey&&state.records.contains(recordKey),"updating executable path preserves internal ID and records");
    home::Store store{directory/L"rules-state.json"};check(store.save(state),"applications, ordered rules and records save as one transaction");auto loaded=store.load();
    check(store.writable&&loaded.applications.size()==1&&loaded.applications[0].id==id&&loaded.applications[0].rules[0].id==second.id&&loaded.records.size()==2&&loaded.records.at(recordKey).savedAt==record.savedAt,"restart preserves IDs, rule order, timestamp and record scopes");
    auto invalid=state;invalid.applications[0].rules[0].className.clear();
    check(!store.save(invalid)&&store.load().applications[0].rules[0].className==L"EditorMain","invalid rule cannot replace last valid state");
    home::State legacy;legacy.records[L"C:\\Apps\\old.exe"]=record;home::migrateLegacyApplications(legacy);auto legacyKey=legacy.records.begin()->first;home::migrateLegacyApplications(legacy);
    check(legacy.applications.size()==1&&legacy.records.size()==1&&legacy.records.begin()->first==legacyKey,"legacy path migration is lossless and idempotent");
    home::State packaged;auto packageId=home::ensureApplication(packaged,{L"C:\\Package\\v1\\app.exe",L"Family!Main"}).id;
    check(home::ensureApplication(packaged,{L"C:\\Package\\v2\\app.exe",L"Family!Main"}).id==packageId&&packaged.applications.size()==1,"AUMID preserves packaged application identity across versioned paths");
    home::ensureApplication(packaged,{L"C:\\Package\\v2\\app.exe",L"Family!Other"});home::validateApplications(packaged);
    check(packaged.applications.size()==2,"different packaged AUMIDs may share an executable without merging");
    home::ensureApplication(packaged,{L"C:\\Package\\v2\\app.exe",L"Family!other"});
    check(packaged.applications.size()==3,"system-returned AUMIDs use exact matching rather than path case folding");
    auto ambiguous=home::ApplicationIdentity{L"C:\\Package\\v2\\app.exe",L""};
    check(!home::findApplication(packaged,ambiguous),"missing AUMID cannot select an arbitrary app when multiple packaged identities share one path");
    auto ambiguousWindow=home::WindowMatch{ambiguous,L"SharedMain",L"Shared"};
    check(!home::resolveWindow(packaged,ambiguousWindow).allowed,"ambiguous packaged identity cannot expose another application's record");
    bool refusedAmbiguous=false;try {home::ensureApplication(packaged,ambiguous);}catch(...){refusedAmbiguous=true;}
    check(refusedAmbiguous&&packaged.applications.size()==3,"ambiguous identity cannot create or merge application entries");
    ambiguousWindow.identity.aumid=L"Family!Other";auto recoveredPolicy=home::resolveWindow(packaged,ambiguousWindow);
    check(recoveredPolicy.allowed&&recoveredPolicy.appId==packaged.applications[1].id&&recoveredPolicy.recordKey==home::scopeKey(packaged.applications[1]),"restored AUMID selects only its matching packaged application");
    home::State changes;auto idA=home::ensureApplication(changes,{L"C:\\one\\tool.exe",L""}).id;auto idB=home::ensureApplication(changes,{L"C:\\two\\tool.exe",L""}).id;
    changes.records[home::scopeKey(changes.applications[0])]=record;changes.records[home::scopeKey(changes.applications[1])]=record;changes.records[home::scopeKey(changes.applications[1])].normal.left=300;
    check(idA!=idB&&changes.applications.size()==2&&home::findApplication(changes,{L"C:\\one\\tool.exe",L""})->id==idA&&home::findApplication(changes,{L"C:\\two\\tool.exe",L""})->id==idB,"same filename at different paths never merges distinct applications");
    changes.applications[0].path=L"C:\\new-location\\tool.exe";
    auto relocated=home::findApplication(changes,{L"C:\\new-location\\tool.exe",L""});
    check(relocated&&relocated->id==idA&&changes.records.at(home::scopeKey(*relocated)).normal.left==record.normal.left&&!home::findApplication(changes,{L"C:\\one\\tool.exe",L""}),"explicit path change retains only the selected application identity and its record");
    home::Store changesStore{directory/L"identity-changes-state.json"};bool changesSaved=changesStore.save(changes);auto changesReloaded=changesStore.load();
    auto secondAfterRestart=home::findApplication(changesReloaded,{L"C:\\two\\tool.exe",L""});
    check(changesSaved&&secondAfterRestart&&secondAfterRestart->id==idB&&changesReloaded.records.at(home::scopeKey(*secondAfterRestart)).normal.left==300,"path changes and separate records survive a state reload");
    home::State upgrade;auto stable=home::ensureApplication(upgrade,{L"C:\\Packages\\v1\\app.exe",L"Vendor.Family!App"}).id;upgrade.records[home::scopeKey(upgrade.applications[0])]=record;
    home::Store upgradeStore{directory/L"identity-upgrade-state.json"};bool beforeUpgrade=upgradeStore.save(upgrade);auto reloadedUpgrade=upgradeStore.load();
    auto& updatedApp=home::ensureApplication(reloadedUpgrade,{L"C:\\Packages\\v2\\app.exe",L"Vendor.Family!App"});
    bool afterUpgrade=upgradeStore.save(reloadedUpgrade);auto finalUpgrade=upgradeStore.load();auto matchUpgrade=home::findApplication(finalUpgrade,{L"C:\\Packages\\v2\\app.exe",L"Vendor.Family!App"});
    check(beforeUpgrade&&afterUpgrade&&updatedApp.id==stable&&matchUpgrade&&matchUpgrade->id==stable&&finalUpgrade.applications.size()==1&&finalUpgrade.records.contains(home::scopeKey(*matchUpgrade)),"packaged version path change preserves persisted stable AUMID and record scope across reloads");
    WNDCLASSW hostClass{};hostClass.hInstance=GetModuleHandleW(nullptr);hostClass.lpszClassName=L"ApplicationFrameWindow";hostClass.lpfnWndProc=DefWindowProcW;
    auto hostClassRegistered=RegisterClassW(&hostClass);HWND emptyHost=hostClassRegistered?CreateWindowExW(0,hostClass.lpszClassName,L"",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,hostClass.hInstance,nullptr):nullptr;
    auto missingIdentity=emptyHost?home::identifyApplication(emptyHost):home::ApplicationIdentity{};
    check(emptyHost&&missingIdentity.path.empty()&&missingIdentity.aumid.empty(),"hosted window without packaged content remains unidentified instead of using shared host path");
    if(emptyHost)DestroyWindow(emptyHost);if(hostClassRegistered)UnregisterClassW(hostClass.lpszClassName,hostClass.hInstance);
    WNDCLASSW chromiumClass{};chromiumClass.hInstance=GetModuleHandleW(nullptr);chromiumClass.lpszClassName=L"Chrome_WidgetWin_1";chromiumClass.lpfnWndProc=DefWindowProcW;
    auto chromiumRegistered=RegisterClassW(&chromiumClass);HWND chromium=chromiumRegistered?CreateWindowExW(0,chromiumClass.lpszClassName,L"",WS_MAXIMIZE|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX,0,0,600,400,nullptr,nullptr,chromiumClass.hInstance,nullptr):nullptr;
    if(chromium)SetWindowLongPtrW(chromium,GWL_STYLE,WS_VISIBLE|WS_MAXIMIZE|WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|0x04000000L);
    auto chromiumDescription=chromium?home::describeWindow(chromium):home::WindowMatch{};
    check(chromium&&IsZoomed(chromium)&&chromiumDescription.defaultEligible,"maximized Chromium window with system controls stays eligible without a caption style");
    if(chromium){SetWindowLongPtrW(chromium,GWL_EXSTYLE,WS_EX_TOOLWINDOW);check(!home::describeWindow(chromium).defaultEligible,"captionless Chromium tool window remains filtered");SetWindowLongPtrW(chromium,GWL_EXSTYLE,0);SetWindowLongPtrW(chromium,GWL_STYLE,WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX);check(!home::describeWindow(chromium).defaultEligible,"unmaximized captionless Chromium window stays filtered by default");DestroyWindow(chromium);}
    if(chromiumRegistered)UnregisterClassW(chromiumClass.lpszClassName,chromiumClass.hInstance);
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    auto resolved=home::resolveApplicationFile(executable);
    check(resolved.error.empty()&&!resolved.identity.path.empty(),"existing executable file resolves without being launched");
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);IShellLinkW* link{};IPersistFile* file{};auto shortcut=directory/L"program.lnk";
    HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));if(SUCCEEDED(hr))hr=link->SetPath(executable);if(SUCCEEDED(hr))hr=link->SetArguments(L"--different-launch-arguments");if(SUCCEEDED(hr))hr=link->QueryInterface(IID_PPV_ARGS(&file));if(SUCCEEDED(hr))hr=file->Save(shortcut.c_str(),TRUE);if(file)file->Release();if(link)link->Release();
    auto linked=home::resolveApplicationFile(shortcut.wstring());
    check(SUCCEEDED(hr)&&linked.error.empty()&&linked.identity.path==resolved.identity.path,"shortcut resolves to same identity independently of launch arguments");
    CoUninitialize();auto unsupported=directory/L"unsupported.txt";{std::ofstream output(unsupported);output<<"test";}
    check(!home::resolveApplicationFile(unsupported.wstring()).error.empty()&&!home::resolveApplicationFile(L"C:\\missing-program.exe").error.empty(),"unsupported and missing file targets return actionable errors");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
