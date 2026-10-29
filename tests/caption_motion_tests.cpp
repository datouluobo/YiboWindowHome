#include "caption_motion.h"
#include <iostream>
#include <chrono>

int main() {
    int failures{};auto check=[&](bool ok,const char* text){std::cout<<(ok?"PASS ":"FAIL ")<<text<<'\n';if(!ok)++failures;};
    home::CaptionMotion cache;home::CaptionMeasurement m,result;
    auto window=reinterpret_cast<HWND>(1);constexpr LONG_PTR style=WS_OVERLAPPEDWINDOW;
    m.window={100,200,900,800};m.visible={108,200,892,792};m.client={108,232,892,792};m.dpi=96;
    m.hasMinimize=true;m.minimize={754,200,800,232};
    check(!cache.follow(window,10,style,false,96,m.window,result),"empty cache requires measurement");
    cache.remember(window,10,style,false,m);
    RECT moved{-900,-200,-100,400};
    check(cache.follow(window,10,style,false,96,moved,result),"pure move reuses measured metrics");
    auto before=home::layoutCaption(m,-20,3),after=home::layoutCaption(result,-20,3);auto expected=before.button;OffsetRect(&expected,-1000,-400);
    check(EqualRect(&expected,&after.button),"negative-coordinate move preserves exact offset and dimensions");
    RECT resized{50,120,1100,920};cache.follow(window,10,style,false,96,resized,result);after=home::layoutCaption(result);
    expected=home::layoutCaption(m).button;OffsetRect(&expected,200,-80);
    check(EqualRect(&expected,&after.button),"resize follows right/top edge without scaling controls");
    check(result.client.left==58&&result.client.right==1092&&result.client.bottom==912,"resize updates client frame margins");
    check(!cache.follow(window,11,style,false,96,moved,result),"reused HWND with another PID invalidates cache");
    check(!cache.follow(reinterpret_cast<HWND>(2),10,style,false,96,moved,result),"another HWND requires measurement");
    check(!cache.follow(window,10,style,false,144,moved,result),"DPI change requires measurement");
    check(!cache.follow(window,10,style,true,96,moved,result),"maximize transition requires measurement");
    check(!cache.follow(window,10,style^WS_THICKFRAME,false,96,moved,result),"frame style change requires measurement");
    check(!cache.follow(window,10,style,false,96,{0,0,0,0},result),"invalid frame is rejected");
    m.hasMinimize=false;m.hasViewport=true;m.viewportTop=250;m.requiresVisualWidth=true;m.hasVisualMinimize=true;m.visualMinimize={754,200,794,250};
    cache.remember(window,10,style,false,m);cache.follow(window,10,style,false,96,resized,result);
    check(result.viewportTop==170&&result.visualMinimize.left==954&&home::layoutCaption(result).button.bottom==170,"custom viewport and measured width follow resize");
    m.hasVisualMinimize=false;cache.remember(window,10,style,false,m);
    check(!cache.follow(window,10,style,false,96,moved,result),"missing custom measurement keeps retry path active");
    m.requiresVisualWidth=false;m.hasCaptionGroup=true;m.controlsLeft=754;cache.remember(window,10,style,false,m);cache.follow(window,10,style,false,96,resized,result);
    check(result.controlsLeft==954,"native caption group follows right edge");
    cache.clear();check(!cache.follow(window,10,style,false,96,moved,result),"session reset clears reusable geometry");
    cache.remember(window,10,style,false,m);auto start=std::chrono::steady_clock::now();LONG checksum{};
    for(int i=0;i<100000;++i){RECT r=m.window;OffsetRect(&r,i%100,i%50);cache.follow(window,10,style,false,96,r,result);checksum+=home::layoutCaption(result).button.left;}
    std::cout<<"100000 cached geometry updates: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<" ms, checksum "<<checksum<<'\n';
    std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
