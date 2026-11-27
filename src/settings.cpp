#include "plugin.h"
#include "filesystem.h"
#include "search.h"
#include "modern_search.h"
#include <set>
#include "settings_resource.h"
#include "availability.h"
#include "result_order.h"
#include "spl_gui.h"
#include <dwmapi.h>
#include <uxtheme.h>

namespace {
struct SettingsState {
    CPluginFSInterfaceAbstract* fs;
    BOOL dark = FALSE;
    Sources sources = DiscoverSources();
    std::vector<Property> catalog;
    std::set<unsigned> selected;
    unsigned sort = 1;
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

void UpdateTheme(HWND dialog, BOOL& current)
{
    const BOOL dark = UseHostDarkTheme();
    if (dark == current) return;
    current = dark;
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

void AddOrder(HWND dialog, const wchar_t* label, unsigned ascending, unsigned current)
{
    const auto index = SendDlgItemMessageW(dialog, IDC_ORDER, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    if (index < 0) return;
    SendDlgItemMessageW(dialog, IDC_ORDER, CB_SETITEMDATA, index, ascending);
    const auto descending = ascending + 1;
    if (current == ascending || current == descending) {
        SendDlgItemMessageW(dialog, IDC_ORDER, CB_SETCURSEL, index, 0);
        CheckDlgButton(dialog, IDC_DESCENDING, current == descending ? BST_CHECKED : BST_UNCHECKED);
    }
}

Source DialogSource(HWND dialog)
{
    return static_cast<Source>(SendDlgItemMessageW(dialog, IDC_SOURCE, CB_GETCURSEL, 0, 0));
}

INT_PTR ThemeMessage(HWND dialog, UINT msg, WPARAM wp, LPARAM lp, BOOL& dark)
{
    if (msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE) UpdateTheme(dialog, dark);
    if (dark && msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) {
        const auto dc = reinterpret_cast<HDC>(wp);
        const COLORREF background = msg == WM_CTLCOLORLISTBOX ? RGB(42, 42, 42) : RGB(32, 32, 32);
        SetBkColor(dc, background);
        SetDCBrushColor(dc, background);
        SetTextColor(dc, IsWindowEnabled(reinterpret_cast<HWND>(lp)) ? RGB(230, 230, 230) : RGB(155, 155, 155));
        return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
    }
    return FALSE;
}

struct AdvancedState {
    const std::vector<Property>& catalog;
    std::set<unsigned> selected;
    unsigned sort;
    bool canSort;
    BOOL dark = FALSE;
};

const Property* FocusedProperty(HWND dialog, const AdvancedState& state)
{
    auto list = GetDlgItem(dialog, IDC_PROPERTIES);
    const auto row = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (row == LB_ERR) return nullptr;
    const auto index = SendMessageW(list, LB_GETITEMDATA, row, 0);
    return static_cast<size_t>(index) < state.catalog.size() ? &state.catalog[index] : nullptr;
}

void UpdatePropertyActions(HWND dialog, const AdvancedState& state)
{
    const auto* p = FocusedProperty(dialog, state);
    EnableWindow(GetDlgItem(dialog, IDC_SORT_PROPERTY), p && state.canSort);
    EnableWindow(GetDlgItem(dialog, IDC_SHOW_PROPERTY), p && !p->canonical.empty());
    CheckDlgButton(dialog, IDC_SHOW_PROPERTY, p && state.selected.count(p->order) ? BST_CHECKED : BST_UNCHECKED);
}

void FillProperties(HWND dialog, const AdvancedState& state)
{
    const auto* previous = FocusedProperty(dialog, state);
    wchar_t filter[256]{};
    GetDlgItemTextW(dialog, IDC_PROPERTY_FILTER, filter, 256);
    auto list = GetDlgItem(dialog, IDC_PROPERTIES);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    LRESULT focus = 0;
    for (size_t i = 0; i < state.catalog.size(); ++i) {
        const auto& p = state.catalog[i];
        if (*filter && FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | NORM_IGNORECASE,
                p.title.c_str(), -1, filter, -1, nullptr, nullptr, nullptr, 0) < 0) continue;
        auto row = SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.title.c_str()));
        if (row < 0) continue;
        SendMessageW(list, LB_SETITEMDATA, row, i);
        if (previous ? previous == &p : (state.sort == p.order || state.sort == p.order + 1)) focus = row;
    }
    SendMessageW(list, LB_SETCURSEL, focus, 0);
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    UpdatePropertyActions(dialog, state);
}

INT_PTR CALLBACK AdvancedProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HELP || (msg == WM_COMMAND && LOWORD(wp) == IDHELP)) {
        gGeneral->OpenHtmlHelp(dialog, HHCDisplayContext, IDH_ADVANCED, FALSE);
        return TRUE;
    }
    if (msg == WM_INITDIALOG) {
        auto& state = *reinterpret_cast<AdvancedState*>(lp);
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        UpdateTheme(dialog, state.dark);
        FillProperties(dialog, state);
        gGeneral->MultiMonCenterWindow(dialog, GetParent(dialog), FALSE);
        return TRUE;
    }
    auto* state = reinterpret_cast<AdvancedState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    if (const auto brush = ThemeMessage(dialog, msg, wp, lp, state->dark)) return brush;
    if (msg == WM_CLOSE) { EndDialog(dialog, IDCANCEL); return TRUE; }
    if (msg != WM_COMMAND) return FALSE;
    switch (LOWORD(wp)) {
    case IDC_PROPERTY_FILTER:
        if (HIWORD(wp) == EN_CHANGE) FillProperties(dialog, *state);
        break;
    case IDC_PROPERTIES:
        if (HIWORD(wp) == LBN_SELCHANGE) UpdatePropertyActions(dialog, *state);
        break;
    case IDC_SHOW_PROPERTY:
        if (const auto* p = FocusedProperty(dialog, *state); p && !p->canonical.empty()) {
            if (IsDlgButtonChecked(dialog, IDC_SHOW_PROPERTY)) state->selected.insert(p->order);
            else state->selected.erase(p->order);
        }
        break;
    case IDC_SORT_PROPERTY:
        if (const auto* p = FocusedProperty(dialog, *state); p && state->canSort) {
            state->sort = p->order;
            EndDialog(dialog, IDC_SORT_PROPERTY);
        }
        break;
    case IDOK:
    case IDCANCEL:
        EndDialog(dialog, LOWORD(wp));
        break;
    default:
        return FALSE;
    }
    return TRUE;
}

void SourceChanged(HWND dialog, SettingsState& state)
{
    const auto modern = state.sources.UseModern(DialogSource(dialog));
    if (modern && state.catalog.empty()) state.catalog = ReadModernCatalog(state.sources);
    SendDlgItemMessageW(dialog, IDC_ORDER, CB_RESETCONTENT, 0, 0);
    if (modern && !state.catalog.empty()) {
        for (const auto& p : state.catalog)
            if (p.canonical.empty() || state.sort == p.order || state.sort == p.order + 1 || state.selected.count(p.order))
                AddOrder(dialog, p.title.c_str(), p.order, state.sort);
    } else {
        AddOrder(dialog, L"Name", 1, state.sort);
        for (const auto& c : metadataColumns) {
            const std::string name(c.title);
            AddOrder(dialog, std::wstring(name.begin(), name.end()).c_str(), c.ascending, state.sort);
        }
    }
    if (SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETCURSEL, 0, 0) == CB_ERR && state.fs) {
        SendDlgItemMessageW(dialog, IDC_ORDER, CB_SETCURSEL, 0, 0);
        CheckDlgButton(dialog, IDC_DESCENDING, BST_UNCHECKED);
    }
    EnableWindow(GetDlgItem(dialog, IDC_ADVANCED), modern && !state.catalog.empty());
}

INT_PTR CALLBACK SettingsProc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HELP || (msg == WM_COMMAND && LOWORD(wp) == IDHELP)) {
        gGeneral->OpenHtmlHelp(dialog, HHCDisplayContext, IDH_SETTINGS, FALSE);
        return TRUE;
    }
    if (msg == WM_INITDIALOG) {
        auto& state = *reinterpret_cast<SettingsState*>(lp);
        auto* fs = state.fs;
        SetWindowLongPtrW(dialog, DWLP_USER, lp);
        UpdateTheme(dialog, state.dark);
        gGUI->ArrangeHorizontalLines(dialog);
        for (int id : {IDC_COLUMNS_RULE, IDC_SEARCH_RULE, IDC_AVAILABILITY_RULE}) {
            const HWND line = GetDlgItem(dialog, id);
            const auto style = GetWindowLongPtrW(line, GWL_STYLE);
            SetWindowLongPtrW(line, GWL_STYLE, (style & ~SS_TYPEMASK) | SS_OWNERDRAW);
            // STATIC adds a non-client edge when an etched control is created.
            SetWindowLongPtrW(line, GWL_EXSTYLE, GetWindowLongPtrW(line, GWL_EXSTYLE) & ~WS_EX_STATICEDGE);
            SetWindowPos(line, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        CheckDlgButton(dialog, IDC_CHECK_AVAILABILITY, AvailabilityChecksEnabled() ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dialog, IDC_RESULT_LIMIT, configuredResultLimit, FALSE);
        const auto mask = VisibleMetadataColumns();
        for (const auto& check : checks)
            CheckDlgButton(dialog, check.control, mask & check.flag ? BST_CHECKED : BST_UNCHECKED);
        state.sort = fs ? GetResultSort(fs) : 0;
        for (const auto& p : visibleProperties) state.selected.insert(p.order);
        for (const auto* name : {L"Automatic (prefer 1.4)",
                state.sources.stable ? L"Everything 1.4" : L"Everything 1.4 (offline)",
                state.sources.modern ? L"Everything 1.5" : L"Everything 1.5 (offline)"})
            SendDlgItemMessageW(dialog, IDC_SOURCE, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendDlgItemMessageW(dialog, IDC_SOURCE, CB_SETCURSEL, static_cast<unsigned>(preferredSource), 0);
        SourceChanged(dialog, state);
        EnableWindow(GetDlgItem(dialog, IDC_DESCENDING), fs != nullptr);
        EnableWindow(GetDlgItem(dialog, IDC_ORDER), fs != nullptr);
        EnableWindow(GetDlgItem(dialog, IDC_ORDER_LABEL), fs != nullptr);
        gGeneral->MultiMonCenterWindow(dialog, GetParent(dialog), FALSE);
        return TRUE;
    }
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(dialog, DWLP_USER));
    if (!state) return FALSE;
    if (const auto brush = ThemeMessage(dialog, msg, wp, lp, state->dark)) return brush;
    if (msg == WM_DRAWITEM && (wp == IDC_COLUMNS_RULE || wp == IDC_SEARCH_RULE || wp == IDC_AVAILABILITY_RULE)) {
        const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        RECT line = item.rcItem;
        if (state->dark) {
            // Match Samandarin's PaintDarkStaticFrame on the Panels configuration page.
            line.top += (line.bottom - line.top) / 2;
            line.bottom = line.top + 1;
            SetDCBrushColor(item.hDC, RGB(0x38, 0x38, 0x38));
            FillRect(item.hDC, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        } else {
            DrawEdge(item.hDC, &line, EDGE_ETCHED, BF_TOP);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDC_SOURCE && HIWORD(wp) == CBN_SELCHANGE)
        SourceChanged(dialog, *state);
    if (msg == WM_COMMAND && LOWORD(wp) == IDC_ADVANCED) {
        const auto row = SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETCURSEL, 0, 0);
        const bool descending = IsDlgButtonChecked(dialog, IDC_DESCENDING) == BST_CHECKED;
        const unsigned current = row == CB_ERR ? 0 : static_cast<unsigned>(
            SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETITEMDATA, row, 0)) + descending;
        AdvancedState draft{state->catalog, state->selected, current, state->fs != nullptr};
        auto result = DialogBoxParamW(gInstance, MAKEINTRESOURCEW(IDD_ADVANCED), dialog, AdvancedProc,
                                      reinterpret_cast<LPARAM>(&draft));
        if (result == IDOK || result == IDC_SORT_PROPERTY) {
            state->selected = std::move(draft.selected);
            state->sort = result == IDC_SORT_PROPERTY ? draft.sort + descending : current;
            SourceChanged(dialog, *state);
        }
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK) {
        BOOL valid = FALSE;
        const UINT limit = GetDlgItemInt(dialog, IDC_RESULT_LIMIT, &valid, FALSE);
        if (!valid || limit > INT_MAX || GetWindowTextLengthW(GetDlgItem(dialog, IDC_RESULT_LIMIT)) > 10) {
            MessageBoxW(dialog, L"Enter a whole number from 0 to 2147483647.", L"Maximum results", MB_OK);
            SetFocus(GetDlgItem(dialog, IDC_RESULT_LIMIT));
            return TRUE;
        }
        unsigned mask = 0;
        for (const auto& check : checks)
            if (IsDlgButtonChecked(dialog, check.control) == BST_CHECKED) mask |= check.flag;
        std::vector<Property> selected;
        for (auto id : state->selected)
            if (auto* p = PropertyForOrder(id)) selected.push_back(*p);
        const bool availabilityChanged = SetAvailabilityChecksEnabled(
            IsDlgButtonChecked(dialog, IDC_CHECK_AVAILABILITY) == BST_CHECKED);
        const bool changed = availabilityChanged || preferredSource != DialogSource(dialog) || visibleProperties != selected || configuredResultLimit != limit;
        configuredResultLimit = limit;
        preferredSource = DialogSource(dialog);
        visibleProperties = std::move(selected);
        if (changed && mask == VisibleMetadataColumns()) RefreshSourceSettings();
        SetVisibleMetadataColumns(mask);
        auto* fs = state->fs;
        const auto order = SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETCURSEL, 0, 0);
        if (fs && IsResultsFS(fs) && order != CB_ERR) {
            const auto pair = SendDlgItemMessageW(dialog, IDC_ORDER, CB_GETITEMDATA, order, 0);
            if (pair != CB_ERR)
                SetResultSort(fs, static_cast<unsigned>(pair) + (IsDlgButtonChecked(dialog, IDC_DESCENDING) == BST_CHECKED));
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
