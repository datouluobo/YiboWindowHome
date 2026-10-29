#include "state.h"
#include "caption.h"
#include "caption_style.h"
#include <windows.h>
#include <commctrl.h>
#include <wtsapi32.h>
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
void CALLBACK hook(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD){}
bool snapshot(const std::filesystem::path& probe,const std::filesystem::path& output,const wchar_t* stage){
    auto path=output/(std::wstring(stage)+L".txt");SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    std::wstring cmd=L"\""+probe.wstring()+L"\" "+std::to_wstring(GetCurrentProcessId());STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;si.hStdOutput=file;si.hStdError=file;si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};bool ok=CreateProcessW(probe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;CloseHandle(file);
    if(!ok)return false;bool done=WaitForSingleObject(pi.hProcess,10000)==WAIT_OBJECT_0;DWORD code{};ok=done&&GetExitCodeProcess(pi.hProcess,&code)&&code==0;
    if(!done){TerminateProcess(pi.hProcess,90);WaitForSingleObject(pi.hProcess,1000);}CloseHandle(pi.hThread);CloseHandle(pi.hProcess);std::wcout<<L"stage="<<stage<<L" snapshot="<<ok<<L'\n';return ok;
}
}
static std::filesystem::path traceProbe,traceDir;
static bool traceEnabled=false,traceOk=true;
void stateHandleTrace(const wchar_t* stage){if(traceEnabled)traceOk=snapshot(traceProbe,traceDir,stage)&&traceOk;}
int wmain(int argc,wchar_t** argv){
    if(argc!=3)return 2;auto probe=std::filesystem::absolute(argv[1]);auto dir=std::filesystem::absolute(argv[2]);if(std::filesystem::exists(dir))return 2;std::filesystem::create_directories(dir);
    HANDLE token{};DWORD integrity{};if(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)){DWORD size{};GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);std::vector<BYTE> data(size);if(GetTokenInformation(token,TokenIntegrityLevel,data.data(),size,&size)){auto label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data.data());auto count=*GetSidSubAuthorityCount(label->Label.Sid);integrity=*GetSidSubAuthority(label->Label.Sid,count-1);}CloseHandle(token);}{std::ofstream info(dir/L"integrity.txt");info<<"integrity-rid="<<integrity<<'\n';}
    bool ok=snapshot(probe,dir,L"01-initial");INITCOMMONCONTROLSEX common{sizeof(common),ICC_STANDARD_CLASSES|ICC_WIN95_CLASSES};InitCommonControlsEx(&common);ok=snapshot(probe,dir,L"02-common-controls")&&ok;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=DefWindowProcW;cls.lpszClassName=L"YiboHome.HandleOriginFixture";RegisterClassW(&cls);ok=snapshot(probe,dir,L"03-register-class")&&ok;
    HWND host=CreateWindowExW(WS_EX_TOOLWINDOW,cls.lpszClassName,L"",WS_POPUP,100,100,600,400,nullptr,nullptr,cls.hInstance,nullptr);if(!host)return 2;
    ok=snapshot(probe,dir,L"04-create-window")&&ok;bool registered=WTSRegisterSessionNotification(host,NOTIFY_FOR_THIS_SESSION)!=FALSE;ok=snapshot(probe,dir,L"05-session-notify")&&ok;
    std::vector<HWINEVENTHOOK> hooks;for(auto range:std::vector<std::pair<DWORD,DWORD>>{{EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND},{EVENT_OBJECT_DESTROY,EVENT_OBJECT_HIDE},{EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE},{EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE},{EVENT_SYSTEM_MENUSTART,EVENT_SYSTEM_MENUPOPUPEND},{EVENT_SYSTEM_MOVESIZESTART,EVENT_SYSTEM_MOVESIZEEND}}){auto h=SetWinEventHook(range.first,range.second,nullptr,hook,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);if(h)hooks.push_back(h);}
    ok=snapshot(probe,dir,L"06-hooks")&&ok;auto displays=home::collectDisplays();ok=snapshot(probe,dir,L"07-displays")&&ok;
    HWND foreground=GetForegroundWindow();if(foreground){for(int i=0;i<25;++i)(void)home::describeWindow(foreground);}
    ok=snapshot(probe,dir,L"08-identity")&&ok;
    if(foreground){for(int i=0;i<25;++i){auto m=home::measureCaption(foreground);RECT r=m.hasMinimize?m.minimize:m.hasVisualMinimize?m.visualMinimize:home::layoutCaption(m).button;(void)home::sampleCaptionPalette(foreground,r,m.dpi);}}
    ok=snapshot(probe,dir,L"09-caption")&&ok;
    auto record=home::capture(host);auto restore=home::restoreDetailed(host,record);ok=(restore!=home::RestoreStatus::Failed)&&ok;ok=snapshot(probe,dir,L"10-record-restore")&&ok;
    auto rawOld=dir/L"raw-old.bin",rawNew=dir/L"raw-new.bin",rawBackup=dir/L"raw-backup.bin";
    auto createRaw=[](const std::filesystem::path& path,char byte){HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)return false;DWORD written{};bool success=WriteFile(h,&byte,1,&written,nullptr)&&written==1&&FlushFileBuffers(h);CloseHandle(h);return success;};
    ok=createRaw(rawOld,'A')&&createRaw(rawNew,'B')&&ok;ok=snapshot(probe,dir,L"11-before-raw-replace")&&ok;
    bool rawReplaced=ReplaceFileW(rawOld.c_str(),rawNew.c_str(),rawBackup.c_str(),0,nullptr,nullptr)!=FALSE;ok=rawReplaced&&ok;ok=snapshot(probe,dir,L"12-after-raw-replace")&&ok;
    {std::ifstream input(rawOld,std::ios::binary);char value{};input.get(value);ok=bool(input)&&value=='B'&&ok;}
    ok=snapshot(probe,dir,L"12a-after-stream-read")&&ok;
    auto rawMove=dir/L"raw-moved.bin";ok=(MoveFileExW(rawNew.c_str(),rawMove.c_str(),MOVEFILE_WRITE_THROUGH)==FALSE)&&ok;
    ok=createRaw(rawNew,'C')&&ok;ok=(MoveFileExW(rawNew.c_str(),rawMove.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE)&&ok;
    ok=snapshot(probe,dir,L"12b-after-move-write-through")&&ok;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);home::State state;
    ok=snapshot(probe,dir,L"12c-before-ensure")&&ok;
    GUID guid{};ok=SUCCEEDED(CoCreateGuid(&guid))&&ok;
    ok=snapshot(probe,dir,L"12c1-after-guid")&&ok;
    for(int i=0;i<1000;++i)ok=SUCCEEDED(CoCreateGuid(&guid))&&ok;
    ok=snapshot(probe,dir,L"12c2-after-guid-repeat")&&ok;
    auto& application=home::ensureApplication(state,{executable,L""});
    ok=snapshot(probe,dir,L"12d-after-ensure")&&ok;
    state.records[home::scopeKey(application)]=record;home::Store store{dir/L"isolated-state.json"};
    ok=snapshot(probe,dir,L"12e-after-scope")&&ok;
    traceProbe=probe;traceDir=dir;traceEnabled=true;ok=store.save(state)&&ok;traceEnabled=false;ok=traceOk&&ok;ok=snapshot(probe,dir,L"13-first-save")&&ok;
    for(int i=0;i<10;++i){ok=store.save(state)&&ok;wchar_t stage[32]{};swprintf_s(stage,L"14-save-%02d",i+1);ok=snapshot(probe,dir,stage)&&ok;}
    for(int i=0;i<15;++i)ok=store.save(state)&&ok;
    ok=snapshot(probe,dir,L"15-repeat-save")&&ok;
    for(int i=0;i<25;++i){auto loaded=store.load();ok=store.writable&&loaded.records.size()==1&&ok;}
    ok=snapshot(probe,dir,L"16-repeat-load")&&ok;
    for(int i=0;i<25;++i)ok=(home::restoreDetailed(host,record)!=home::RestoreStatus::Failed)&&ok;
    ok=snapshot(probe,dir,L"17-repeat-restore")&&ok;
    NOTIFYICONDATAW tray{sizeof(tray)};tray.hWnd=host;tray.uID=1;tray.uFlags=NIF_ICON|NIF_TIP;tray.hIcon=LoadIconW(nullptr,IDI_APPLICATION);wcscpy_s(tray.szTip,L"风钩隔离资源测试");
    bool trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;ok=trayAdded&&ok;ok=snapshot(probe,dir,L"19-tray-added")&&ok;
    NOTIFYICONDATAW notice{sizeof(notice)};notice.hWnd=host;notice.uID=1;notice.uFlags=NIF_INFO;wcscpy_s(notice.szInfoTitle,L"风钩隔离资源测试");wcscpy_s(notice.szInfo,L"正在检查通知相关系统资源。");notice.dwInfoFlags=NIIF_INFO;
    bool notified=Shell_NotifyIconW(NIM_MODIFY,&notice)!=FALSE;ok=notified&&ok;Sleep(500);ok=snapshot(probe,dir,L"20-first-notification")&&ok;
    for(int i=0;i<10;++i)ok=(Shell_NotifyIconW(NIM_MODIFY,&notice)!=FALSE)&&ok;
    Sleep(500);ok=snapshot(probe,dir,L"21-repeat-notification")&&ok;
    if(trayAdded)Shell_NotifyIconW(NIM_DELETE,&tray);
    for(auto h:hooks)UnhookWinEvent(h);if(registered)WTSUnRegisterSessionNotification(host);DestroyWindow(host);
    ok=snapshot(probe,dir,L"18-cleanup")&&ok;std::wcout<<L"display-count="<<displays.size()<<L" hook-count="<<hooks.size()<<L" foreground-present="<<(foreground!=nullptr)<<L" raw-replace="<<rawReplaced<<L'\n';return ok?0:1;
}
