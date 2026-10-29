#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <oleacc.h>
#include <iostream>
#include <string>
#include <iomanip>

void escaped(BSTR text) { if(text) for(UINT i=0;i<SysStringLen(text);++i) std::wcout<<L"\\u"<<std::hex<<std::setw(4)<<std::setfill(L'0')<<unsigned(text[i])<<std::dec; }

void inspect(IAccessible* object,int depth,int& budget) {
    if(depth>7||--budget<0) return;
    VARIANT self; VariantInit(&self); self.vt=VT_I4; self.lVal=CHILDID_SELF;
    VARIANT role; VariantInit(&role); object->get_accRole(self,&role);
    BSTR name{}; object->get_accName(self,&name); LONG x{},y{},w{},h{}; object->accLocation(&x,&y,&w,&h,self);
    LONG count{}; object->get_accChildCount(&count);
    std::wcout<<std::wstring(depth,L' ')<<L"role="<<(role.vt==VT_I4?role.lVal:-1)<<L" children="<<count<<L" rect="<<x<<L","<<y<<L","<<w<<L","<<h;
    if(name&&role.vt==VT_I4&&role.lVal==ROLE_SYSTEM_PUSHBUTTON) { std::wcout<<L" button="; escaped(name); }
    std::wcout<<L"\n"; SysFreeString(name); VariantClear(&role);
    VARIANT children[64]{}; LONG actual{};
    if(SUCCEEDED(AccessibleChildren(object,0,64,children,&actual))) for(LONG i=0;i<actual;++i) {
        auto& child=children[i];
        if(child.vt==VT_DISPATCH&&child.pdispVal) { IAccessible* next{}; if(SUCCEEDED(child.pdispVal->QueryInterface(IID_IAccessible,reinterpret_cast<void**>(&next)))) { inspect(next,depth+1,budget); next->Release(); } }
        else if(child.vt==VT_I4) {
            VARIANT r; VariantInit(&r); BSTR n{}; LONG a{},b{},c{},d{}; object->get_accRole(child,&r); object->get_accName(child,&n); object->accLocation(&a,&b,&c,&d,child);
            std::wcout<<L" child-id="<<child.lVal<<L" role="<<(r.vt==VT_I4?r.lVal:-1)<<L" rect="<<a<<L","<<b<<L","<<c<<L","<<d;
            if(n&&r.vt==VT_I4&&r.lVal==ROLE_SYSTEM_PUSHBUTTON) { std::wcout<<L" button="; escaped(n); } std::wcout<<L"\n"; SysFreeString(n); VariantClear(&r);
            IDispatch* dispatch{}; if(SUCCEEDED(object->get_accChild(child,&dispatch))&&dispatch) { IAccessible* next{}; if(SUCCEEDED(dispatch->QueryInterface(IID_IAccessible,reinterpret_cast<void**>(&next)))) { inspect(next,depth+1,budget); next->Release(); } dispatch->Release(); }
        }
        VariantClear(&child);
    }
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    HWND root=reinterpret_cast<HWND>(std::stoull(argv[1]));
    for(DWORD id:{DWORD(OBJID_CLIENT),DWORD(OBJID_WINDOW),DWORD(OBJID_TITLEBAR)}) {
        IAccessible* object{}; HRESULT hr=AccessibleObjectFromWindow(root,id,IID_IAccessible,reinterpret_cast<void**>(&object));
        std::wcout<<L"object="<<std::hex<<id<<L" hr="<<hr<<std::dec<<L"\n";
        if(SUCCEEDED(hr)&&object) { int budget=128; inspect(object,0,budget); object->Release(); }
    }
    CoUninitialize();
}
