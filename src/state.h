#pragma once
#include <windows.h>
#include <filesystem>
#include <map>
#include <string>
#include "displays.h"
#include "rules.h"

namespace home {
struct Record {
    RECT normal{}, work{}, monitor{};
    std::wstring device;
    UINT dpi{96};
    bool maximized{};
    RECT actual{};
    std::wstring displayIdentity;
    std::vector<Display> topology;
    std::wstring savedAt;
    std::wstring name;
};
struct State {
    int offsetX{}, offsetY{};
    std::map<std::wstring, Record> records;
    std::vector<Application> applications;
    // Scope -> one selected record key. Absence selects the default slot.
    std::map<std::wstring,std::wstring> activeRecords;
};
// Prototype schema is isolated from the eventual production state.json.
struct Store {
    std::filesystem::path path;
    bool writable{true};
    std::wstring warning;
    State load();
    bool save(const State& state);
};
struct PositionEntry {
    std::wstring key,name;
    bool isDefault{},active{};
    const Record* record{};
};
std::wstring positionScope(const std::wstring& recordKey);
std::wstring activePositionKey(const State& state,const std::wstring& scope);
const Record* activePositionRecord(const State& state,const std::wstring& scope);
std::vector<PositionEntry> positionEntries(const State& state,const std::wstring& scope);
// Edit a draft, then Store::save it before replacing live state.
std::wstring savePositionRecord(State& state,const std::wstring& scope,Record record,bool createNew,const std::wstring& name=L"");
bool activatePositionRecord(State& state,const std::wstring& scope,const std::wstring& key);
bool renamePositionRecord(State& state,const std::wstring& key,const std::wstring& name);
bool erasePositionRecord(State& state,const std::wstring& key);
void erasePositionScope(State& state,const std::wstring& scope);
std::wstring appKey(HWND window);
Record capture(HWND window);
RECT restoreRect(const Record& saved, const MONITORINFOEXW& current, UINT dpi);
bool restore(HWND window, const Record& saved);
struct RestorePlan {
    RECT normal{};
    size_t displayIndex{};
    bool environmentChanged{},fallback{},crossScreenReduced{},visibilityCorrected{};
};
std::optional<RestorePlan> planRestore(const Record& saved,std::span<const Display> current,RECT currentWindow);

struct PreviewBounds {double left{},top{},right{},bottom{};};
struct PreviewDisplay {Display display;PreviewBounds monitor,work;};
struct DesktopPreview {
    RECT desktopBounds{};
    double scale{};
    std::vector<PreviewDisplay> displays;
    std::optional<RestorePlan> plan;
    std::optional<PreviewBounds> window;
    RECT windowBounds{};
};
// Physical desktop coordinates -> fitted canvas coordinates; never moves windows.
std::optional<DesktopPreview> buildDesktopPreview(const Record* record,std::span<const Display> displays,RECT currentWindow,RECT viewport);
// Enumerates connected desktop displays anew on every call, including hotplug.
std::optional<DesktopPreview> currentDesktopPreview(const Record* record,RECT currentWindow,RECT viewport);
enum class RestoreStatus { Restored,Adapted,Limited,Failed };
RestoreStatus restoreDetailed(HWND window,const Record& saved);
}
