#include <windows.h>
#include <iostream>
#include <string>
namespace {
DWORD integrity(){HANDLE token{};if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return 0;DWORD size{};GetTokenInformation(token,TokenIntegrityLevel,nullptr,0,&size);auto data=new BYTE[size];DWORD rid{};if(GetTokenInformation(token,TokenIntegrityLevel,data,size,&size)){auto label=reinterpret_cast<TOKEN_MANDATORY_LABEL*>(data);auto count=*GetSidSubAuthorityCount(label->Label.Sid);rid=*GetSidSubAuthority(label->Label.Sid,count-1);}delete[] data;CloseHandle(token);return rid;}
bool createSuspended(const wchar_t* path,DWORD& error){std::wstring cmd=L"\""+std::wstring(path)+L"\"";STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};SetLastError(0);bool started=CreateProcessW(path,cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)!=FALSE;error=GetLastError();if(started){TerminateProcess(pi.hProcess,0);WaitForSingleObject(pi.hProcess,5000);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);}return started;}
}
int wmain(int argc,wchar_t** argv){if(argc!=3)return 2;auto level=integrity();DWORD appError{},fixtureError{};bool appStarted=createSuspended(argv[1],appError);bool fixtureStarted=createSuspended(argv[2],fixtureError);bool ok=level==SECURITY_MANDATORY_MEDIUM_RID&&!appStarted&&appError==ERROR_ELEVATION_REQUIRED&&fixtureStarted;std::cout<<"caller-integrity="<<level<<" app-started="<<appStarted<<" app-error="<<appError<<" fixture-started="<<fixtureStarted<<" fixture-error="<<fixtureError<<" result="<<(ok?"PASS":"FAIL")<<'\n';return ok?0:1;}
