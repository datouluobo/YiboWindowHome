#include "state.h"
#include <algorithm>
#include <iostream>
#include <random>

int main(){
    int failures{};auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
    constexpr unsigned seed=0x5949424f;std::mt19937 random(seed);auto draw=[&](int low,int high){return std::uniform_int_distribution<int>(low,high)(random);};
    bool unchanged=true,changed=true,roundtrip=true,immutable=true,enumeration=true;size_t generated{};
    for(int n=0;n<10000;++n){
        UINT dpi=UINT(draw(2,8)*48);LONG left=draw(-8000,4000),top=draw(-4000,2000),width=draw(1280,3840),height=draw(720,2160);
        home::Display a{L"DISPLAY1",L"panel-a",{left,top,left+width,top+height},{left,top+draw(0,60),left+width,top+height-40},dpi,true};
        home::Display b{L"DISPLAY2",L"panel-b",{left+width,top,left+width+1920,top+1080},{left+width,top,left+width+1920,top+1040},96,false};
        std::vector<home::Display> screens{a,b};home::Record r;r.monitor=a.monitor;r.work=a.work;r.device=a.device;r.displayIdentity=a.identity;r.dpi=a.dpi;r.topology=screens;
        LONG w=draw(100,std::min<LONG>(1200,width)),h=draw(100,std::min<LONG>(650,a.work.bottom-a.work.top));
        LONG x=draw(a.work.left,a.work.right-w),y=draw(a.work.top,a.work.bottom-h);r.normal={x,y,x+w,y+h};r.actual=r.normal;
        auto original=r;RECT current{b.work.left+10,b.work.top+10,b.work.left+500,b.work.top+400};auto p=home::planRestore(r,screens,current);
        unchanged=unchanged&&p&&!p->environmentChanged&&!p->fallback&&EqualRect(&p->normal,&r.normal);
        auto reordered=screens;std::reverse(reordered.begin(),reordered.end());p=home::planRestore(r,reordered,current);
        enumeration=enumeration&&p&&!p->environmentChanged&&p->displayIndex==1&&EqualRect(&p->normal,&r.normal);
        auto altered=screens;auto& d=altered[0];LONG nx=draw(-8000,4000),ny=draw(-4000,2000),nw=draw(800,3840),nh=draw(600,2160);
        d.monitor={nx,ny,nx+nw,ny+nh};d.work={nx,ny+draw(0,60),nx+nw,ny+nh-40};d.dpi=UINT(draw(2,8)*48);
        p=home::planRestore(r,altered,current);
        changed=changed&&p&&p->displayIndex==0&&p->environmentChanged&&p->normal.left>=d.work.left&&p->normal.top>=d.work.top&&p->normal.right<=d.work.right&&p->normal.bottom<=d.work.bottom&&home::hasReachableCaption(p->normal,altered);
        auto detached=std::vector<home::Display>{b};p=home::planRestore(r,detached,current);
        changed=changed&&p&&p->fallback&&home::hasReachableCaption(p->normal,detached);
        p=home::planRestore(r,screens,current);roundtrip=roundtrip&&p&&EqualRect(&p->normal,&original.normal)&&!p->environmentChanged;
        immutable=immutable&&EqualRect(&r.normal,&original.normal)&&r.displayIdentity==original.displayIdentity&&home::sameTopology(r.topology,original.topology);++generated;
    }
    check(unchanged,"10000 generated unchanged environments retain exact physical geometry");
    check(enumeration,"10000 reordered enumerations preserve physical anchor and geometry");
    check(changed,"20000 changed or disconnected environments retain reachable captions within selected work area");
    check(roundtrip,"10000 detach/reconnect sequences recover original record without drift");
    check(immutable,"all generated recovery plans preserve source records and topologies");
    // Independent expectation: any active matching exclusion wins; otherwise
    // the first matching active inclusion selects the scope and state offset.
    bool matching=true,inheritance=true;
    for(int n=0;n<10000;++n){
        home::State s;s.offsetX=-3;s.offsetY=2;home::Application app;app.id=L"a-1";app.name=L"App";app.path=L"C:\\test\\app.exe";app.enabled=draw(0,4)!=0;
        app.normal={draw(0,1)!=0,-20,4};app.maximized={draw(0,1)!=0,-40,8};
        for(int i=0;i<12;++i){home::WindowRule r;r.id=L"r-"+std::to_wstring(i);r.name=L"Rule";r.className=draw(0,1)?L"Match":L"Other";r.active=draw(0,3)!=0;r.exclude=draw(0,4)==0;r.normal={draw(0,1)!=0,-100-i,i};r.maximized={draw(0,1)!=0,-200-i,-i};app.rules.push_back(r);}
        home::WindowMatch window;window.identity.path=app.path;window.className=L"Match";window.maximized=draw(0,1)!=0;s.applications.push_back(app);
        bool denied=!app.enabled;const home::WindowRule* first{};
        for(auto& r:app.rules)if(r.active&&r.className==window.className){if(r.exclude)denied=true;else if(!first)first=&r;}
        auto p=home::resolveWindow(s,window);matching=matching&&(p.allowed==!denied);
        if(!denied){matching=matching&&p.recordKey==home::scopeKey(app,first?first->id:L"");home::Offset expected{true,s.offsetX,s.offsetY};auto a=window.maximized?app.maximized:app.normal;if(a.custom)expected=a;if(first){auto r=window.maximized?first->maximized:first->normal;if(r.custom)expected=r;}
            inheritance=inheritance&&p.x==expected.x&&p.y==expected.y;}
    }
    check(matching,"10000 generated rule sets honor disable exclusion and stable inclusion priority");
    check(inheritance,"generated ordinary and maximized policies honor independent three-level inheritance");
    std::cout<<"Seed: "<<seed<<"; topology cases: "<<generated<<"; policy cases: 10000\nFailures: "<<failures<<'\n';return failures?1:0;
}
