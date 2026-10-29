#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace home {
struct Offset { bool custom{};int x{},y{}; };
enum class TitleMatch { Any,Contains,Equals };
struct WindowRule {
    std::wstring id,name,className,title;
    TitleMatch titleMatch{TitleMatch::Any};
    bool exclude{},active{true};
    Offset normal,maximized;
};
struct Application {
    std::wstring id,name,path,aumid;
    bool enabled{true};
    Offset normal,maximized;
    std::vector<WindowRule> rules;
};
struct ApplicationIdentity {std::wstring path,aumid;};
struct WindowMatch {
    ApplicationIdentity identity;
    std::wstring className,title;
    bool maximized{},defaultEligible{true},systemSafe{true};
};
struct Policy {
    bool allowed{},explicitRule{};
    int x{},y{};
    std::wstring appId,scopeId,recordKey,ruleName;
};
struct State;
std::wstring normalizeAppPath(const std::wstring& path);
std::wstring newId(const wchar_t* prefix);
ApplicationIdentity identifyApplication(HWND window);
WindowMatch describeWindow(HWND window);
const Application* findApplication(const State& state,const ApplicationIdentity& identity);
Application& ensureApplication(State& state,const ApplicationIdentity& identity,const std::wstring& name=L"");
std::wstring scopeKey(const Application& app,const std::wstring& ruleId=L"");
Policy resolveWindow(const State& state,const WindowMatch& window);
void migrateLegacyApplications(State& state);
void validateApplications(const State& state);
}
