#include "preview.h"
#include "state.h"
#include "window_ops.h"
namespace home {
bool PreviewPlacement::begin(HWND window,bool maximized) {
    restore();
    if(!IsWindow(window)||IsIconic(window))return false;
    DWORD_PTR response{};if(!SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&response))return false;
    saved_={sizeof(saved_)};
    if(!GetWindowPlacement(window,&saved_))return false;
    window_=window;GetWindowThreadProcessId(window,&pid_);
    changed_=(IsZoomed(window)!=FALSE)!=maximized;
    if(changed_&&!boundedShowWindow(window,maximized?SW_MAXIMIZE:SW_RESTORE)){restore();return false;}
    if((IsZoomed(window)!=FALSE)!=maximized){restore();return false;}
    return true;
}
bool PreviewPlacement::beginDesktop(HWND window,const Record& record) {
    restore();
    if(!IsWindow(window)||IsIconic(window))return false;
    DWORD_PTR response{};if(!SendMessageTimeoutW(window,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&response))return false;
    saved_={sizeof(saved_)};if(!GetWindowPlacement(window,&saved_))return false;
    window_=window;GetWindowThreadProcessId(window,&pid_);changed_=true;
    if(restoreDetailed(window,record)==RestoreStatus::Failed){restore();return false;}
    return true;
}
void PreviewPlacement::restore() {
    DWORD currentPid{};
    DWORD_PTR response{};
    if(window_&&changed_&&IsWindow(window_)&&GetWindowThreadProcessId(window_,&currentPid)&&currentPid==pid_&&SendMessageTimeoutW(window_,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,WindowOperationTimeoutMs,&response))boundedWindowPlacement(window_,saved_);
    window_=nullptr;pid_=0;changed_=false;
}
}
