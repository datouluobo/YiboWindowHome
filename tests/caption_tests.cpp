#include "caption.h"
#include <iostream>

int main() {
    int failures{};
    auto check=[&](bool ok,const char* name) { std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n'; if(!ok) ++failures; };
    home::CaptionMeasurement m;
    m.window={100,100,900,700}; m.visible={108,100,892,692}; m.client={108,140,892,692}; m.hasMinimize=true; m.minimize={750,108,796,140}; m.fallbackHeight=23;
    auto standard=home::layoutCaption(m);
    check(standard.button.top==m.minimize.top&&standard.button.bottom==m.minimize.bottom,"standard button aligns with caption controls");
    check(standard.button.right-standard.button.left==m.minimize.right-m.minimize.left&&standard.widthSource==home::CaptionWidthSource::MinimizeButton&&standard.button.right==m.minimize.left,"button matches minimize width and touches its left edge");
    auto wider=m; wider.window.right+=300; wider.visible.right+=300; wider.client.right+=300; OffsetRect(&wider.minimize,300,0); auto wide=home::layoutCaption(wider);
    check(wide.button.top==standard.button.top&&wide.button.bottom==standard.button.bottom&&wide.button.left-standard.button.left==300,"window width changes anchor without changing button height");
    auto moved=m; OffsetRect(&moved.window,-1200,-900); OffsetRect(&moved.visible,-1200,-900); OffsetRect(&moved.client,-1200,-900); OffsetRect(&moved.minimize,-1200,-900); auto negative=home::layoutCaption(moved);
    check(negative.button.left==standard.button.left-1200&&negative.button.top==standard.button.top-900&&negative.button.bottom-negative.button.top==32,"movement into negative coordinates preserves geometry");
    auto custom=m; custom.dpi=120; custom.fallbackWidth=58; custom.visible={2559,1,3309,1380}; custom.client={2560,1,3308,1379}; custom.hasMinimize=false; custom.hasViewport=true; custom.viewportTop=51;
    auto qt=home::layoutCaption(custom);
    check(qt.source==home::CaptionSource::ChildViewport&&qt.button.top==1&&qt.button.bottom==51&&qt.button.right-qt.button.left==58&&qt.widthSource==home::CaptionWidthSource::SystemDefault,"custom caption preserves actual height with explicit width fallback");
    custom.dpi=180; custom.viewportTop=76; auto scaled=home::layoutCaption(custom);
    check(scaled.button.bottom-scaled.button.top==75,"DPI-scaled custom caption follows measured boundary");
    auto nc=m; nc.hasMinimize=false; nc.client.top=140; auto native=home::layoutCaption(nc);
    check(native.source==home::CaptionSource::NonClient&&native.button.top==100&&native.button.bottom==140,"nonclient boundary supplies caption height");
    nc.client.top=100; auto fallback=home::layoutCaption(nc);
    check(fallback.source==home::CaptionSource::SystemMetrics&&fallback.button.bottom-fallback.button.top==23,"unknown caption uses stable system fallback");
    auto adjusted=home::layoutCaption(m,500,5);
    check(adjusted.button.right<=m.minimize.left&&adjusted.button.top==standard.button.top+5&&adjusted.button.bottom-adjusted.button.top==32,"manual offset preserves size and avoids caption controls");
    auto maximized=m; maximized.visible.top=0; maximized.minimize.top=0; maximized.minimize.bottom=30; auto max=home::layoutCaption(maximized);
    check(max.button.top==0&&max.button.bottom==30,"maximized caption aligns at screen top");
    auto group=custom; group.hasCaptionGroup=true; group.controlsLeft=3120; group.fallbackWidth=40;
    auto measuredGroup=home::layoutCaption(group);
    check(measuredGroup.button.right-measuredGroup.button.left==40&&measuredGroup.button.right==3120&&measuredGroup.widthSource==home::CaptionWidthSource::CaptionGroup,"DWM group supplies button width with zero gap");
    auto unusual=m; unusual.minimize.right=unusual.minimize.left+63;
    auto actualWidth=home::layoutCaption(unusual);
    check(actualWidth.button.right-actualWidth.button.left==63,"non-default minimize width is preserved");
    auto visual=custom; visual.requiresVisualWidth=true; visual.hasVisualMinimize=true; visual.visualMinimize={3140,1,3180,76};
    auto measuredVisual=home::layoutCaption(visual);
    check(measuredVisual.widthSource==home::CaptionWidthSource::MeasuredCaption&&measuredVisual.button.right-measuredVisual.button.left==40&&measuredVisual.button.right==3140,"visual measurement supplies width and zero-gap minimize anchor");
    auto translatedVisual=visual; OffsetRect(&translatedVisual.visible,170,-60); OffsetRect(&translatedVisual.client,170,-60); OffsetRect(&translatedVisual.visualMinimize,170,-60); translatedVisual.viewportTop-=60;
    auto translatedLayout=home::layoutCaption(translatedVisual);
    check(translatedLayout.button.left==measuredVisual.button.left+170&&translatedLayout.button.top==measuredVisual.button.top-60&&translatedLayout.button.right-translatedLayout.button.left==40,"moving measured custom caption preserves width and translates anchor");
    visual.hasVisualMinimize=false;
    check(home::layoutCaption(visual).widthSource==home::CaptionWidthSource::Unavailable,"custom adapter waits for measurement instead of default width");
    auto chromium=custom; chromium.hasVisualMinimize=true;chromium.hasMeasuredControlRow=true;chromium.visualMinimize={3140,1,3197,47};chromium.viewportTop=56;chromium.fallbackHeight=29;
    auto measuredRow=home::layoutCaption(chromium);
    check(measuredRow.source==home::CaptionSource::MeasuredControls&&measuredRow.button.top==1&&measuredRow.button.bottom==47&&measuredRow.button.right==3140&&measuredRow.button.right-measuredRow.button.left==57,"measured control row overrides viewport and system caption metrics");
    std::cout<<"Failures: "<<failures<<'\n'; return failures?1:0;
}
