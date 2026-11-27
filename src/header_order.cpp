#include "plugin.h"
#include "filesystem.h"
#include "result_listing.h"
#include "header_order.h"

#include <Everything.h>
#include <algorithm>
#include <vector>

namespace {
constexpr UINT_PTR observerId = 1;
using Rows = std::vector<const ResultRow*>;

Rows VisibleFileOrder()
{
    Rows rows;
    int index = 0;
    BOOL directory;
    while (const CFileData* file = gGeneral->GetPanelItem(PANEL_SOURCE, &index, &directory)) {
        // 0.16 keeps folders in a separate group and may always sort them by
        // name. Files reveal the chosen direction independently of that option.
        if (!directory && file->PluginData)
            rows.push_back(reinterpret_cast<const ResultRow*>(file->PluginData));
    }
    return rows;
}

// Unlike a standard WC_HEADER, 0.16's native header reports neither the
// selected column nor its direction. Accept only an unambiguous ordering of
// the same file rows after the native header has finished handling the click.
DWORD ObservedSort(const Rows& rows)
{
    bool nameAsc = true, nameDesc = true, parentAsc = true, parentDesc = true;
    for (size_t i = 1; i < rows.size(); ++i) {
        const auto& a = *rows[i - 1];
        const auto& b = *rows[i];
        int name = gGeneral->RegSetStrICmp(a.displayName.c_str(), b.displayName.c_str());
        if (!name)
            name = gGeneral->RegSetStrCmp(a.displayName.c_str(), b.displayName.c_str());
        const int parent = gGeneral->RegSetStrICmp(a.displayParent.c_str(), b.displayParent.c_str());
        nameAsc &= name <= 0;
        nameDesc &= name >= 0;
        // The host breaks ties on custom column values by ascending Name in
        // both directions (fileswn3.cpp, LessPluginCustomNameExt).
        parentAsc &= parent < 0 || (!parent && name <= 0);
        parentDesc &= parent > 0 || (!parent && name <= 0);
    }
    if (int(nameAsc) + int(nameDesc) + int(parentAsc) + int(parentDesc) != 1)
        return 0;
    if (nameAsc) return EVERYTHING_SORT_NAME_ASCENDING;
    if (nameDesc) return EVERYTHING_SORT_NAME_DESCENDING;
    if (parentAsc) return EVERYTHING_SORT_PATH_ASCENDING;
    return EVERYTHING_SORT_PATH_DESCENDING;
}

LRESULT CALLBACK HeaderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    auto* fs = reinterpret_cast<CPluginFSInterfaceAbstract*>(data);
    if (msg == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, HeaderProc, observerId);
    if (msg != WM_LBUTTONUP || GetCapture() != hwnd ||
        gGeneral->GetPanelPluginFS(PANEL_SOURCE) != fs || !IsListingCurrent(fs))
        return DefSubclassProc(hwnd, msg, wp, lp);

    auto* listing = gGeneral->GetPanelPluginData(PANEL_SOURCE);
    const Rows before = VisibleFileOrder();
    RememberResultSelection(fs);
    BeginHeaderSortObservation();
    const LRESULT result = DefSubclassProc(hwnd, msg, wp, lp);
    const auto ascending = EndHeaderSortObservation();
    if (IsResultsFS(fs) && gGeneral->GetPanelPluginFS(PANEL_SOURCE) == fs &&
        gGeneral->GetPanelPluginData(PANEL_SOURCE) == listing && IsListingCurrent(fs)) {
        const Rows after = VisibleFileOrder();
        if (ascending) {
            // SDK v2 pairs each ascending ID with the following descending ID.
            const auto descending = ascending + 1;
            const auto current = GetResultSort(fs);
            const bool reverse = current == ascending || (ascending == leastOpenedSort && current != descending);
            SetResultSort(fs, reverse ? descending : ascending);
        } else if (after != before && after.size() == before.size()) {
            if (DWORD sort = ObservedSort(after))
                SetResultSort(fs, sort);
        }
    }
    ScheduleEditorSync();
    return result;
}
}

void ObserveResultHeader(HWND header, CPluginFSInterfaceAbstract* fs)
{
    if (header)
        SetWindowSubclass(header, HeaderProc, observerId, reinterpret_cast<DWORD_PTR>(fs));
}

void StopObservingResultHeader(HWND header)
{
    if (header)
        RemoveWindowSubclass(header, HeaderProc, observerId);
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
        SendMessageW(frame, WM_COMMAND, 700, 0); // Open Samandarin 0.16 CM_ACTIVENAME
    }
}
