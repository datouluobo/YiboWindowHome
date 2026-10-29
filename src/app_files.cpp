#include "app_files.h"
#include <shobjidl.h>
#include <shlobj.h>
#include <filesystem>
#include <algorithm>

namespace home {
FileIdentity resolveApplicationFile(const std::wstring& file) {
    FileIdentity result;auto path=normalizeAppPath(file);
    if(path.empty()){result.error=L"请选择完整的程序或快捷方式路径。";return result;}
    auto extension=std::filesystem::path(path).extension().wstring();
    if(extension==L".lnk") {
        HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        IShellLinkW* link{};IPersistFile* persisted{};
        HRESULT hr=CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link));
        if(SUCCEEDED(hr))hr=link->QueryInterface(IID_PPV_ARGS(&persisted));
        if(SUCCEEDED(hr))hr=persisted->Load(path.c_str(),STGM_READ);
        wchar_t target[32768]{};
        if(SUCCEEDED(hr))hr=link->GetPath(target,32768,nullptr,SLGP_RAWPATH);
        if(persisted)persisted->Release();if(link)link->Release();if(SUCCEEDED(initialized))CoUninitialize();
        if(FAILED(hr)||!target[0]){result.error=L"此快捷方式没有可读取的 exe 目标，请改用选窗口。";return result;}
        wchar_t expanded[32768]{};DWORD size=ExpandEnvironmentStringsW(target,expanded,32768);
        path=size&&size<=32768?normalizeAppPath(expanded):L"";
        extension=std::filesystem::path(path).extension().wstring();
    }
    DWORD attributes=GetFileAttributesW(path.c_str());
    if(path.empty()||extension!=L".exe"||attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        result.error=L"请选择存在的 exe，或目标为 exe 的快捷方式。";return result;
    }
    result.identity.path=path;return result;
}
}
