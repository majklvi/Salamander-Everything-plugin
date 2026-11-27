#include "filter_dialog.h"
#include "filesystem.h"
#include <algorithm>
#include <uxtheme.h>

namespace
{
// IDs in Open Samandarin 0.16's IDD_CHANGEFILTER resource.
constexpr int off = 406, mask = 407, maskEdit = 409;
constexpr int useEverything = 12001, filterList = 12002;
struct Session {
    EverythingFilter current;
    std::vector<EverythingFilter> filters;
    bool attached = false;
    bool accepted = false;
    int initialSelection = -1;
};
thread_local Session *opening = nullptr;

RECT Units(HWND dialog, int x, int y, int w, int h)
{
    RECT r{x, y, x + w, y + h};
    MapDialogRect(dialog, &r);
    return r;
}

void SelectMode(HWND dlg, int mode)
{
    for (const int id : {off, mask, useEverything})
        CheckDlgButton(dlg, id, id == mode ? BST_CHECKED : BST_UNCHECKED);
}

LRESULT CALLBACK FilterProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data)
{
    auto &state = *reinterpret_cast<Session *>(data);
    if ((msg == WM_HELP || (msg == WM_COMMAND && LOWORD(wp) == IDHELP)) &&
        IsDlgButtonChecked(dlg, useEverything) == BST_CHECKED) {
        gGeneral->OpenHtmlHelp(dlg, HHCDisplayContext, IDH_FILTERS, FALSE);
        return TRUE;
    }
    if (msg == WM_INITDIALOG) {
        auto result = DefSubclassProc(dlg, msg, wp, lp);
        if (state.initialSelection >= 0 && !IsDlgButtonChecked(dlg, mask))
            SelectMode(dlg, useEverything);
        return result;
    }
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(dlg, FilterProc, id);
    if (msg == WM_CLOSE)
        state.accepted = false;
    if (msg == WM_COMMAND) {
        const int control = LOWORD(wp), notification = HIWORD(wp);
        if (control == IDCANCEL)
            state.accepted = false;
        if (control == IDOK) {
            const bool use = IsDlgButtonChecked(dlg, useEverything) == BST_CHECKED;
            const auto index = SendDlgItemMessageW(dlg, filterList, CB_GETCURSEL, 0, 0);
            if (use && (index < 0 || static_cast<size_t>(index) >= state.filters.size()))
                return 0;
            state.current = use ? state.filters[index] : EverythingFilter{};
            state.accepted = true;
            // Let the host commit its mask/history normally. An Everything
            // query already filters before the viewport, so turn the panel mask off.
            if (use)
                SelectMode(dlg, off);
        } else if (control == useEverything && notification == BN_CLICKED) {
            SelectMode(dlg, useEverything);
            SetFocus(GetDlgItem(dlg, filterList));
            return 0;
        } else if (control == filterList && notification == CBN_SELCHANGE) {
            SelectMode(dlg, useEverything);
            return 0;
        } else if ((control == off || control == mask) && notification == BN_CLICKED) {
            SelectMode(dlg, control);
        } else if (control == maskEdit && (notification == CBN_SELCHANGE || notification == CBN_EDITCHANGE)) {
            SelectMode(dlg, mask);
        }
    }
    return DefSubclassProc(dlg, msg, wp, lp);
}

HWND AddControl(HWND dlg, const wchar_t *cls, const wchar_t *caption, DWORD style, int id, RECT r)
{
    HWND control = CreateWindowExW(0, cls, caption, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, r.left, r.top,
                                   r.right - r.left, r.bottom - r.top, dlg,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), gInstance, nullptr);
    if (control)
        SendMessageW(control, WM_SETFONT, SendDlgItemMessageW(dlg, mask, WM_GETFONT, 0, 0), TRUE);
    return control;
}

void ExtendDialog(HWND dlg, Session &state)
{
    HWND radio = AddControl(dlg, L"Button", L"Use &Everything Filter", BS_AUTORADIOBUTTON, useEverything,
                            Units(dlg, 16, 57, 172, 12));
    HWND combo =
        AddControl(dlg, L"ComboBox", L"", CBS_DROPDOWNLIST | WS_VSCROLL, filterList, Units(dlg, 25, 71, 201, 125));
    if (!radio || !combo || !SetWindowSubclass(dlg, FilterProc, 1, reinterpret_cast<DWORD_PTR>(&state))) {
        if (radio)
            DestroyWindow(radio);
        if (combo)
            DestroyWindow(combo);
        return;
    }

    // Keep the stock dialog's translated labels, mask history, validation,
    // help and native OK/Cancel handling. Make room above its button row.
    const int dy = Units(dlg, 0, 0, 0, 35).bottom;
    const int divider = Units(dlg, 0, 62, 0, 0).top;
    RECT wr{};
    GetWindowRect(dlg, &wr);
    constexpr UINT stay = SWP_NOACTIVATE | SWP_NOZORDER;
    SetWindowPos(dlg, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top + dy, stay | SWP_NOMOVE);
    for (HWND child = GetWindow(dlg, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (child == radio || child == combo)
            continue;
        RECT r{};
        GetWindowRect(child, &r);
        MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT *>(&r), 2);
        if (r.top >= divider)
            SetWindowPos(child, nullptr, r.left, r.top + dy, 0, 0, stay | SWP_NOSIZE);
    }
    constexpr UINT reorder = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE;
    SetWindowPos(radio, GetDlgItem(dlg, maskEdit), 0, 0, 0, 0, reorder);
    SetWindowPos(combo, radio, 0, 0, 0, 0, reorder);
    if (UseHostDarkTheme()) {
        SetWindowTheme(radio, L"DarkMode_Explorer", nullptr);
        SetWindowTheme(combo, L"DarkMode_CFD", nullptr);
    }
    int selected = -1;
    for (const auto &f : state.filters) {
        auto preview = f.expression.empty() ? std::wstring(L"all items") : f.expression;
        std::replace_if(
            preview.begin(), preview.end(), [](wchar_t c) { return c == L'\n' || c == L'\r' || c == L'\t'; },
            L' ');
        if (preview.size() > 48)
            preview = preview.substr(0, 47) + L"…";
        auto label = f.name + L" (" + preview + L")";
        const int row =
            static_cast<int>(SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str())));
        if (f.name == state.current.name)
            selected = row;
    }
    SendMessageW(combo, CB_SETDROPPEDWIDTH, Units(dlg, 0, 0, 325, 0).right, 0);
    SendMessageW(combo, CB_SETCURSEL, selected >= 0 ? selected : 0, 0);
    EnableWindow(radio, !state.filters.empty());
    EnableWindow(combo, !state.filters.empty());
    state.initialSelection = selected;
    state.attached = true;
}

// The SDK exposes the panel mask for reading only. Extend the host's live
// dialog instance during this command so it remains the writer of that mask.
LRESULT CALLBACK BeforeDialogInit(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && opening && !opening->attached) {
        const auto &call = *reinterpret_cast<CWPSTRUCT *>(lp);
        if (call.message == WM_INITDIALOG && GetDlgItem(call.hwnd, off) && GetDlgItem(call.hwnd, mask) &&
            GetDlgItem(call.hwnd, maskEdit))
            ExtendDialog(call.hwnd, *opening);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}
} // namespace

LRESULT ShowResultFilter(HWND frame, WPARAM command, LPARAM param, CPluginFSInterfaceAbstract *fs)
{
    if (opening)
        return DefSubclassProc(frame, WM_COMMAND, command, param);
    RefreshFilterCatalog();
    Session state{GetEverythingFilter(fs), FilterCatalog()};
    opening = &state;
    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, BeforeDialogInit, nullptr, GetCurrentThreadId());
    const auto result = DefSubclassProc(frame, WM_COMMAND, command, param);
    if (hook)
        UnhookWindowsHookEx(hook);
    opening = nullptr;
    if (state.accepted)
        SetEverythingFilter(fs, std::move(state.current));
    return result;
}
