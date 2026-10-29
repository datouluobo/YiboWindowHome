#include "state.h"
#include <algorithm>
#include <cwctype>
#include <objbase.h>
#include <appmodel.h>
#include <set>
#include <stdexcept>

namespace home {
namespace {
bool equal(const std::wstring& a,const std::wstring& b) {return CompareStringOrdinal(a.c_str(),int(a.size()),b.c_str(),int(b.size()),TRUE)==CSTR_EQUAL;}
bool ambiguousIdentity(const State& state,const ApplicationIdentity& identity) {
    if(!identity.aumid.empty()||identity.path.empty())return false;
    size_t matches{};
    for(const auto& app:state.applications)if(equal(app.path,identity.path)&&++matches>1)return true;
    return false;
}
bool matches(const WindowRule& rule,const WindowMatch& window) {
    if(!rule.active||rule.className!=window.className)return false;
    return rule.titleMatch==TitleMatch::Any||
        (rule.titleMatch==TitleMatch::Contains&&window.title.find(rule.title)!=std::wstring::npos)||
        (rule.titleMatch==TitleMatch::Equals&&window.title==rule.title);
}
bool validId(const std::wstring& id) {return !id.empty()&&id.size()<=64&&std::all_of(id.begin(),id.end(),[](wchar_t c){return (c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c==L'-';});}
bool validText(const std::wstring& text,size_t limit) {return text.size()<=limit&&text.find(L'\0')==std::wstring::npos;}
void validateOffset(const Offset& offset) {if(offset.x < -500||offset.x>500||offset.y < -500||offset.y>500)throw std::runtime_error("invalid offset");}
ApplicationIdentity identifyProcess(DWORD pid) {
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);if(!process)return {};
    wchar_t path[32768]{};DWORD size=32768;ApplicationIdentity identity;
    if(QueryFullProcessImageNameW(process,0,path,&size))identity.path=normalizeAppPath(std::wstring(path,size));
    UINT32 length{};if(GetApplicationUserModelId(process,&length,nullptr)==ERROR_INSUFFICIENT_BUFFER&&length<=1024) {
        std::vector<wchar_t> aumid(length);if(GetApplicationUserModelId(process,&length,aumid.data())==ERROR_SUCCESS)identity.aumid=aumid.data();
    }
    CloseHandle(process);return identity;
}
}
std::wstring normalizeAppPath(const std::wstring& path) {
    if(path.empty()||path.size()>32767||path.find(L'\0')!=std::wstring::npos||!std::filesystem::path(path).is_absolute())return {};
    wchar_t full[32768]{};DWORD size=GetFullPathNameW(path.c_str(),32768,full,nullptr);if(!size||size>=32768)return {};
    std::wstring result(full,size);
    std::replace(result.begin(),result.end(),L'/',L'\\');
    std::transform(result.begin(),result.end(),result.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
    return result;
}
std::wstring newId(const wchar_t* prefix) {
    GUID guid{};if(FAILED(CoCreateGuid(&guid)))throw std::runtime_error("GUID");
    wchar_t text[40]{};StringFromGUID2(guid,text,40);std::wstring id=prefix;
    for(auto c:std::wstring(text))if(c!=L'{'&&c!=L'}'&&c!=L'-')id+=static_cast<wchar_t>(towlower(c));
    return id;
}
ApplicationIdentity identifyApplication(HWND window) {
    DWORD pid{};GetWindowThreadProcessId(window,&pid);auto identity=identifyProcess(pid);
    if(!identity.aumid.empty())return identity;

    // UWP content runs in its packaged process, while the visible root HWND
    // belongs to ApplicationFrameHost.exe. Resolve that root through its
    // packaged CoreWindow child so per-app state does not collapse to the host.
    wchar_t rootClass[256]{};GetClassNameW(window,rootClass,256);
    if(wcscmp(rootClass,L"ApplicationFrameWindow")!=0)return identity;
    struct Search{ApplicationIdentity identity;} search;
    EnumChildWindows(window,[](HWND child,LPARAM data)->BOOL{
        wchar_t cls[256]{};GetClassNameW(child,cls,256);if(wcscmp(cls,L"Windows.UI.Core.CoreWindow")!=0)return TRUE;
        DWORD childPid{};GetWindowThreadProcessId(child,&childPid);auto candidate=identifyProcess(childPid);
        if(candidate.aumid.empty()||candidate.path.empty())return TRUE;
        reinterpret_cast<Search*>(data)->identity=std::move(candidate);return FALSE;
    },reinterpret_cast<LPARAM>(&search));
    // A shared host is not an application identity. During startup, shutdown,
    // or access failure its CoreWindow can be absent; retry later instead of
    // assigning unrelated hosted applications to ApplicationFrameHost.exe.
    return std::move(search.identity);
}
WindowMatch describeWindow(HWND window) {
    WindowMatch match;match.identity=identifyApplication(window);
    wchar_t cls[256]{},title[4097]{};GetClassNameW(window,cls,256);GetWindowTextW(window,title,4097);
    match.className=cls;match.title=title;match.maximized=IsZoomed(window)!=FALSE;
    auto style=GetWindowLongPtrW(window,GWL_STYLE),ex=GetWindowLongPtrW(window,GWL_EXSTYLE);
    constexpr LONG_PTR controls=WS_THICKFRAME|WS_MINIMIZEBOX|WS_MAXIMIZEBOX;
    bool maximizedChromium=match.maximized&&match.className==L"Chrome_WidgetWin_1"&&(style&controls)==controls;
    match.defaultEligible=((style&WS_CAPTION)!=0||maximizedChromium)&&(ex&WS_EX_TOOLWINDOW)==0;
    DWORD pid{};GetWindowThreadProcessId(window,&pid);
    match.systemSafe=pid!=GetCurrentProcessId()&&GetAncestor(window,GA_ROOT)==window&&match.className!=L"Progman"&&match.className!=L"WorkerW"&&match.className!=L"Shell_TrayWnd"&&match.className!=L"Shell_SecondaryTrayWnd";
    return match;
}
const Application* findApplication(const State& state,const ApplicationIdentity& identity) {
    // AUMID keeps packaged applications stable across versioned install paths.
    if(!identity.aumid.empty())for(auto& app:state.applications)if(!app.aumid.empty()&&app.aumid==identity.aumid)return &app;
    if(ambiguousIdentity(state,identity))return nullptr;
    for(auto& app:state.applications)if(!identity.path.empty()&&equal(app.path,identity.path)&&(identity.aumid.empty()||app.aumid.empty()))return &app;
    return nullptr;
}
std::wstring scopeKey(const Application& app,const std::wstring& ruleId) {return app.id+L"/"+(ruleId.empty()?L"default":ruleId);}
Application& ensureApplication(State& state,const ApplicationIdentity& identity,const std::wstring& name) {
    ApplicationIdentity canonical{normalizeAppPath(identity.path),identity.aumid};
    if(ambiguousIdentity(state,canonical))throw std::runtime_error("ambiguous application identity");
    if(auto app=findApplication(state,canonical)) {
        auto& existing=state.applications[size_t(app-state.applications.data())];
        if(!identity.aumid.empty()){existing.aumid=identity.aumid;auto updated=normalizeAppPath(identity.path);if(!updated.empty())existing.path=updated;}
        return existing;
    }
    auto path=normalizeAppPath(identity.path);if(path.empty()||state.applications.size()>=1024)throw std::runtime_error("application identity");
    Application app;app.id=newId(L"a-");app.path=path;app.aumid=identity.aumid;
    app.name=name.empty()?std::filesystem::path(path).stem().wstring():name;
    state.applications.push_back(std::move(app));return state.applications.back();
}
Policy resolveWindow(const State& state,const WindowMatch& window) {
    Policy policy;policy.x=state.offsetX;policy.y=state.offsetY;
    if(!window.systemSafe||window.identity.path.empty()||ambiguousIdentity(state,window.identity))return policy;
    auto app=findApplication(state,window.identity);
    if(!app) {policy.allowed=window.defaultEligible;policy.recordKey=window.identity.path;return policy;}
    policy.appId=app->id;policy.recordKey=scopeKey(*app);
    if(!app->enabled)return policy;
    for(auto& rule:app->rules)if(rule.exclude&&matches(rule,window)) {policy.ruleName=rule.name;return policy;}
    auto apply=[&](const Offset& offset){if(offset.custom){policy.x=offset.x;policy.y=offset.y;}};
    apply(window.maximized?app->maximized:app->normal);
    for(auto& rule:app->rules)if(!rule.exclude&&matches(rule,window)) {
        policy.allowed=true;policy.explicitRule=true;policy.scopeId=rule.id;policy.ruleName=rule.name;policy.recordKey=scopeKey(*app,rule.id);
        apply(window.maximized?rule.maximized:rule.normal);return policy;
    }
    policy.allowed=window.defaultEligible;return policy;
}
void migrateLegacyApplications(State& state) {
    std::vector<std::wstring> paths;
    for(auto& [key,record]:state.records)if(key.find(L'/')==std::wstring::npos||key.find(L':')!=std::wstring::npos)paths.push_back(key);
    for(auto& path:paths) {
        auto& app=ensureApplication(state,{path,L""});auto key=scopeKey(app);
        auto node=state.records.extract(path);if(!state.records.contains(key)){node.key()=key;state.records.insert(std::move(node));}
    }
}
void validateApplications(const State& state) {
    if(state.applications.size()>1024)throw std::runtime_error("application count");
    std::set<std::wstring> ids,keys;
    for(size_t i=0;i<state.applications.size();++i) {
        auto& app=state.applications[i];
        if(!validId(app.id)||!ids.insert(app.id).second||!validText(app.name,256)||app.name.empty()||normalizeAppPath(app.path).empty()||!validText(app.aumid,1024)||app.rules.size()>128)throw std::runtime_error("application configuration");
        for(size_t j=0;j<i;++j) {
            auto& other=state.applications[j];
            if((equal(app.path,other.path)&&(app.aumid.empty()||other.aumid.empty()||app.aumid==other.aumid))||(!app.aumid.empty()&&!other.aumid.empty()&&app.aumid==other.aumid))throw std::runtime_error("duplicate application");
        }
        validateOffset(app.normal);validateOffset(app.maximized);keys.insert(scopeKey(app));
        for(auto& rule:app.rules) {
            if(!validId(rule.id)||!ids.insert(rule.id).second||!validText(rule.name,256)||rule.name.empty()||!validText(rule.className,255)||rule.className.empty()||!validText(rule.title,4096)||int(rule.titleMatch)<0||int(rule.titleMatch)>2||(rule.titleMatch!=TitleMatch::Any&&rule.title.empty()))throw std::runtime_error("window rule");
            validateOffset(rule.normal);validateOffset(rule.maximized);keys.insert(scopeKey(app,rule.id));
        }
    }
    if(state.records.size()>4096||state.activeRecords.size()>4096)throw std::runtime_error("record count");
    std::map<std::wstring,size_t> counts;
    for(auto& [key,record]:state.records) {
        auto scope=positionScope(key);
        if(!keys.contains(scope)||!validText(record.name,256))throw std::runtime_error("unbound record");
        if(key!=scope) {
            auto id=key.substr(scope.size()+1);
            if(!id.starts_with(L"p-")||!validId(id)||record.name.empty()||
                std::all_of(record.name.begin(),record.name.end(),[](wchar_t ch){return iswspace(ch)!=0;}))throw std::runtime_error("position record");
        }
        // Reserve one permanent default slot even if it has not been captured.
        if(key!=scope&&++counts[scope]>127)throw std::runtime_error("position count");
    }
    for(auto& [scope,key]:state.activeRecords)
        if(!keys.contains(scope)||positionScope(key)!=scope||!state.records.contains(key))throw std::runtime_error("active position record");
}
}
