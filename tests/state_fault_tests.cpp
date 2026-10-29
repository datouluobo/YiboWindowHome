#include "state.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
std::string bytes(const std::filesystem::path& p){std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& p,const std::string& s){std::ofstream out(p,std::ios::binary);out<<s;}
bool child(const std::filesystem::path& path,const wchar_t* mode,DWORD expected=0){
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=L"\""+std::wstring(exe)+L"\" "+mode+L" \""+path.wstring()+L"\"";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    DWORD code{};bool done=WaitForSingleObject(process.hProcess,10000)==WAIT_OBJECT_0;if(!done){TerminateProcess(process.hProcess,90);WaitForSingleObject(process.hProcess,1000);}bool ok=done&&GetExitCodeProcess(process.hProcess,&code)&&code==expected;
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return ok;
}
home::State fixture(){
    home::State s;home::Application a;a.id=L"a-1";a.name=L"测试 \"应用\" \\ \U0001f680";a.path=L"C:\\tests\\app.exe";
    home::WindowRule r;r.id=L"r-1";r.name=L"窗口";r.className=L"TestClass";r.titleMatch=home::TitleMatch::Contains;r.title=L"包含\n制表\t反斜杠\\引号\"";a.rules.push_back(r);s.applications.push_back(a);
    home::Record rec;rec.normal={-1200,-500,-400,100};rec.actual=rec.normal;rec.monitor={-1920,-1080,0,0};rec.work={-1920,-1040,0,0};rec.device=L"DISPLAY1";rec.displayIdentity=L"panel-a";rec.dpi=144;rec.savedAt=L"2026-10-27 UTC";
    rec.topology={{rec.device,rec.displayIdentity,rec.monitor,rec.work,144,true}};s.records[home::scopeKey(a)]=rec;return s;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring(argv[1])==L"--inspect-state"){home::Store s{argv[2]};auto data=s.load();std::cout<<"Read-only state inspection: writable="<<s.writable<<" warning="<<!s.warning.empty()<<" applications="<<data.applications.size()<<" records="<<data.records.size()<<'\n';return s.writable?0:3;}
    if(argc==3&&std::wstring(argv[1]).rfind(L"--crash-",0)==0){auto stage=std::wstring(argv[1]).substr(8);SetEnvironmentVariableW(L"YIBO_STATE_TEST_CRASH_STAGE",stage.c_str());home::Store s{argv[2]};auto data=fixture();data.offsetX=-88;return s.save(data)?0:4;}
    if(argc==3){home::Store childStore{argv[2]};auto childState=childStore.load();if(!childStore.writable||childState.offsetX!=-40||childState.records.size()!=1||childState.applications.size()!=1||childState.applications[0].name!=fixture().applications[0].name)return 3;
        if(std::wstring(argv[1])==L"--child-read")return 0;if(std::wstring(argv[1])==L"--child-update"){childState.offsetX=-55;return childStore.save(childState)?0:4;}return 2;}
    if(argc!=2)return 2;std::cout<<std::unitbuf;int failures{};
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    auto dir=std::filesystem::path(argv[1]);std::filesystem::create_directories(dir);
    auto crashDir=dir/(L"crash-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(crashDir);
    auto absentPath=crashDir/L"missing-primary.json";home::Store backupOnly{std::filesystem::path(absentPath.wstring()+L".bak")};auto backupFixture=fixture();backupFixture.offsetX=-71;
    bool backupReady=backupOnly.save(backupFixture);auto backupBytes=bytes(backupOnly.path);home::Store missingPrimary{absentPath};auto fromBackup=missingPrimary.load();
    check(backupReady&&!missingPrimary.writable&&!missingPrimary.warning.empty()&&fromBackup.offsetX==-71&&fromBackup.records.size()==1&&!missingPrimary.save(fixture())&&!std::filesystem::exists(absentPath)&&bytes(backupOnly.path)==backupBytes,"missing primary with valid backup recovers protected state without creating an empty replacement");
    auto badAbsent=crashDir/L"missing-primary-bad-backup.json";auto badBackup=std::filesystem::path(badAbsent.wstring()+L".bak");write(badBackup,"invalid backup");home::Store badMissing{badAbsent};auto noRecovery=badMissing.load();
    check(!badMissing.writable&&!badMissing.warning.empty()&&noRecovery.records.empty()&&!badMissing.save(fixture())&&!std::filesystem::exists(badAbsent)&&bytes(badBackup)=="invalid backup","missing primary with invalid backup preserves evidence and blocks replacement");
    auto lockedBackupPath=crashDir/L"backup-sharing.json";home::Store backupSharing{lockedBackupPath};auto prior=fixture();auto newer=prior;newer.offsetX=-21;
    bool seeded=backupSharing.save(prior)&&backupSharing.save(newer);auto primaryBefore=bytes(lockedBackupPath);auto lockedBackup=std::filesystem::path(lockedBackupPath.wstring()+L".bak");auto backupBefore=bytes(lockedBackup);
    HANDLE backupHandle=CreateFileW(lockedBackup.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    bool blocked=seeded&&backupHandle!=INVALID_HANDLE_VALUE&&!backupSharing.save(prior)&&bytes(lockedBackupPath)==primaryBefore&&bytes(lockedBackup)==backupBefore;
    if(backupHandle!=INVALID_HANDLE_VALUE)CloseHandle(backupHandle);
    check(blocked,"locked backup prevents replacement while preserving primary and backup bytes");
    auto afterBlocked=backupSharing.load();bool retried=backupSharing.writable&&afterBlocked.offsetX==-21&&backupSharing.save(prior);
    check(retried&&backupSharing.load().offsetX==0&&bytes(lockedBackup)==primaryBefore,"retry after releasing backup lock commits and backs up the previous primary");
    for(auto stage:{L"opened",L"written",L"flushed",L"validated",L"committed"})for(bool existed:{false,true}){
        auto testPath=crashDir/(std::wstring(stage)+(existed?L"-existing.json":L"-first.json"));home::Store original{testPath};auto initial=fixture();
        bool prepared=!existed||original.save(initial);auto originalBytes=existed?bytes(testPath):std::string{};
        auto option=std::wstring(L"--crash-")+stage;bool killed=prepared&&child(testPath,option.c_str(),197);
        home::Store restarted{testPath};auto recovered=restarted.load();bool committed=std::wstring(stage)==L"committed";
        bool expectedState=committed?(restarted.writable&&recovered.offsetX==-88&&recovered.records.size()==1):(existed?(restarted.writable&&bytes(testPath)==originalBytes&&recovered.offsetX==0):(!std::filesystem::exists(testPath)&&restarted.writable&&recovered.records.empty()));
        bool backupOk=!committed||!existed||bytes(testPath.wstring()+L".bak")==originalBytes;
        std::string label="abrupt process termination at ";for(auto p=stage;*p;++p)label+=char(*p);label+=existed?" with prior state":" on first save";
        check(killed&&expectedState&&backupOk,label.c_str());
    }
    auto path=dir/L"faults.json";home::Store store{path};auto state=fixture();
    check(store.save(state),"fault fixture saves in isolated directory");auto good=bytes(path);
    auto loaded=store.load();
    check(store.writable&&loaded.applications[0].name==state.applications[0].name&&loaded.applications[0].rules[0].title==state.applications[0].rules[0].title,"escaped controls quotes backslashes and surrogate pair round trip");
    auto replacement=state;replacement.offsetX=-40;
    HANDLE held=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    check(held!=INVALID_HANDLE_VALUE,"exclusive replacement-denial fixture opens");
    bool denied=!store.save(replacement)&&bytes(path)==good;if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    check(denied,"sharing violation cannot replace valid state or claim save success");
    check(store.save(replacement),"save succeeds after sharing violation is released");
    check(bytes(path.wstring()+L".bak")==good,"retry retains exact prior valid backup");
    auto latest=bytes(path);
    auto temp=std::filesystem::path(path.wstring()+L".tmp");write(temp,"held temporary output");
    held=CreateFileW(temp.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
    denied=held!=INVALID_HANDLE_VALUE&&!store.save(state)&&bytes(path)==latest;
    if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    check(denied,"unwritable temporary file preserves authoritative state");
    write(temp,"interrupted transaction");loaded=store.load();
    check(store.writable&&loaded.offsetX==-40&&bytes(temp)=="interrupted transaction","orphan temporary transaction is ignored at load without deleting evidence");
    check(child(path,L"--child-read"),"fresh process reads durable records offsets and Unicode identity after restart");
    bool updated=child(path,L"--child-update");loaded=store.load();
    check(updated&&store.writable&&loaded.offsetX==-55&&loaded.records.size()==1,"separate process update becomes visible through durable reload");
    auto disabled=loaded;disabled.applications[0].enabled=false;disabled.applications[0].rules[0].active=false;disabled.records[home::scopeKey(disabled.applications[0],disabled.applications[0].rules[0].id)]=state.records.begin()->second;
    home::Store disabledStore{dir/L"disabled.json"};bool disabledSaved=disabledStore.save(disabled);auto disabledRead=disabledStore.load();
    check(disabledSaved&&!disabledRead.applications[0].enabled&&!disabledRead.applications[0].rules[0].active&&disabledRead.records.size()==2,"disabled application and inactive window rule preserve both durable record scopes");
    auto malformed=dir/L"malformed.json";bool truncations=true;size_t tested{};
    for(size_t n=0;n<good.size();n+=std::max<size_t>(1,good.size()/128)){
        write(malformed,good.substr(0,n));home::Store broken{malformed};auto ignored=broken.load();
        truncations=truncations&&!broken.writable&&!broken.save(state)&&bytes(malformed)==good.substr(0,n);++tested;
    }
    check(truncations,"systematic truncated JSON inputs remain protected and unchanged");std::cout<<"Truncation boundaries: "<<tested<<'\n';
    auto malformedCase=[&](std::string text){write(malformed,text);home::Store broken{malformed};broken.load();return !broken.writable&&!broken.save(state)&&bytes(malformed)==text;};
    auto mutate=[&](std::string from,std::string to){auto s=good;auto p=s.find(from);if(p==s.npos)return false;s.replace(p,from.size(),to);return malformedCase(s);};
    check(malformedCase(good+" unexpected"),"trailing garbage is rejected without rewriting");
    check(mutate("\"offsetX\":0","\"offsetX\":2147483648"),"overflowing JSON integer is rejected");
    check(mutate("\"dpi\":144","\"dpi\":0"),"zero record DPI is rejected");
    check(mutate("\"enabled\":1","\"enabled\":2"),"nonboolean application state is rejected");
    check(mutate("\"titleMatch\":1","\"titleMatch\":9"),"unknown title-match enum is rejected");
    check(mutate("\\u6d4b","\\uQQQQ"),"invalid Unicode escape is rejected");
    check(mutate("\"prototypeSchema\":4","\"prototypeSchema\":5"),"future schema without backup is protected");
    auto oversized=dir/L"oversized.json";write(oversized,std::string(4*1024*1024+1,' '));home::Store huge{oversized};huge.load();
    check(!huge.writable&&!huge.save(state)&&std::filesystem::file_size(oversized)==4*1024*1024+1,"oversized input is protected by file limit");
    // Both current and backup are corrupt; load must preserve both independently.
    write(path,"bad current");write(path.wstring()+L".bak","bad backup");loaded=store.load();
    check(!store.writable&&loaded.records.empty()&&!store.warning.empty()&&!store.save(state)&&bytes(path)=="bad current"&&bytes(path.wstring()+L".bak")=="bad backup","dual corruption preserves both files and disables all writes");
    write(path,good);write(path.wstring()+L".bak","bad backup");loaded=store.load();
    check(store.writable&&loaded.records.size()==1,"valid primary is independent of corrupt backup");
    auto invalid=state;invalid.records[L"a-1/missing"]=invalid.records.begin()->second;
    check(!store.save(invalid)&&bytes(path)==good,"unbound record cannot replace valid state");
    invalid=state;invalid.applications.push_back(invalid.applications[0]);
    check(!store.save(invalid)&&bytes(path)==good,"duplicate IDs and identities cannot replace valid state");
    home::State maximum;
    for(int i=0;i<1024;++i){home::Application a;a.id=L"a-"+std::to_wstring(i);a.name=L"Application "+std::to_wstring(i);a.path=L"C:\\scale\\app"+std::to_wstring(i)+L".exe";
        for(int j=0;j<3;++j){home::WindowRule r;r.id=L"r-"+std::to_wstring(i)+L"-"+std::to_wstring(j);r.name=L"Rule";r.className=L"Class"+std::to_wstring(j);a.rules.push_back(r);}
        maximum.records[home::scopeKey(a)]=state.records.begin()->second;for(auto& r:a.rules)maximum.records[home::scopeKey(a,r.id)]=state.records.begin()->second;maximum.applications.push_back(std::move(a));
    }
    home::Store scale{dir/L"maximum.json"};bool saved=scale.save(maximum);auto maxRead=scale.load();
    check(saved&&scale.writable&&maxRead.applications.size()==1024&&maxRead.records.size()==4096,"1024 applications and 4096 independent records save and reload at capacity");
    auto maxBytes=bytes(scale.path);home::Application extra;extra.id=L"a-extra";extra.name=L"Extra";extra.path=L"C:\\scale\\extra.exe";maximum.applications.push_back(extra);
    check(!scale.save(maximum)&&bytes(scale.path)==maxBytes,"1025th application cannot overwrite capacity state");maximum.applications.pop_back();
    home::WindowRule extraRule;extraRule.id=L"r-extra";extraRule.name=L"Extra";extraRule.className=L"Extra";maximum.applications[0].rules.push_back(extraRule);maximum.records[home::scopeKey(maximum.applications[0],extraRule.id)]=state.records.begin()->second;
    check(!scale.save(maximum)&&bytes(scale.path)==maxBytes,"4097th bound record cannot overwrite capacity state");
    auto ruleLimit=state;auto& rules=ruleLimit.applications[0].rules;rules.clear();
    for(int i=0;i<128;++i){auto r=extraRule;r.id=L"r-"+std::to_wstring(i);rules.push_back(r);}
    home::Store ruleStore{dir/L"rule-limit.json"};saved=ruleStore.save(ruleLimit);loaded=ruleStore.load();
    check(saved&&loaded.applications[0].rules.size()==128,"128 rules round trip at supported limit");auto ruleBytes=bytes(ruleStore.path);rules.push_back(extraRule);
    check(!ruleStore.save(ruleLimit)&&bytes(ruleStore.path)==ruleBytes,"129th rule cannot replace valid state");
    auto large=ruleLimit;large.applications[0].rules.resize(128);for(auto& r:large.applications[0].rules){r.titleMatch=home::TitleMatch::Contains;r.title=std::wstring(4096,L'中');}
    auto second=large.applications[0];second.id=L"a-second";second.path=L"C:\\tests\\second.exe";for(auto& r:second.rules)r.id+=L"-second";large.applications.push_back(second);
    check(!ruleStore.save(large)&&bytes(ruleStore.path)==ruleBytes,"serialized output over four MiB is rejected before replacing good state");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
