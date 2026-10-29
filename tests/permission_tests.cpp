#include "state.h"
#include "preview.h"
#include "window_ops.h"
#include <sddl.h>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <chrono>

namespace {
std::string bytes(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
DWORD integrity(HANDLE process){
    HANDLE token{};if(!OpenProcessToken(process,TOKEN_QUERY,&token))return MAXDWORD;
    DWORD size{};GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<BYTE> data(size);DWORD rid=MAXDWORD;
    if(size&&GetTokenInformation(token,TokenIntegrityLevel,data.data(),size,&size)){auto label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data.data());if(IsValidSid(label->Label.Sid)){auto count=*GetSidSubAuthorityCount(label->Label.Sid);if(count)rid=*GetSidSubAuthority(label->Label.Sid,count-1);}}
    CloseHandle(token);return rid;
}
bool lowerChild(HANDLE process){
    HANDLE token{};if(!OpenProcessToken(process,TOKEN_QUERY|TOKEN_ADJUST_DEFAULT,&token))return false;
    PSID sid{};bool ok=false;if(ConvertStringSidToSidW(L"S-1-16-4096",&sid)){TOKEN_MANDATORY_LABEL label{};label.Label.Sid=sid;label.Label.Attributes=SE_GROUP_INTEGRITY;ok=SetTokenInformation(token,TokenIntegrityLevel,&label,DWORD(sizeof(label)+GetLengthSid(sid)))!=FALSE;LocalFree(sid);}CloseHandle(token);return ok;
}
LRESULT CALLBACK procedure(HWND w,UINT msg,WPARAM p,LPARAM l){if(msg==WM_CLOSE){DestroyWindow(w);PostQuitMessage(0);return 0;}return DefWindowProcW(w,msg,p,l);}
int child(HWND target,const std::filesystem::path& path){
    std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    DWORD level=integrity(GetCurrentProcess());std::cout<<"Child integrity RID: "<<level<<'\n';check(level==SECURITY_MANDATORY_LOW_RID,"permission fixture really runs at low process integrity");if(level!=SECURITY_MANDATORY_LOW_RID)return 2;
    home::Store store{path};auto state=store.load();bool loaded=store.writable&&state.records.size()==1;check(loaded,"low-integrity controller can read the isolated test record");if(!loaded)return 2;
    auto original=bytes(path);auto result=home::restoreDetailed(target,state.records.begin()->second);DWORD restoreError=GetLastError();
    std::cout<<"Restore status="<<int(result)<<" error="<<restoreError<<'\n';check(result==home::RestoreStatus::Failed&&restoreError==ERROR_ACCESS_DENIED,"UIPI-denied restore reports failure and access-denied evidence");
    bool topped=home::boundedWindowPos(target,HWND_TOPMOST,0,0,0,0,SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOSIZE);DWORD topError=GetLastError();
    std::cout<<"Topmost result="<<topped<<" error="<<topError<<'\n';check(!topped&&topError==ERROR_ACCESS_DENIED,"UIPI-denied topmost request cannot report success");
    home::PreviewPlacement preview;bool begun=preview.begin(target,true);DWORD previewError=GetLastError();
    std::cout<<"Preview result="<<begun<<" error="<<previewError<<'\n';check(!begun&&previewError==ERROR_ACCESS_DENIED,"UIPI-denied preview cannot report a successful state switch");
    state.offsetX=-99;bool saved=store.save(state);DWORD saveError=GetLastError();
    std::cout<<"Save result="<<saved<<" error="<<saveError<<'\n';check(!saved&&saveError==ERROR_ACCESS_DENIED,"mandatory-integrity denied write cannot report successful persistence");
    check(bytes(path)==original,"permission-denied save preserves exact original record bytes");return failures?1:0;
}
int mediumHighTarget(HWND target){
    std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    DWORD caller=integrity(GetCurrentProcess());DWORD pid{};GetWindowThreadProcessId(target,&pid);DWORD targetLevel=pid?integrity(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)):MAXDWORD;
    std::cout<<"Caller integrity RID: "<<caller<<" target integrity RID: "<<targetLevel<<" target PID: "<<pid<<'\n';
    check(caller==SECURITY_MANDATORY_MEDIUM_RID,"controller runs at ordinary medium integrity");
    check(IsWindow(target)&&targetLevel>=SECURITY_MANDATORY_HIGH_RID,"selected live target is a higher-integrity application window");
    if(caller!=SECURITY_MANDATORY_MEDIUM_RID||!IsWindow(target)||targetLevel<SECURITY_MANDATORY_HIGH_RID)return 2;
    RECT before{},after{};WINDOWPLACEMENT beforePlacement{sizeof(beforePlacement)},afterPlacement{sizeof(afterPlacement)};
    DWORD_PTR response{};bool readable=GetWindowRect(target,&before)&&GetWindowPlacement(target,&beforePlacement)&&SendMessageTimeoutW(target,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,home::WindowOperationTimeoutMs,&response)!=0;
    LONG_PTR beforeStyle=GetWindowLongPtrW(target,GWL_EXSTYLE);HWND foreground=GetForegroundWindow();
    auto record=home::capture(target);
    check(readable&&EqualRect(&before,&record.actual)&&record.maximized==(IsZoomed(target)!=FALSE),"high-integrity target is responsive and its current geometry can be captured without changes");
    if(!readable||!EqualRect(&before,&record.actual)||record.maximized!=(IsZoomed(target)!=FALSE))return 2;
    auto result=home::restoreDetailed(target,record);DWORD error=GetLastError();
    std::cout<<"Restore status="<<int(result)<<" error="<<error<<'\n';
    check(result==home::RestoreStatus::Failed&&error==ERROR_ACCESS_DENIED,"medium-to-high restore reports UIPI access denied");
    GetWindowRect(target,&after);GetWindowPlacement(target,&afterPlacement);
    check(EqualRect(&before,&after),"denied medium-to-high request preserves exact target bounds");
    check(beforePlacement.showCmd==afterPlacement.showCmd&&IsZoomed(target)==(beforePlacement.showCmd==SW_SHOWMAXIMIZED),"denied medium-to-high request preserves show state");
    check((GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)==(beforeStyle&WS_EX_TOPMOST),"denied medium-to-high request preserves z-order style");
    check(GetForegroundWindow()==foreground,"denied medium-to-high request leaves the foreground window unchanged");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
int packagedTarget(const std::wstring& expectedAumid,const std::wstring& expectedClass=L"",bool exerciseMaximum=false){
    std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    struct Search{const std::wstring* aumid;const std::wstring* className;HWND found{};} search{&expectedAumid,&expectedClass};
    EnumWindows([](HWND window,LPARAM data)->BOOL{
        auto& search=*reinterpret_cast<Search*>(data);if(!IsWindowVisible(window)||IsIconic(window))return TRUE;
        auto description=home::describeWindow(window);
        if(description.systemSafe&&!description.identity.aumid.empty()&&description.identity.aumid==*search.aumid&&(search.className->empty()||description.className==*search.className)){search.found=window;return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    HWND target=search.found;check(target!=nullptr,"visible packaged application window resolves from its AUMID");if(!target)return 2;
    auto description=home::describeWindow(target);DWORD pid{};GetWindowThreadProcessId(target,&pid);
    std::string classLabel;for(wchar_t c:description.className)classLabel.push_back(c<=127?static_cast<char>(c):'?');
    std::cout<<"Target PID: "<<pid<<" HWND="<<reinterpret_cast<UINT_PTR>(target)<<" class="<<classLabel<<"; packaged AUMID and executable path verified\n";
    check(description.identity.aumid==expectedAumid&&!description.identity.path.empty()&&!description.className.empty()&&description.systemSafe,"packaged identity, executable path, HWND class and safe-window policy resolve");
    RECT before{},after{};WINDOWPLACEMENT beforePlacement{sizeof(beforePlacement)},afterPlacement{sizeof(afterPlacement)};
    bool captured=GetWindowRect(target,&before)&&GetWindowPlacement(target,&beforePlacement)&&SendMessageTimeoutW(target,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,home::WindowOperationTimeoutMs,nullptr)!=0;
    LONG_PTR beforeStyle=GetWindowLongPtrW(target,GWL_EXSTYLE);HWND foreground=GetForegroundWindow();auto record=captured?home::capture(target):home::Record{};
    check(captured&&!record.topology.empty()&&EqualRect(&before,&record.actual)&&record.maximized==(IsZoomed(target)!=FALSE),"packaged window geometry, topology and show state capture without mutation");
    std::cout<<"Before rect="<<before.left<<","<<before.top<<","<<before.right<<","<<before.bottom<<" show="<<beforePlacement.showCmd<<" style="<<GetWindowLongPtrW(target,GWL_STYLE)<<" exstyle="<<beforeStyle<<'\n';
    if(!captured)return 2;
    int width=before.right-before.left,height=before.bottom-before.top;bool moved=false;auto moveStart=std::chrono::steady_clock::now();SetLastError(0);
    if(!record.maximized&&width>0&&height>0)moved=home::boundedWindowPos(target,nullptr,before.left+12,before.top+12,width,height,SWP_NOACTIVATE|SWP_NOZORDER);
    DWORD moveError=GetLastError();auto moveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-moveStart).count();GetWindowRect(target,&after);
    std::cout<<"Move result="<<moved<<" error="<<moveError<<" elapsed-ms="<<moveMs<<" rect="<<after.left<<","<<after.top<<","<<after.right<<","<<after.bottom<<'\n';
    check(moved&&!EqualRect(&before,&after),"packaged target accepts bounded nonactivating geometry change");
    SetLastError(0);auto restoreStart=std::chrono::steady_clock::now();auto status=home::restoreDetailed(target,record);DWORD error=GetLastError();auto restoreMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-restoreStart).count();GetWindowRect(target,&after);GetWindowPlacement(target,&afterPlacement);
    std::cout<<"Restore status="<<int(status)<<" error="<<error<<" elapsed-ms="<<restoreMs<<" rect="<<after.left<<","<<after.top<<","<<after.right<<","<<after.bottom<<'\n';
    check(status==home::RestoreStatus::Restored&&EqualRect(&after,&record.normal),"packaged target restores its exact recorded rectangle");
    if(exerciseMaximum){
        auto maximum=record;maximum.maximized=true;SetLastError(0);auto maximumStatus=home::restoreDetailed(target,maximum);DWORD maximumError=GetLastError();bool zoomed=IsZoomed(target)!=FALSE;
        bool unmaximized=home::boundedShowWindow(target,SW_RESTORE);RECT manualBounds{};GetWindowRect(target,&manualBounds);
        auto ordinaryStatus=home::restoreDetailed(target,record);GetWindowRect(target,&after);GetWindowPlacement(target,&afterPlacement);
        std::cout<<"Maximum status="<<int(maximumStatus)<<" error="<<maximumError<<" zoomed="<<zoomed<<" manual-restore="<<unmaximized<<" manual="<<manualBounds.left<<","<<manualBounds.top<<","<<manualBounds.right<<","<<manualBounds.bottom<<" ordinary status="<<int(ordinaryStatus)<<" returned="<<after.left<<","<<after.top<<","<<after.right<<","<<after.bottom<<'\n';
        check(!record.maximized&&maximumStatus==home::RestoreStatus::Restored&&zoomed&&unmaximized&&EqualRect(&manualBounds,&record.normal)&&ordinaryStatus==home::RestoreStatus::Restored&&EqualRect(&after,&record.normal),"packaged maximum restore and manual unmaximize retain saved ordinary bounds");
    }
    check(beforePlacement.showCmd==afterPlacement.showCmd&&IsZoomed(target)==(beforePlacement.showCmd==SW_SHOWMAXIMIZED),"packaged restore preserves show state");
    check((GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)==(beforeStyle&WS_EX_TOPMOST),"packaged restore preserves topmost state");
    check(GetForegroundWindow()==foreground,"packaged restore preserves the foreground window");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
int isolatedTarget(DWORD expectedPid,const std::wstring& expectedPath){
    std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    struct Search{DWORD pid;HWND found{};} search{expectedPid};
    EnumWindows([](HWND w,LPARAM p)->BOOL{auto& s=*reinterpret_cast<Search*>(p);DWORD pid{};GetWindowThreadProcessId(w,&pid);if(pid==s.pid&&IsWindowVisible(w)&&!IsIconic(w)&&GetAncestor(w,GA_ROOT)==w){s.found=w;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
    HWND target=search.found;check(target!=nullptr,"isolated process has a visible top-level target");if(!target)return 2;
    auto description=home::describeWindow(target);auto expected=home::normalizeAppPath(expectedPath);
    check(!expected.empty()&&description.identity.path==expected&&description.systemSafe&&!description.className.empty(),"target identity and window class match the isolated executable");
    if(expected.empty()||description.identity.path!=expected||!description.systemSafe)return 2;
    RECT before{},after{};WINDOWPLACEMENT placement{sizeof(placement)},afterPlacement{sizeof(afterPlacement)};auto foreground=GetForegroundWindow();auto ex=GetWindowLongPtrW(target,GWL_EXSTYLE);
    bool captured=GetWindowRect(target,&before)&&GetWindowPlacement(target,&placement)&&home::windowOperationReady(target);auto saved=captured?home::capture(target):home::Record{};
    check(captured&&!saved.maximized&&!saved.topology.empty()&&EqualRect(&saved.actual,&before),"ordinary position and topology capture from isolated window");if(!captured||saved.maximized)return 2;
    bool moved=home::boundedWindowPos(target,nullptr,before.left+24,before.top+24,before.right-before.left,before.bottom-before.top,SWP_NOACTIVATE|SWP_NOZORDER);GetWindowRect(target,&after);
    check(moved&&!EqualRect(&before,&after),"isolated window accepts bounded nonactivating movement");
    auto status=home::restoreDetailed(target,saved);GetWindowRect(target,&after);
    check(status==home::RestoreStatus::Restored&&EqualRect(&saved.normal,&after),"ordinary restore returns to the exact recorded rectangle");
    std::cout<<"Foreground before maximize="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<'\n';
    auto maximized=saved;maximized.maximized=true;status=home::restoreDetailed(target,maximized);
    std::cout<<"Foreground after maximize="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<'\n';
    bool zoomed=IsZoomed(target)!=FALSE;auto normalStatus=home::restoreDetailed(target,saved);GetWindowRect(target,&after);GetWindowPlacement(target,&afterPlacement);
    std::cout<<"Maximum status="<<int(status)<<" zoomed="<<zoomed<<" ordinary status="<<int(normalStatus)<<" saved="<<saved.normal.left<<","<<saved.normal.top<<","<<saved.normal.right<<","<<saved.normal.bottom<<" returned="<<after.left<<","<<after.top<<","<<after.right<<","<<after.bottom<<" show before="<<placement.showCmd<<" after="<<afterPlacement.showCmd<<" topmost before="<<(ex&WS_EX_TOPMOST)<<" after="<<(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)<<'\n';
    check(status==home::RestoreStatus::Restored&&zoomed&&normalStatus==home::RestoreStatus::Restored&&EqualRect(&saved.normal,&after),"maximum restore and subsequent ordinary restore retain the saved bounds");
    check(placement.showCmd==afterPlacement.showCmd&&(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)==(ex&WS_EX_TOPMOST),"restore preserves original show and topmost state");
    std::cout<<"Foreground before="<<reinterpret_cast<UINT_PTR>(foreground)<<" after="<<reinterpret_cast<UINT_PTR>(GetForegroundWindow())<<" target="<<reinterpret_cast<UINT_PTR>(target)<<'\n';
    check(GetForegroundWindow()==foreground,"isolated restore preserves foreground");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--low-child")return child(reinterpret_cast<HWND>(std::stoull(argv[2])),argv[3]);
    if(argc==3&&std::wstring(argv[1])==L"--medium-high-target")return mediumHighTarget(reinterpret_cast<HWND>(std::stoull(argv[2])));
    if(argc==3&&std::wstring(argv[1])==L"--packaged-target")return packagedTarget(argv[2]);
    if(argc==4&&std::wstring(argv[1])==L"--packaged-class-target")return packagedTarget(argv[2],argv[3]);
    if(argc==4&&std::wstring(argv[1])==L"--packaged-class-max-target")return packagedTarget(argv[2],argv[3],true);
    if(argc==4&&std::wstring(argv[1])==L"--isolated-target")return isolatedTarget(static_cast<DWORD>(std::stoul(argv[2])),argv[3]);
    if(argc!=2)return 2;std::cout<<std::unitbuf;int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    auto directory=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(directory);auto displays=home::collectDisplays();if(displays.empty())return 2;auto work=displays[0].work;
    auto parentLevel=integrity(GetCurrentProcess());std::cout<<"Parent integrity RID: "<<parentLevel<<'\n';check(parentLevel>=SECURITY_MANDATORY_MEDIUM_RID&&parentLevel!=MAXDWORD,"target fixture has a verified higher integrity level");
    std::promise<HWND> ready;auto future=ready.get_future();HWND foreground=GetForegroundWindow();
    std::thread worker([&]{WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=procedure;c.lpszClassName=L"YiboHome.PermissionTest";RegisterClassW(&c);auto w=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"风钩 · 权限测试",WS_OVERLAPPEDWINDOW,work.left+150,work.top+120,640,420,nullptr,nullptr,c.hInstance,nullptr);if(w){ShowWindow(w,SW_SHOWNOACTIVATE);if(!IsWindowVisible(w))ShowWindow(w,SW_SHOWNOACTIVATE);}ready.set_value(w);MSG msg{};while(w&&GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}if(IsWindow(w))DestroyWindow(w);});
    auto target=future.get();if(!target){worker.join();return 2;}
    RECT before{},after{};GetWindowRect(target,&before);auto beforeStyle=GetWindowLongPtrW(target,GWL_EXSTYLE);home::State state;auto app=home::ensureApplication(state,home::identifyApplication(target),L"Permission fixture");auto record=home::capture(target);OffsetRect(&record.normal,100,80);state.records[home::scopeKey(app)]=record;home::Store store{directory/L"state.json"};bool initialized=store.save(state);check(initialized,"permission fixture initializes only an isolated record file");auto original=bytes(store.path);
    SECURITY_ATTRIBUTES inherit{sizeof(inherit),nullptr,TRUE};auto outputPath=directory/L"low-child-output.txt";
    HANDLE output=CreateFileW(outputPath.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&inherit,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&inherit,OPEN_EXISTING,0,nullptr);
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=L"\""+std::wstring(exe)+L"\" --low-child "+std::to_wstring(reinterpret_cast<UINT_PTR>(target))+L" \""+store.path.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW|STARTF_USESTDHANDLES;startup.wShowWindow=SW_HIDE;startup.hStdOutput=output;startup.hStdError=output;startup.hStdInput=input;PROCESS_INFORMATION process{};
    bool started=initialized&&output!=INVALID_HANDLE_VALUE&&input!=INVALID_HANDLE_VALUE&&CreateProcessW(exe,command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    check(started,"owned controller process starts suspended before any test instructions");
    bool lowered=false,completed=false;DWORD code=MAXDWORD;
    if(started){lowered=lowerChild(process.hProcess)&&integrity(process.hProcess)==SECURITY_MANDATORY_LOW_RID;check(lowered,"only the owned suspended child token is reduced to low integrity");if(lowered){ResumeThread(process.hThread);completed=WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0;if(completed)GetExitCodeProcess(process.hProcess,&code);}
        if(!completed){TerminateProcess(process.hProcess,92);WaitForSingleObject(process.hProcess,1000);}CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    if(input!=INVALID_HANDLE_VALUE)CloseHandle(input);if(output!=INVALID_HANDLE_VALUE)CloseHandle(output);
    if(started&&lowered)std::cout<<bytes(outputPath);
    check(completed&&code==0,"low-integrity permission checks complete successfully within deadline");
    GetWindowRect(target,&after);check(EqualRect(&before,&after),"denied external restore leaves higher-integrity window bounds unchanged");
    check(!IsZoomed(target)&&(GetWindowLongPtrW(target,GWL_EXSTYLE)&WS_EX_TOPMOST)==(beforeStyle&WS_EX_TOPMOST),"denied preview and topmost requests leave target state unchanged");
    check(bytes(store.path)==original,"parent independently verifies denied write did not alter stored record");
    check(integrity(GetCurrentProcess())==parentLevel,"controller test does not change the parent token");
    check(GetForegroundWindow()==foreground,"permission checks preserve the foreground application");
    PostMessageW(target,WM_CLOSE,0,0);worker.join();std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
