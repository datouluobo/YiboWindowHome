#include "state.h"
#include "caption_motion.h"
#include "caption_style.h"
#include <psapi.h>
#include <chrono>
#include <thread>
#include <iostream>

struct Resources {SIZE_T commit{},working{};DWORD handles{},gdi{},user{};};
Resources resources(){Resources r;PROCESS_MEMORY_COUNTERS_EX memory{};memory.cb=sizeof(memory);GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));r.commit=memory.PrivateUsage;r.working=memory.WorkingSetSize;GetProcessHandleCount(GetCurrentProcess(),&r.handles);r.gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);r.user=GetGuiResources(GetCurrentProcess(),GR_USEROBJECTS);return r;}
int wmain(int argc,wchar_t** argv){
    if(argc<2||argc>3)return 2;std::cout<<std::unitbuf;int seconds=argc==3?_wtoi(argv[2]):15;if(seconds<5||seconds>86400)return 2;
    std::filesystem::create_directories(argv[1]);home::Store store{std::filesystem::path(argv[1])/L"soak.json"};home::State state;
    home::Display screen{L"DISPLAY1",L"panel-a",{-1920,0,0,1080},{-1920,40,0,1080},144,true};
    home::Record record;record.normal={-1800,140,-1000,740};record.actual=record.normal;record.monitor=screen.monitor;record.work=screen.work;record.dpi=screen.dpi;record.device=screen.device;record.displayIdentity=screen.identity;record.topology={screen};
    for(int i=0;i<64;++i){home::Application app;app.id=L"a-"+std::to_wstring(i);app.name=L"Soak Application";app.path=L"C:\\soak\\app"+std::to_wstring(i)+L".exe";
        for(int j=0;j<8;++j){home::WindowRule r;r.id=L"r-"+std::to_wstring(i)+L"-"+std::to_wstring(j);r.name=L"Rule";r.className=L"Window";r.titleMatch=home::TitleMatch::Equals;r.title=std::to_wstring(j);r.normal={true,-10-j,j};app.rules.push_back(r);state.records[home::scopeKey(app,r.id)]=record;}
        state.applications.push_back(app);}
    HDC dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=160;info.bmiHeader.biHeight=-64;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels{};
    auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);auto old=bitmap?SelectObject(dc,bitmap):nullptr;
    if(!dc||!bitmap||!pixels){std::cout<<"FAIL offscreen drawing fixture\n";if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);return 1;}
    home::CaptionPalette palette;home::CaptionMeasurement m;m.window={0,0,800,600};m.visible=m.window;m.client={0,32,800,600};m.hasMinimize=true;m.minimize={662,0,708,32};home::CaptionMotion motion;auto fake=reinterpret_cast<HWND>(1);motion.remember(fake,1,WS_OVERLAPPEDWINDOW,false,m);
    auto start=std::chrono::steady_clock::now();auto elapsed=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
    auto original=resources(),baseline=original,peak=original;bool plans=true,policies=true,durable=true;size_t cycles{},writes{};int previousSecond=-1;std::uint64_t checksum{};
    while(elapsed()<seconds){
        for(int j=0;j<100;++j){auto p=home::planRestore(record,std::span(&screen,1),{});plans=plans&&p&&EqualRect(&p->normal,&record.normal);++checksum;}
        for(int j=0;j<512;++j){auto& app=state.applications[size_t(j)%state.applications.size()];home::WindowMatch w;w.identity.path=app.path;w.className=L"Window";w.title=std::to_wstring(j%8);auto p=home::resolveWindow(state,w);policies=policies&&p.allowed&&p.explicitRule;checksum+=p.recordKey.size();}
        for(int j=0;j<10000;++j){home::CaptionMeasurement out;RECT bounds=m.window;OffsetRect(&bounds,j%100,j%50);bool ok=motion.follow(fake,1,WS_OVERLAPPEDWINDOW,false,96,bounds,out);plans=plans&&ok;checksum+=home::layoutCaption(out).button.left;}
        for(int j=0;j<200;++j){RECT r{0,0,46,32};auto interaction=static_cast<home::CaptionInteraction>(j%3);home::paintCaptionBackground(dc,r,palette,interaction);home::drawCaptionGlyph(dc,r,palette,static_cast<home::CaptionGlyph>(j%4));}
        int second=int(elapsed());if(second!=previousSecond){
            state.offsetY=second%20;durable=durable&&store.save(state);auto loaded=store.load();durable=durable&&store.writable&&loaded.records.size()==512&&loaded.applications.size()==64&&loaded.offsetY==state.offsetY;++writes;
            auto now=resources();if(second==2)baseline=now;peak.commit=std::max(peak.commit,now.commit);peak.handles=std::max(peak.handles,now.handles);peak.gdi=std::max(peak.gdi,now.gdi);peak.user=std::max(peak.user,now.user);
            if(second%10==0)std::cout<<"SAMPLE seconds="<<second<<" private="<<now.commit<<" working="<<now.working<<" handles="<<now.handles<<" GDI="<<now.gdi<<" USER="<<now.user<<" cycles="<<cycles<<'\n';previousSecond=second;
        }
        ++cycles;std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    auto final=resources();SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);int failures{};
    auto check=[&](bool ok,const char* label){std::cout<<(ok?"PASS ":"FAIL ")<<label<<'\n';if(!ok)++failures;};
    check(plans,"soak recovery and motion geometry remain consistent");check(policies,"soak application and independent rule scopes remain consistent");check(durable,"soak repeated atomic saves and reloads retain all records");
    check(final.handles<=baseline.handles+2&&peak.handles<=baseline.handles+4,"soak process handles remain bounded after warmup");
    check(final.gdi==baseline.gdi&&peak.gdi<=baseline.gdi+2,"soak offscreen background and glyph drawing have no GDI growth");
    check(final.user==baseline.user&&peak.user<=baseline.user+2,"soak USER resources remain stable");
    check(final.commit<=baseline.commit+2*1024*1024&&peak.commit<=baseline.commit+4*1024*1024,"soak private commit stays within warmup growth allowance");
    check(peak.commit<=15*1024*1024,"64 applications and 512 records remain within private-commit target in core harness");
    std::cout<<"Duration: "<<elapsed()<<" seconds; cycles: "<<cycles<<"; save/reloads: "<<writes<<"; checksum: "<<checksum<<'\n';
    std::cout<<"Private initial/warm/final/peak: "<<original.commit<<'/'<<baseline.commit<<'/'<<final.commit<<'/'<<peak.commit<<"; handles warm/final: "<<baseline.handles<<'/'<<final.handles<<"; GDI warm/final: "<<baseline.gdi<<'/'<<final.gdi<<"; USER warm/final: "<<baseline.user<<'/'<<final.user<<"\nFailures: "<<failures<<'\n';return failures?1:0;
}
