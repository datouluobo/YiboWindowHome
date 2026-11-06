#include "state.h"
#include "window_ops.h"
#include "emulator_layout.h"
#include <shellscalingapi.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef YIBO_STATE_HANDLE_TRACE
void stateHandleTrace(const wchar_t* stage);
#define STATE_HANDLE_TRACE(stage) stateHandleTrace(stage)
#else
#define STATE_HANDLE_TRACE(stage) ((void)0)
#endif

namespace home {
namespace {
#ifdef YIBO_STATE_CRASH_TEST
void crashTestPoint(const wchar_t* stage) {
    wchar_t requested[64]{};
    if(GetEnvironmentVariableW(L"YIBO_STATE_TEST_CRASH_STAGE",requested,64)&&wcscmp(requested,stage)==0)
        TerminateProcess(GetCurrentProcess(),197);
}
#else
void crashTestPoint(const wchar_t*) {}
#endif
// Deliberately small, strict reader for our own versioned prototype JSON schema.
// Strings are ASCII JSON with every UTF-16 code unit escaped by the writer.
struct Reader {
    std::istream& in;
    void token(char c) { in >> std::ws; if (in.get() != c) throw std::runtime_error("JSON token"); }
    int number() { int n{}; if (!(in >> n)) throw std::runtime_error("JSON number"); return n; }
    bool boolean() {int value=number();if(value!=0&&value!=1)throw std::runtime_error("JSON boolean");return value==1;}
    std::wstring str() {
        token('"'); std::wstring s;
        for (;;) {
            int c = in.get();
            if (c == '"') return s;
            if (c < 32 || c > 126 || s.size() > 32768) throw std::runtime_error("JSON string");
            if (c == '\\') {
                c = in.get();
                if (c == 'u') {
                    int value{};
                    for (int i=0; i<4; ++i) {
                        int h=in.get(); int v=(h>='0'&&h<='9')?h-'0':(h>='a'&&h<='f')?h-'a'+10:(h>='A'&&h<='F')?h-'A'+10:-1;
                        if(v<0) throw std::runtime_error("JSON unicode");
                        value=value*16+v;
                    }
                    s+=static_cast<wchar_t>(value); continue;
                }
                if (c!='\\' && c!='"' && c!='/') throw std::runtime_error("JSON escape");
            }
            s+=static_cast<wchar_t>(c);
        }
    }
    void key(const wchar_t* expected) { if(str()!=expected) throw std::runtime_error("JSON key"); token(':'); }
    RECT rect() { token('['); RECT r{}; r.left=number(); token(','); r.top=number(); token(','); r.right=number(); token(','); r.bottom=number(); token(']'); return r; }
    Offset offset() {token('[');Offset o;o.custom=boolean();token(',');o.x=number();token(',');o.y=number();token(']');return o;}
};
std::string quote(const std::wstring& value) {
    std::ostringstream out; out << '"' << std::hex << std::setfill('0');
    for(wchar_t c:value) out << "\\u" << std::setw(4) << static_cast<unsigned>(c);
    out << '"'; return out.str();
}
void rect(std::ostream& out, RECT r) { out << '[' << r.left << ',' << r.top << ',' << r.right << ',' << r.bottom << ']'; }
void offset(std::ostream& out,const Offset& o) {out<<'['<<o.custom<<','<<o.x<<','<<o.y<<']';}
State read(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>4*1024*1024) throw std::runtime_error("oversize");
    std::ifstream in(path); Reader j{in}; State state;
    j.token('{'); j.key(L"prototypeSchema"); int schema=j.number();if(schema<1||schema>4) throw std::runtime_error("schema");
    j.token(','); j.key(L"offsetX"); state.offsetX=j.number();
    j.token(','); j.key(L"offsetY"); state.offsetY=j.number();
    if(state.offsetX < -500 || state.offsetX>500 || state.offsetY < -500 || state.offsetY>500) throw std::runtime_error("offset");
    j.token(','); j.key(L"records"); j.token('['); in>>std::ws;
    while(in.peek()!=']') {
        if(!state.records.empty()) j.token(',');
        j.token('{'); j.key(L"app"); auto key=j.str(); Record r;
        j.token(','); j.key(L"normal"); r.normal=j.rect();
        j.token(','); j.key(L"work"); r.work=j.rect();
        j.token(','); j.key(L"monitor"); r.monitor=j.rect();
        j.token(','); j.key(L"device"); r.device=j.str();
        j.token(','); j.key(L"dpi"); r.dpi=j.number();
        j.token(','); j.key(L"maximized"); int max=j.number(); r.maximized=max==1;
        auto valid=[](RECT a) { return a.left>=-100000 && a.top>=-100000 && a.right<=100000 && a.bottom<=100000 && a.right>a.left && a.bottom>a.top; };
        r.actual=r.normal;
        if(schema>=2) {
            j.token(',');j.key(L"actual");r.actual=j.rect();
            j.token(',');j.key(L"displayIdentity");r.displayIdentity=j.str();
            j.token(',');j.key(L"topology");j.token('[');in>>std::ws;
            while(in.peek()!=']') {
                if(!r.topology.empty())j.token(',');
                Display d;j.token('{');j.key(L"device");d.device=j.str();
                j.token(',');j.key(L"identity");d.identity=j.str();
                j.token(',');j.key(L"monitor");d.monitor=j.rect();
                j.token(',');j.key(L"work");d.work=j.rect();
                j.token(',');j.key(L"dpi");d.dpi=j.number();
                j.token(',');j.key(L"primary");int primary=j.number();d.primary=primary==1;j.token('}');
                if(d.device.empty()||d.device.find(L'\0')!=std::wstring::npos||d.identity.find(L'\0')!=std::wstring::npos||!valid(d.monitor)||!valid(d.work)||d.dpi<48||d.dpi>960||(primary!=0&&primary!=1)||r.topology.size()>=64)throw std::runtime_error("invalid display");
                r.topology.push_back(std::move(d));in>>std::ws;
            }
            j.token(']');
        }
        if(schema>=3){j.token(',');j.key(L"savedAt");r.savedAt=j.str();if(r.savedAt.size()>64)throw std::runtime_error("timestamp");}
        if(schema>=4){j.token(',');j.key(L"name");r.name=j.str();}
        j.token('}');
        if(!valid(r.actual)||r.displayIdentity.find(L'\0')!=std::wstring::npos)throw std::runtime_error("invalid actual rectangle");
        if(key.empty() || key.find(L'\0')!=std::wstring::npos || !valid(r.normal)||!valid(r.work)||!valid(r.monitor)||r.dpi<48||r.dpi>960||(max!=0&&max!=1)||state.records.size()>=4096||state.records.contains(key)) throw std::runtime_error("invalid record");
        state.records.emplace(std::move(key),r); in>>std::ws;
    }
    j.token(']');
    if(schema>=3) {
        j.token(',');j.key(L"applications");j.token('[');in>>std::ws;
        while(in.peek()!=']') {
            if(!state.applications.empty())j.token(',');
            Application app;j.token('{');j.key(L"id");app.id=j.str();
            j.token(',');j.key(L"name");app.name=j.str();j.token(',');j.key(L"path");app.path=j.str();
            j.token(',');j.key(L"aumid");app.aumid=j.str();j.token(',');j.key(L"enabled");app.enabled=j.boolean();
            j.token(',');j.key(L"normal");app.normal=j.offset();j.token(',');j.key(L"maximized");app.maximized=j.offset();
            j.token(',');j.key(L"rules");j.token('[');in>>std::ws;
            while(in.peek()!=']') {
                if(!app.rules.empty())j.token(',');
                WindowRule rule;j.token('{');j.key(L"id");rule.id=j.str();j.token(',');j.key(L"name");rule.name=j.str();
                j.token(',');j.key(L"class");rule.className=j.str();j.token(',');j.key(L"titleMatch");rule.titleMatch=static_cast<TitleMatch>(j.number());
                j.token(',');j.key(L"title");rule.title=j.str();j.token(',');j.key(L"exclude");rule.exclude=j.boolean();
                j.token(',');j.key(L"active");rule.active=j.boolean();j.token(',');j.key(L"normal");rule.normal=j.offset();
                j.token(',');j.key(L"maximized");rule.maximized=j.offset();j.token('}');
                if(app.rules.size()>=128)throw std::runtime_error("rule count");
                app.rules.push_back(std::move(rule));in>>std::ws;
            }
            j.token(']');j.token('}');if(state.applications.size()>=1024)throw std::runtime_error("application count");
            state.applications.push_back(std::move(app));in>>std::ws;
        }
        j.token(']');
    }
    if(schema>=4) {
        j.token(',');j.key(L"activeRecords");j.token('[');in>>std::ws;
        while(in.peek()!=']') {
            if(!state.activeRecords.empty())j.token(',');
            j.token('{');j.key(L"scope");auto scope=j.str();j.token(',');j.key(L"record");auto key=j.str();j.token('}');
            if(state.activeRecords.size()>=4096||!state.activeRecords.emplace(scope,key).second)throw std::runtime_error("active records");
            in>>std::ws;
        }
        j.token(']');
    }
    j.token('}'); in>>std::ws; if(!in.eof()) throw std::runtime_error("trailing data");
    if(schema<3)migrateLegacyApplications(state);
    validateApplications(state);
    return state;
}
}
State Store::load() {
    writable=true; warning.clear();
    auto backup=path; backup+=L".bak";
    bool primaryExists=std::filesystem::exists(path);
    if(!primaryExists&&!std::filesystem::exists(backup)) return {};
    if(primaryExists)try { return read(path); } catch(...) {}
    try { auto state=read(backup); warning=primaryExists?L"原型状态损坏，已读取备份；为保留故障文件，本次会话禁止写入。":L"原型状态缺失，已读取备份；为保留原有数据，本次会话禁止写入。"; writable=false; return state; } catch(...) {}
    writable=false; warning=primaryExists?L"原型状态及备份无法读取；已保留文件，本次会话禁止写入。":L"原型状态缺失且备份无法读取；已保留备份，本次会话禁止写入。"; return {};
}
bool Store::save(const State& state) {
    if(!writable) return false;
    try {
        STATE_HANDLE_TRACE(L"save-entry");
        auto writing=state;migrateLegacyApplications(writing);validateApplications(writing);
        std::filesystem::create_directories(path.parent_path());
        STATE_HANDLE_TRACE(L"save-validated-input");
        std::ostringstream out; out << "{\"prototypeSchema\":4,\"offsetX\":" << writing.offsetX << ",\"offsetY\":" << writing.offsetY << ",\"records\":[\n";
        bool comma=false;
        for(const auto& [key,r]:writing.records) {
            if(comma) out << ",\n"; comma=true;
            out << "{\"app\":" << quote(key) << ",\"normal\":"; rect(out,r.normal);
            out << ",\"work\":"; rect(out,r.work); out << ",\"monitor\":"; rect(out,r.monitor);
            out << ",\"device\":" << quote(r.device) << ",\"dpi\":" << r.dpi << ",\"maximized\":" << r.maximized;
            out << ",\"actual\":";rect(out,r.actual.right>r.actual.left&&r.actual.bottom>r.actual.top?r.actual:r.normal);
            out << ",\"displayIdentity\":" << quote(r.displayIdentity) << ",\"topology\":[";
            bool separator=false;
            for(auto& d:r.topology) {
                if(separator)out<<',';separator=true;
                out<<"{\"device\":"<<quote(d.device)<<",\"identity\":"<<quote(d.identity)<<",\"monitor\":";rect(out,d.monitor);
                out<<",\"work\":";rect(out,d.work);out<<",\"dpi\":"<<d.dpi<<",\"primary\":"<<d.primary<<'}';
            }
            out<<"],\"savedAt\":"<<quote(r.savedAt)<<",\"name\":"<<quote(r.name)<<'}';
        }
        out << "\n],\"applications\":[\n";bool appComma=false;
        for(auto& app:writing.applications) {
            if(appComma)out<<",\n";appComma=true;
            out<<"{\"id\":"<<quote(app.id)<<",\"name\":"<<quote(app.name)<<",\"path\":"<<quote(app.path)<<",\"aumid\":"<<quote(app.aumid)<<",\"enabled\":"<<app.enabled<<",\"normal\":";offset(out,app.normal);
            out<<",\"maximized\":";offset(out,app.maximized);out<<",\"rules\":[";bool ruleComma=false;
            for(auto& rule:app.rules) {
                if(ruleComma)out<<',';ruleComma=true;
                out<<"{\"id\":"<<quote(rule.id)<<",\"name\":"<<quote(rule.name)<<",\"class\":"<<quote(rule.className)<<",\"titleMatch\":"<<int(rule.titleMatch)<<",\"title\":"<<quote(rule.title)<<",\"exclude\":"<<rule.exclude<<",\"active\":"<<rule.active<<",\"normal\":";offset(out,rule.normal);
                out<<",\"maximized\":";offset(out,rule.maximized);out<<'}';
            }
            out<<"]}";
        }
        out << "\n],\"activeRecords\":[";bool activeComma=false;
        for(auto& [scope,key]:writing.activeRecords) {
            if(activeComma)out<<',';activeComma=true;out<<"{\"scope\":"<<quote(scope)<<",\"record\":"<<quote(key)<<'}';
        }
        out<<"]}\n"; auto bytes=out.str();if(bytes.size()>4*1024*1024)return false;
        STATE_HANDLE_TRACE(L"save-serialized");
        auto temp=path; temp+=L".tmp";
        // Validate the exact serialized state before replacing a readable file.
        // The normal writer emits fixed order, but callers can still supply bad data.
        // Validation uses the same reader below after the durable temporary write.
        HANDLE file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) return false;
        crashTestPoint(L"opened");
        DWORD written{}; bool ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size();
        if(ok)crashTestPoint(L"written");
        ok=ok&&FlushFileBuffers(file);
        CloseHandle(file); if(!ok) return false;
        STATE_HANDLE_TRACE(L"save-flushed");
        crashTestPoint(L"flushed");
        read(temp);
        STATE_HANDLE_TRACE(L"save-readback");
        crashTestPoint(L"validated");
        if(std::filesystem::exists(path)) {
            auto backup=path; backup+=L".bak";
            bool replaced=ReplaceFileW(path.c_str(),temp.c_str(),backup.c_str(),0,nullptr,nullptr)!=FALSE;
            if(replaced)crashTestPoint(L"committed");return replaced;
        }
        bool moved=MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;
        STATE_HANDLE_TRACE(L"save-moved");
        if(moved)crashTestPoint(L"committed");return moved;
    } catch(...) { return false; }
}
std::wstring positionScope(const std::wstring& key) {
    auto first=key.find(L'/');if(first==key.npos)return key;
    auto second=key.find(L'/',first+1);return second==key.npos?key:key.substr(0,second);
}
std::wstring activePositionKey(const State& state,const std::wstring& scope) {
    auto found=state.activeRecords.find(scope);return found==state.activeRecords.end()?scope:found->second;
}
const Record* activePositionRecord(const State& state,const std::wstring& scope) {
    auto found=state.records.find(activePositionKey(state,scope));return found==state.records.end()?nullptr:&found->second;
}
std::vector<PositionEntry> positionEntries(const State& state,const std::wstring& scope) {
    auto active=activePositionKey(state,scope);auto found=state.records.find(scope);
    std::vector<PositionEntry> entries{{scope,L"默认记录",true,active==scope,found==state.records.end()?nullptr:&found->second}};
    auto prefix=scope+L"/";
    for(auto it=state.records.lower_bound(prefix);it!=state.records.end()&&it->first.starts_with(prefix);++it)
        entries.push_back({it->first,it->second.name,false,it->first==active,&it->second});
    return entries;
}
namespace {
bool positionScopeExists(const State& state,const std::wstring& scope) {
    for(auto& app:state.applications) {
        if(scopeKey(app)==scope)return true;
        for(auto& rule:app.rules)if(scopeKey(app,rule.id)==scope)return true;
    }
    return false;
}
bool positionNameValid(const std::wstring& name) {
    return !name.empty()&&name.size()<=256&&name.find(L'\0')==name.npos&&
        std::any_of(name.begin(),name.end(),[](wchar_t ch){return !iswspace(ch);});
}
}
std::wstring savePositionRecord(State& state,const std::wstring& scope,Record record,bool createNew,const std::wstring& name) {
    if(!positionScopeExists(state,scope))throw std::runtime_error("unknown position scope");
    bool hadActive=activePositionRecord(state,scope)!=nullptr;
    std::wstring key=scope;
    if(createNew) {
        auto entries=positionEntries(state,scope);
        if(entries.size()>=128)throw std::runtime_error("position count");
        record.name=name.empty()?L"位置 "+std::to_wstring(entries.size()):name;
        if(!positionNameValid(record.name))throw std::runtime_error("position name");
        key=scope+L"/"+newId(L"p-");
    } else record.name=L"默认记录";
    if(!state.records.contains(key)&&state.records.size()>=4096)throw std::runtime_error("record count");
    state.records.insert_or_assign(key,std::move(record));
    if(!hadActive&&createNew)state.activeRecords[scope]=key;
    return key;
}
bool activatePositionRecord(State& state,const std::wstring& scope,const std::wstring& key) {
    if(!positionScopeExists(state,scope)||positionScope(key)!=scope||!state.records.contains(key))return false;
    if(key==scope)state.activeRecords.erase(scope);else state.activeRecords[scope]=key;
    return true;
}
bool renamePositionRecord(State& state,const std::wstring& key,const std::wstring& name) {
    auto found=state.records.find(key);
    if(found==state.records.end()||positionScope(key)==key||!positionNameValid(name))return false;
    found->second.name=name;return true;
}
bool erasePositionRecord(State& state,const std::wstring& key) {
    auto scope=positionScope(key);auto active=activePositionKey(state,scope);
    if(!state.records.erase(key))return false;
    if(active==key) {
        state.activeRecords.erase(scope);
        if(!state.records.contains(scope)) {
            auto prefix=scope+L"/";auto next=state.records.lower_bound(prefix);
            if(next!=state.records.end()&&next->first.starts_with(prefix))state.activeRecords[scope]=next->first;
        }
    }
    return true;
}
void erasePositionScope(State& state,const std::wstring& scope) {
    state.records.erase(scope);auto prefix=scope+L"/";
    for(auto it=state.records.lower_bound(prefix);it!=state.records.end()&&it->first.starts_with(prefix);)it=state.records.erase(it);
    state.activeRecords.erase(scope);
}
std::wstring appKey(HWND window) {
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); if(!process) return {};
    wchar_t path[32768]; DWORD size=32768;
    bool ok=QueryFullProcessImageNameW(process,0,path,&size)!=FALSE; CloseHandle(process); if(!ok) return {};
    std::wstring result(path,size); std::transform(result.begin(),result.end(),result.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));}); return result;
}
Record capture(HWND window) {
    Record r; WINDOWPLACEMENT wp{sizeof(wp)};
    SYSTEMTIME now{};GetSystemTime(&now);wchar_t timestamp[40]{};
    swprintf_s(timestamp,L"%04u-%02u-%02u %02u:%02u:%02u UTC",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond);r.savedAt=timestamp;
    if(!GetWindowPlacement(window,&wp)) throw std::runtime_error("placement");
    if(!GetWindowRect(window,&r.actual))throw std::runtime_error("bounds");
    r.topology=collectDisplays();auto selected=displayForRect(r.actual,r.topology);
    if(!selected)throw std::runtime_error("monitor");
    const auto& display=r.topology[*selected];
    r.normal=wp.rcNormalPosition;
    // WINDOWPLACEMENT uses workspace coordinates for ordinary top-level windows.
    if(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)) OffsetRect(&r.normal,display.work.left-display.monitor.left,display.work.top-display.monitor.top);
    if(!IsZoomed(window) && !IsIconic(window)) r.normal=r.actual;
    r.work=display.work;r.monitor=display.monitor;r.device=display.device;r.displayIdentity=display.identity;r.dpi=display.dpi;
    r.maximized=IsZoomed(window)!=FALSE; return r;
}
RestoreStatus restoreDetailed(HWND window,const Record& saved) {
    RECT before{};if(!GetWindowRect(window,&before))return RestoreStatus::Failed;
    DWORD_PTR response{};
    if(!SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&response))return RestoreStatus::Failed;
    auto displays=collectDisplays();auto plan=planRestore(saved,displays,before);if(!plan)return RestoreStatus::Failed;
    RECT target=plan->normal;auto& selected=displays[plan->displayIndex];
    auto path=describeWindow(window).identity.path;
    auto name=path.find_last_of(L"\\/");
    bool emulator=_wcsicmp(path.c_str()+(name==std::wstring::npos?0:name+1),L"Androws.exe")==0;
    if(emulator) {
        if((IsZoomed(window)||IsIconic(window))&&!boundedShowWindow(window,SW_RESTORE))return RestoreStatus::Failed;
        if(!boundedNativeResize(window,target))return RestoreStatus::Failed;
        if(!saved.maximized&&!finishNativeVerticalFit(window,target,selected.work))return RestoreStatus::Failed;
        RECT actual{};if(!GetWindowRect(window,&actual))return RestoreStatus::Failed;
        bool exact=abs(actual.left-target.left)<=2&&abs(actual.top-target.top)<=2&&abs(actual.right-target.right)<=2&&abs(actual.bottom-target.bottom)<=2;
        if(saved.maximized) {
            if(!boundedShowWindow(window,SW_MAXIMIZE)||!GetWindowRect(window,&actual))return RestoreStatus::Failed;
            if(!IsZoomed(window)||displayForRect(actual,displays)!=std::optional<size_t>(plan->displayIndex))return RestoreStatus::Limited;
        } else if(!exact)return RestoreStatus::Limited;
        return plan->environmentChanged||plan->fallback||plan->visibilityCorrected?RestoreStatus::Adapted:RestoreStatus::Restored;
    }
    if((IsZoomed(window)||IsIconic(window))&&!boundedShowWindow(window,SW_RESTORE))return RestoreStatus::Failed;
    UINT oldDpi=GetDpiForWindow(window);
    auto place=[&](RECT rect){
        return boundedWindowPos(window,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
    };
    if(!place(target))return RestoreStatus::Failed;
    RECT actual{};if(!GetWindowRect(window,&actual))return RestoreStatus::Failed;
    auto close=[](RECT a,RECT b){return abs(a.left-b.left)<=2&&abs(a.top-b.top)<=2&&abs(a.right-b.right)<=2&&abs(a.bottom-b.bottom)<=2;};
    bool limited=!close(actual,target);
    bool perMonitor=GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window))==DPI_AWARENESS_PER_MONITOR_AWARE;
    bool singleDisplayTarget=false;
    for(size_t i=0;i<displays.size();++i){RECT intersection{};if(IntersectRect(&intersection,&target,&displays[i].monitor)){if(i!=plan->displayIndex){singleDisplayTarget=false;break;}singleDisplayTarget=true;}}
    bool needsDpiBridge=perMonitor&&singleDisplayTarget&&GetDpiForWindow(window)!=selected.dpi;
    // One bounded correction after a DPI transition or an application's minimum
    // size adjustment; do not repeatedly fight the target's layout rules.
    bool unreachable=!hasReachableCaption(actual,displays);
    if(needsDpiBridge) {
        // A simultaneous move/full-work-area resize can retain the source DPI.
        // Use one small, contained intermediate placement to let Windows switch
        // DPI, then reapply the exact plan. Never do this to preserved spans.
        UINT sourceDpi=GetDpiForWindow(window);if(!sourceDpi)return RestoreStatus::Failed;
        LONG width=std::max(1,MulDiv((selected.work.right-selected.work.left)/2,std::min(sourceDpi,selected.dpi),selected.dpi));
        LONG height=std::max(1,MulDiv((selected.work.bottom-selected.work.top)/2,std::min(sourceDpi,selected.dpi),selected.dpi));
        RECT bridge{selected.work.left+(selected.work.right-selected.work.left-width)/2,selected.work.top+(selected.work.bottom-selected.work.top-height)/2,0,0};bridge.right=bridge.left+width;bridge.bottom=bridge.top+height;
        if(!place(bridge)||!place(target)||!GetWindowRect(window,&actual))return RestoreStatus::Failed;
        limited=!close(actual,target)||GetDpiForWindow(window)!=selected.dpi;
    } else if(unreachable||(GetDpiForWindow(window)!=oldDpi&&!close(actual,target))) {
        DWORD_PTR barrier{};if(!SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&barrier))return RestoreStatus::Failed;
        RECT corrected=unreachable?makeCaptionReachable(actual,selected):target;
        if(!place(corrected)||!GetWindowRect(window,&actual))return RestoreStatus::Failed;
        limited=!close(actual,target);
    }
    if(saved.maximized) {
        // Ensure the ordinary placement selects the intended screen before
        // maximizing, including a saved ordinary rectangle that crossed screens.
        auto at=displayForRect(actual,displays);
        if(!at||*at!=plan->displayIndex) {
            RECT centered=actual;LONG width=actual.right-actual.left,height=actual.bottom-actual.top;
            centered.left=selected.work.left+(selected.work.right-selected.work.left-width)/2;
            centered.top=selected.work.top+(selected.work.bottom-selected.work.top-height)/2;
            centered.right=centered.left+width;centered.bottom=centered.top+height;
            if(!place(centered))return RestoreStatus::Failed;
        }
        if(!boundedShowWindow(window,SW_MAXIMIZE))return RestoreStatus::Failed;
        if(!IsZoomed(window))return RestoreStatus::Limited;
        RECT bounds{};if(!GetWindowRect(window,&bounds))return RestoreStatus::Failed;
        if(displayForRect(bounds,displays)!=std::optional<size_t>(plan->displayIndex))return RestoreStatus::Limited;
        limited=limited||(perMonitor&&GetDpiForWindow(window)!=selected.dpi);
    } else limited=limited||!close(actual,target)||!hasReachableCaption(actual,displays);
    if(limited)return RestoreStatus::Limited;
    return plan->environmentChanged||plan->fallback||plan->visibilityCorrected?RestoreStatus::Adapted:RestoreStatus::Restored;
}
bool restore(HWND window,const Record& saved) {
    auto status=restoreDetailed(window,saved);return status==RestoreStatus::Restored||status==RestoreStatus::Adapted;
}
}
