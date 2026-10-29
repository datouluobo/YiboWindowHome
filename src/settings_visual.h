// Private presentation helpers, included by settings.cpp inside its namespace.
COLORREF uiInk(Context& c){return c.highContrast?GetSysColor(COLOR_WINDOWTEXT):RGB(23,36,55);}
COLORREF uiMuted(Context& c){return c.highContrast?GetSysColor(COLOR_WINDOWTEXT):RGB(79,96,119);}
COLORREF uiBlue(Context& c){return c.highContrast?GetSysColor(COLOR_HIGHLIGHT):RGB(0,91,232);}
COLORREF uiWhite(Context& c){return c.highContrast?GetSysColor(COLOR_WINDOW):RGB(255,255,255);}
COLORREF uiTint(Context& c){return c.highContrast?GetSysColor(COLOR_HIGHLIGHT):RGB(232,241,255);}
COLORREF uiSide(Context& c){return c.highContrast?GetSysColor(COLOR_WINDOW):RGB(245,248,252);}
COLORREF uiLine(Context& c){return c.highContrast?GetSysColor(COLOR_WINDOWTEXT):RGB(213,223,236);}
void uiFill(HDC dc,RECT r,COLORREF color){auto b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
void uiText(HDC dc,const std::wstring& text,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS){auto f=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,text.c_str(),int(text.size()),&r,flags);SelectObject(dc,f);}
Gdiplus::Color uiColor(COLORREF color){return Gdiplus::Color(255,GetRValue(color),GetGValue(color),GetBValue(color));}
void uiRound(HDC dc,RECT r,COLORREF bg,COLORREF border,int radius){
    if(r.right<=r.left||r.bottom<=r.top)return;Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    float x=float(r.left)+.5f,y=float(r.top)+.5f,w=float(r.right-r.left)-1,h=float(r.bottom-r.top)-1,d=std::max(1.f,std::min({float(radius*2),w,h}));Gdiplus::GraphicsPath path;
    path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();Gdiplus::SolidBrush fill(uiColor(bg));Gdiplus::Pen stroke(uiColor(border),1.f);graphics.FillPath(&fill,&path);graphics.DrawPath(&stroke,&path);
}
void uiSymbol(Context& c,HDC dc,wchar_t glyph,RECT rect,COLORREF color){uiText(dc,std::wstring(1,glyph),rect,c.iconFont,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
void uiMouse(Context& c,HDC dc,RECT r,int button){
    float h=float(std::min(r.bottom-r.top,r.right-r.left));if(h<8)return;float w=h*.58f,x=float(r.left)+(float(r.right-r.left)-w)/2,y=float(r.top)+(float(r.bottom-r.top)-h)/2;Gdiplus::Graphics g(dc);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Gdiplus::GraphicsPath shape;float d=w;shape.AddArc(x,y,d,d,180,180);shape.AddLine(x+w,y+d/2,x+w,y+h-d/2);shape.AddArc(x,y+h-d,d,d,0,180);shape.CloseFigure();Gdiplus::SolidBrush white(uiColor(uiWhite(c))),blue(uiColor(uiBlue(c)));Gdiplus::Pen line(uiColor(uiMuted(c)),std::max(1.f,h/16.f));g.FillPath(&white,&shape);g.Save();g.SetClip(&shape);if(button==0||button==1){float left=button==0?x:x+w/2;g.FillRectangle(&blue,left,y,w/2,h*.40f);}else g.FillRectangle(&blue,x+w*.42f,y+h*.10f,w*.16f,h*.24f);g.ResetClip();g.DrawPath(&line,&shape);g.DrawLine(&line,x+w/2,y,x+w/2,y+h*.32f);
}
bool uiNumber(int id){return id==GlobalX||id==GlobalY||id==AppX||id==AppY||id==AppMaxX||id==AppMaxY||id==RuleX||id==RuleY||id==RuleMaxX||id==RuleMaxY;}
void uiEditChrome(Context& c,HWND window){
    RECT bounds{},client{};GetWindowRect(window,&bounds);GetClientRect(window,&client);POINT origin{};ClientToScreen(window,&origin);OffsetRect(&client,origin.x-bounds.left,origin.y-bounds.top);RECT area{0,0,bounds.right-bounds.left,bounds.bottom-bounds.top};
    auto dc=GetWindowDC(window);if(!dc)return;int saved=SaveDC(dc);ExcludeClipRect(dc,client.left,client.top,client.right,client.bottom);int dpi=int(GetDpiForWindow(window));auto s=[&](int v){return MulDiv(v,dpi,96);};int id=GetDlgCtrlID(window);auto bg=IsWindowEnabled(window)?uiWhite(c):uiSide(c);
    uiFill(dc,area,bg);if(id!=Path)uiRound(dc,area,bg,GetFocus()==window?uiBlue(c):uiLine(c),s(6));
    if(id==SearchApps){RECT icon{s(9),0,s(29),area.bottom};uiSymbol(c,dc,L'\xE721',icon,uiMuted(c));}
    if(uiNumber(id)){auto color=IsWindowEnabled(window)?uiMuted(c):RGB(164,176,192);RECT up{area.right-s(27),s(2),area.right-s(4),area.bottom/2},down=up;down.top=area.bottom/2;down.bottom=area.bottom-s(2);uiText(dc,L"\xE70E",up,c.smallIconFont,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE);uiText(dc,L"\xE70D",down,c.smallIconFont,color,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
    RestoreDC(dc,saved);ReleaseDC(window,dc);
}
void uiRadio(Context& c,HDC dc,RECT rect,bool checked,bool enabled=true){
    if(c.highContrast){DrawFrameControl(dc,&rect,DFC_BUTTON,DFCS_BUTTONRADIO|(checked?DFCS_CHECKED:0)|(!enabled?DFCS_INACTIVE:0));return;}
    float diameter=float(std::min(rect.right-rect.left,rect.bottom-rect.top));if(diameter<3)return;float x=float(rect.left)+(float(rect.right-rect.left)-diameter)/2,y=float(rect.top)+(float(rect.bottom-rect.top)-diameter)/2;
    Gdiplus::Graphics graphics(dc);graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);float stroke=std::max(1.f,diameter/10.f);auto color=enabled?(checked?uiBlue(c):RGB(122,141,166)):RGB(177,189,204);Gdiplus::SolidBrush fill(uiColor(uiWhite(c)));Gdiplus::Pen outline(uiColor(color),stroke);float inset=stroke/2.f;graphics.FillEllipse(&fill,x+inset,y+inset,diameter-stroke,diameter-stroke);graphics.DrawEllipse(&outline,x+inset,y+inset,diameter-stroke,diameter-stroke);
    if(checked){Gdiplus::SolidBrush dot(uiColor(color));float dotSize=diameter*.48f;graphics.FillEllipse(&dot,x+(diameter-dotSize)/2,y+(diameter-dotSize)/2,dotSize,dotSize);}
}
void initializeVisuals(Context& c){
    struct GraphicsSession{ULONG_PTR token{};GraphicsSession(){Gdiplus::GdiplusStartupInput input;Gdiplus::GdiplusStartup(&token,&input,nullptr);}~GraphicsSession(){if(token)Gdiplus::GdiplusShutdown(token);}};static GraphicsSession graphicsSession;
    HIGHCONTRASTW high{sizeof(high)};SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(high),&high,0);c.highContrast=(high.dwFlags&HCF_HIGHCONTRASTON)!=0;
    if(c.background)DeleteObject(c.background);if(c.sidebar)DeleteObject(c.sidebar);if(c.tint)DeleteObject(c.tint);
    c.background=CreateSolidBrush(uiWhite(c));c.sidebar=CreateSolidBrush(uiSide(c));c.tint=CreateSolidBrush(uiTint(c));
}
void paintSettings(Context& c,HDC dc){
    RECT client{};GetClientRect(c.window,&client);uiFill(dc,client,uiWhite(c));int dpi=int(GetDpiForWindow(c.window));
    RECT side{0,0,MulDiv(264-c.scrollX,dpi,96),client.bottom};uiFill(dc,side,uiSide(c));
    RECT line{side.right,0,side.right+1,client.bottom};uiFill(dc,line,uiLine(c));
    if(c.tab==1&&c.selected>=0){
        int width=std::max(1040,MulDiv(client.right,96,dpi)),height=std::max(760,MulDiv(client.bottom,96,dpi));int col=(width-320-32)/2;
        int y=height-350;
        auto r=[&](int x,int py,int w,int h)->RECT{return {MulDiv(x-c.scrollX,dpi,96),MulDiv(py-c.scrollY,dpi,96),MulDiv(x+w-c.scrollX,dpi,96),MulDiv(py+h-c.scrollY,dpi,96)};};
        uiText(dc,L"普通窗口",r(292,y,col,30),c.sectionFont,uiInk(c));uiText(dc,L"最大化窗口",r(292+col+32,y,col,30),c.sectionFont,uiInk(c));
        uiFill(dc,r(292+col+16,y,1,height-y-72),uiLine(c));
        uiText(dc,L"水平 X：-500 至 0，负数向左；垂直 Y：-500 至 500，负上正下。",r(292,height-51,width-460,20),c.smallFont,uiMuted(c));
    }
    int height=std::max(760,MulDiv(client.bottom,96,dpi));
    RECT help{MulDiv(20-c.scrollX,dpi,96),MulDiv(height-282-c.scrollY,dpi,96),MulDiv(244-c.scrollX,dpi,96),MulDiv(height-84-c.scrollY,dpi,96)};
    uiRound(dc,help,uiWhite(c),uiLine(c),MulDiv(8,dpi,96));
}
void paintDesktop(Context& c,HDC dc,RECT area){
    int dpi=int(GetDpiForWindow(c.window));auto s=[&](int v){return MulDiv(v,dpi,96);};
    uiFill(dc,area,uiWhite(c));RECT viewport=area;InflateRect(&viewport,-s(20),-s(14));viewport.bottom-=s(48);
    const Record* record=nullptr;auto found=c.draft.records.find(c.positionKey);if(found!=c.draft.records.end())record=&found->second;
    RECT currentBounds{};if(IsWindow(c.sample))GetWindowRect(c.sample,&currentBounds);
    auto model=buildDesktopPreview(record,c.displays,currentBounds,viewport);
    if(!model){uiText(dc,L"暂时无法读取显示器布局",area,c.font,uiMuted(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE);return;}
    auto rect=[](PreviewBounds p)->RECT{return {LONG(std::lround(p.left)),LONG(std::lround(p.top)),LONG(std::lround(p.right)),LONG(std::lround(p.bottom))};};
    for(size_t i=0;i<model->displays.size();++i){auto& view=model->displays[i];auto bounds=rect(view.monitor),work=rect(view.work);
        InflateRect(&bounds,-s(3),0);uiRound(dc,bounds,uiMuted(c),uiMuted(c),s(4));auto screen=bounds;InflateRect(&screen,-s(4),-s(4));uiRound(dc,screen,c.highContrast?uiWhite(c):RGB(222,237,255),uiLine(c),s(2));
        if(!c.highContrast&&screen.right-screen.left>2&&screen.bottom-screen.top>2){Gdiplus::Rect fill(screen.left+1,screen.top+1,screen.right-screen.left-2,screen.bottom-screen.top-2);Gdiplus::Graphics graphics(dc);Gdiplus::LinearGradientBrush gradient(fill,uiColor(RGB(225,239,255)),uiColor(RGB(205,226,251)),Gdiplus::LinearGradientModeVertical);graphics.FillRectangle(&gradient,fill);}
        RECT stem{(bounds.left+bounds.right)/2-s(10),bounds.bottom,(bounds.left+bounds.right)/2+s(10),bounds.bottom+s(9)};uiFill(dc,stem,uiMuted(c));RECT foot{stem.left-s(14),stem.bottom,stem.right+s(14),stem.bottom+s(4)};uiRound(dc,foot,uiMuted(c),uiMuted(c),s(2));
        auto label=bounds;label.top=bounds.bottom+s(16);label.bottom=label.top+s(18);
        uiText(dc,L"显示器 "+std::to_wstring(i+1)+(view.display.primary?L"（主屏）":L""),label,c.smallFont,uiMuted(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        OffsetRect(&label,0,s(20));uiText(dc,std::to_wstring(view.display.monitor.right-view.display.monitor.left)+L" × "+std::to_wstring(view.display.monitor.bottom-view.display.monitor.top)+L" · "+std::to_wstring(MulDiv(int(view.display.dpi),100,96))+L"%",label,c.smallFont,uiMuted(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    if(model->window){
        auto window=rect(*model->window);
        // A window wholly on one monitor should remain wholly inside its drawn
        // screen, including when its real bounds touch the monitor edges.
        for(const auto& display:model->displays){auto monitor=rect(display.monitor);if(window.left>=monitor.left&&window.top>=monitor.top&&window.right<=monitor.right&&window.bottom<=monitor.bottom){auto screen=monitor;InflateRect(&screen,-s(7),-s(4));window.left=std::max(window.left,screen.left);window.top=std::max(window.top,screen.top);window.right=std::min(window.right,screen.right);window.bottom=std::min(window.bottom,screen.bottom);break;}}
        int saved=SaveDC(dc);IntersectClipRect(dc,viewport.left,viewport.top,viewport.right,viewport.bottom);HRGN screens=CreateRectRgn(0,0,0,0);for(const auto& display:model->displays){auto screen=rect(display.monitor);InflateRect(&screen,-s(7),-s(4));if(screen.right>screen.left&&screen.bottom>screen.top){HRGN region=CreateRectRgn(screen.left,screen.top,screen.right,screen.bottom);CombineRgn(screens,screens,region,RGN_OR);DeleteObject(region);}}ExtSelectClipRgn(dc,screens,RGN_AND);DeleteObject(screens);
        uiRound(dc,window,uiTint(c),uiBlue(c),s(3));auto bar=window;bar.bottom=std::min(window.bottom,window.top+s(24));InflateRect(&bar,-1,-1);uiFill(dc,bar,c.highContrast?uiBlue(c):RGB(206,226,255));
        if(c.headerIcon&&window.right-window.left>s(110))DrawIconEx(dc,bar.left+s(5),bar.top+s(4),c.headerIcon,s(15),s(15),0,nullptr,DI_NORMAL);
        for(int i=0;i<3&&window.right-window.left>s(110);++i){RECT button{bar.right-s(24*(i+1)),bar.top,bar.right-s(24*i),bar.bottom};uiText(dc,std::wstring(1,i==0?L'\xE8BB':i==1?L'\xE922':L'\xE921'),button,c.smallIconFont,uiMuted(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
        auto label=window;label.top=bar.bottom;auto font=window.right-window.left<s(160)?c.smallFont:c.sectionFont;uiText(dc,std::to_wstring(model->windowBounds.right-model->windowBounds.left)+L" × "+std::to_wstring(model->windowBounds.bottom-model->windowBounds.top),label,font,c.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):uiBlue(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);RestoreDC(dc,saved);
    }
}
void paintCaption(Context& c,HDC dc,RECT area){
    int dpi=int(GetDpiForWindow(c.window));auto s=[&](int v){return MulDiv(v,dpi,96);};
    uiFill(dc,area,uiWhite(c));uiRound(dc,area,c.highContrast?uiWhite(c):RGB(243,248,255),uiLine(c),s(8));
    RECT title=area;title.left+=s(20);title.top+=s(16);title.bottom=title.top+s(25);uiText(dc,c.previewMaximized?L"标题栏示意 · 最大化窗口":L"标题栏示意 · 普通窗口",title,c.font,uiMuted(c));
    RECT bar{area.left+s(20),(area.top+area.bottom)/2-s(10),area.right-s(20),(area.top+area.bottom)/2+s(44)};uiRound(dc,bar,uiWhite(c),uiLine(c),s(6));
    if(c.headerIcon)DrawIconEx(dc,bar.left+s(15),bar.top+s(14),c.headerIcon,s(26),s(26),0,nullptr,DI_NORMAL);
    auto name=bar;name.left+=s(54);name.right-=s(190);uiText(dc,text(c,Heading),name,c.font,uiInk(c));
    int cell=s(42);RECT close{bar.right-cell,bar.top,bar.right,bar.bottom},max=close,min=close;OffsetRect(&max,-cell,0);OffsetRect(&min,-2*cell,0);
    uiSymbol(c,dc,L'\xE8BB',close,uiMuted(c));uiSymbol(c,dc,c.previewMaximized?L'\xE923':L'\xE922',max,uiMuted(c));uiSymbol(c,dc,L'\xE921',min,uiMuted(c));
    RECT hook=min;OffsetRect(&hook,-cell,0);int x=c.draft.offsetX,y=c.draft.offsetY;
    int base=c.tab==2?(c.previewMaximized?RuleMax:RuleNormal):(c.previewMaximized?AppMax:AppNormal);
    if(c.selected>=0){integer(c,base+1,x,0);integer(c,base+2,y);}
    OffsetRect(&hook,s(std::min(0,x)),s(y));bool outside=hook.left<area.left||hook.right>area.right||hook.top<area.top+s(45)||hook.bottom>area.bottom;if(outside){int dx=hook.left<area.left?int(area.left-hook.left):hook.right>area.right?int(area.right-hook.right):0;int dy=hook.top<area.top+s(45)?int(area.top+s(45)-hook.top):hook.bottom>area.bottom?int(area.bottom-hook.bottom):0;OffsetRect(&hook,dx,dy);RECT note{area.left+s(20),area.bottom-s(25),area.right-s(20),area.bottom};uiText(dc,L"偏移超出示意范围：X "+std::to_wstring(x)+L" · Y "+std::to_wstring(y),note,c.smallFont,uiMuted(c));}int clip=SaveDC(dc);IntersectClipRect(dc,area.left,area.top,area.right,area.bottom);
    RECT hookBackground=hook;InflateRect(&hookBackground,-s(4),-s(7));uiRound(dc,hookBackground,uiTint(c),uiTint(c),s(5));CaptionPalette palette;palette.foreground=uiBlue(c);palette.glyphWidth=s(15);palette.stroke=std::max(1,s(1));drawCaptionGlyph(dc,hook,palette,CaptionGlyph::Return);
    RECT annotation{hook.left-s(13),hook.top-s(40),hook.right+s(13),hook.top-s(14)};uiRound(dc,annotation,uiBlue(c),uiBlue(c),s(4));uiText(dc,L"风钩按钮",annotation,c.smallFont,uiWhite(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE);RECT arrow{hook.left,annotation.bottom,hook.right,hook.top+s(2)};uiText(dc,L"\xE70D",arrow,c.smallIconFont,uiBlue(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE);RestoreDC(dc,clip);
}
void drawControl(Context& c,DRAWITEMSTRUCT& draw){
    HDC dc=draw.hDC;RECT r=draw.rcItem;int dpi=int(GetDpiForWindow(c.window));auto s=[&](int v){return MulDiv(v,dpi,96);};
    if(draw.CtlID==AppIcon){uiFill(dc,r,uiWhite(c));int row=ListView_GetNextItem(item(c,Nav),-1,LVNI_SELECTED);LVITEMW info{};info.mask=LVIF_IMAGE;info.iItem=row;
        if(row>=0&&ListView_GetItem(item(c,Nav),&info)&&c.icons)ImageList_DrawEx(c.icons,info.iImage,dc,r.left+s(4),r.top+s(4),s(40),s(40),CLR_NONE,CLR_NONE,ILD_TRANSPARENT);
        else DrawIconEx(dc,r.left+s(6),r.top+s(6),LoadIconW(nullptr,IDI_APPLICATION),s(36),s(36),0,nullptr,DI_NORMAL);return;}
    if(draw.CtlID==Nav){
        if(draw.itemID>=c.navIndices.size())return;int index=c.navIndices[draw.itemID];bool selectedRow=(draw.itemState&ODS_SELECTED)!=0;
        uiFill(dc,r,uiSide(c));RECT inside=r;InflateRect(&inside,-s(4),-s(3));if(selectedRow){uiRound(dc,inside,uiTint(c),c.highContrast?uiBlue(c):RGB(202,220,247),s(8));RECT accent{inside.left+s(1),(inside.top+inside.bottom)/2-s(15),inside.left+s(5),(inside.top+inside.bottom)/2+s(15)};uiRound(dc,accent,uiBlue(c),uiBlue(c),s(2));}
        LVITEMW info{};info.mask=LVIF_IMAGE;info.iItem=int(draw.itemID);ListView_GetItem(item(c,Nav),&info);
        if(index<0){RECT icon{r.left+s(12),r.top+s(7),r.left+s(46),r.top+s(41)};uiSymbol(c,dc,L'\xE713',icon,uiInk(c));}else if(c.icons)ImageList_DrawEx(c.icons,info.iImage,dc,r.left+s(14),r.top+s(8),s(32),s(32),CLR_NONE,CLR_NONE,ILD_TRANSPARENT);
        std::wstring name=index<0?L"全局默认":c.draft.applications[size_t(index)].name;
        if(index>=0&&!c.draft.applications[size_t(index)].enabled)name+=L" · 已禁用";
        auto first=r;first.left+=s(58);first.right-=s(8);auto nameFont=c.font;if(selectedRow){SIZE extent{};auto old=SelectObject(dc,c.strongFont);GetTextExtentPoint32W(dc,name.c_str(),int(name.size()),&extent);SelectObject(dc,old);if(extent.cx<=first.right-first.left)nameFont=c.strongFont;}uiText(dc,name,first,nameFont,c.highContrast&&selectedRow?GetSysColor(COLOR_HIGHLIGHTTEXT):uiInk(c));
        if((draw.itemState&ODS_FOCUS)&&GetFocus()==draw.hwndItem&&!(SendMessageW(draw.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){InflateRect(&inside,-s(3),-s(3));DrawFocusRect(dc,&inside);}return;
    }
    if(draw.CtlID==Positions){
        if(draw.itemID>=c.positionKeys.size())return;const auto& key=c.positionKeys[draw.itemID];bool chosen=(draw.itemState&ODS_SELECTED)!=0;bool active=c.draft.records.contains(key)&&activePositionKey(c.draft,positionScope(key))==key;
        COLORREF bg=chosen?uiTint(c):uiWhite(c);uiFill(dc,r,bg);int edge=r.left;
        for(int column=0;column<6;++column){int width=ListView_GetColumnWidth(item(c,Positions),column);RECT cell{edge+s(8),r.top,edge+width-s(8),r.bottom};
            if(column==0){RECT radio{edge+s(13),(r.top+r.bottom)/2-s(8),edge+s(29),(r.top+r.bottom)/2+s(8)};uiRadio(c,dc,radio,active,c.draft.records.contains(key));}
            else if(column==1){bool isDefault=key==positionScope(key);auto found=c.draft.records.find(key);auto name=isDefault?std::wstring(L"默认记录"):found!=c.draft.records.end()?found->second.name:L"";bool badge=active||isDefault;RECT textRect=cell;if(badge)textRect.right-=s(active?66:48);uiText(dc,name,textRect,c.font,c.highContrast&&chosen?GetSysColor(COLOR_HIGHLIGHTTEXT):uiInk(c));if(badge){RECT tag{cell.right-s(active?60:42),(r.top+r.bottom)/2-s(11),cell.right,(r.top+r.bottom)/2+s(11)};auto color=active?uiBlue(c):c.highContrast?uiBlue(c):RGB(255,242,217);uiRound(dc,tag,color,color,s(5));uiText(dc,active?L"启用中":L"默认",tag,c.smallFont,active||c.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(133,72,0),DT_CENTER|DT_VCENTER|DT_SINGLELINE);}}
            else{wchar_t value[512]{};ListView_GetItemText(item(c,Positions),int(draw.itemID),column,value,512);uiText(dc,value,cell,c.smallFont,c.highContrast&&chosen?GetSysColor(COLOR_HIGHLIGHTTEXT):uiMuted(c));}
            edge+=width;
        }
        RECT line{r.left,r.bottom-1,r.right,r.bottom};uiFill(dc,line,uiLine(c));if(draw.itemState&ODS_FOCUS){InflateRect(&r,-2,-2);DrawFocusRect(dc,&r);}return;
    }
    bool enabled=(draw.itemState&ODS_DISABLED)==0,primary=draw.CtlID==Save,hot=c.hot==draw.hwndItem;
    bool pressed=(draw.itemState&ODS_SELECTED)!=0;COLORREF bg=primary?(enabled?(pressed?RGB(0,70,180):uiBlue(c)):uiLine(c)):((hot||pressed)?uiTint(c):uiWhite(c));
    bool reset=draw.CtlID==AppNormal+7000||draw.CtlID==AppMax+7000;uiRound(dc,r,bg,primary?bg:reset&&!hot?uiWhite(c):uiLine(c),s(6));auto label=r;InflateRect(&label,-s(8),0);auto foreground=!enabled?uiMuted(c):primary?(c.highContrast?GetSysColor(COLOR_HIGHLIGHTTEXT):RGB(255,255,255)):uiBlue(c);
    wchar_t glyph=draw.CtlID==Save?L'\xE74E':draw.CtlID==Browse?L'\xE8B7':draw.CtlID==Pick?L'\xE81D':draw.CtlID==AddPosition?L'\xE710':draw.CtlID==PreviewDesktop||draw.CtlID==Preview||draw.CtlID==PreviewMax?L'\xE768':draw.CtlID==Inherit?L'\xE72C':0;
    if(reset)glyph=L'\xE72C';if(glyph){auto caption=text(c,int(draw.CtlID));SIZE size{};auto old=SelectObject(dc,c.font);GetTextExtentPoint32W(dc,caption.c_str(),int(caption.size()),&size);SelectObject(dc,old);int group=std::min(int(r.right-r.left)-s(16),int(size.cx)+s(24));int left=(r.left+r.right-group)/2;RECT icon{left,r.top,left+s(20),r.bottom};uiSymbol(c,dc,glyph,icon,foreground);label.left=icon.right+s(4);label.right=r.right-s(8);uiText(dc,caption,label,c.font,foreground);}
    else uiText(dc,text(c,int(draw.CtlID)),label,c.font,foreground,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    if((draw.itemState&ODS_FOCUS)&&!(SendMessageW(draw.hwndItem,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT focus=r;InflateRect(&focus,-s(2),-s(2));uiRound(dc,focus,bg,primary?uiWhite(c):uiBlue(c),s(5));}
}
LRESULT CALLBACK visualControlProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto& c=*reinterpret_cast<Context*>(data);int id=GetDlgCtrlID(window);
    wchar_t className[24]{};GetClassNameW(window,className,24);bool edit=wcscmp(className,L"Edit")==0;
    if(id==PositionName&&c.positionEditing&&message==WM_KEYDOWN&&w==VK_RETURN){SetFocus(item(c,Positions));return 0;}
    if(id==PositionName&&c.positionEditing&&message==WM_KEYDOWN&&w==VK_ESCAPE){c.positionEditing=false;c.pendingPositionName=false;ShowWindow(window,SW_HIDE);int row=ListView_GetNextItem(item(c,Positions),-1,LVNI_SELECTED);selectPosition(c,row);SetFocus(item(c,Positions));return 0;}
    if(edit&&message==WM_NCCALCSIZE){auto result=DefSubclassProc(window,message,w,l);auto rect=w?&reinterpret_cast<NCCALCSIZE_PARAMS*>(l)->rgrc[0]:reinterpret_cast<RECT*>(l);int dpi=int(GetDpiForWindow(window));auto dc=GetDC(window);auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(window,dc);int pad=std::max(0,(int(rect->bottom-rect->top)-int(metrics.tmHeight))/2);rect->top+=pad;rect->bottom-=pad;rect->left+=MulDiv(id==Path?0:id==SearchApps?34:10,dpi,96);rect->right-=MulDiv(uiNumber(id)?30:id==Path?0:10,dpi,96);return result;}
    if(edit&&message==WM_NCPAINT){uiEditChrome(c,window);return 0;}
    if(edit&&uiNumber(id)&&message==WM_NCHITTEST){RECT area{};GetWindowRect(window,&area);POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(PtInRect(&area,point)&&point.x>=area.right-MulDiv(29,GetDpiForWindow(window),96))return HTVSCROLL;}
    if(edit&&uiNumber(id)&&((message==WM_NCLBUTTONDOWN&&w==HTVSCROLL)||(message==WM_KEYDOWN&&(w==VK_UP||w==VK_DOWN)))){bool up=w==VK_UP;if(message==WM_NCLBUTTONDOWN){RECT r{};GetWindowRect(window,&r);up=GET_Y_LPARAM(l)<(r.top+r.bottom)/2;}int maximum=id==GlobalX||id==AppX||id==AppMaxX||id==RuleX||id==RuleMaxX?0:500;auto value=std::clamp(wcstol(text(c,id).c_str(),nullptr,10),-500L,long(maximum));SetFocus(window);set(c,id,std::to_wstring(std::clamp(value+(up?1:-1),-500L,long(maximum))));uiEditChrome(c,window);return 0;}
    if(message==WM_MOUSEMOVE){if(c.hot!=window){c.hot=window;InvalidateRect(window,nullptr,FALSE);}TRACKMOUSEEVENT tracking{sizeof(tracking),TME_LEAVE,window,0};TrackMouseEvent(&tracking);}
    if(message==WM_MOUSELEAVE){if(c.hot==window)c.hot=nullptr;InvalidateRect(window,nullptr,FALSE);}
    bool canvas=id==DesktopCanvas||id==CaptionCanvas,tabs=id==Tabs,note=id==Summary||id==HelpBody||id==PreviewNote;
    LONG_PTR type=GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK;bool toggle=!edit&&!canvas&&!tabs&&(type==BS_AUTOCHECKBOX||type==BS_RADIOBUTTON);
    if(message==WM_ERASEBKGND&&(canvas||tabs||toggle||note))return 1;
    if((message==WM_PAINT||message==WM_PRINTCLIENT)&&(canvas||tabs||toggle||note)){
        PAINTSTRUCT paint{};HDC dc=message==WM_PAINT?BeginPaint(window,&paint):reinterpret_cast<HDC>(w);RECT area{};GetClientRect(window,&area);int dpi=int(GetDpiForWindow(window));auto s=[&](int v){return MulDiv(v,dpi,96);};
        HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,std::max(1L,area.right),std::max(1L,area.bottom));auto old=SelectObject(buffer,bitmap);uiFill(buffer,area,uiWhite(c));
        if(canvas){if(id==DesktopCanvas)paintDesktop(c,buffer,area);else paintCaption(c,buffer,area);}
        else if(note){
            if(id==HelpBody){RECT intro{0,0,area.right,s(23)};uiText(buffer,L"在目标窗口的风钩按钮上",intro,c.smallFont,uiMuted(c));const wchar_t* labels[]={L"左键 · 恢复启用记录",L"右键 · 更新默认记录",L"Ctrl + 右键 · 新增记录",L"中键 · 切换窗口置顶"};for(int i=0;i<4;++i){RECT icon{0,s(23+i*29),s(22),s(23+i*29+25)};uiMouse(c,buffer,icon,i==0?0:i==3?2:1);RECT label{s(27),icon.top,area.right,icon.bottom};uiText(buffer,labels[i],label,c.smallFont,uiMuted(c));}}
            else {bool summary=id==Summary;if(summary)uiRound(buffer,area,uiTint(c),uiTint(c),s(5));RECT icon{s(summary?10:0),0,s(summary?32:22),area.bottom};uiSymbol(c,buffer,L'\xE946',icon,uiBlue(c));RECT label=area;label.left=icon.right+s(8);uiText(buffer,text(c,id),label,c.smallFont,summary?uiBlue(c):uiMuted(c));}
        }
        else if(tabs){for(int i=0;i<TabCtrl_GetItemCount(window);++i){RECT tab{};TabCtrl_GetItemRect(window,i,&tab);bool selectedTab=i==TabCtrl_GetCurSel(window);wchar_t caption[80]{};TCITEMW entry{};entry.mask=TCIF_TEXT;entry.pszText=caption;entry.cchTextMax=80;TabCtrl_GetItem(window,i,&entry);uiText(buffer,caption,tab,selectedTab?c.strongFont:c.font,selectedTab?uiBlue(c):uiMuted(c),DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(selectedTab){RECT mark{tab.left,area.bottom-s(3),tab.right,area.bottom};uiFill(buffer,mark,uiBlue(c));}if(GetFocus()==window&&selectedTab){InflateRect(&tab,-s(5),-s(5));DrawFocusRect(buffer,&tab);}}RECT line{0,area.bottom-1,area.right,area.bottom};uiFill(buffer,line,uiLine(c));}
        else{
            bool enabled=IsWindowEnabled(window)!=FALSE,checked=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED;RECT icon{s(2),(area.bottom-s(18))/2,s(20),(area.bottom+s(18))/2};
            if(id==Enabled){RECT track{s(2),(area.bottom-s(21))/2,s(42),(area.bottom+s(21))/2};uiRound(buffer,track,checked?uiBlue(c):uiLine(c),checked?uiBlue(c):uiLine(c),s(21));int diameter=s(15),left=checked?track.right-diameter-s(3):track.left+s(3);RECT thumb{left,track.top+s(3),left+diameter,track.top+s(3)+diameter};uiRound(buffer,thumb,uiWhite(c),uiWhite(c),diameter);icon.right=s(48);}
            else if(id==Active)DrawFrameControl(buffer,&icon,DFC_BUTTON,DFCS_BUTTONCHECK|(checked?DFCS_CHECKED:0)|(!enabled?DFCS_INACTIVE:0));
            else uiRadio(c,buffer,icon,checked,enabled);
            RECT label=area;label.left=icon.right+s(4);auto caption=text(c,id);if(id==Enabled)caption=L"";if(id==AppNormal||id==AppMax)caption=L"自定义";
            uiText(buffer,caption,label,c.font,enabled?uiInk(c):uiMuted(c));if(GetFocus()==window){InflateRect(&label,-2,-2);DrawFocusRect(buffer,&label);}
        }
        BitBlt(dc,0,0,area.right,area.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);if(message==WM_PAINT)EndPaint(window,&paint);return 0;
    }
    auto result=DefSubclassProc(window,message,w,l);
    if(edit&&message==WM_SETFONT){SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,0);SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
    if(edit&&(message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_ENABLE||message==WM_PAINT))uiEditChrome(c,window);
    if(message==BM_SETCHECK||message==WM_ENABLE||message==WM_SETFOCUS||message==WM_KILLFOCUS||message==WM_SETTEXT||message==WM_KEYUP||message==WM_LBUTTONUP)InvalidateRect(window,nullptr,FALSE);
    if(message==WM_SETFOCUS||message==WM_KILLFOCUS)InvalidateRect(c.window,nullptr,FALSE);
    return result;
}
