#pragma once
#include <windows.h>
#include <commctrl.h>

// Set forward to delegate after this handler returns. For messages with a
// synchronous result (e.g. a header sort), leave it false and return the result.
using SubclassHandler = LRESULT (CALLBACK*)(HWND, UINT, WPARAM, LPARAM, bool& forward, DWORD_PTR);
extern "C" LRESULT CALLBACK DispatchSubclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

inline BOOL ObserveWindow(HWND window, SubclassHandler handler, DWORD_PTR data)
{
    return SetWindowSubclass(window, DispatchSubclass, reinterpret_cast<UINT_PTR>(handler), data);
}

inline BOOL StopObservingWindow(HWND window, SubclassHandler handler)
{
    return RemoveWindowSubclass(window, DispatchSubclass, reinterpret_cast<UINT_PTR>(handler));
}
