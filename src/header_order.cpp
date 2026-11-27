#include "plugin.h"
#include "subclass_dispatch.h"
#include "filesystem.h"
#include "result_listing.h"
#include "header_order.h"

#include <Everything.h>
#include "spl_gui.h"
#include <windowsx.h>
#include <algorithm>
#include <vector>

namespace {
using Rows = std::vector<const ResultRow*>;

Rows VisibleFileOrder(int panel = PANEL_SOURCE)
{
    Rows rows;
    int index = 0;
    BOOL directory;
    while (const CFileData* file = gGeneral->GetPanelItem(panel, &index, &directory)) {
        // 0.16 keeps folders in a separate group and may always sort them by
        // name. Files reveal the chosen direction independently of that option.
        if (!directory && file->PluginData)
            rows.push_back(reinterpret_cast<const ResultRow*>(file->PluginData));
    }
    return rows;
}

// Menu and accelerator IDs from the pinned 0.16 resource.rh2.
constexpr UINT leftName = 688, rightName = 694, activeName = 700;
constexpr DWORD nativeOrders[] = {EVERYTHING_SORT_NAME_ASCENDING, EVERYTHING_SORT_EXTENSION_ASCENDING,
                                  EVERYTHING_SORT_DATE_MODIFIED_ASCENDING, EVERYTHING_SORT_SIZE_ASCENDING};
bool settingHostOrder = false;
struct AppliedOrder {
    CPluginFSInterfaceAbstract* fs = nullptr;
    DWORD sort = 0;
};
AppliedOrder applied[2];

DWORD ClickOrder(CPluginFSInterfaceAbstract* fs, DWORD ascending)
{
    const auto current = GetResultSort(fs);
    return ascending + (current == ascending || (ascending == leastOpenedSort && current != ascending + 1));
}

// Ask the pinned host to populate its own Sort By submenu without opening it.
// Its checked command identifies the actual header, even for a one-row page.
DWORD CheckedNativeSort(int panel)
{
    auto* menu = gGUI->CreateMenuPopup();
    if (!menu) return 0;
    const bool left = panel == PANEL_LEFT;
    SendMessageW(gGeneral->GetMainWindowHWND(), WM_USER_INITMENUPOPUP,
                 reinterpret_cast<WPARAM>(menu), MAKELPARAM(0, left ? 5505 : 5544));
    MENU_ITEM_INFO item{};
    item.Mask = MENU_MASK_STATE;
    DWORD result = 0;
    for (UINT i = 0; i < std::size(nativeOrders); ++i) {
        if (menu->GetItemInfo((left ? leftName : rightName) + i, FALSE, &item) &&
            (item.State & MENU_STATE_CHECKED)) {
            result = nativeOrders[i];
            break;
        }
    }
    gGUI->DestroyMenuPopup(menu);
    return result;
}

HWND pressedHeader = nullptr;
POINT pressPoint{};
LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& forward, DWORD_PTR data)
{
    auto* fs = reinterpret_cast<CPluginFSInterfaceAbstract*>(data);
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        // A fixed-width divider takes capture too, but resizing is not a sort.
        const HCURSOR divider = LoadCursorW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(940));
        pressedHeader = GetCursor() == divider ? nullptr : hwnd;
        pressPoint = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    }
    if (msg == WM_NCDESTROY) {
        if (pressedHeader == hwnd) pressedHeader = nullptr;
        StopObservingWindow(hwnd, HeaderProc);
    }
    if (msg != WM_LBUTTONUP || pressedHeader != hwnd || GetCapture() != hwnd ||
        abs(GET_X_LPARAM(lp) - pressPoint.x) > GetSystemMetrics(SM_CXDRAG) ||
        abs(GET_Y_LPARAM(lp) - pressPoint.y) > GetSystemMetrics(SM_CYDRAG) ||
        gGeneral->GetPanelPluginFS(PANEL_SOURCE) != fs || !IsListingCurrent(fs)) {
        forward = true;
        return 0;
    }

    auto* listing = gGeneral->GetPanelPluginData(PANEL_SOURCE);
    pressedHeader = nullptr;
    RememberResultSelection(fs);
    BeginHeaderSortObservation();
    const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    const auto ascending = EndHeaderSortObservation();
    if (IsResultsFS(fs) && gGeneral->GetPanelPluginFS(PANEL_SOURCE) == fs &&
        gGeneral->GetPanelPluginData(PANEL_SOURCE) == listing && IsListingCurrent(fs)) {
        const DWORD native = CheckedNativeSort(gGeneral->GetSourcePanel());
        if (native || ascending) SetResultSort(fs, ClickOrder(fs, ascending ? ascending : native));
    }
    ScheduleEditorSync();
    return result;
}
}

bool HandleNativeSortCommand(UINT command)
{
    if (settingHostOrder) return false;
    int panel;
    UINT first;
    if (command >= leftName && command < leftName + 4) { panel = PANEL_LEFT; first = leftName; }
    else if (command >= rightName && command < rightName + 4) { panel = PANEL_RIGHT; first = rightName; }
    else if (command >= activeName && command < activeName + 4) { panel = PANEL_SOURCE; first = activeName; }
    else return false;
    auto* fs = gGeneral->GetPanelPluginFS(panel);
    if (!IsResultsFS(fs)) return false;
    SetResultSort(fs, ClickOrder(fs, nativeOrders[command - first]));
    return true;
}

void ForgetNativeOrder(CPluginFSInterfaceAbstract* fs)
{
    for (auto& order : applied)
        if (order.fs == fs) order = {};
}

void ApplyNativeOrder(HWND frame, int panel)
{
    auto* fs = gGeneral->GetPanelPluginFS(panel);
    auto& previous = applied[panel == PANEL_RIGHT];
    if (!IsResultsFS(fs) || UsesRankedNames(GetResultSort(fs))) {
        previous = {};
        return;
    }
    if (!IsListingCurrent(fs)) return;
    auto* listing = gGeneral->GetPanelPluginData(panel);
    const auto sort = GetResultSort(fs);
    if (!TakeNativeOrderUpdate(listing, sort)) return;
    const auto ascending = (sort & 1) ? sort : sort - 1;
    const auto i = std::find(std::begin(nativeOrders), std::end(nativeOrders), ascending);
    if (i == std::end(nativeOrders)) return;
    const UINT base = panel == PANEL_LEFT ? leftName : rightName;
    const UINT cmd = base + static_cast<UINT>(i - nativeOrders);
    const DWORD checked = CheckedNativeSort(panel);
    if (previous.fs == fs && previous.sort == sort && checked == ascending) {
        if (ascending != EVERYTHING_SORT_DATE_MODIFIED_ASCENDING) return;
        // Reissuing sort commands scrolls back to the caret. Only correct the
        // host's date direction if the refreshed rows actually run backwards.
        const auto rows = VisibleFileOrder(panel);
        if (rows.size() < 2 || rows.front()->hit.modified == rows.back()->hit.modified ||
            ((rows.front()->hit.modified > rows.back()->hit.modified) == (sort != ascending))) return;
    }
    previous = {fs, sort};
    // The host only exposes toggle commands. Select a different key first so
    // the following request starts in a known direction on either panel.
    settingHostOrder = true;
    if (!checked || checked == ascending)
        SendMessageW(frame, WM_COMMAND, cmd == base ? base + 1 : base, 0);
    SendMessageW(frame, WM_COMMAND, cmd, 0);
    bool reverse = sort != ascending;
    if (ascending == EVERYTHING_SORT_DATE_MODIFIED_ASCENDING) {
        const auto rows = VisibleFileOrder(panel);
        if (rows.size() > 1 && rows.front()->hit.modified > rows.back()->hit.modified)
            reverse = !reverse; // Samandarin's "Sort newer on top" preference.
    }
    if (reverse) SendMessageW(frame, WM_COMMAND, cmd, 0);
    settingHostOrder = false;
}

void ObserveResultHeader(HWND header, CPluginFSInterfaceAbstract* fs)
{
    if (header)
        ObserveWindow(header, HeaderProc, reinterpret_cast<DWORD_PTR>(fs));
}

void StopObservingResultHeader(HWND header)
{
    if (header)
        StopObservingWindow(header, HeaderProc);
}

void ApplyRankedOrder(HWND frame, CPluginFSInterfaceAbstract* fs)
{
    if (!UsesRankedNames(GetResultSort(fs)) || !IsListingCurrent(fs)) return;
    // The immutable page stores its rows contiguously in SDK order. Samandarin
    // may have retained a reverse or custom native sort from the previous view.
    // CM_ACTIVENAME selects Name ascending, or toggles it if already selected.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (gGeneral->GetPanelPluginFS(PANEL_SOURCE) != fs) return;
        const auto rows = VisibleFileOrder();
        if (std::is_sorted(rows.begin(), rows.end())) return;
        if (HasFilteredRanks(gGeneral->GetPanelPluginData(PANEL_SOURCE))) {
            // 0.16 sends these commands to the first visible sortable custom
            // column. Supply its private numeric sort key only for this sort.
            const UINT command = gGeneral->GetSourcePanel() == PANEL_LEFT ? 2860 : 2892;
            SetRankingFilteredRows(true);
            SendMessageW(frame, WM_COMMAND, command, 0);
            SetRankingFilteredRows(false);
        } else {
            settingHostOrder = true;
            SendMessageW(frame, WM_COMMAND, 700, 0); // CM_ACTIVENAME
            settingHostOrder = false;
        }
    }
}
