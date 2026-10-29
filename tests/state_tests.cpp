#include "state.h"
#include <fstream>
#include <iostream>
#include <sstream>

std::string bytes(const std::filesystem::path& path) {std::ifstream input(path);return {std::istreambuf_iterator<char>(input),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    auto directory=std::filesystem::path(argv[1]);std::filesystem::create_directories(directory);
    int failures{};auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    auto path=directory/L"legacy.json";
    const std::string legacy="{\"prototypeSchema\":1,\"offsetX\":-7,\"offsetY\":3,\"records\":[{\"app\":\"c:\\\\app.exe\",\"normal\":[100,200,900,800],\"work\":[0,0,1920,1040],\"monitor\":[0,0,1920,1080],\"device\":\"DISPLAY1\",\"dpi\":96,\"maximized\":0}]}";
    {std::ofstream out(path);out<<legacy;}
    home::Store store{path};auto state=store.load();
    check(store.writable&&state.offsetX==-7&&state.records.size()==1&&state.records.begin()->second.topology.empty(),"schema 1 records remain readable");
    check(bytes(path)==legacy,"loading legacy data does not rewrite it");
    auto legacyTwo=legacy;auto version=legacyTwo.find("\"prototypeSchema\":1");legacyTwo.replace(version,19,"\"prototypeSchema\":2");
    auto afterMax=legacyTwo.find("\"maximized\":0")+13;
    legacyTwo.insert(afterMax,",\"actual\":[100,200,900,800],\"displayIdentity\":\"panel-one\",\"topology\":[{\"device\":\"DISPLAY1\",\"identity\":\"panel-one\",\"monitor\":[0,0,1920,1080],\"work\":[0,0,1920,1040],\"dpi\":96,\"primary\":1}]");
    auto twoPath=directory/L"schema-two.json";{std::ofstream output(twoPath);output<<legacyTwo;}
    home::Store secondGeneration{twoPath};auto secondState=secondGeneration.load();
    check(secondGeneration.writable&&secondState.applications.size()==1&&secondState.records.size()==1&&secondState.records.begin()->second.displayIdentity==L"panel-one"&&secondState.records.begin()->second.topology.size()==1,"schema 2 topology migrates into application default scope");
    check(bytes(twoPath)==legacyTwo&&secondState.records.begin()->first==home::scopeKey(secondState.applications[0]),"schema 2 identity migration is in memory until next successful save");
    check(store.save(state)&&bytes(path).find("\"prototypeSchema\":4")!=std::string::npos,"next successful save upgrades to schema 4");
    check(bytes(path.wstring()+L".bak")==legacy,"schema upgrade keeps exact previous state as backup");
    auto upgraded=store.load();
    check(store.writable&&upgraded.records.size()==1&&upgraded.offsetY==3&&upgraded.records.begin()->second.normal.bottom==800,"upgraded legacy geometry and offsets round trip");
    auto& record=state.records.begin()->second;
    record.displayIdentity=L"panel-\u663e\u793a\u5668";record.actual={-1920,0,0,1080};record.maximized=true;
    record.topology={{L"DISPLAY1",record.displayIdentity,{0,0,1920,1080},{0,40,1920,1080},120,true},{L"DISPLAY2",L"panel-b",{-1920,0,0,1080},{-1920,0,0,1040},144,false}};
    check(store.save(state),"schema 3 topology transaction saves");
    auto restored=store.load();auto& result=restored.records.begin()->second;
    check(result.displayIdentity==record.displayIdentity&&result.maximized&&EqualRect(&record.actual,&result.actual)&&home::sameTopology(record.topology,result.topology),"Unicode identities, actual bounds and full topology round trip");
    auto valid=bytes(path);
    auto invalidState=state;invalidState.records.begin()->second.topology.front().dpi=0;
    check(!store.save(invalidState)&&bytes(path)==valid,"invalid display metadata cannot replace readable state");
    auto future=valid;auto at=future.find("\"prototypeSchema\":4");future.replace(at,19,"\"prototypeSchema\":9");
    {std::ofstream out(path);out<<future;}
    auto protectedState=store.load();
    check(!store.writable&&!store.warning.empty()&&!store.save(state)&&bytes(path)==future,"future schema remains protected even when a backup is readable");
    check(!protectedState.records.empty(),"protected future file can expose the last valid backup");
    auto invalid=directory/L"invalid.json";{std::ofstream out(invalid);out<<"{broken";}
    home::Store broken{invalid};broken.load();
    check(!broken.writable&&!broken.save(state)&&bytes(invalid)=="{broken","unrecoverable state is preserved without overwriting");
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
