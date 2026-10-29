#pragma once
#include "state.h"
namespace home {
LRESULT CALLBACK settingsProc(HWND window,UINT message,WPARAM wParam,LPARAM lParam);
HWND createSettings(HINSTANCE instance,HWND* slot,State& state,Store& store,void(*changed)(),HWND sample=nullptr,bool visible=true);
const State* settingsPreviewState();
}
