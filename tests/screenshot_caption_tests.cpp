#include "visual_caption.h"
#include "caption_style.h"
#include <wincodec.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
struct Image {UINT width{},height{};std::vector<std::uint32_t> pixels;};
Image load(const wchar_t* name) {
    IWICImagingFactory* factory{};IWICBitmapDecoder* decoder{};IWICBitmapFrameDecode* frame{};IWICFormatConverter* converter{};
    auto path=std::filesystem::path(CAPTION_FIXTURES)/name;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(hr))hr=factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder);
    if(SUCCEEDED(hr))hr=decoder->GetFrame(0,&frame);
    if(SUCCEEDED(hr))hr=factory->CreateFormatConverter(&converter);
    if(SUCCEEDED(hr))hr=converter->Initialize(frame,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom);
    Image image;
    if(SUCCEEDED(hr))hr=converter->GetSize(&image.width,&image.height);
    if(SUCCEEDED(hr)){image.pixels.resize(size_t(image.width)*image.height);hr=converter->CopyPixels(nullptr,image.width*4,UINT(image.pixels.size()*4),reinterpret_cast<BYTE*>(image.pixels.data()));}
    if(converter)converter->Release();if(frame)frame->Release();if(decoder)decoder->Release();if(factory)factory->Release();
    if(FAILED(hr))throw std::runtime_error("Unable to read screenshot fixture");return image;
}
int wmain(int argc,wchar_t** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int failures{};
    struct Test {const wchar_t* file;const char* label;int controls,width,height,anchor;};
    for(auto test:{Test{L"chrome-caption.png","Chrome",3,57,50,234},Test{L"androws-caption.png","Androws",4,40,50,578},Test{L"codex-caption.png","Codex",3,57,46,246}}) {
        auto image=load(test.file);auto r=home::detectCaptionControls(image.pixels,image.width,image.height,120,test.controls);
        if(r)std::cout<<test.label<<" detected="<<r->left<<","<<r->top<<","<<r->right<<","<<r->bottom<<"\n";
        bool ok=r&&abs((r->right-r->left)-test.width)<=1&&r->top==0&&abs(r->bottom-test.height)<=1&&abs(r->left-test.anchor)<=1;
        std::cout<<(ok?"PASS ":"FAIL ")<<test.label<<" uploaded caption geometry\n";if(!ok)++failures;
        // Chrome's supplied image has a tooltip over the lower half of its
        // minimize button. Only the two unobscured images are style fixtures.
        if(r&&std::wstring_view(test.file)!=L"chrome-caption.png") {
            int w=r->right-r->left,h=r->bottom-r->top;
            std::vector<std::uint32_t> tile(size_t(w)*h);
            for(int y=0;y<h;++y)for(int x=0;x<w;++x)tile[size_t(y)*w+x]=image.pixels[size_t(std::min<int>(r->top+y,image.height-1))*image.width+r->left+x];
            auto style=home::analyzeCaptionButton(tile,w,h,120);
            COLORREF expected=test.controls==4?RGB(240,242,247):RGB(238,244,249);
            auto closeChannel=[](int a,int b){return abs(a-b)<=2;};
            bool match=style.valid&&closeChannel(GetRValue(style.background),GetRValue(expected))&&closeChannel(GetGValue(style.background),GetGValue(expected))&&closeChannel(GetBValue(style.background),GetBValue(expected))&&style.glyphWidth>=10&&style.glyphWidth<=14;
            std::cout<<(match?"PASS ":"FAIL ")<<test.label<<" native normal background and glyph RGB="<<int(GetRValue(style.background))<<","<<int(GetGValue(style.background))<<","<<int(GetBValue(style.background))<<" glyph="<<style.glyphWidth<<"\n";if(!match)++failures;
        }
    }
    for(int i=1;i<argc;++i) {
        auto image=load(argv[i]);int height=std::min<int>(60,image.height);
        auto r=home::detectAndrowsMinimize({image.pixels.data(),size_t(image.width)*height},image.width,height,144);
        bool ok=r&&r->right-r->left>=40&&r->right-r->left<=60;
        std::cout<<(ok?"PASS ":"FAIL ")<<"Androws supplied sidebar screenshot";
        if(r)std::cout<<" minimize="<<r->left<<","<<r->right<<" hook="<<r->left-(r->right-r->left)<<","<<r->left;
        std::cout<<'\n';if(!ok)++failures;
    }
    CoUninitialize();std::cout<<"Failures: "<<failures<<'\n';return failures?1:0;
}
