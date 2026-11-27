#include "plugin.h"
#include "filesystem.h"
#include "search.h"
#include "settings_resource.h"
#include "result_order.h"
#include <dwmapi.h>
#include <uxtheme.h>

namespace {
struct SettingsState {
    CPluginFSInterfaceAbstract* fs;
    BOOL dark = FALSE;
};

BOOL CALLBACK ThemeControl(HWND control, LPARAM dark)
{
    wchar_t name[32]{};
    GetClassNameW(control, name, _countof(name));
    if (lstrcmpiW(name, L"Button") == 0) {
        SetWindowTheme(control, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    } else if (lstrcmpiW(name, L"ComboBox") == 0) {
        SetWindowTheme(control, dark ? L"DarkMode_CFD" : nullptr, nullptr);
    }
    return TRUE;
}

void UpdateTheme(HWND dialog, SettingsState& state)
{
    BOOL dark = FALSE;
    gGeneral->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &dark, sizeof(dark), nullptr);
    HIGHCONTRASTW contrast{sizeof(contrast), 0, nullptr};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) dark = FALSE;
    if (dark == state.dark) return;
    state.dark = dark;
    DwmSetWindowAttribute(dialog, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    EnumChildWindows(dialog, ThemeControl, dark);
    RedrawWindow(dialog, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

constexpr struct { int control; MetadataColumn flag; } checks[] = {
    {IDC_MODIFIED, ColumnModified}, {IDC_CREATED, ColumnCreated}, {IDC_ACCESSED, ColumnAccessed},
    {IDC_FILE_SIZE, ColumnSize}, {IDC_PARENT_FOLDER, ColumnParent}, {IDC_EXTENSION, ColumnExtension},
    {IDC_ATTRIBUTES, ColumnAttributes}, {IDC_RUN_COUNT, ColumnRunCount}, {IDC_DATE_RUN, ColumnDateRun},
    {IDC_RECENTLY_CHANGED, ColumnRecentlyChanged}, {IDC_FILE_LIST, ColumnFileList}
};

void AddOrder(HWND dialog, const char* label, unsigned ascending, unsigned descending, unsigned current)
{
    const auto index = SendDlgItemMessageA(dialog, IDC_ORDER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    if (index < 0) return;
    SendDlgItemMessageW(dialog, IDC_ORDER, CB_SETITEMDATA, index, MAKELONG(ascending, descending));
    if (current == ascending || current == descending) {
        SendDlgItemMessageW(dialog, IDC_ORDER, CB_SETCURSEL, index, 0);
        CheckDlgButton(dialog, IDC_DESCENDING, current == descending ? BST_CHECKED : BST_UNCHECKED);
    }
}

INT_PTR CALLBACK SettingsProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INITDIALOG) {
        auto& state = *reinterpret_cast<SettingsState*>(lp);
        auto* fs = state.fs;
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        UpdateTheme(dialog, state);
        const auto mask = VisibleMetadataColumns();
        for (const auto& check : checks)
            CheckDlgButton(dialog, check.control, mask & check.flag ? BST_CHECKED : BST_UNCHECKED);
        const auto current = fs ? GetResultSort(fs) : 0;
        AddOrder(dialog, "Name", EVERYTHING_SORT_NAME_ASCENDING, EVERYTHING_SORT_NAME_DESCENDING, current);
        for (const auto& column : metadataColumns)
            AddOrder(dialog, column.title, column.ascending, column.descending, current);
        EnableWindow(GetDlgItem(dialog, IDC_DESCENDING), fs != nullptr);
        EnableWindow(GetDlgItem(dialog, IDC_ORDER), fs != nullptr);
        EnableWindow(GetDlgItem(dialog, IDC_ORDER_LABEL), fs != nullptr);
        gGeneral->MultiMonCenterWindow(dialog, GetParent(dialog), FALSE);
        return TRUE;
    }
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    if (msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE) UpdateTheme(dialog, *state);
    if (state->dark && msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) {
        const auto dc = reinterpret_cast<HDC>(wp);
        const COLORREF background = msg == WM_CTLCOLORLISTBOX ? RGB(42, 42, 42) : RGB(32, 32, 32);
        SetBkColor(dc, background);
        SetDCBrushColor(dc, background);
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? RGB(230, 230, 230) : RGB(155, 155, 155));
        return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        unsigned mask = 0;
        for (const auto& check : checks)
            if (IsDlgButtonChecked(dialog, check.control) == BST_CHECKED) mask |= check.flag;
        SetVisibleMetadataColumns(mask);
        auto* fs = state->fs;
        const auto order = SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETCURSEL, 0, 0);
        if (fs && IsResultsFS(fs) && order != CB_ERR) {
            const auto pair = SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETITEMDATA, order, 0);
            if (pair != CB_ERR)
                SetResultSort(fs, IsDlgButtonChecked(dialog, IDC_DESCENDING) ? HIWORD(pair) : LOWORD(pair));
        }
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    if (msg == WM_CLOSE || (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL)) {
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
}

void ShowSearchSettings(HWND parent)
{
    auto* fs = gGeneral->GetPanelPluginFS(PANEL_SOURCE);
    SettingsState state{IsResultsFS(fs) ? fs : nullptr};
    if (DialogBoxParamW(gInstance, MAKEINTRESOURCEW(IDD_SEARCH_SETTINGS), parent, SettingsProc,
                        reinterpret_cast<LPARAM>(&state)) == -1)
        MessageBoxW(parent, L"Could not open Everything Search settings.", L"Everything Search", MB_ICONERROR);
}
