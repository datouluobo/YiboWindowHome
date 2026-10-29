#include "settings.h"
#include "app_files.h"
#include "preview.h"
#include "caption_style.h"
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>
#include <set>
#include <memory>
#include <cwctype>
#include <uxtheme.h>
#include <cmath>
#include <vssym32.h>
#include <objidl.h>
#include <gdiplus.h>
#include <appmodel.h>

namespace home {
namespace {
enum Id {Nav=200,GlobalX=201,GlobalY=202,Save=203,Tabs=204,Pick=205,Browse=206,
    Name=210,Enabled=211,Path=212,UpdatePath=213,AppNormal=214,AppX=215,AppY=216,AppMax=217,AppMaxX=218,AppMaxY=219,
    RuleList=230,AddRule=231,RemoveRule=232,UpRule=233,DownRule=234,RuleName=235,Class=236,TitleMode=237,Title=238,Action=239,Active=240,
    RuleNormal=241,RuleX=242,RuleY=243,RuleMax=244,RuleMaxX=245,RuleMaxY=246,
    RemoveApp=250,Inherit=251,Preview=252,ClearApp=253,ClearRule=254,PreviewMax=255,Heading=260,Summary=261,RuleSummary=262,Status=263,Identity=264,
    SearchApps=270,MoreApp=271,AppIcon=272,DesktopCanvas=273,CaptionCanvas=274,Positions=275,AddPosition=276,ActivatePosition=277,RenamePosition=278,DeletePosition=279,PositionName=280,PositionScope=281,PreviewDesktop=282,CaptionMode=283,RenameApp=284,
    SidebarTitle=300,HelpTitle=301,HelpBody=302,DropHint=303,PageTitle=304,PageHelp=305,RecordsTitle=306,RecordsHelp=307,GlobalHelp=308,GlobalXLabel=309,GlobalYLabel=310,RuleHelp=311,RuleNameLabel=312,ClassLabel=313,ScopeLabel=314,CaptionModeLabel=315,PreviewNote=316};
struct Widget {HWND window{};int group{},x{},y{},width{},height{},fontStyle{};bool muted{};};
struct Context {
    HINSTANCE instance{};HWND window{},*slot{};State* live{};Store* store{};void(*changed)(){};
    State draft,baseline;HFONT font{},strongFont{},headingFont{},sectionFont{},smallFont{},iconFont{},smallIconFont{};HIMAGELIST icons{},recordImages{};std::vector<Widget> widgets;
    HBRUSH background{},sidebar{},tint{};HICON headerIcon{};bool highContrast{},renaming{},positionEditing{},desktopPreview{},painting{},pendingPositionName{};HWND hot{};
    std::vector<int> navIndices;std::vector<std::wstring> positionKeys;std::wstring positionKey;int recordScope{};
    std::vector<Display> displays;
    int selected{-1},rule{-1},tab{},scrollX{},scrollY{};bool loading{},dirty{},selecting{},preview{},pickUnavailable{},previewMaximized{};
    HWND sample{},picked{},outline{};DWORD samplePid{};
    PreviewPlacement placement;
};
Context* current{};
struct Creation {Context* context{};bool adopted{};};
Context& ctx(HWND window) {return *reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));}
HWND item(Context& c,int id) {return GetDlgItem(c.window,id);}
LRESULT CALLBACK visualControlProc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
void initializeVisuals(Context&);void paintSettings(Context&,HDC);void drawControl(Context&,DRAWITEMSTRUCT&);
void refreshPositions(Context&,bool keepSelection=true);void selectPosition(Context&,int);void syncExternal(Context&);
HICON applicationIcon(const Application& app) {
    // Codex supplies a transparent dark glyph for light surfaces in its package.
    if(app.aumid==L"OpenAI.Codex_2p2nqsd0c76g0!App") {
        UINT32 count{},length{};
        if(GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0",&count,nullptr,&length,nullptr)==ERROR_INSUFFICIENT_BUFFER&&count&&length) {
            std::vector<PWSTR> names(count);std::vector<wchar_t> buffer(length);
            if(GetPackagesByPackageFamily(L"OpenAI.Codex_2p2nqsd0c76g0",&count,names.data(),&length,buffer.data())==ERROR_SUCCESS) {
                for(auto name:names) {
                    UINT32 pathLength{};
                    if(GetPackagePathByFullName(name,&pathLength,nullptr)!=ERROR_INSUFFICIENT_BUFFER)continue;
                    std::vector<wchar_t> path(pathLength);
                    if(GetPackagePathByFullName(name,&pathLength,path.data())!=ERROR_SUCCESS)continue;
                    auto file=std::wstring(path.data())+L"\\assets\\Square44x44Logo.targetsize-48_altform-lightunplated.png";
                    Gdiplus::GdiplusStartupInput input;ULONG_PTR token{};
                    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)break;
                    HICON icon{};{Gdiplus::Bitmap bitmap(file.c_str());if(bitmap.GetLastStatus()==Gdiplus::Ok)bitmap.GetHICON(&icon);}
                    Gdiplus::GdiplusShutdown(token);if(icon)return icon;
                }
            }
        }
    }
    if(!app.aumid.empty()) {
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        if(SUCCEEDED(initialized)||initialized==RPC_E_CHANGED_MODE) {
            PIDLIST_ABSOLUTE pidl{};auto name=L"shell:AppsFolder\\"+app.aumid;
            HICON result{};
            if(SUCCEEDED(SHParseDisplayName(name.c_str(),nullptr,&pidl,0,nullptr))) {
                SHFILEINFOW info{};
                if(SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl),0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON))result=info.hIcon;
                CoTaskMemFree(pidl);
            }
            if(SUCCEEDED(initialized))CoUninitialize();
            if(result)return result;
        }
    }
    SHFILEINFOW info{};
    if(SHGetFileInfoW(app.path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON))return info.hIcon;
    return nullptr;
}
std::wstring text(Context& c,int id) {auto window=item(c,id);int count=GetWindowTextLengthW(window);std::wstring result(size_t(count)+1,L'\0');GetWindowTextW(window,result.data(),count+1);result.resize(count);return result;}
void set(Context& c,int id,const std::wstring& value) {SetWindowTextW(item(c,id),value.c_str());}
bool checked(Context& c,int id) {return SendMessageW(item(c,id),BM_GETCHECK,0,0)==BST_CHECKED;}
void check(Context& c,int id,bool value) {SendMessageW(item(c,id),BM_SETCHECK,value?BST_CHECKED:BST_UNCHECKED,0);}
void status(Context& c,const std::wstring& message) {
    std::wstring hint;
    if(c.selected>=0&&size_t(c.selected)<c.draft.applications.size()&&IsWindow(c.sample)) {
        auto model=describeWindow(c.sample);auto app=findApplication(c.draft,model.identity);
        if(app&&app->id==c.draft.applications[size_t(c.selected)].id) {
            auto policy=resolveWindow(c.draft,model);
            hint=std::wstring(model.maximized?L" · 当前：最大化":L" · 当前：普通");
            if(policy.allowed)hint+=L" · X="+std::to_wstring(std::min(0,policy.x))+L"，Y="+std::to_wstring(policy.y)+(policy.x>0?L"（X 超限按 0）":L"");
            hint+=L" · "+std::wstring(!app->enabled?L"应用禁用":!policy.allowed?L"不显示按钮":policy.ruleName.empty()?L"应用默认":L"命中“"+policy.ruleName+L"”");
        }
    }
    set(c,Status,message+hint);
}
void error(Context& c,const std::wstring& message) {MessageBoxW(c.window,message.c_str(),L"风钩",MB_OK|MB_ICONWARNING);}
HWND widget(Context& c,const wchar_t* cls,const wchar_t* label,DWORD style,int x,int y,int width,int height,int id,int group) {
    HWND window=CreateWindowExW(0,cls,label,WS_CHILD|style,x,y,width,height,c.window,reinterpret_cast<HMENU>(INT_PTR(id)),c.instance,nullptr);
    c.widgets.push_back({window,group,x,y,width,height});SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(c.font),FALSE);
    if(wcscmp(cls,L"BUTTON")==0||wcscmp(cls,L"EDIT")==0||wcscmp(cls,WC_TABCONTROLW)==0||id==DesktopCanvas||id==CaptionCanvas||id==Summary||id==HelpBody||id==PreviewNote)SetWindowSubclass(window,visualControlProc,2,reinterpret_cast<DWORD_PTR>(&c));
    return window;
}
void label(Context& c,const wchar_t* value,int x,int y,int width,int group) {widget(c,L"STATIC",value,0,x,y,width,22,0,group);}
void button(Context& c,const wchar_t* value,int id,int x,int y,int width,int group) {widget(c,L"BUTTON",value,WS_TABSTOP|BS_OWNERDRAW,x,y,width,36,id,group);}
void edit(Context& c,int id,int x,int y,int width,int group,DWORD extra=0,int height=26) {
    auto window=widget(c,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|extra,x,y,width,height,id,group);
    SendMessageW(window,EM_SETLIMITTEXT,id==Path?32767:id==Title?4096:id==Class?255:256,0);
    SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(8,8));
}
void combo(Context& c,int id,int x,int y,int width,int group,std::initializer_list<const wchar_t*> values) {
    auto window=widget(c,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,x,y,width,180,id,group);
    for(auto value:values)SendMessageW(window,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
    SendMessageW(window,CB_SETCURSEL,0,0);
}
void offsetRow(Context& c,const wchar_t* name,int base,int y,int group) {
    widget(c,L"BUTTON",L"继承全局",BS_RADIOBUTTON|WS_TABSTOP|WS_GROUP,0,0,238,30,base+3000,group);
    widget(c,L"BUTTON",name,BS_RADIOBUTTON|WS_TABSTOP,0,y,238,30,base,group);
    widget(c,L"STATIC",L"",0,0,0,300,26,base+4000,group);
    widget(c,L"STATIC",L"水平 X",0,0,0,64,26,base+1000,group);edit(c,base+1,0,0,112,group,0,32);
    widget(c,L"STATIC",L"垂直 Y",0,0,0,64,26,base+2000,group);edit(c,base+2,0,0,112,group,0,32);
}
void fonts(Context& c) {
    auto previous=c.font,previousStrong=c.strongFont,previousHeading=c.headingFont,previousSection=c.sectionFont,previousSmall=c.smallFont,previousIcon=c.iconFont,previousSmallIcon=c.smallIconFont;UINT dpi=GetDpiForWindow(c.window);
    auto make=[&](int size,int weight,const wchar_t* family=L"Microsoft YaHei UI"){return CreateFontW(-MulDiv(size,dpi,96),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,family);};
    c.font=make(15,FW_NORMAL);c.strongFont=make(15,FW_SEMIBOLD);c.headingFont=make(24,FW_SEMIBOLD);c.sectionFont=make(18,FW_SEMIBOLD);c.smallFont=make(13,FW_NORMAL);c.iconFont=make(20,FW_NORMAL,L"Segoe MDL2 Assets");c.smallIconFont=make(12,FW_NORMAL,L"Segoe MDL2 Assets");
    for(auto& w:c.widgets)SendMessageW(w.window,WM_SETFONT,reinterpret_cast<WPARAM>(w.fontStyle==1?c.headingFont:w.fontStyle==2?c.sectionFont:w.fontStyle==3?c.smallFont:c.font),TRUE);
    if(previous)DeleteObject(previous);if(previousStrong)DeleteObject(previousStrong);if(previousHeading)DeleteObject(previousHeading);if(previousSection)DeleteObject(previousSection);if(previousSmall)DeleteObject(previousSmall);if(previousIcon)DeleteObject(previousIcon);if(previousSmallIcon)DeleteObject(previousSmallIcon);
}
void layout(Context& c) {
    if(c.painting)return;c.painting=true;
    RECT client{};GetClientRect(c.window,&client);UINT dpi=GetDpiForWindow(c.window);
    int visibleWidth=MulDiv(client.right,96,dpi),visibleHeight=MulDiv(client.bottom,96,dpi);
    SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS,0,1039,UINT(std::max(1,visibleWidth)),c.scrollX};
    SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE|SIF_POS,0,759,UINT(std::max(1,visibleHeight)),c.scrollY};
    SetScrollInfo(c.window,SB_HORZ,&horizontal,TRUE);SetScrollInfo(c.window,SB_VERT,&vertical,TRUE);
    c.scrollX=GetScrollPos(c.window,SB_HORZ);c.scrollY=GetScrollPos(c.window,SB_VERT);
    int width=std::max(1040,visibleWidth),height=std::max(760,visibleHeight),x=292,right=width-28,span=right-x;
    auto place=[&](int id,int px,int py,int pw,int ph){for(auto& widget:c.widgets)if(GetDlgCtrlID(widget.window)==id){widget.x=px;widget.y=py;widget.width=pw;widget.height=ph;break;}};
    place(SidebarTitle,24,24,216,28);place(SearchApps,20,67,224,34);place(Nav,12,119,244,height-398);
    place(HelpTitle,30,height-268,210,26);place(HelpBody,30,height-237,210,151);
    place(Pick,20,height-48,108,38);place(Browse,136,height-48,108,38);
    auto dc=GetDC(c.window);auto previousFont=SelectObject(dc,c.headingFont);SIZE titleSize{};auto heading=c.selected>=0&&size_t(c.selected)<c.draft.applications.size()?c.draft.applications[size_t(c.selected)].name:std::wstring(L"全局默认");GetTextExtentPoint32W(dc,heading.c_str(),int(heading.size()),&titleSize);SelectObject(dc,previousFont);ReleaseDC(c.window,dc);
    int headingWidth=std::clamp(MulDiv(titleSize.cx,96,dpi)+32,180,std::max(180,span-300));
    place(AppIcon,x,23,48,48);place(Heading,x+64,22,headingWidth,38);place(Name,x+64,22,headingWidth,38);
    place(Path,x+64,67,span-72,28);place(Enabled,right-238,29,48,30);place(Save,right-176,22,132,40);place(MoreApp,right-36,22,36,40);
    place(Tabs,x,110,span,42);place(Status,x,height-29,span,22);
    place(PageTitle,x,172,148,27);place(PreviewDesktop,right-164,165,164,36);place(ScopeLabel,x+160,172,64,26);place(PositionScope,x+228,170,200,160);
    place(PageHelp,x,202,span,25);
    int diagramHeight=std::clamp(height-650,128,220),recordsY=214+diagramHeight+56,tableY=recordsY+40,tableHeight=std::max(100,height-16-tableY);
    place(DesktopCanvas,x,214,span,diagramHeight);place(Summary,x,220+diagramHeight,span,34);
    place(RecordsTitle,x,recordsY,span-440,26);place(ActivatePosition,right-370,recordsY,100,34);place(DeletePosition,right-260,recordsY,106,34);place(AddPosition,right-144,recordsY,144,34);
    place(Positions,x,tableY,span,tableHeight);
    place(PositionName,x,height-80,std::min(360,span-320),34);place(RenamePosition,right-202,height-80,88,34);
    int captionHeight=std::clamp(height-660,130,240),settingsY=height-350,col=(span-32)/2;
    place(CaptionCanvas,x,236,span,captionHeight);place(PreviewNote,x,246+captionHeight,span,28);
    place(Preview,c.tab==1?right-164:right-296,165,c.tab==1?164:140,38);place(PreviewMax,right-148,165,148,38);
    place(CaptionModeLabel,x+160,172,64,26);place(CaptionMode,x+228,171,164,160);
    place(Inherit,right-110,height-77,110,34);
    auto appOffsets=[&](int base,int px) {
        place(base+7000,px+col-112,settingsY,112,30);place(base+3000,px,settingsY+56,col,30);place(base+4000,px+30,settingsY+91,col-30,25);place(base,px,settingsY+130,col,30);
        place(base+1000,px+30,settingsY+176,64,28);place(base+1,px+94,settingsY+172,126,36);
        place(base+2000,px+30,settingsY+222,64,28);place(base+2,px+94,settingsY+218,126,36);
    };
    appOffsets(AppNormal,x);appOffsets(AppMax,x+col+32);
    place(GlobalXLabel,x,settingsY+52,100,28);place(GlobalX,x+110,settingsY+48,144,34);place(GlobalYLabel,x+col+32,settingsY+52,100,28);place(GlobalY,x+col+142,settingsY+48,144,34);
    place(GlobalHelp,x,settingsY+112,span,148);
    int listHeight=std::clamp(height-650,100,220),detailY=248+listHeight+18;
    place(RuleHelp,x,170,span-318,44);place(AddRule,x,218,86,30);place(UpRule,x+96,218,76,30);place(DownRule,x+182,218,76,30);place(RemoveRule,right-106,218,106,30);
    place(RuleList,x,260,span,listHeight);detailY=278+listHeight;
    place(RuleNameLabel,x,detailY+5,90,24);place(RuleName,x+100,detailY,span-100,32);
    place(ClassLabel,x,detailY+45,90,24);place(Class,x+100,detailY+40,span-100,32);
    place(TitleMode,x,detailY+80,152,160);place(Title,x+168,detailY+80,span-168,32);
    place(Action,x,detailY+120,260,160);place(Active,x+280,detailY+120,180,30);
    auto ruleOffsets=[&](int base,int y) {
        place(base,x,y,224,30);place(base+3000,x+230,y,150,30);place(base+4000,x,y+30,200,22);
        place(base+1000,x+392,y+4,54,25);place(base+1,x+448,y,100,32);place(base+2000,x+568,y+4,54,25);place(base+2,x+624,y,100,32);
    };
    ruleOffsets(RuleNormal,detailY+166);ruleOffsets(RuleMax,detailY+222);
    place(RuleSummary,x,height-77,span-280,42);place(ClearRule,right-266,height-77,148,34);
    // Existing IDs remain available to integrations; their replacements are
    // the record editor and the app overflow menu.
    for(auto& widget:c.widgets) {
        int id=GetDlgCtrlID(widget.window);bool visible=widget.group==0||(c.selected>=0&&(widget.group==4||widget.group==c.tab+1||widget.group==6&&(c.tab==1||c.tab==2)))||(c.selected<0&&widget.group==5);
        if(id==Name)visible=c.selected>=0&&c.renaming;
        if(id==Heading&&c.renaming)visible=false;
        if(id==PageTitle)visible=c.tab!=2;
        if(id==PageHelp)visible=c.tab==1||c.selected<0;
        if(id==RecordsHelp)visible=false;
        if(id==DropHint||id==Status||id==RenamePosition)visible=false;
        if(id==PositionName)visible=c.positionEditing&&c.tab==0;
        if(id==CaptionCanvas)visible=c.tab==1||c.selected<0;
        if(id==CaptionMode)visible=c.tab==1||c.selected<0;
        if(id==CaptionModeLabel)visible=c.tab==1||c.selected<0;
        if(id==PositionScope)visible=c.selected>=0&&c.tab==0;
        if(id==PreviewMax||id==Inherit)visible=c.selected>=0&&c.tab==2;
        SetWindowPos(widget.window,nullptr,MulDiv(widget.x-c.scrollX,dpi,96),MulDiv(widget.y-c.scrollY,dpi,96),MulDiv(widget.width,dpi,96),MulDiv(widget.height,dpi,96),SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOCOPYBITS|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
    }
    if(c.positionEditing&&c.tab==0){int row=ListView_GetNextItem(item(c,Positions),-1,LVNI_SELECTED);RECT cell{};if(row>=0&&ListView_GetSubItemRect(item(c,Positions),row,1,LVIR_BOUNDS,&cell)){MapWindowPoints(item(c,Positions),c.window,reinterpret_cast<POINT*>(&cell),2);SetWindowPos(item(c,PositionName),HWND_TOP,cell.left,cell.top,cell.right-cell.left,cell.bottom-cell.top,SWP_SHOWWINDOW|SWP_NOACTIVATE);}}
    ListView_SetColumnWidth(item(c,Nav),0,MulDiv(224,dpi,96));ListView_SetColumnWidth(item(c,Nav),1,0);
    int extra=std::max(0,span-720);int widths[]={44,180+extra*30/100,80+extra*14/100,110+extra*17/100,120+extra*17/100,166+extra*22/100};for(int i=0;i<6;++i)ListView_SetColumnWidth(item(c,Positions),i,MulDiv(widths[i],dpi,96));
    TabCtrl_SetItemSize(item(c,Tabs),MulDiv(172,dpi,96),MulDiv(38,dpi,96));
    RedrawWindow(c.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
    c.painting=false;
}
void offsetLabel(Context& c,int base,bool custom) {
    bool normal=base==AppNormal||base==RuleNormal,rule=base==RuleNormal||base==RuleMax;
    set(c,base,std::wstring(normal?L"普通窗口：":L"最大化：")+L"自定义");
    set(c,base+3000,rule?L"继承应用":L"继承全局");check(c,base+3000,!custom);
    set(c,base+4000,L"生效偏移：X "+text(c,base+1)+L" · Y "+text(c,base+2)+L"（DIP）");
}
Offset parentOffset(Context& c,int base) {
    Offset parent{false,c.draft.offsetX,c.draft.offsetY};
    if((base==RuleNormal||base==RuleMax)&&c.selected>=0&&size_t(c.selected)<c.draft.applications.size()) {
        auto& app=c.draft.applications[size_t(c.selected)];auto offset=base==RuleNormal?app.normal:app.maximized;if(offset.custom){parent.x=offset.x;parent.y=offset.y;}
    }
    return parent;
}
void loadOffset(Context& c,int base,const Offset& offset) {
    auto shown=offset.custom?offset:parentOffset(c,base);
    check(c,base,offset.custom);
    set(c,base+1,std::to_wstring(shown.x));set(c,base+2,std::to_wstring(shown.y));EnableWindow(item(c,base+1),offset.custom);EnableWindow(item(c,base+2),offset.custom);
    offsetLabel(c,base,offset.custom);
}
bool integer(Context& c,int id,int& value,int maximum=500) {
    auto input=text(c,id);wchar_t* end{};long number=wcstol(input.c_str(),&end,10);
    if(input.empty()||*end||number < -500||number>maximum)return false;value=int(number);return true;
}
bool readOffset(Context& c,int base,Offset& offset) {offset.custom=checked(c,base);return !offset.custom||(integer(c,base+1,offset.x,0)&&integer(c,base+2,offset.y));}
Application* selected(Context& c) {return c.selected>=0&&size_t(c.selected)<c.draft.applications.size()?&c.draft.applications[size_t(c.selected)]:nullptr;}
WindowRule* selectedRule(Context& c) {auto app=selected(c);return app&&c.rule>=0&&size_t(c.rule)<app->rules.size()?&app->rules[size_t(c.rule)]:nullptr;}
bool flush(Context& c,bool explain=true) {
    if(c.loading)return true;
    bool valid=true;
    if(auto app=selected(c)) {
        {
            auto name=text(c,Name);auto normal=app->normal,maximized=app->maximized;
            valid=!name.empty()&&readOffset(c,AppNormal,normal)&&readOffset(c,AppMax,maximized);
            if(valid){app->name=name;app->enabled=checked(c,Enabled);app->normal=normal;app->maximized=maximized;set(c,Heading,name);auto row=std::find(c.navIndices.begin(),c.navIndices.end(),c.selected);if(row!=c.navIndices.end())ListView_SetItemText(item(c,Nav),int(row-c.navIndices.begin()),0,app->name.data());}
        }
        if(c.tab==2)if(auto rule=selectedRule(c)) {
            auto next=*rule;next.name=text(c,RuleName);next.className=text(c,Class);next.title=text(c,Title);
            next.titleMatch=TitleMatch(SendMessageW(item(c,TitleMode),CB_GETCURSEL,0,0));next.exclude=SendMessageW(item(c,Action),CB_GETCURSEL,0,0)==1;next.active=checked(c,Active);
            valid=valid&&!next.name.empty()&&!next.className.empty()&&(next.titleMatch==TitleMatch::Any||!next.title.empty())&&readOffset(c,RuleNormal,next.normal)&&readOffset(c,RuleMax,next.maximized);
            if(valid){*rule=std::move(next);ListView_SetItemText(item(c,RuleList),c.rule,0,rule->name.data());}
        }
    } else valid=integer(c,GlobalX,c.draft.offsetX,0)&&integer(c,GlobalY,c.draft.offsetY);
    if(valid&&c.pendingPositionName){if(!renamePositionRecord(c.draft,c.positionKey,text(c,PositionName))){if(explain)error(c,L"记录名称不能为空或超过 256 字。");return false;}c.pendingPositionName=false;}
    if(!valid&&explain)error(c,L"请填写名称、类名和所需的标题条件。\r\n水平 X：-500 至 0，负数向左；正数会遮挡系统按钮，不能应用。\r\n垂直 Y：-500 至 500，负数向上、正数向下。");
    return valid;
}
std::wstring recordSummary(Context& c,const std::wstring& key) {
    auto i=c.draft.records.find(key);if(i==c.draft.records.end())return L"尚无位置记录；在目标窗口的风钩按钮上右键记录。";
    auto& r=i->second;
    return std::to_wstring(r.normal.right-r.normal.left)+L" × "+std::to_wstring(r.normal.bottom-r.normal.top)+L" px · "+(r.maximized?L"最大化":L"普通")+L" · "+r.device+L"\r\n"+(r.savedAt.empty()?L"旧版记录，保存时间未知":r.savedAt);
}
void rulesList(Context& c) {
    ListView_DeleteAllItems(item(c,RuleList));auto app=selected(c);if(!app)return;
    for(size_t i=0;i<app->rules.size();++i) {
        auto& rule=app->rules[i];LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=int(i);row.pszText=rule.name.data();ListView_InsertItem(item(c,RuleList),&row);
        auto condition=rule.className+(rule.titleMatch==TitleMatch::Any?L"":rule.titleMatch==TitleMatch::Contains?L" · 包含 "+rule.title:L" · 等于 "+rule.title);
        ListView_SetItemText(item(c,RuleList),int(i),1,condition.data());
        std::wstring action=!rule.active?L"停用":rule.exclude?L"排除":L"独立记录";ListView_SetItemText(item(c,RuleList),int(i),2,action.data());
    }
    if(c.rule>=0)ListView_SetItemState(item(c,RuleList),c.rule,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
}
void loadRule(Context& c) {
    bool wasLoading=c.loading;c.loading=true;auto rule=selectedRule(c);
    for(int id:std::initializer_list<int>{RuleName,Class,TitleMode,Title,Action,Active,RuleNormal,RuleMax,RuleNormal+3000,RuleMax+3000,RemoveRule,UpRule,DownRule,ClearRule})EnableWindow(item(c,id),rule!=nullptr);
    if(rule) {
        set(c,RuleName,rule->name);set(c,Class,rule->className);set(c,Title,rule->title);
        SendMessageW(item(c,TitleMode),CB_SETCURSEL,int(rule->titleMatch),0);SendMessageW(item(c,Action),CB_SETCURSEL,rule->exclude?1:0,0);check(c,Active,rule->active);
        loadOffset(c,RuleNormal,rule->normal);loadOffset(c,RuleMax,rule->maximized);
        auto app=selected(c);set(c,RuleSummary,recordSummary(c,scopeKey(*app,rule->id)));
        EnableWindow(item(c,ClearRule),c.draft.records.contains(scopeKey(*app,rule->id)));
        EnableWindow(item(c,Title),rule->titleMatch!=TitleMatch::Any);EnableWindow(item(c,UpRule),c.rule>0);EnableWindow(item(c,DownRule),size_t(c.rule+1)<app->rules.size());
    } else {
        set(c,RuleName,L"");set(c,Class,L"");set(c,Title,L"");set(c,RuleSummary,L"新建规则，或选中已有规则进行编辑。类名为精确匹配，列表越靠前优先级越高。");
        for(auto id:{RuleX,RuleY,RuleMaxX,RuleMaxY})EnableWindow(item(c,id),FALSE);
    }
    bool allowed=selected(c)&&selected(c)->enabled&&(c.tab!=2||(rule&&rule->active&&!rule->exclude));
    EnableWindow(item(c,Preview),allowed);
    EnableWindow(item(c,PreviewMax),allowed);
    EnableWindow(item(c,Inherit),c.tab!=2||rule!=nullptr);
    c.loading=wasLoading;
}
void loadSelection(Context& c) {
    c.loading=true;
    HICON nextIcon{};if(auto app=selected(c))nextIcon=applicationIcon(*app);
    if(!nextIcon)nextIcon=CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));SendMessageW(item(c,AppIcon),STM_SETICON,reinterpret_cast<WPARAM>(nextIcon),0);if(c.headerIcon)DestroyIcon(c.headerIcon);c.headerIcon=nextIcon;
    if(auto app=selected(c)) {
        set(c,Heading,app->name);set(c,Name,app->name);check(c,Enabled,app->enabled);
        set(c,Path,app->path);set(c,Identity,L"应用 ID："+app->id+(app->aumid.empty()?L"":L" · AUMID: "+app->aumid));
        loadOffset(c,AppNormal,app->normal);loadOffset(c,AppMax,app->maximized);
        if(c.rule<0&&!app->rules.empty())c.rule=0;
        set(c,Summary,recordSummary(c,scopeKey(*app)));rulesList(c);loadRule(c);
        EnableWindow(item(c,ClearApp),c.draft.records.contains(scopeKey(*app)));
    } else {set(c,Heading,L"全局默认");set(c,Path,L"未单独配置的应用使用这些按钮偏移");set(c,GlobalX,std::to_wstring(c.draft.offsetX));set(c,GlobalY,std::to_wstring(c.draft.offsetY));c.tab=1;}
    set(c,PageTitle,c.tab==0?L"桌面位置预览":L"按钮位置预览");
    set(c,Preview,c.tab==1?L"预览按钮位置":L"预览普通");
    c.renaming=false;
    if(auto app=selected(c)) {
        SendMessageW(item(c,PositionScope),CB_RESETCONTENT,0,0);SendMessageW(item(c,PositionScope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"应用默认记录"));
        for(auto& rule:app->rules)SendMessageW(item(c,PositionScope),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(rule.name.c_str()));
        c.recordScope=std::clamp(c.recordScope,0,int(app->rules.size()));SendMessageW(item(c,PositionScope),CB_SETCURSEL,c.recordScope,0);
        refreshPositions(c);
    }
    SendMessageW(item(c,Tabs),TCM_SETCURSEL,c.tab,0);c.loading=false;layout(c);
    InvalidateRect(item(c,AppIcon),nullptr,TRUE);
}
void navigation(Context& c,bool reload=true) {
    c.loading=true;ListView_DeleteAllItems(item(c,Nav));if(c.icons)ImageList_RemoveAll(c.icons);
    c.navIndices.clear();auto query=text(c,SearchApps);std::transform(query.begin(),query.end(),query.begin(),[](wchar_t ch){return wchar_t(towlower(ch));});
    auto fallback=LoadIconW(nullptr,IDI_APPLICATION);ImageList_AddIcon(c.icons,fallback);
    auto add=[&](int index,const std::wstring& name,int image,const std::wstring& state){LVITEMW row{};row.mask=LVIF_TEXT|LVIF_IMAGE;row.iItem=index;row.pszText=const_cast<LPWSTR>(name.c_str());row.iImage=image;ListView_InsertItem(item(c,Nav),&row);ListView_SetItemText(item(c,Nav),index,1,const_cast<LPWSTR>(state.c_str()));};
    add(0,L"全局默认",0,L"");c.navIndices.push_back(-1);
    for(size_t i=0;i<c.draft.applications.size();++i) {
        auto& app=c.draft.applications[i];int icon=0;
        auto searchable=app.name+L" "+app.path;std::transform(searchable.begin(),searchable.end(),searchable.begin(),[](wchar_t ch){return wchar_t(towlower(ch));});if(!query.empty()&&searchable.find(query)==searchable.npos)continue;
        if(i<64) {if(auto loaded=applicationIcon(app)){icon=ImageList_AddIcon(c.icons,loaded);DestroyIcon(loaded);}}
        size_t count{};for(auto& [key,record]:c.draft.records)if(key.starts_with(app.id+L"/"))++count;
        add(int(c.navIndices.size()),app.name,icon,(app.enabled?L"启用":L"禁用")+(count?L" · 有记录":std::wstring()));c.navIndices.push_back(int(i));
    }
    auto row=std::find(c.navIndices.begin(),c.navIndices.end(),c.selected);if(row!=c.navIndices.end())ListView_SetItemState(item(c,Nav),int(row-c.navIndices.begin()),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);c.loading=false;if(reload)loadSelection(c);
}
std::wstring selectedPositionScope(Context& c) {
    auto app=selected(c);if(!app)return {};
    return scopeKey(*app,c.recordScope>0&&size_t(c.recordScope)<=app->rules.size()?app->rules[size_t(c.recordScope-1)].id:L"");
}
void refreshDesktopSummary(Context& c) {
    auto found=c.draft.records.find(c.positionKey);bool recorded=found!=c.draft.records.end(),isDefault=c.positionKey==positionScope(c.positionKey);
    RECT currentBounds{};if(IsWindow(c.sample))GetWindowRect(c.sample,&currentBounds);auto model=buildDesktopPreview(recorded?&found->second:nullptr,c.displays,currentBounds,{0,0,1000,500});
    if(recorded&&model&&model->plan){auto r=model->windowBounds;set(c,Summary,L"当前预览："+(isDefault?std::wstring(L"默认记录"):found->second.name)+L" · 显示器 "+std::to_wstring(model->plan->displayIndex+1)+L" · 桌面坐标 ("+std::to_wstring(r.left)+L", "+std::to_wstring(r.top)+L") · "+std::to_wstring(r.right-r.left)+L" × "+std::to_wstring(r.bottom-r.top)+(model->plan->environmentChanged?L" · 已按当前显示环境换算":L""));}
    else set(c,Summary,L"此记录尚未保存位置。右键更新默认记录，Ctrl + 右键新增独立记录。");
}
void selectPosition(Context& c,int row) {
    if(row<0||size_t(row)>=c.positionKeys.size())return;c.positionKey=c.positionKeys[size_t(row)];auto found=c.draft.records.find(c.positionKey);
    bool recorded=found!=c.draft.records.end(),isDefault=c.positionKey==positionScope(c.positionKey),was=c.loading;c.loading=true;
    set(c,PositionName,isDefault?L"默认记录":recorded?found->second.name:L"");
    SendMessageW(item(c,PositionName),EM_SETREADONLY,isDefault,0);EnableWindow(item(c,ActivatePosition),recorded);EnableWindow(item(c,RenamePosition),recorded&&!isDefault);EnableWindow(item(c,DeletePosition),recorded);EnableWindow(item(c,PreviewDesktop),recorded&&selected(c)&&selected(c)->enabled);EnableWindow(item(c,AddPosition),selected(c)&&selected(c)->enabled);
    refreshDesktopSummary(c);
    c.pendingPositionName=false;c.loading=was;InvalidateRect(item(c,DesktopCanvas),nullptr,TRUE);
}
void refreshPositions(Context& c,bool keepSelection) {
    auto scope=selectedPositionScope(c);bool was=c.loading;c.loading=true;ListView_DeleteAllItems(item(c,Positions));c.positionKeys.clear();
    if(scope.empty()){c.loading=was;return;}auto entries=positionEntries(c.draft,scope);
    for(size_t i=0;i<entries.size();++i){auto& entry=entries[i];c.positionKeys.push_back(entry.key);LVITEMW row{};row.mask=LVIF_TEXT;row.iItem=int(i);row.pszText=const_cast<LPWSTR>(L"");ListView_InsertItem(item(c,Positions),&row);
        auto name=entry.name+(entry.isDefault?L"（默认）":L"")+(entry.active&&entry.record?L" · 启用中":L"");ListView_SetItemText(item(c,Positions),int(i),1,name.data());
        std::wstring display=L"—",position=L"—",size=L"尚未记录",time=L"—";
        if(entry.record){auto& r=*entry.record;display=r.device;for(size_t j=0;j<c.displays.size();++j)if((!r.displayIdentity.empty()&&r.displayIdentity==c.displays[j].identity)||(r.displayIdentity.empty()&&_wcsicmp(r.device.c_str(),c.displays[j].device.c_str())==0)){display=L"显示器 "+std::to_wstring(j+1);break;}
            position=L"("+std::to_wstring(r.normal.left)+L", "+std::to_wstring(r.normal.top)+L")";size=std::to_wstring(r.normal.right-r.normal.left)+L" × "+std::to_wstring(r.normal.bottom-r.normal.top)+(r.maximized?L" · 最大化":L"");time=r.savedAt.empty()?L"时间未知":r.savedAt;if(time.size()>=23&&time.find(L" UTC")!=std::wstring::npos)time=time.substr(0,16);
        }
        ListView_SetItemText(item(c,Positions),int(i),2,display.data());ListView_SetItemText(item(c,Positions),int(i),3,position.data());ListView_SetItemText(item(c,Positions),int(i),4,size.data());ListView_SetItemText(item(c,Positions),int(i),5,time.data());ListView_SetCheckState(item(c,Positions),int(i),entry.active&&entry.record);
    }
    if(!keepSelection||std::find(c.positionKeys.begin(),c.positionKeys.end(),c.positionKey)==c.positionKeys.end())c.positionKey=activePositionKey(c.draft,scope);
    auto chosen=std::find(c.positionKeys.begin(),c.positionKeys.end(),c.positionKey);int row=chosen==c.positionKeys.end()?0:int(chosen-c.positionKeys.begin());
    ListView_SetItemState(item(c,Positions),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);selectPosition(c,row);c.loading=was;layout(c);
}
void syncExternal(Context& c) {
    if(c.preview||c.selecting||c.loading)return;bool changed=false;
    for(auto& app:c.live->applications){auto has=[&](const State& state){return std::any_of(state.applications.begin(),state.applications.end(),[&](const Application& candidate){return candidate.id==app.id;});};if(!has(c.baseline)&&!has(c.draft)){c.draft.applications.push_back(app);c.baseline.applications.push_back(app);changed=true;}}
    for(auto& [key,record]:c.live->records){auto before=c.baseline.records.find(key),after=c.draft.records.find(key);
        if(before==c.baseline.records.end()&&after==c.draft.records.end()){c.draft.records[key]=record;c.baseline.records[key]=record;changed=true;}
        else if(before!=c.baseline.records.end()&&after!=c.draft.records.end()&&(record.savedAt!=before->second.savedAt||!EqualRect(&record.normal,&before->second.normal))){auto name=after->second.name;bool renamed=name!=before->second.name;after->second=record;if(renamed)after->second.name=name;before->second=record;changed=true;}
    }
    for(auto& [scope,key]:c.live->activeRecords)if(activePositionKey(c.draft,scope)==activePositionKey(c.baseline,scope)&&activePositionKey(c.baseline,scope)!=key){c.draft.activeRecords[scope]=key;c.baseline.activeRecords[scope]=key;changed=true;}
    if(changed&&GetFocus()!=item(c,PositionName)&&!c.renaming&&!c.pendingPositionName){navigation(c,false);refreshPositions(c);}
}
bool commit(Context& c) {
    if(!flush(c))return false;
    auto next=c.draft;
    for(auto& live:c.live->applications) {
        auto before=std::find_if(c.baseline.applications.begin(),c.baseline.applications.end(),[&](auto& a){return a.id==live.id;});
        auto after=std::find_if(next.applications.begin(),next.applications.end(),[&](auto& a){return a.id==live.id;});
        if(before==c.baseline.applications.end()&&after==next.applications.end())next.applications.push_back(live);
        else if(before!=c.baseline.applications.end()&&after!=next.applications.end()&&after->path==before->path&&after->aumid==before->aumid){after->path=live.path;after->aumid=live.aumid;}
    }
    std::set<std::wstring> scopes;
    for(auto& app:next.applications){scopes.insert(scopeKey(app));for(auto& rule:app.rules)scopes.insert(scopeKey(app,rule.id));}
    next.records=c.live->records;std::wstring conflicts;
    for(auto it=next.records.begin();it!=next.records.end();) {
        bool removed=!scopes.contains(positionScope(it->first))||(c.baseline.records.contains(it->first)&&!c.draft.records.contains(it->first));
        if(removed) {
            auto before=c.baseline.records.find(it->first);
            if(before==c.baseline.records.end()||before->second.savedAt!=it->second.savedAt||!EqualRect(&before->second.normal,&it->second.normal))conflicts+=L"\r\n"+it->first;
            it=next.records.erase(it);
        } else ++it;
    }
    for(auto& [key,record]:c.draft.records)if(scopes.contains(positionScope(key))) {
        auto before=c.baseline.records.find(key);
        if(before==c.baseline.records.end())next.records.emplace(key,record);
        else if(record.name!=before->second.name){auto live=next.records.find(key);if(live!=next.records.end())live->second.name=record.name;}
    }
    // Settings edits must retain records and active selections created by the
    // titlebar while this draft was open, including records in existing scopes.
    next.activeRecords=c.live->activeRecords;
    for(auto& scope:scopes) {
        if(activePositionKey(c.draft,scope)!=activePositionKey(c.baseline,scope)) {
            auto key=activePositionKey(c.draft,scope);
            if(next.records.contains(key)){if(key==scope)next.activeRecords.erase(scope);else next.activeRecords[scope]=key;}
        }
    }
    for(auto it=next.activeRecords.begin();it!=next.activeRecords.end();) {
        if(!scopes.contains(it->first)||!next.records.contains(it->second))it=next.activeRecords.erase(it);else ++it;
    }
    for(auto& scope:scopes)if(!activePositionRecord(next,scope)) {
        auto entries=positionEntries(next,scope);
        for(auto& entry:entries)if(entry.record){activatePositionRecord(next,scope,entry.key);break;}
    }
    if(!conflicts.empty()&&MessageBoxW(c.window,(L"以下待移除范围在编辑期间新增或更新了位置记录。仍要移除吗？"+conflicts).c_str(),L"确认最新记录",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES)return false;
    try {validateApplications(next);}catch(...){error(c,L"应用身份或窗口规则有重复或无效内容，请检查后保存。");return false;}
    if(!c.store->save(next)){error(c,L"保存失败，原配置和记录已保留。请检查数据目录是否可写。");return false;}
    *c.live=next;c.draft=next;c.baseline=std::move(next);c.dirty=false;if(c.changed)c.changed();navigation(c);status(c,L"已保存；切回目标应用即可生效。");return true;
}
std::wstring browseFile(HWND owner) {
    wchar_t path[32768]{};OPENFILENAMEW dialog{sizeof(dialog)};dialog.hwndOwner=owner;dialog.lpstrFilter=L"程序或快捷方式 (*.exe;*.lnk)\0*.exe;*.lnk\0\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog)?path:L"";
}
void addIdentity(Context& c,const ApplicationIdentity& identity,const WindowMatch* window=nullptr,bool asRule=false) {
    auto existing=findApplication(c.draft,identity);bool modified=!existing;
    auto previousPath=existing?existing->path:L"",previousAumid=existing?existing->aumid:L"";
    auto& app=ensureApplication(c.draft,identity);modified=modified||app.path!=previousPath||app.aumid!=previousAumid;
    c.selected=int(&app-c.draft.applications.data());c.rule=-1;
    if(asRule&&window) {
        for(size_t i=0;i<app.rules.size();++i)if(app.rules[i].className==window->className&&app.rules[i].titleMatch==TitleMatch::Any&&!app.rules[i].exclude){c.rule=int(i);break;}
        if(c.rule<0) {if(app.rules.size()>=128){error(c,L"每个应用最多保留 128 条规则。");return;}WindowRule rule;rule.id=newId(L"r-");rule.name=window->className;rule.className=window->className;app.rules.push_back(rule);c.rule=int(app.rules.size()-1);modified=true;}
        c.tab=2;
    } else c.tab=0;
    c.dirty=c.dirty||modified;navigation(c);status(c,modified?L"已加入编辑列表，保存后生效。":L"已定位到已有应用，配置和位置记录保持不变。");
}
void addFile(Context& c,const std::wstring& file) {
    auto resolved=resolveApplicationFile(file);if(!resolved.error.empty()){error(c,resolved.error+L"\r\n"+file);return;}
    addIdentity(c,resolved.identity);
}
void confirmPicked(Context& c,HWND window) {
    auto model=describeWindow(window);if(!model.systemSafe||model.identity.path.empty())return;
    TASKDIALOG_BUTTON buttons[]={{101,L"添加整个应用"},{102,L"添加该窗口类型"},{IDCANCEL,L"取消"}};
    auto content=model.identity.path+L"\r\n\r\n窗口类名："+model.className+L"\r\n当前标题："+model.title+L"\r\n\r\n标题仅供参考，不会自动作为规则条件。";
    TASKDIALOGCONFIG dialog{sizeof(dialog)};dialog.hwndParent=c.window;dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;dialog.pszWindowTitle=L"添加窗口";dialog.pszMainInstruction=L"选择应用或窗口类型";dialog.pszContent=content.c_str();dialog.cButtons=3;dialog.pButtons=buttons;dialog.nDefaultButton=101;
    int choice{};if(SUCCEEDED(TaskDialogIndirect(&dialog,&choice,nullptr,nullptr))&&(choice==101||choice==102)) {c.sample=window;GetWindowThreadProcessId(window,&c.samplePid);addIdentity(c,model.identity,&model,choice==102);}
}
void outline(Context& c,HWND target) {
    if(!target){if(c.outline)ShowWindow(c.outline,SW_HIDE);return;}
    RECT rect{};if(!GetWindowRect(target,&rect))return;
    if(!c.outline) {
        WNDCLASSW cls{};cls.hInstance=c.instance;cls.lpszClassName=L"YiboHome.PickOutline";cls.hbrBackground=GetSysColorBrush(COLOR_HIGHLIGHT);cls.lpfnWndProc=DefWindowProcW;RegisterClassW(&cls);
        c.outline=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,L"YiboHome.PickOutline",L"",WS_POPUP,0,0,0,0,c.window,nullptr,c.instance,nullptr);
        SetLayeredWindowAttributes(c.outline,0,200,LWA_ALPHA);
    }
    int width=rect.right-rect.left,height=rect.bottom-rect.top,border=std::max(2,MulDiv(3,GetDpiForWindow(target),96));
    HRGN region=CreateRectRgn(0,0,width,height),inner=CreateRectRgn(border,border,std::max(border,width-border),std::max(border,height-border));CombineRgn(region,region,inner,RGN_DIFF);DeleteObject(inner);
    if(!SetWindowRgn(c.outline,region,FALSE))DeleteObject(region);
    SetWindowPos(c.outline,HWND_TOPMOST,rect.left,rect.top,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
LRESULT CALLBACK pickerProc(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto& c=*reinterpret_cast<Context*>(data);
    if(message==WM_LBUTTONDOWN) {if(!flush(c))return 0;c.selecting=true;c.picked=nullptr;c.pickUnavailable=false;SetFocus(window);SetCapture(window);SetCursor(LoadCursorW(nullptr,IDC_CROSS));status(c,L"按住鼠标拖到目标窗口，松开选择；Esc 取消。");return 0;}
    if(message==WM_KEYDOWN&&w==VK_ESCAPE&&c.selecting){c.selecting=false;outline(c,nullptr);ReleaseCapture();status(c,L"已取消选窗口。");return 0;}
    if(message==WM_MOUSEMOVE&&c.selecting) {
        SetCursor(LoadCursorW(nullptr,IDC_CROSS));POINT point{};GetCursorPos(&point);auto hit=GetAncestor(WindowFromPoint(point),GA_ROOT);
        auto model=describeWindow(hit);c.pickUnavailable=IsWindowVisible(hit)&&model.systemSafe&&model.identity.path.empty();if(!IsWindowVisible(hit)||!model.systemSafe||model.identity.path.empty())hit=nullptr;
        c.picked=hit;outline(c,hit);if(hit)status(c,model.className+L" · "+model.title);else if(c.pickUnavailable)status(c,L"无法读取该程序身份；可改用浏览添加。管理员或受保护窗口可能受权限限制。");return 0;
    }
    if(message==WM_LBUTTONUP&&c.selecting) {auto picked=c.picked;c.selecting=false;outline(c,nullptr);ReleaseCapture();SetCursor(LoadCursorW(nullptr,IDC_ARROW));if(picked)confirmPicked(c,picked);else status(c,c.pickUnavailable?L"无法读取该程序身份，请改用浏览添加。":L"没有选中可用窗口；本工具、桌面和任务栏不能添加。");return 0;}
    if(message==WM_CAPTURECHANGED||message==WM_CANCELMODE) {c.selecting=false;outline(c,nullptr);}
    return DefSubclassProc(window,message,w,l);
}
void endPreview(Context& c,bool restore) {
    if(!c.preview)return;c.preview=false;KillTimer(c.window,1);c.placement.restore();if(c.changed)c.changed();if(restore){ShowWindow(c.window,SW_RESTORE);SetForegroundWindow(c.window);}status(c,std::wstring(c.desktopPreview?L"桌面位置":c.previewMaximized?L"最大化按钮":L"普通按钮")+L"预览已结束，窗口已还原；更改需保存。");c.desktopPreview=false;
}
HWND findSample(Context& c,const std::wstring& scope,bool exact=true) {
    auto usable=[&](HWND window){auto model=describeWindow(window);auto app=selected(c);auto found=findApplication(c.draft,model.identity);auto policy=resolveWindow(c.draft,model);return window&&IsWindowVisible(window)&&!IsIconic(window)&&app&&model.systemSafe&&found&&found->id==app->id&&policy.allowed&&(!exact||policy.recordKey==scope);};
    if(usable(c.sample))return c.sample;
    struct Search {Context* c;const std::wstring* scope;bool exact;HWND found{};} search{&c,&scope,exact};
    EnumWindows([](HWND window,LPARAM data)->BOOL {auto& s=*reinterpret_cast<Search*>(data);if(!IsWindowVisible(window)||IsIconic(window))return TRUE;auto app=selected(*s.c);auto model=describeWindow(window);auto found=findApplication(s.c->draft,model.identity);auto policy=resolveWindow(s.c->draft,model);if(app&&model.systemSafe&&found&&found->id==app->id&&policy.allowed&&(!s.exact||policy.recordKey==*s.scope)){s.found=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
    c.sample=search.found;return c.sample;
}
void previewDesktop(Context& c) {
    if(!flush(c))return;auto found=c.draft.records.find(c.positionKey);if(found==c.draft.records.end()){error(c,L"请先选中一条已有位置记录。");return;}
    auto target=findSample(c,positionScope(c.positionKey));if(!target){error(c,L"请打开属于当前记录范围的应用窗口；被排除或已禁用的窗口不能预览。");return;}
    if(!c.placement.beginDesktop(target,found->second)){error(c,L"无法预览此窗口位置；请检查目标应用状态。");return;}
    c.desktopPreview=true;c.preview=true;ShowWindow(c.window,SW_MINIMIZE);
    if(!SetForegroundWindow(target)){endPreview(c,true);error(c,L"无法激活预览窗口，请先切换到该应用后重试。");return;}
    SetTimer(c.window,1,5000,nullptr);if(c.changed)c.changed();
}
void preview(Context& c,bool maximized) {
    if(!flush(c))return;auto app=selected(c);if(!app)return;
    auto rule=c.tab==2?selectedRule(c):nullptr;
    if(c.tab==2&&(!rule||rule->exclude||!rule->active)){error(c,L"请选择生效中的独立记录规则，再预览其按钮偏移。");return;}
    auto wanted=rule?rule->id:L"";
    auto usable=[&](HWND window){auto model=describeWindow(window);auto found=findApplication(c.draft,model.identity);auto policy=resolveWindow(c.draft,model);return window&&IsWindowVisible(window)&&!IsIconic(window)&&model.systemSafe&&found&&found->id==app->id&&policy.allowed&&(wanted.empty()||policy.scopeId==wanted);};
    if(!usable(c.sample)) {
        struct Search {Context* c;Application* app;std::wstring wanted;HWND result{};} search{&c,app,wanted};
        EnumWindows([](HWND window,LPARAM data)->BOOL {auto& s=*reinterpret_cast<Search*>(data);if(!IsWindowVisible(window)||IsIconic(window))return TRUE;auto model=describeWindow(window);auto found=findApplication(s.c->draft,model.identity);auto policy=resolveWindow(s.c->draft,model);if(model.systemSafe&&found&&found->id==s.app->id&&policy.allowed&&(s.wanted.empty()||policy.scopeId==s.wanted)){s.result=window;return FALSE;}return TRUE;},reinterpret_cast<LPARAM>(&search));
        c.sample=search.result;
    }
    if(!usable(c.sample)){error(c,rule?L"没有找到实际命中此规则的窗口，请检查类名、标题条件和规则顺序。":L"请先打开此应用的可用窗口，或用“选窗口”指定。被排除的窗口不会显示预览。");return;}
    if(!c.placement.begin(c.sample,maximized)){error(c,L"此窗口无法切换到所选预览状态，请先手动调整后重试。");return;}
    if(!usable(c.sample)){c.placement.restore();error(c,L"切换状态后窗口不再命中所选规则，请检查标题条件和规则顺序。");return;}
    c.desktopPreview=false;c.previewMaximized=maximized;SendMessageW(item(c,CaptionMode),CB_SETCURSEL,maximized?1:0,0);c.preview=true;ShowWindow(c.window,SW_MINIMIZE);if(!SetForegroundWindow(c.sample)){endPreview(c,true);error(c,L"无法激活预览窗口，请先切换到该应用后重试。");return;}
    SetTimer(c.window,1,5000,nullptr);if(c.changed)c.changed();
}
bool confirm(Context& c,const std::wstring& message) {return MessageBoxW(c.window,message.c_str(),L"确认移除",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)==IDYES;}
#include "settings_visual.h"
void build(Context& c) {
    initializeVisuals(c);fonts(c);c.displays=collectDisplays();
    auto info=[&](int id,const wchar_t* value,int group,int fontStyle=0,bool muted=false,int height=26){widget(c,L"STATIC",value,0,0,0,100,height,id,group);c.widgets.back().fontStyle=fontStyle;c.widgets.back().muted=muted;};
    auto action=[&](int id,const wchar_t* value,int group){button(c,value,id,0,0,100,group);};
    info(SidebarTitle,L"应用列表",0,2);edit(c,SearchApps,0,0,220,0,0,34);SendMessageW(item(c,SearchApps),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"搜索应用名称或路径…"));
    auto nav=widget(c,WC_LISTVIEWW,L"应用列表",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER|LVS_OWNERDRAWFIXED|LVS_SHAREIMAGELISTS,0,0,220,400,Nav,0);
    ListView_SetExtendedListViewStyle(nav,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);ListView_SetBkColor(nav,uiSide(c));ListView_SetTextBkColor(nav,CLR_NONE);
    LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=200;column.pszText=const_cast<LPWSTR>(L"应用");ListView_InsertColumn(nav,0,&column);column.cx=0;column.pszText=const_cast<LPWSTR>(L"状态");ListView_InsertColumn(nav,1,&column);
    int iconSize=MulDiv(32,GetDpiForWindow(c.window),96);c.icons=ImageList_Create(iconSize,iconSize,ILC_COLOR32|ILC_MASK,4,8);ListView_SetImageList(nav,c.icons,LVSIL_SMALL);
    info(HelpTitle,L"使用说明",0,2);info(HelpBody,L"在目标窗口的风钩按钮上操作\r\n\r\n左键：恢复启用记录\r\n右键：更新默认记录\r\nCtrl + 右键：新增记录\r\n中键：置顶 / 取消置顶",0,0,true,140);
    action(Pick,L"选窗口",0);action(Browse,L"浏览添加",0);info(DropHint,L"可拖入 exe 或快捷方式",0,3,true);
    widget(c,L"STATIC",L"应用图标",SS_ICON|SS_CENTERIMAGE|SS_REALSIZECONTROL,0,0,48,48,AppIcon,0);info(Heading,L"全局默认",0,1);SetWindowLongPtrW(item(c,Heading),GWL_STYLE,GetWindowLongPtrW(item(c,Heading),GWL_STYLE)|SS_ENDELLIPSIS);
    edit(c,Name,0,0,300,4,0,38);c.widgets.back().fontStyle=1;
    edit(c,Path,0,0,500,0,ES_READONLY,28);SetWindowLongPtrW(item(c,Path),GWL_STYLE,GetWindowLongPtrW(item(c,Path),GWL_STYLE)&~WS_BORDER);
    widget(c,L"BUTTON",L"启用此应用",WS_TABSTOP|BS_AUTOCHECKBOX,0,0,180,30,Enabled,4);
    action(Save,L"保存更改",0);action(MoreApp,L"…",4);
    auto tabs=widget(c,WC_TABCONTROLW,L"设置分类",WS_TABSTOP|TCS_FIXEDWIDTH|TCS_BUTTONS|TCS_FLATBUTTONS,0,0,800,42,Tabs,0);SetWindowTheme(tabs,L"",L"");
    for(auto title:{L"窗口位置记录",L"按钮位置调整",L"窗口规则（高级）"}){TCITEMW tab{};tab.mask=TCIF_TEXT;tab.pszText=const_cast<LPWSTR>(title);TabCtrl_InsertItem(tabs,TabCtrl_GetItemCount(tabs),&tab);}
    info(PageTitle,L"桌面位置预览",0,2);info(PageHelp,L"调整风钩按钮相对于应用标题栏的位置；普通与最大化窗口可分别设置。",0,0,true);
    widget(c,L"STATIC",L"当前显示器布局与记录位置预览",SS_NOTIFY,0,0,800,200,DesktopCanvas,1);
    widget(c,L"STATIC",L"风钩按钮相对标题栏的位置预览",SS_NOTIFY,0,0,800,200,CaptionCanvas,2);
    info(PreviewNote,L"实际预览持续 5 秒，结束后还原窗口。",2,0,true);
    info(CaptionModeLabel,L"窗口状态",2,3,true);combo(c,CaptionMode,0,0,160,2,{L"普通窗口示意",L"最大化窗口示意"});
    info(ScopeLabel,L"记录范围",1,3,true);combo(c,PositionScope,0,0,220,1,{});action(PreviewDesktop,L"预览桌面位置",1);info(Summary,L"",1,3,true);
    info(RecordsTitle,L"窗口位置记录",1,2);info(RecordsHelp,L"同一范围可保存多个位置，每次启用一条供左键恢复。",1,3,true);action(AddPosition,L"新增当前位置",1);
    auto positions=widget(c,WC_LISTVIEWW,L"窗口位置记录",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_OWNERDRAWFIXED|LVS_SHAREIMAGELISTS,0,0,800,180,Positions,1);
    ListView_SetExtendedListViewStyle(positions,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_CHECKBOXES);ListView_SetBkColor(positions,uiWhite(c));ListView_SetTextBkColor(positions,CLR_NONE);
    // LVS_SHAREIMAGELISTS leaves state-image ownership with this context.
    c.recordImages=ListView_GetImageList(positions,LVSIL_STATE);
    for(auto name:{L"启用",L"记录名称",L"显示器",L"位置（X, Y）",L"尺寸（像素）",L"更新时间 UTC"}){column.pszText=const_cast<LPWSTR>(name);column.cx=100;ListView_InsertColumn(positions,Header_GetItemCount(ListView_GetHeader(positions)),&column);}
    edit(c,PositionName,0,0,300,1,0,34);SendMessageW(item(c,PositionName),EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"编辑所选记录的名称"));
    action(ActivatePosition,L"启用此记录",1);action(RenamePosition,L"确认名称",1);action(DeletePosition,L"删除记录…",1);
    offsetRow(c,L"普通窗口：自定义",AppNormal,0,2);offsetRow(c,L"最大化：自定义",AppMax,0,2);
    action(AppNormal+7000,L"恢复继承",2);action(AppMax+7000,L"恢复继承",2);
    info(GlobalXLabel,L"水平 X（DIP）",5);edit(c,GlobalX,0,0,140,5,0,34);info(GlobalYLabel,L"垂直 Y（DIP）",5);edit(c,GlobalY,0,0,140,5,0,34);
    info(GlobalHelp,L"未单独配置的应用继承全局按钮偏移。\r\n\r\n水平 X：-500 至 0，负数向左，0 与最小化按钮贴合。\r\n垂直 Y：-500 至 500，负数向上、正数向下。\r\n\r\n首次右键记录会自动建立应用条目。",5,0,true,148);
    info(RuleHelp,L"按窗口类型区分记录或排除对话框。日常使用可不配置；列表越靠前优先级越高。",3,3,true,44);
    auto rules=widget(c,WC_LISTVIEWW,L"窗口规则",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_SHAREIMAGELISTS,0,0,800,120,RuleList,3);ListView_SetExtendedListViewStyle(rules,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
    for(auto entry:{std::pair{L"规则名称",180},std::pair{L"匹配条件",360},std::pair{L"行为",140}}){column.pszText=const_cast<LPWSTR>(entry.first);column.cx=MulDiv(entry.second,GetDpiForWindow(c.window),96);ListView_InsertColumn(rules,Header_GetItemCount(ListView_GetHeader(rules)),&column);}
    action(AddRule,L"新建规则",3);action(UpRule,L"上移",3);action(DownRule,L"下移",3);action(RemoveRule,L"移除规则…",3);
    info(RuleNameLabel,L"规则名称",3);edit(c,RuleName,0,0,600,3,0,32);info(ClassLabel,L"窗口类名",3);edit(c,Class,0,0,600,3,0,32);
    combo(c,TitleMode,0,0,152,3,{L"不限制标题",L"标题包含",L"标题等于"});edit(c,Title,0,0,600,3,0,32);
    combo(c,Action,0,0,260,3,{L"独立记录并显示按钮",L"排除此类窗口"});widget(c,L"BUTTON",L"规则生效",BS_AUTOCHECKBOX|WS_TABSTOP,0,0,180,30,Active,3);
    offsetRow(c,L"普通窗口：自定义",RuleNormal,0,3);offsetRow(c,L"最大化：自定义",RuleMax,0,3);info(RuleSummary,L"",3,3,true,42);action(ClearRule,L"清除默认记录…",3);
    action(Inherit,L"恢复继承",6);action(Preview,L"预览普通",6);action(PreviewMax,L"预览最大化",6);
    action(UpdatePath,L"更新程序…",-1);action(RemoveApp,L"移除应用…",-1);action(ClearApp,L"清除默认记录…",-1);info(Identity,L"",-1);
    info(Status,L"更改需保存；记录选择用于预览，勾选启用项后左键才会恢复该记录。",0,3,true);
    SetWindowSubclass(item(c,Pick),pickerProc,1,reinterpret_cast<DWORD_PTR>(&c));DragAcceptFiles(c.window,TRUE);fonts(c);navigation(c);SetTimer(c.window,2,2000,nullptr);
    if(!c.store->writable){EnableWindow(item(c,Save),FALSE);status(c,c.store->warning+L" 此次只读。");}
}
void command(Context& c,int id,int notification) {
    if(c.loading)return;
    if(id==SearchApps&&notification==EN_CHANGE){navigation(c,false);return;}
    if(id==CaptionMode&&notification==CBN_SELCHANGE){c.previewMaximized=SendMessageW(item(c,CaptionMode),CB_GETCURSEL,0,0)==1;InvalidateRect(item(c,CaptionCanvas),nullptr,TRUE);return;}
    if(id==PositionScope&&notification==CBN_SELCHANGE){if(!flush(c)){SendMessageW(item(c,PositionScope),CB_SETCURSEL,c.recordScope,0);return;}c.recordScope=int(SendMessageW(item(c,PositionScope),CB_GETCURSEL,0,0));c.positionKey.clear();refreshPositions(c,false);return;}
    if(id==PositionName&&notification==EN_CHANGE){c.pendingPositionName=true;c.dirty=true;status(c,L"记录名称有更改；点击“保存更改”应用。");return;}
    if(id==PositionName&&notification==EN_KILLFOCUS&&c.positionEditing){if(c.pendingPositionName&&!renamePositionRecord(c.draft,c.positionKey,text(c,PositionName))){error(c,L"记录名称不能为空或超过 256 字。");SetFocus(item(c,PositionName));return;}c.positionEditing=false;c.pendingPositionName=false;ShowWindow(item(c,PositionName),SW_HIDE);c.dirty=true;refreshPositions(c);return;}
    if(id==Name&&notification==EN_KILLFOCUS&&c.renaming){if(flush(c,false)){c.renaming=false;layout(c);}return;}
    for(auto base:{AppNormal,AppMax,RuleNormal,RuleMax})if(id==base+3000&&notification==BN_CLICKED){check(c,base,false);loadOffset(c,base,{});c.dirty=true;flush(c,false);InvalidateRect(item(c,CaptionCanvas),nullptr,TRUE);status(c,L"已恢复继承，保存后生效。");return;}
    for(auto base:{AppNormal,AppMax})if(id==base+7000&&notification==BN_CLICKED){loadOffset(c,base,{});c.dirty=true;flush(c,false);InvalidateRect(item(c,CaptionCanvas),nullptr,TRUE);status(c,L"已恢复当前窗口状态的继承设置，保存后生效。");return;}
    for(auto base:{AppNormal,AppMax,RuleNormal,RuleMax})if(id==base&&notification==BN_CLICKED){check(c,base,true);check(c,base+3000,false);}
    if(notification==EN_CHANGE||notification==CBN_SELCHANGE||(notification==BN_CLICKED&&(id==Enabled||id==Active||id==AppNormal||id==AppMax||id==RuleNormal||id==RuleMax))) {
        c.dirty=true;
        for(auto base:{AppNormal,AppMax,RuleNormal,RuleMax})if(id==base&&notification==BN_CLICKED){if(!checked(c,base)){c.loading=true;loadOffset(c,base,{});c.loading=false;}else offsetLabel(c,base,true);}
        for(auto base:{AppNormal,AppMax,RuleNormal,RuleMax}){EnableWindow(item(c,base+1),checked(c,base));EnableWindow(item(c,base+2),checked(c,base));}
        EnableWindow(item(c,Title),SendMessageW(item(c,TitleMode),CB_GETCURSEL,0,0)!=0);
        bool valid=flush(c,false);
        if(c.tab==2){bool allowed=selectedRule(c)&&checked(c,Active)&&SendMessageW(item(c,Action),CB_GETCURSEL,0,0)==0;EnableWindow(item(c,Preview),allowed);EnableWindow(item(c,PreviewMax),allowed);}
        if(auto app=selected(c)){bool allowed=app->enabled&&(c.tab!=2||(selectedRule(c)&&selectedRule(c)->active&&!selectedRule(c)->exclude));EnableWindow(item(c,Preview),allowed);EnableWindow(item(c,PreviewMax),allowed);EnableWindow(item(c,AddPosition),app->enabled);EnableWindow(item(c,PreviewDesktop),app->enabled&&c.draft.records.contains(c.positionKey));}
        for(auto base:{AppNormal,AppMax,RuleNormal,RuleMax})offsetLabel(c,base,checked(c,base));
        InvalidateRect(item(c,CaptionCanvas),nullptr,TRUE);InvalidateRect(item(c,Nav),nullptr,TRUE);
        status(c,valid?L"有未保存的更改；请预览对应状态，或保存生效。":L"字段未填完或超出范围（X -500～0；Y -500～500），暂不应用。");return;
    }
    // EN_UPDATE/EN_SETFOCUS and combo focus/dropdown notifications are not
    // commands to submit the form. In particular, '-' is valid while typing.
    if(notification!=BN_CLICKED)return;
    if(id==MoreApp){HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,RenameApp,L"重命名应用");AppendMenuW(menu,MF_STRING,UpdatePath,L"更新程序路径…");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,RemoveApp,L"移除应用…");RECT r{};GetWindowRect(item(c,MoreApp),&r);int chosen=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN,r.right,r.bottom,0,c.window,nullptr);DestroyMenu(menu);if(chosen)command(c,chosen,BN_CLICKED);return;}
    if(id==RenameApp){c.renaming=true;layout(c);SetFocus(item(c,Name));SendMessageW(item(c,Name),EM_SETSEL,0,-1);return;}
    if(id==RenamePosition){if(!renamePositionRecord(c.draft,c.positionKey,text(c,PositionName))){error(c,L"名称不能为空或超过 256 字；默认记录名称固定。");return;}c.dirty=true;refreshPositions(c);status(c,L"记录名称已修改；保存更改后生效。");return;}
    if(id==ActivatePosition){if(activatePositionRecord(c.draft,selectedPositionScope(c),c.positionKey)){c.dirty=true;refreshPositions(c);status(c,L"已选择启用记录；保存后用于左键恢复。");}return;}
    if(id==DeletePosition){
        if(!c.draft.records.contains(c.positionKey))return;
        auto message=c.positionKey==positionScope(c.positionKey)?L"清除默认记录的位置信息？其他记录保留。":L"删除所选位置记录？";
        if(confirm(c,std::wstring(message)+L"\r\n范围："+c.positionKey)){erasePositionRecord(c.draft,c.positionKey);c.dirty=true;refreshPositions(c,false);status(c,L"位置记录已更新；保存更改后生效。");}return;
    }
    if(id==AddPosition){if(!flush(c))return;auto scope=selectedPositionScope(c);auto target=findSample(c,scope);if(!target){error(c,L"请先打开当前记录范围的应用窗口，或使用“选窗口”指定。新增操作会读取它的当前位置。");return;}try{c.positionKey=savePositionRecord(c.draft,scope,capture(target),true);c.dirty=true;refreshPositions(c);status(c,L"已新增当前位置记录；保存后生效。");}catch(...){error(c,L"无法新增位置记录，请检查目标窗口或记录数量限制。");}return;}
    if(id==PreviewDesktop){previewDesktop(c);return;}
    if(id==Save){commit(c);return;}
    if(id==Browse){if(flush(c)){auto file=browseFile(c.window);if(!file.empty())addFile(c,file);}return;}
    if(id==Preview||id==PreviewMax){preview(c,id==PreviewMax||(c.tab==1&&c.previewMaximized));return;}
    if(id==Inherit&&selected(c)) {
        if(c.tab==2&&!selectedRule(c))return;
        c.loading=true;loadOffset(c,c.tab==2?RuleNormal:AppNormal,{});loadOffset(c,c.tab==2?RuleMax:AppMax,{});c.loading=false;
        c.dirty=true;flush(c,false);status(c,L"已恢复继承，保存后生效。");return;
    }
    if(id!=RemoveRule&&id!=RemoveApp&&!flush(c))return;auto app=selected(c);
    if(id==UpdatePath&&app) {
        auto file=browseFile(c.window);if(file.empty())return;auto resolved=resolveApplicationFile(file);if(!resolved.error.empty()){error(c,resolved.error);return;}
        auto other=findApplication(c.draft,resolved.identity);if(other&&other->id!=app->id){error(c,L"此程序已在名单中，请选择该条目。");return;}
        app->path=resolved.identity.path;app->aumid=resolved.identity.aumid;c.dirty=true;loadSelection(c);status(c,L"已更新程序路径，应用 ID 和位置记录保留；保存后生效。");
    }
    if(id==AddRule&&app&&app->rules.size()<128) {WindowRule rule;rule.id=newId(L"r-");rule.name=L"新窗口规则";app->rules.push_back(rule);c.rule=int(app->rules.size()-1);c.dirty=true;c.loading=true;rulesList(c);loadRule(c);c.loading=false;SetFocus(item(c,Class));}
    if((id==UpRule||id==DownRule)&&app&&selectedRule(c)) {int next=c.rule+(id==UpRule?-1:1);if(next>=0&&size_t(next)<app->rules.size()){std::swap(app->rules[size_t(c.rule)],app->rules[size_t(next)]);c.rule=next;c.dirty=true;c.loading=true;rulesList(c);loadRule(c);c.loading=false;}}
    if(id==RemoveRule&&app&&selectedRule(c)) {auto rule=*selectedRule(c);auto key=scopeKey(*app,rule.id);size_t count{};for(auto& [recordKey,record]:c.draft.records)if(positionScope(recordKey)==key)++count;if(confirm(c,L"移除规则“"+rule.name+L"”及其 "+std::to_wstring(count)+L" 份位置记录？\r\n范围："+key)){erasePositionScope(c.draft,key);app->rules.erase(app->rules.begin()+c.rule);c.rule=app->rules.empty()?-1:std::min(c.rule,int(app->rules.size()-1));c.dirty=true;loadSelection(c);}}
    if((id==ClearApp||id==ClearRule)&&app) {auto rule=id==ClearRule?selectedRule(c):nullptr;if(id==ClearRule&&!rule)return;auto key=scopeKey(*app,rule?rule->id:L"");if(c.draft.records.contains(key)&&confirm(c,L"清除此范围的默认位置记录？应用配置和其他记录保留。\r\n范围："+key)){erasePositionRecord(c.draft,key);c.dirty=true;loadSelection(c);}}
    if(id==RemoveApp&&app) {auto prefix=app->id+L"/";size_t count{};for(auto& [key,record]:c.draft.records)if(key.starts_with(prefix))++count;
        if(confirm(c,L"从配置中移除应用“"+app->name+L"”、其 "+std::to_wstring(app->rules.size())+L" 条规则及 "+std::to_wstring(count)+L" 份位置记录？\r\n应用 ID："+app->id+L"\r\n配置："+c.store->path.wstring())){std::erase_if(c.draft.records,[&](auto& entry){return entry.first.starts_with(prefix);});c.draft.applications.erase(c.draft.applications.begin()+c.selected);c.selected=-1;c.rule=-1;c.dirty=true;navigation(c);}}
}
void beginPositionEdit(Context& c,int row) {
    if(row<0||size_t(row)>=c.positionKeys.size())return;auto& key=c.positionKeys[size_t(row)];if(key==positionScope(key)||!c.draft.records.contains(key))return;
    ListView_SetItemState(item(c,Positions),row,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);if(c.positionKey!=key)return;c.positionEditing=true;layout(c);SetFocus(item(c,PositionName));SendMessageW(item(c,PositionName),EM_SETSEL,0,-1);
}
// Owner-drawn report rows are measured only when the list view is created.
// Recreate the two lists after a DPI change, preserving their visible data and
// selection without reloading the draft or touching an in-progress edit.
void remeasureList(Context& c,int id) {
    HWND old=item(c,id);if(!old)return;auto widgetIt=std::find_if(c.widgets.begin(),c.widgets.end(),[&](const Widget& w){return w.window==old;});if(widgetIt==c.widgets.end())return;
    struct Row {std::vector<std::wstring> cells;int image{};UINT state{};};std::vector<Row> rows;
    int columns=Header_GetItemCount(ListView_GetHeader(old));for(int i=0;i<ListView_GetItemCount(old);++i){Row row;LVITEMW entry{};entry.mask=LVIF_IMAGE|LVIF_STATE;entry.iItem=i;entry.stateMask=LVIS_SELECTED|LVIS_FOCUSED|LVIS_STATEIMAGEMASK;ListView_GetItem(old,&entry);row.image=entry.iImage;row.state=entry.state;for(int j=0;j<columns;++j){wchar_t value[4096]{};ListView_GetItemText(old,i,j,value,4096);row.cells.emplace_back(value);}rows.push_back(std::move(row));}
    bool focused=GetFocus()==old,visible=IsWindowVisible(old)!=FALSE;auto previous=GetWindow(old,GW_HWNDPREV);auto style=DWORD(GetWindowLongPtrW(old,GWL_STYLE))&~WS_VISIBLE;
    auto fresh=CreateWindowExW(0,WC_LISTVIEWW,id==Nav?L"应用列表":L"窗口位置记录",style,0,0,100,100,c.window,reinterpret_cast<HMENU>(INT_PTR(id)),c.instance,nullptr);if(!fresh)return;
    bool wasLoading=c.loading;c.loading=true;SendMessageW(fresh,WM_SETFONT,reinterpret_cast<WPARAM>(c.font),FALSE);
    ListView_SetExtendedListViewStyle(fresh,id==Nav?LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP:LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_CHECKBOXES);
    ListView_SetBkColor(fresh,id==Nav?uiSide(c):uiWhite(c));ListView_SetTextBkColor(fresh,CLR_NONE);
    for(int j=0;j<columns;++j){wchar_t name[128]{};LVCOLUMNW col{};col.mask=LVCF_TEXT|LVCF_WIDTH;col.pszText=name;col.cchTextMax=128;ListView_GetColumn(old,j,&col);ListView_InsertColumn(fresh,j,&col);}
    if(id==Nav)ListView_SetImageList(fresh,c.icons,LVSIL_SMALL);else {auto previousImages=c.recordImages;c.recordImages=ListView_GetImageList(fresh,LVSIL_STATE);ListView_SetImageList(old,nullptr,LVSIL_STATE);if(previousImages)ImageList_Destroy(previousImages);}
    for(size_t i=0;i<rows.size();++i){auto& row=rows[i];LVITEMW entry{};entry.mask=LVIF_TEXT|LVIF_IMAGE;entry.iItem=int(i);entry.pszText=row.cells[0].data();entry.iImage=row.image;ListView_InsertItem(fresh,&entry);for(int j=1;j<columns;++j)ListView_SetItemText(fresh,int(i),j,row.cells[size_t(j)].data());ListView_SetItemState(fresh,int(i),row.state,LVIS_SELECTED|LVIS_FOCUSED|LVIS_STATEIMAGEMASK);}
    widgetIt->window=fresh;DestroyWindow(old);SetWindowPos(fresh,previous?previous:HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:0));if(focused)SetFocus(fresh);c.loading=wasLoading;
}
}
const State* settingsPreviewState() {return current&&current->preview?&current->draft:nullptr;}
LRESULT CALLBACK settingsProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_NCCREATE){auto creation=static_cast<Creation*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);if(!creation||!creation->context)return FALSE;auto c=creation->context;creation->adopted=true;c->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c));return TRUE;}
    auto pointer=reinterpret_cast<Context*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(!pointer)return DefWindowProcW(window,message,w,l);auto& c=*pointer;
    try {
        switch(message) {
        case WM_CREATE:current=&c;build(c);return 0;
        case WM_SIZE:layout(c);return 0;
        case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT paint{};auto dc=BeginPaint(window,&paint);paintSettings(c,dc);EndPaint(window,&paint);return 0;}
        case WM_MEASUREITEM:{auto draw=reinterpret_cast<MEASUREITEMSTRUCT*>(l);draw->itemHeight=MulDiv(draw->CtlID==Nav?48:32,GetDpiForWindow(window),96);return TRUE;}
        case WM_DRAWITEM:{drawControl(c,*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:case WM_CTLCOLORBTN: {
            auto dc=reinterpret_cast<HDC>(w);auto child=reinterpret_cast<HWND>(l);int id=GetDlgCtrlID(child);bool sidebar=id==SidebarTitle||id==HelpTitle||id==HelpBody||id==DropHint;
            auto widget=std::find_if(c.widgets.begin(),c.widgets.end(),[&](const Widget& entry){return entry.window==child;});
            bool disabledNumber=uiNumber(id)&&!IsWindowEnabled(child);bool muted=id==Path||!IsWindowEnabled(child)||(widget!=c.widgets.end()&&widget->muted);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,muted?uiMuted(c):uiInk(c));SetBkColor(dc,sidebar||disabledNumber?uiSide(c):uiWhite(c));return reinterpret_cast<LRESULT>(sidebar||disabledNumber?c.sidebar:c.background);
        }
        case WM_HSCROLL:case WM_VSCROLL: {
            int bar=message==WM_HSCROLL?SB_HORZ:SB_VERT;SCROLLINFO info{sizeof(info),SIF_ALL};GetScrollInfo(window,bar,&info);int position=info.nPos;
            switch(LOWORD(w)){case SB_LINEUP:position-=24;break;case SB_LINEDOWN:position+=24;break;case SB_PAGEUP:position-=info.nPage;break;case SB_PAGEDOWN:position+=info.nPage;break;case SB_THUMBTRACK:position=info.nTrackPos;break;default:break;}
            if(bar==SB_HORZ)c.scrollX=position;else c.scrollY=position;layout(c);return 0;
        }
        case WM_MOUSEWHEEL:c.scrollY-=GET_WHEEL_DELTA_WPARAM(w)/WHEEL_DELTA*48;layout(c);return 0;
        case WM_DPICHANGED: {auto rect=reinterpret_cast<RECT*>(l);SetWindowPos(window,nullptr,rect->left,rect->top,rect->right-rect->left,rect->bottom-rect->top,SWP_NOZORDER|SWP_NOACTIVATE);fonts(c);remeasureList(c,Nav);remeasureList(c,Positions);int size=MulDiv(32,GetDpiForWindow(window),96);auto images=ImageList_Create(size,size,ILC_COLOR32|ILC_MASK,4,8);ListView_SetImageList(item(c,Nav),images,LVSIL_SMALL);if(c.icons)ImageList_Destroy(c.icons);c.icons=images;navigation(c,false);layout(c);return 0;}
        case WM_DISPLAYCHANGE:case WM_SETTINGCHANGE:case WM_THEMECHANGED:{initializeVisuals(c);c.displays=collectDisplays();refreshDesktopSummary(c);ListView_SetBkColor(item(c,Nav),uiSide(c));ListView_SetBkColor(item(c,Positions),uiWhite(c));InvalidateRect(item(c,DesktopCanvas),nullptr,TRUE);InvalidateRect(window,nullptr,TRUE);return 0;}
        case WM_COMMAND:
            if(LOWORD(w)==IDOK){if(c.renaming){if(flush(c)){c.renaming=false;layout(c);}}else commit(c);return 0;}
            if(LOWORD(w)==IDCANCEL) {
                if(c.selecting){c.selecting=false;ReleaseCapture();outline(c,nullptr);status(c,L"已取消选窗口。");}
                else SendMessageW(window,WM_CLOSE,0,0);
                return 0;
            }
            command(c,LOWORD(w),HIWORD(w));return 0;
        case WM_NOTIFY: {
            auto notification=reinterpret_cast<NMHDR*>(l);
            if(notification->idFrom==Tabs&&notification->code==TCN_SELCHANGING)return flush(c)?FALSE:TRUE;
            if(notification->idFrom==Tabs&&notification->code==TCN_SELCHANGE){c.tab=TabCtrl_GetCurSel(item(c,Tabs));loadSelection(c);status(c,c.tab==0?L"更改需保存；启用一条记录后，左键恢复该记录。":c.tab==1?L"调整按钮位置后，请保存更改。":L"规则按列表顺序匹配；修改后请保存。");return 0;}
            if(notification->idFrom==Positions&&!c.loading) {
                auto change=reinterpret_cast<NMLISTVIEW*>(l);
                if(notification->code==NM_DBLCLK){auto click=reinterpret_cast<NMITEMACTIVATE*>(l);if(click->iSubItem==1)beginPositionEdit(c,click->iItem);return 0;}
                if(notification->code==NM_RCLICK){auto click=reinterpret_cast<NMITEMACTIVATE*>(l);if(click->iItem>=0&&size_t(click->iItem)<c.positionKeys.size()){ListView_SetItemState(item(c,Positions),click->iItem,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);auto menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING|(c.positionKey==positionScope(c.positionKey)||!c.draft.records.contains(c.positionKey)?MF_GRAYED:0),1,L"重命名");POINT cursor{};GetCursorPos(&cursor);auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,window,nullptr);DestroyMenu(menu);if(action==1)beginPositionEdit(c,click->iItem);}return 0;}
                if(notification->code==LVN_ITEMCHANGING&&(change->uChanged&LVIF_STATE)&&(change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED))return flush(c)?FALSE:TRUE;
                if(notification->code==LVN_ITEMCHANGED&&(change->uChanged&LVIF_STATE)){
                    if((change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED))selectPosition(c,change->iItem);
                    if((change->uNewState&LVIS_STATEIMAGEMASK)!=(change->uOldState&LVIS_STATEIMAGEMASK)&&change->iItem>=0&&size_t(change->iItem)<c.positionKeys.size()){
                        auto key=c.positionKeys[size_t(change->iItem)];if((change->uNewState&LVIS_STATEIMAGEMASK)==INDEXTOSTATEIMAGEMASK(2)&&activatePositionRecord(c.draft,selectedPositionScope(c),key)){c.dirty=true;status(c,L"已选择启用记录；保存后用于左键恢复。");}
                        c.loading=true;auto entries=positionEntries(c.draft,selectedPositionScope(c));for(size_t i=0;i<entries.size();++i){ListView_SetCheckState(item(c,Positions),int(i),entries[i].active&&entries[i].record);auto name=entries[i].name+(entries[i].isDefault?L"（默认）":L"")+(entries[i].active&&entries[i].record?L" · 启用中":L"");ListView_SetItemText(item(c,Positions),int(i),1,name.data());}c.loading=false;
                    }return 0;
                }
            }
            if((notification->idFrom==Nav||notification->idFrom==RuleList)&&!c.loading) {
                auto change=reinterpret_cast<NMLISTVIEW*>(l);
                if(notification->code==LVN_ITEMCHANGING&&(change->uChanged&LVIF_STATE)&&(change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED))return flush(c)?FALSE:TRUE;
                if(notification->code==LVN_ITEMCHANGED&&(change->uChanged&LVIF_STATE)&&(change->uNewState&LVIS_SELECTED)&&!(change->uOldState&LVIS_SELECTED)) {
                    if(notification->idFrom==Nav&&change->iItem>=0&&size_t(change->iItem)<c.navIndices.size()){c.selected=c.navIndices[size_t(change->iItem)];c.rule=-1;c.recordScope=0;c.positionKey.clear();c.tab=0;loadSelection(c);}else if(notification->idFrom==RuleList){c.rule=change->iItem;loadRule(c);}return 0;
                }
            }break;
        }
        case WM_DROPFILES: {auto drop=reinterpret_cast<HDROP>(w);if(flush(c)){UINT count=DragQueryFileW(drop,0xffffffff,nullptr,0);if(count>16)error(c,L"每次最多拖入 16 个程序或快捷方式。");else for(UINT i=0;i<count;++i){wchar_t file[32768]{};DragQueryFileW(drop,i,file,32768);addFile(c,file);}}DragFinish(drop);return 0;}
        case WM_TIMER:if(w==1)endPreview(c,true);else if(w==2){syncExternal(c);auto displays=collectDisplays();if(!sameTopology(c.displays,displays)){c.displays=std::move(displays);refreshDesktopSummary(c);InvalidateRect(item(c,DesktopCanvas),nullptr,TRUE);}}return 0;
        case WM_CANCELMODE:if(c.selecting){c.selecting=false;ReleaseCapture();outline(c,nullptr);}return 0;
        case WM_CLOSE: {
            endPreview(c,false);
            if(c.dirty){int choice=MessageBoxW(window,L"保存应用配置和窗口规则的更改吗？",L"风钩",MB_YESNOCANCEL|MB_ICONQUESTION);if(choice==IDCANCEL)return 0;if(choice==IDYES&&!commit(c))return 0;}
            DestroyWindow(window);return 0;
        }
        case WM_NCDESTROY: {
            endPreview(c,false);KillTimer(window,2);if(c.selecting)ReleaseCapture();if(c.outline)DestroyWindow(c.outline);if(c.headerIcon)DestroyIcon(c.headerIcon);if(c.icons)ImageList_Destroy(c.icons);if(c.recordImages)ImageList_Destroy(c.recordImages);if(c.font)DeleteObject(c.font);if(c.strongFont)DeleteObject(c.strongFont);if(c.headingFont)DeleteObject(c.headingFont);if(c.sectionFont)DeleteObject(c.sectionFont);if(c.smallFont)DeleteObject(c.smallFont);if(c.iconFont)DeleteObject(c.iconFont);if(c.smallIconFont)DeleteObject(c.smallIconFont);if(c.background)DeleteObject(c.background);if(c.sidebar)DeleteObject(c.sidebar);if(c.tint)DeleteObject(c.tint);
            if(c.slot)*c.slot=nullptr;if(current==&c)current=nullptr;SetWindowLongPtrW(window,GWLP_USERDATA,0);delete pointer;return DefWindowProcW(window,message,w,l);
        }
        }
    } catch(...) {error(c,L"无法完成本次设置操作，已保存的数据保持不变。");}
    return DefWindowProcW(window,message,w,l);
}
HWND createSettings(HINSTANCE instance,HWND* slot,State& state,Store& store,void(*changed)(),HWND sample,bool visible) {
    auto owner=std::make_unique<Context>();auto c=owner.get();c->instance=instance;c->slot=slot;c->live=&state;c->store=&store;c->changed=changed;c->draft=state;c->baseline=state;c->sample=sample;c->selected=state.applications.empty()?-1:0;
    UINT dpi=GetDpiForSystem();RECT rect{0,0,MulDiv(1180,dpi,96),MulDiv(850,dpi,96)};DWORD style=WS_OVERLAPPEDWINDOW|WS_HSCROLL|WS_VSCROLL|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&rect,style,FALSE,0,dpi);
    MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(sample,MONITOR_DEFAULTTOPRIMARY),&monitor);
    int width=std::min(rect.right-rect.left,monitor.rcWork.right-monitor.rcWork.left),height=std::min(rect.bottom-rect.top,monitor.rcWork.bottom-monitor.rcWork.top);
    Creation creation{c};
    auto window=CreateWindowExW(0,L"YiboHome.Settings",L"风钩 · YiboWindowHome 0.9.0",style,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,nullptr,nullptr,instance,&creation);
    if(creation.adopted)owner.release();
    if(slot)*slot=window;
    if(visible&&window){ShowWindow(window,SW_SHOW);if(!IsWindowVisible(window))ShowWindow(window,SW_SHOW);SetForegroundWindow(window);}return window;
}
}
