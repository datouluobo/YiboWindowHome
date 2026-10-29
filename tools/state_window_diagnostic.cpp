#include "state.h"
#include <windows.h>
#include <iostream>
#include <filesystem>
int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;home::Store store{argv[1]};auto state=store.load();
    std::wcout<<L"state-readable="<<store.writable<<L" warning="<<!store.warning.empty()<<L" applications="<<state.applications.size()<<L" records="<<state.records.size()<<L'\n';
    struct Context{home::State* state;HWND foreground;} c{&state,GetForegroundWindow()};
    EnumWindows([](HWND w,LPARAM l)->BOOL{
        auto& c=*reinterpret_cast<Context*>(l);if(!IsWindowVisible(w)||IsIconic(w)||GetAncestor(w,GA_ROOT)!=w)return TRUE;
        auto desc=home::describeWindow(w);if(!desc.systemSafe||desc.identity.path.empty())return TRUE;
        auto app=home::findApplication(*c.state,desc.identity);auto policy=home::resolveWindow(*c.state,desc);
        auto found=c.state->records.find(policy.recordKey);RECT r{};GetWindowRect(w,&r);DWORD pid{};GetWindowThreadProcessId(w,&pid);
        auto style=GetWindowLongPtrW(w,GWL_STYLE),ex=GetWindowLongPtrW(w,GWL_EXSTYLE);
        std::wcout<<L"pid="<<pid<<L" app="<<std::filesystem::path(desc.identity.path).filename().wstring()<<L" class="<<desc.className<<L" foreground="<<(w==c.foreground)<<L" aumid="<<!desc.identity.aumid.empty()<<L" known="<<(app!=nullptr)<<L" app-enabled="<<(app?int(app->enabled):-1)<<L" rules="<<(app?app->rules.size():0)<<L" style="<<style<<L" exstyle="<<ex<<L" has-caption="<<((style&WS_CAPTION)==WS_CAPTION)<<L" has-toolwindow="<<!!(ex&WS_EX_TOOLWINDOW)<<L" default-eligible="<<desc.defaultEligible<<L" allowed="<<policy.allowed<<L" explicit-rule="<<policy.explicitRule<<L" record="<<(found!=c.state->records.end())<<L" maximized="<<desc.maximized<<L" current="<<r.left<<L','<<r.top<<L','<<r.right<<L','<<r.bottom;
        if(found!=c.state->records.end()){auto a=found->second.normal;std::wcout<<L" saved="<<a.left<<L','<<a.top<<L','<<a.right<<L','<<a.bottom<<L" saved-max="<<found->second.maximized;}
        std::wcout<<L'\n';return TRUE;
    },reinterpret_cast<LPARAM>(&c));return 0;
}
