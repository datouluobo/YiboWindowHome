#include "state.h"
#include <fstream>
#include <iostream>

std::string bytes(const std::filesystem::path& p) {std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    home::State state;auto& app=home::ensureApplication(state,{L"C:\\positions\\app.exe",L""});
    auto scope=home::scopeKey(app);home::WindowRule rule;rule.id=home::newId(L"r-");rule.name=L"Editor";rule.className=L"Editor";app.rules.push_back(rule);
    auto ruleScope=home::scopeKey(app,rule.id);
    home::Record original;original.normal={100,100,900,700};original.actual=original.normal;original.work={0,0,1920,1040};original.monitor={0,0,1920,1080};original.device=L"DISPLAY1";original.savedAt=L"2026-10-29 00:00:00 UTC";
    auto entries=home::positionEntries(state,scope);
    check(entries.size()==1&&entries[0].isDefault&&entries[0].active&&!entries[0].record,"empty scope exposes a permanent default slot");
    home::savePositionRecord(state,scope,original,false);
    auto reading=original;OffsetRect(&reading.normal,200,100);reading.actual=reading.normal;
    auto key=home::savePositionRecord(state,scope,reading,true,L"阅读布局");
    auto other=home::savePositionRecord(state,scope,reading,true,L"副屏参考");
    check(state.records.size()==3&&key!=other&&home::activePositionKey(state,scope)==scope&&EqualRect(&state.records.at(scope).normal,&original.normal),"new positions have stable distinct keys and preserve default and activation");
    check(home::activatePositionRecord(state,scope,key)&&home::activePositionRecord(state,scope)==&state.records.at(key),"single selection resolves the chosen left-click record");
    auto replacement=original;OffsetRect(&replacement.normal,40,20);replacement.actual=replacement.normal;
    home::savePositionRecord(state,scope,replacement,false);
    check(home::activePositionKey(state,scope)==key&&EqualRect(&state.records.at(scope).normal,&replacement.normal)&&EqualRect(&state.records.at(key).normal,&reading.normal),"ordinary save always updates default while another position stays active");
    home::savePositionRecord(state,ruleScope,original,false);
    auto ruleKey=home::savePositionRecord(state,ruleScope,reading,true,L"规则位置");
    check(!home::activatePositionRecord(state,scope,ruleKey)&&home::activatePositionRecord(state,ruleScope,ruleKey)&&home::activePositionKey(state,scope)==key,"rule scopes have independent activation and reject cross-scope selection");
    check(home::renamePositionRecord(state,key,L"阅读窗口")&&!home::renamePositionRecord(state,scope,L"改名")&&!home::renamePositionRecord(state,key,L"  "),"custom records can be renamed while default identity and valid labels remain enforced");
    home::Store store{std::filesystem::path(argv[1])/L"multi-records.json"};check(store.save(state),"schema 4 commits multi-position state atomically");
    auto loaded=store.load();
    check(store.writable&&loaded.records.size()==5&&home::activePositionKey(loaded,scope)==key&&home::activePositionKey(loaded,ruleScope)==ruleKey&&loaded.records.at(key).name==L"阅读窗口","names, records and independent active selections survive restart");
    auto good=bytes(store.path);auto invalid=state;invalid.activeRecords[scope]=ruleKey;
    check(!store.save(invalid)&&bytes(store.path)==good,"cross-scope activation cannot overwrite readable state");
    invalid=state;invalid.activeRecords[scope]=scope+L"/p-missing";
    check(!store.save(invalid)&&bytes(store.path)==good,"dangling activation is rejected transactionally");
    invalid=state;invalid.records.at(key).name=L"";
    check(!store.save(invalid)&&bytes(store.path)==good,"unnamed additional position is rejected");
    auto before=state.records.size();bool rejected=false;
    try{home::savePositionRecord(state,L"missing/default",original,true);}catch(...){rejected=true;}
    check(rejected&&state.records.size()==before,"unknown scopes do not mutate draft state");
    home::erasePositionRecord(state,key);
    check(home::activePositionKey(state,scope)==scope&&state.records.contains(other),"removing active custom position returns to default without touching siblings");
    home::erasePositionRecord(state,scope);
    check(home::activePositionKey(state,scope)==other&&home::activePositionRecord(state,scope),"removing default contents selects a remaining position");
    home::erasePositionScope(state,ruleScope);
    check(!state.records.contains(ruleScope)&&!state.records.contains(ruleKey)&&!state.activeRecords.contains(ruleScope)&&state.records.contains(other),"removing a rule scope clears its positions and activation only");
    home::erasePositionScope(state,scope);home::savePositionRecord(state,scope,original,true,L"首条位置");
    check(home::activePositionRecord(state,scope)&&home::activePositionKey(state,scope)!=scope,"first new position becomes usable when default is empty");
    auto bounded=state;while(home::positionEntries(bounded,scope).size()<128)home::savePositionRecord(bounded,scope,original,true);
    auto boundedSize=bounded.records.size();rejected=false;try{home::savePositionRecord(bounded,scope,original,true);}catch(...){rejected=true;}
    check(rejected&&bounded.records.size()==boundedSize,"per-scope limit reserves default slot and rejects excess without dropping records");
    // A real schema 3 file stays unchanged on load and maps its existing slot to default.
    auto legacyPath=std::filesystem::path(argv[1])/L"schema-three.json";
    {std::ofstream out(legacyPath);out<<"{\"prototypeSchema\":3,\"offsetX\":0,\"offsetY\":0,\"records\":[{\"app\":\"a-old/default\",\"normal\":[100,100,900,700],\"work\":[0,0,1920,1040],\"monitor\":[0,0,1920,1080],\"device\":\"DISPLAY1\",\"dpi\":96,\"maximized\":0,\"actual\":[100,100,900,700],\"displayIdentity\":\"\",\"topology\":[],\"savedAt\":\"\"}],\"applications\":[{\"id\":\"a-old\",\"name\":\"Old\",\"path\":\"C:\\\\old.exe\",\"aumid\":\"\",\"enabled\":1,\"normal\":[0,0,0],\"maximized\":[0,0,0],\"rules\":[]}]}";}
    auto legacyBytes=bytes(legacyPath);home::Store old{legacyPath};auto migrated=old.load();
    check(old.writable&&home::activePositionRecord(migrated,L"a-old/default")&&bytes(legacyPath)==legacyBytes,"schema 3 loads its original position as active default without rewriting");
    check(old.save(migrated)&&bytes(legacyPath.wstring()+L".bak")==legacyBytes&&old.load().records.size()==1,"schema 3 upgrades on successful save with exact prior backup");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
