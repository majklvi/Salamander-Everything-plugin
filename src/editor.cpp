#include "plugin.h"
#include "subclass_dispatch.h"
#include "filesystem.h"
#include "header_order.h"
#include "actions.h"
#include "search.h"
#include "filter_dialog.h"
#include "spl_gui.h"

#include <uxtheme.h>
#include <algorithm>
#include <cwchar>
#include <list>
#include <utility>

namespace {
// Private window classes from Open Samandarin 0.16. Only bind a visible list
// when its physical side and the SDK's active results filesystem agree.
constexpr wchar_t mainClass[] = L"SalamanderMainWindowVer25";
constexpr wchar_t rightFrameClass[] = L"SalamanderDetachedPanelWindow";
constexpr wchar_t tornFrameClass[] = L"SalamanderDetachedTabWindow";
constexpr wchar_t listClass[] = L"SalamanderItemsBox";
constexpr wchar_t hostChildClass[] = L"WinLib Universal Window2";
constexpr wchar_t editorClass[] = L"Majklvi.EverythingSearch.Editor";
constexpr int filesControlId = 950;
// Stock 0.16 resource.rh2: the command used by its own Alt+Left accelerator.
constexpr UINT activeBackCommand = 832;
constexpr UINT changeFilterCommand = 779; // CM_CHANGEFILTER
constexpr UINT syncMessage = WM_APP + 1;
constexpr UINT_PTR pointerTimer = 1;
constexpr UINT placementFlags = SWP_NOZORDER | SWP_NOACTIVATE;

struct Binding {
    CPluginFSInterfaceAbstract* fs = nullptr;
    HWND frame = nullptr;
    HWND panel = nullptr;
    HWND list = nullptr;
    HWND header = nullptr;
    HWND popup = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    int fontCellHeight = 0;
    bool dark = false;
    bool composing = false;
    bool enterWhenListed = false;
    RECT natural{}; // Last rectangle proposed by the host, in panel coordinates.
    int rowHeight = 0;
    int minListHeight = 0;
};

std::list<Binding> bindings;
HWND dispatcher = nullptr;
HHOOK editKeyHook = nullptr;
constexpr UINT editFocusedResult = WM_APP + 62;
HWINEVENTHOOK foregroundObserver = nullptr;
HWINEVENTHOOK listFocusObserver = nullptr;
ATOM classAtom = 0;
bool syncPosted = false;
bool closing = false;
bool waitingForPointer = false;
CPluginFSInterfaceAbstract* focusRequest = nullptr;

LRESULT CALLBACK HostProc(HWND, UINT, WPARAM, LPARAM, bool& forward, DWORD_PTR);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, bool& forward, DWORD_PTR);

bool HasClass(HWND hwnd, const wchar_t* expected)
{
    wchar_t name[96];
    return GetClassNameW(hwnd, name, ARRAYSIZE(name)) && wcscmp(name, expected) == 0;
}

RECT InParent(HWND window, HWND parent)
{
    RECT r{};
    GetWindowRect(window, &r);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&r), 2);
    return r;
}

bool DisplayedTab(int side, CSalamanderPanelTabInfo& info)
{
    const int count = gGeneral->GetPanelTabCount(side);
    for (int i = 0; i < count; ++i) {
        info = {};
        info.StructSize = sizeof(info);
        if (gGeneral->GetPanelTabInfo(side, i, &info) &&
            (info.Flags & PANEL_TAB_FLAG_ACTIVE_ON_SIDE))
            return true;
    }
    return false;
}

HWND ListOnSide(HWND frame, int side)
{
    // In 0.16 GetMainWindowHWND() already resolves the host frame for the
    // active panel. A torn tab is removed from its original side's SDK array.
    // In that case its live FS identity and its single-list frame identify it.
    const bool main = HasClass(frame, mainClass);
    if (!main && !HasClass(frame, rightFrameClass) && !HasClass(frame, tornFrameClass)) return nullptr;
    HWND lists[2]{};
    int count = 0;
    for (HWND panel = GetWindow(frame, GW_CHILD); panel; panel = GetWindow(panel, GW_HWNDNEXT)) {
        if (!IsWindowVisible(panel) || !HasClass(panel, hostChildClass))
            continue;
        HWND list = GetDlgItem(panel, filesControlId);
        if (!HasClass(list, listClass) || !IsWindowVisible(list))
            continue;
        if (count == 2)
            return nullptr;
        lists[count++] = list;
    }
    if (!main)
        return count == 1 ? lists[0] : nullptr;
    if (count == 2) {
        RECT a = InParent(lists[0], frame);
        RECT b = InParent(lists[1], frame);
        if (b.left < a.left) {
            std::swap(a, b);
            std::swap(lists[0], lists[1]);
        }
        if (a.right <= b.left)
            return lists[side == PANEL_LEFT ? 0 : 1];
    }
    if (count == 1) {
        CSalamanderPanelTabInfo right{};
        if ((side == PANEL_LEFT && DisplayedTab(PANEL_RIGHT, right) && (right.Flags & PANEL_TAB_FLAG_DETACHED)) ||
            lists[0] == GetFocus())
            return lists[0];
    }
    return nullptr;
}

HWND SourceList(HWND frame, CPluginFSInterfaceAbstract* fs)
{
    int side;
    if (GetWindowThreadProcessId(frame, nullptr) != GetCurrentThreadId() ||
        !gGeneral->GetPanelWithPluginFS(fs, side) || side != gGeneral->GetSourcePanel())
        return nullptr;
    const bool main = HasClass(frame, mainClass);
    const bool rightFrame = HasClass(frame, rightFrameClass);
    const bool torn = HasClass(frame, tornFrameClass);
    if (!main && !rightFrame && !torn)
        return nullptr;
    CSalamanderPanelTabInfo tab{};
    const bool sourceOnSide = DisplayedTab(side, tab) && (tab.Flags & PANEL_TAB_FLAG_SOURCE);
    if (torn) {
        if (sourceOnSide) return nullptr;
    } else {
        const bool detachedSide = (tab.Flags & PANEL_TAB_FLAG_DETACHED) != 0;
        if (!sourceOnSide || rightFrame != detachedSide || (rightFrame && side != PANEL_RIGHT))
            return nullptr;
    }

    return ListOnSide(frame, side);
}

// The header and the horizontal scrollbar container share a window class.
// Only the container has WS_CLIPCHILDREN. A hidden header or another view
// means there is no supported strip for this milestone.
HWND FindHeader(HWND list)
{
    for (HWND child = GetWindow(list, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        auto style = GetWindowLongPtrW(child, GWL_STYLE);
        if ((style & WS_VISIBLE) && !(style & WS_CLIPCHILDREN) && HasClass(child, hostChildClass)) {
            return child;
        }
    }
    return nullptr;
}

int DetailedRowHeight(int fontHeight, UINT dpi)
{
    // 0.16 scales the header padding, keeps four device pixels of row
    // padding and rounds the 16-DIP panel icon up to an available size.
    constexpr UINT iconDpis[] = {96, 120, 144, 192, 240, 288, 384, 480};
    UINT iconDpi = 480;
    for (UINT bucket : iconDpis) {
        if (dpi <= bucket) {
            iconDpi = bucket;
            break;
        }
    }
    return std::max(fontHeight + 4, MulDiv(16, iconDpi, 96) + 1);
}

int StripHeight(const Binding& binding)
{
    return binding.natural.right > binding.natural.left &&
           binding.natural.bottom - binding.natural.top >= binding.rowHeight + binding.minListHeight
        ? binding.rowHeight : 0;
}

void PlaceWindow(HWND window, const RECT& r, UINT flags)
{
    SetWindowPos(window, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
}

void Unbind(Binding& binding)
{
    Binding old = binding;
    binding = {};
    StopObservingResultHeader(old.header);
    if (old.frame && std::none_of(bindings.begin(), bindings.end(),
        [&](const Binding& other) { return other.frame == old.frame; }))
        StopObservingWindow(old.frame, HostProc);
    if (old.panel) StopObservingWindow(old.panel, HostProc);
    if (old.list && StopObservingWindow(old.list, HostProc) &&
        GetParent(old.list) == old.panel) {
        PlaceWindow(old.list, old.natural, placementFlags);
    }
    if (old.popup) DestroyWindow(old.popup);
    if (old.font) DeleteObject(old.font);
}

void Layout(Binding& binding)
{
    const int h = StripHeight(binding);
    RECT results = binding.natural;
    results.top += h;
    RECT current = InParent(binding.list, binding.panel);
    if (!EqualRect(&results, &current))
        PlaceWindow(binding.list, results, placementFlags | SWP_NOSENDCHANGING);
    if (h == 0 || IsIconic(binding.frame) ||
        !IsWindowVisible(binding.panel)) {
        ShowWindow(binding.popup, SW_HIDE);
        return;
    }
    RECT strip = binding.natural;
    strip.bottom = strip.top + h;
    MapWindowPoints(binding.panel, HWND_DESKTOP, reinterpret_cast<POINT*>(&strip), 2);
    GetWindowRect(binding.popup, &current);
    if (!EqualRect(&strip, &current) || !IsWindowVisible(binding.popup))
        PlaceWindow(binding.popup, strip, placementFlags | SWP_SHOWWINDOW);
}

bool Bind(Binding& binding, HWND frame, HWND list, CPluginFSInterfaceAbstract* fs)
{
    binding.frame = frame;
    binding.list = list;
    binding.panel = GetParent(list);
    binding.fs = fs;
    binding.natural = InParent(list, binding.panel);
    binding.popup = CreateWindowExW(WS_EX_TOOLWINDOW, editorClass, L"Everything search",
        WS_POPUP | WS_CLIPCHILDREN | (IsWindowEnabled(frame) ? 0 : WS_DISABLED),
        0, 0, 0, 0, frame, nullptr, gInstance, nullptr);
    if (!binding.popup)
        return false;
    binding.edit = CreateWindowExW(0, L"EDIT", GetQuery(fs).c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0, 0, 0,
        binding.popup, nullptr, gInstance, nullptr);
    if (!binding.edit)
        return false;
    SendMessageW(binding.edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search Everything"));
    SendMessageW(binding.edit, EM_SETSEL, GetQuery(fs).size(), GetQuery(fs).size());
    return ObserveWindow(binding.edit, EditProc, 0) &&
           ObserveWindow(frame, HostProc, 0) &&
           ObserveWindow(binding.panel, HostProc, 0) &&
           ObserveWindow(list, HostProc, 0);
}

void EnterResultsIfReady(Binding& binding)
{
    if (!binding.enterWhenListed)
        return;
    if (!binding.list || !binding.edit || GetFocus() != binding.edit ||
        gGeneral->GetPanelPluginFS(PANEL_SOURCE) != binding.fs) {
        binding.enterWhenListed = false;
        return;
    }
    if (!IsListingCurrent(binding.fs))
        return;
    binding.enterWhenListed = false;
    int index = 0;
    if (const CFileData* first = gGeneral->GetPanelItem(PANEL_SOURCE, &index, nullptr)) {
        gGeneral->SetPanelFocusedItem(PANEL_SOURCE, first, FALSE);
        SetFocus(binding.list);
    }
}

bool PointerGesturePending()
{
    return GetCapture() || (GetKeyState(VK_LBUTTON) & 0x8000) ||
           (GetKeyState(VK_RBUTTON) & 0x8000) || HIWORD(GetQueueStatus(QS_MOUSEBUTTON));
}

// Directory commands from Open Samandarin 0.16 resource.rh2.
constexpr UINT compareDirsCommand = 737, findFileCommand = 741, dirMenuCommand = 924;

bool DirectoryCommandUnavailable(UINT command)
{
    if (command == compareDirsCommand)
        return IsResultsFS(gGeneral->GetPanelPluginFS(PANEL_LEFT)) ||
               IsResultsFS(gGeneral->GetPanelPluginFS(PANEL_RIGHT));
    return (command == findFileCommand || command == dirMenuCommand) &&
           IsResultsFS(gGeneral->GetPanelPluginFS(PANEL_SOURCE));
}

// Host menus persist across panel switches. Restore each item's original state
// and enable condition when its popup closes, before the host reuses it.
struct DirectoryMenuItem {
    UINT command;
    CGUIMenuPopupAbstract* popup = nullptr;
    MENU_ITEM_INFO original{};
};
DirectoryMenuItem directoryCommands[] = {{compareDirsCommand}, {findFileCommand}, {dirMenuCommand}};

void RestoreDirectoryCommands(CGUIMenuPopupAbstract* popup)
{
    for (auto& item : directoryCommands) {
        if (item.popup && (!popup || popup == item.popup)) {
            item.popup->SetItemInfo(item.command, FALSE, &item.original);
            item.popup = nullptr;
        }
    }
}

HWND sortFrame = nullptr;
LRESULT CALLBACK SortFrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& forward, DWORD_PTR)
{
    if (msg == WM_COMMAND && !lp && DirectoryCommandUnavailable(LOWORD(wp))) return 0;
    if (msg == WM_COMMAND && !lp && HandleNativeSortCommand(LOWORD(wp))) return 0;
    if (msg == WM_USER_INITMENUPOPUP && wp) {
        const auto result = DefSubclassProc(hwnd, msg, wp, lp);
        auto* popup = reinterpret_cast<CGUIMenuPopupAbstract*>(wp);
        for (auto& item : directoryCommands) {
            if (item.popup || popup->FindItemPosition(item.command) < 0 ||
                !DirectoryCommandUnavailable(item.command)) continue;
            item.original.Mask = MENU_MASK_STATE | MENU_MASK_ENABLER;
            if (!popup->GetItemInfo(item.command, FALSE, &item.original)) continue;
            auto disabled = item.original;
            disabled.State |= MENU_STATE_GRAYED;
            disabled.Enabler = nullptr;
            if (popup->SetItemInfo(item.command, FALSE, &disabled)) item.popup = popup;
        }
        return result;
    }
    if (msg == WM_USER_UNINITMENUPOPUP && wp)
        RestoreDirectoryCommands(reinterpret_cast<CGUIMenuPopupAbstract*>(wp));
    if (msg == WM_NCDESTROY) {
        RestoreDirectoryCommands(nullptr);
        StopObservingWindow(hwnd, SortFrameProc);
        sortFrame = nullptr;
    }
    forward = true;
    return 0;
}

void UpdateStrip(Binding& binding, bool focusRequested)
{
    HWND list = binding.list;
    HWND frame = binding.frame;
    auto* fs = binding.fs;
    HWND header = FindHeader(list);
    if (header != binding.header) {
        StopObservingResultHeader(binding.header);
        binding.header = header;
    }
    const bool dark = UseHostDarkTheme();
    if (binding.edit && binding.dark != dark) {
        binding.dark = dark;
        SetWindowTheme(binding.edit, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        RedrawWindow(binding.edit, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
    }
    ObserveResultHeader(header, fs);
    if (!header) {
        // Keep watching the same results list for header re-creation when the
        // user switches back to Detailed view. The list uses its full height.
        binding.rowHeight = 0;
        binding.enterWhenListed = false;
        Layout(binding);
        return;
    }
    const UINT dpi = GetDpiForWindow(list);
    RECT headerRect{};
    GetClientRect(header, &headerRect);
    const int fontHeight = headerRect.bottom - MulDiv(4, dpi, 96);
    const int row = DetailedRowHeight(fontHeight, dpi);
    binding.rowHeight = row;
    if (fontHeight > 0 && fontHeight != binding.fontCellHeight) {
        // The header measures a full text cell. Positive lfHeight uses that same GDI metric.
        HFONT font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH, L"Segoe UI");
        if (font) {
            SendMessageW(binding.edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            if (binding.font) DeleteObject(binding.font);
            binding.font = font;
            binding.fontCellHeight = fontHeight;
        }
    }
    binding.minListHeight = headerRect.bottom + row + GetSystemMetricsForDpi(SM_CYHSCROLL, dpi);
    Layout(binding);
    if (focusRequested && IsWindowVisible(binding.popup) && IsWindowEnabled(binding.popup)) {
        SetActiveWindow(binding.popup);
        SetFocus(binding.edit);
    }
    EnterResultsIfReady(binding);
    if (gGeneral->GetPanelPluginFS(PANEL_SOURCE) == fs && IsWindowEnabled(frame) && IsWindowVisible(binding.popup))
        CheckSearchPrerequisite(frame);
}

void Sync()
{
    HWND frame = gGeneral->GetMainWindowHWND();
    if (frame != sortFrame) {
        RestoreDirectoryCommands(nullptr);
        if (sortFrame) StopObservingWindow(sortFrame, SortFrameProc);
        sortFrame = frame && ObserveWindow(frame, SortFrameProc, 0) ? frame : nullptr;
    }
    if (sortFrame) {
        ApplyNativeOrder(frame, PANEL_LEFT);
        ApplyNativeOrder(frame, PANEL_RIGHT);
    }
    CPluginFSInterfaceAbstract* fs = gGeneral->GetPanelPluginFS(PANEL_SOURCE);
    if (IsResultsFS(fs)) ApplyRankedOrder(frame, fs);
    RestoreResultSelection();
    HWND sourceList = IsResultsFS(fs) ? SourceList(frame, fs) : nullptr;
    const bool focusRequested = fs && focusRequest == fs;
    focusRequest = nullptr;

    // A panel switch changes the source, but leaves both displayed tabs and
    // their list windows in place. Release a strip only when its view changes.
    DWORD start = 0, end = 0;
    bool movingEditor = false;
    bindings.remove_if([&](Binding& b) {
        bool keep = b.fs && b.list && b.panel && b.frame && GetParent(b.list) == b.panel &&
                    GetParent(b.panel) == b.frame && IsWindowVisible(b.panel);
        if (keep && b.fs == fs) keep = b.list == sourceList;
        else if (keep && !HasClass(b.frame, tornFrameClass)) {
            int side;
            keep = gGeneral->GetPanelWithPluginFS(b.fs, side) &&
                   ListOnSide(b.frame, side) == b.list;
        }
        if (keep && sourceList == b.list && b.fs != fs) keep = false;
        if (keep) return false;
        if (fs && b.fs == fs && b.edit) {
            SendMessageW(b.edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
            movingEditor = true;
        }
        Unbind(b);
        return true;
    });

    auto ensureStrip = [&](HWND list, CPluginFSInterfaceAbstract* owner) {
        if (!list || std::any_of(bindings.begin(), bindings.end(),
                [&](const Binding& b) { return b.list == list; })) return;
        if (!focusRequested && PointerGesturePending()) {
            if (!waitingForPointer) waitingForPointer = SetTimer(dispatcher, pointerTimer, 25, nullptr) != 0;
            if (waitingForPointer) return;
        }
        auto& b = bindings.emplace_back();
        if (!Bind(b, frame, list, owner)) {
            Unbind(b);
            bindings.pop_back();
        } else if (movingEditor && owner == fs)
            SendMessageW(b.edit, EM_SETSEL, start, end);
    };
    ensureStrip(sourceList, fs);
    if (HasClass(frame, mainClass)) {
        for (int side : {PANEL_LEFT, PANEL_RIGHT}) {
            auto* owner = gGeneral->GetPanelPluginFS(side);
            CSalamanderPanelTabInfo tab{};
            if (owner != fs && IsResultsFS(owner) && DisplayedTab(side, tab) &&
                !(tab.Flags & PANEL_TAB_FLAG_DETACHED))
                ensureStrip(ListOnSide(frame, side), owner);
        }
    }
    for (auto& b : bindings) UpdateStrip(b, focusRequested && b.fs == fs);
}

LRESULT CALLBACK HostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& forward, DWORD_PTR)
{
    for (auto& b : bindings) {
        if (hwnd != b.frame) continue;
        if (msg == WM_COMMAND && LOWORD(wp) == changeFilterCommand &&
            gGeneral->GetPanelPluginFS(PANEL_SOURCE) == b.fs)
            return ShowResultFilter(hwnd, wp, lp, b.fs);
        if (msg == WM_ENABLE && b.popup) EnableWindow(b.popup, wp != FALSE);
        if (msg == WM_NCDESTROY) b.frame = nullptr;
    }
    auto it = std::find_if(bindings.begin(), bindings.end(), [&](const Binding& b) {
        return hwnd == b.list || hwnd == b.panel;
    });
    if (it == bindings.end()) {
        if (msg == WM_NCDESTROY) StopObservingWindow(hwnd, HostProc);
        if (msg == WM_WINDOWPOSCHANGED || msg == WM_ACTIVATE || msg == WM_ENABLE ||
            msg == WM_SHOWWINDOW || msg == WM_DPICHANGED || msg == WM_NCDESTROY)
            ScheduleEditorSync();
        forward = true;
        return 0;
    }
    Binding& binding = *it;
    if (hwnd == binding.list &&
        (msg == WM_MOUSEWHEEL || msg == WM_KEYDOWN || msg == WM_LBUTTONDOWN || msg == WM_VSCROLL))
        ForgetResultScroll(binding.fs);
    if (hwnd == binding.list && msg == editFocusedResult) {
        SearchHit hit;
        if (gGeneral->GetPanelPluginFS(PANEL_SOURCE) == binding.fs &&
            GetResultHit(binding.fs, gGeneral->GetPanelFocusedItem(PANEL_SOURCE, nullptr), hit))
            EditResult(binding.frame, hit);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        StopObservingWindow(hwnd, HostProc);
        if (hwnd == binding.list) binding.list = nullptr;
        if (hwnd == binding.panel) binding.panel = nullptr;
        ScheduleEditorSync();
    } else if (hwnd == binding.list && msg == WM_WINDOWPOSCHANGING) {
        WINDOWPOS* p = reinterpret_cast<WINDOWPOS*>(lp);
        RECT& r = binding.natural;
        int w = r.right - r.left;
        int h = r.bottom - r.top;
        if (!(p->flags & SWP_NOSIZE)) { w = p->cx; h = p->cy; }
        if (!(p->flags & SWP_NOMOVE)) { r.left = p->x; r.top = p->y; }
        r.right = r.left + w;
        r.bottom = r.top + h;
        if (!(p->flags & SWP_NOMOVE)) p->y = r.top + StripHeight(binding);
        if (!(p->flags & SWP_NOSIZE)) p->cy = h - StripHeight(binding);
    } else if (msg == WM_WINDOWPOSCHANGED || msg == WM_ACTIVATE || msg == WM_ENABLE || msg == WM_SHOWWINDOW ||
               msg == WM_DPICHANGED || msg == WM_DPICHANGED_AFTERPARENT || msg == WM_PARENTNOTIFY) {
        if (hwnd == binding.panel && msg == WM_SHOWWINDOW && !wp && binding.popup)
            ShowWindow(binding.popup, SW_HIDE);
        ScheduleEditorSync();
    }
    forward = true;
    return 0;
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool& forward, DWORD_PTR)
{
    auto it = std::find_if(bindings.begin(), bindings.end(),
        [&](const Binding& b) { return b.edit == hwnd; });
    if (it == bindings.end()) {
        forward = true;
        return 0;
    }
    Binding& binding = *it;
    if (msg == WM_HELP) {
        gGeneral->OpenHtmlHelp(hwnd, HHCDisplayContext, IDH_SEARCH, FALSE);
        return 0;
    }
    if (msg == WM_KILLFOCUS) binding.enterWhenListed = false;
    if (msg == WM_IME_STARTCOMPOSITION) binding.composing = true;
    if (msg == WM_IME_ENDCOMPOSITION) binding.composing = false;
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (msg == WM_KEYDOWN && wp >= VK_F3 && wp <= VK_F6 && control &&
        !(GetKeyState(VK_SHIFT) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && !binding.composing) {
        if (!(lp & (1L << 30)))
            PostMessageW(binding.frame, WM_COMMAND, 700 + wp - VK_F3, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_F3 && (GetKeyState(VK_SHIFT) & 0x8000) &&
        !control && !(GetKeyState(VK_MENU) & 0x8000) && !binding.composing &&
        binding.fs && gGeneral->GetPanelPluginFS(PANEL_SOURCE) == binding.fs) {
        if (!(lp & (1L << 30)))
            gGeneral->PostMenuExtCommand(openContainingFolder, FALSE);
        return 0;
    }
    const bool back = msg == WM_SYSKEYDOWN && wp == VK_LEFT && (lp & (1L << 29)) && !control;
    const bool filter = msg == WM_KEYDOWN && wp == VK_F12 && control && !(GetKeyState(VK_MENU) & 0x8000);
    if ((back || filter) && !(GetKeyState(VK_SHIFT) & 0x8000) &&
        !binding.composing && binding.frame && gGeneral->GetPanelPluginFS(PANEL_SOURCE) == binding.fs) {
        if (!(lp & (1L << 30))) {
            binding.enterWhenListed = false;
            SetActiveWindow(binding.frame);
            SetFocus(binding.list);
            PostMessageW(binding.frame, WM_COMMAND, MAKEWPARAM(filter ? changeFilterCommand : activeBackCommand, 1), 0);
        }
        return 0;
    }
    if (msg == WM_CHAR && !binding.composing && (wp == VK_TAB || wp == VK_ESCAPE || wp == VK_RETURN))
        return 0;
    if (msg == WM_KEYDOWN && !binding.composing && binding.list &&
        (wp == VK_TAB || wp == VK_ESCAPE || wp == VK_DOWN || wp == VK_RETURN)) {
        binding.enterWhenListed = false;
        if (gGeneral->GetPanelPluginFS(PANEL_SOURCE) != binding.fs) {
            ScheduleEditorSync();
            return 0;
        }
        if (wp == VK_TAB) {
            gGeneral->ChangePanel();
        } else if (wp == VK_ESCAPE) {
            SetFocus(binding.list);
        } else {
            binding.enterWhenListed = true;
            EnterResultsIfReady(binding);
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        StopObservingWindow(hwnd, EditProc);
        if (hwnd == binding.edit) binding.edit = nullptr;
    }
    forward = true;
    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (hwnd == dispatcher && msg == WM_TIMER && wp == pointerTimer) {
        if (!PointerGesturePending()) {
            KillTimer(hwnd, pointerTimer);
            waitingForPointer = false;
            ScheduleEditorSync();
        }
        return 0;
    }
    if (hwnd == dispatcher && msg == syncMessage) {
        syncPosted = false;
        Sync();
        return 0;
    }
    auto it = std::find_if(bindings.begin(), bindings.end(),
        [&](const Binding& b) { return b.popup == hwnd; });
    if (it != bindings.end()) {
        Binding& binding = *it;
        if (msg == WM_MOUSEACTIVATE && gGeneral->GetPanelPluginFS(PANEL_SOURCE) != binding.fs) {
            int side;
            CSalamanderPanelTabInfo tab{};
            if (HasClass(binding.frame, tornFrameClass)) {
                SetActiveWindow(binding.frame);
                SetFocus(binding.list);
            } else if (gGeneral->GetPanelWithPluginFS(binding.fs, side) && DisplayedTab(side, tab)) {
                gGeneral->ActivatePanelTab(tab.TabId, TRUE);
            }
            return gGeneral->GetPanelPluginFS(PANEL_SOURCE) == binding.fs ? MA_ACTIVATE : MA_NOACTIVATEANDEAT;
        }
        if ((msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC) && binding.dark &&
            reinterpret_cast<HWND>(lp) == binding.edit) {
            const auto dc = reinterpret_cast<HDC>(wp);
            constexpr COLORREF background = RGB(42, 42, 42);
            SetBkColor(dc, background);
            SetTextColor(dc, RGB(230, 230, 230));
            SetDCBrushColor(dc, background);
            return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
        }
        if (msg == WM_THEMECHANGED || msg == WM_SETTINGCHANGE) ScheduleEditorSync();
        if (msg == WM_NCDESTROY) binding.popup = nullptr;
        if (msg == WM_SIZE && binding.edit) {
            MoveWindow(binding.edit, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
            return 0;
        }
        if (msg == WM_COMMAND && HIWORD(wp) == EN_CHANGE &&
            reinterpret_cast<HWND>(lp) == binding.edit && IsResultsFS(binding.fs) &&
            gGeneral->GetPanelPluginFS(PANEL_SOURCE) == binding.fs) {
            binding.enterWhenListed = false;
            const int length = GetWindowTextLengthW(binding.edit);
            std::wstring text(static_cast<size_t>(length) + 1, L'\0');
            text.resize(GetWindowTextW(binding.edit, text.data(), length + 1));
            SetQuery(binding.fs, text);
            return 0;
        }
        if (msg == WM_CLOSE) return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
void CALLBACK HostFocusChanged(HWINEVENTHOOK, DWORD event, HWND window, LONG, LONG, DWORD, DWORD)
{
    // A real result can receive focus on mouse-up after WM_MOUSEACTIVATE
    // preserved the press without activating the frame. Re-read the SDK on
    // our next message, once the host has finished selecting that panel.
    if (dispatcher && (event == EVENT_SYSTEM_FOREGROUND || HasClass(window, listClass)))
        ScheduleEditorSync();
}

// The 0.16 FS interface has no EditFile callback. Translate bare F4 before
// the host consumes its accelerator, only for the bound result list.
LRESULT CALLBACK ResultKey(int code, WPARAM removed, LPARAM data)
{
    if (code == HC_ACTION && removed == PM_REMOVE) {
        auto* msg = reinterpret_cast<MSG*>(data);
        if (msg->message == WM_KEYDOWN &&
            msg->wParam == VK_F4 && !(GetKeyState(VK_SHIFT) & 0x8000) &&
            !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
            std::any_of(bindings.begin(), bindings.end(), [&](const Binding& b) { return b.list == msg->hwnd; }))
            msg->message = (msg->lParam & (1L << 30)) ? WM_NULL : editFocusedResult;
    }
    return CallNextHookEx(nullptr, code, removed, data);
}

bool EnsureDispatcher()
{
    if (!dispatcher) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = gInstance;
        wc.lpszClassName = editorClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
        if (!classAtom) classAtom = RegisterClassW(&wc);
        if (!classAtom) return false;
        dispatcher = CreateWindowExW(0, editorClass, nullptr, 0, 0, 0, 0, 0,
                                     HWND_MESSAGE, nullptr, gInstance, nullptr);
        if (!dispatcher) return false;
        foregroundObserver = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
            nullptr, HostFocusChanged, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
        listFocusObserver = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS,
            nullptr, HostFocusChanged, GetCurrentProcessId(), GetCurrentThreadId(), WINEVENT_OUTOFCONTEXT);
        editKeyHook = SetWindowsHookExW(WH_GETMESSAGE, ResultKey, nullptr, GetCurrentThreadId());
        if (!editKeyHook || !foregroundObserver || !listFocusObserver) {
            if (editKeyHook) UnhookWindowsHookEx(editKeyHook);
            editKeyHook = nullptr;
            if (foregroundObserver) UnhookWinEvent(foregroundObserver);
            if (listFocusObserver) UnhookWinEvent(listFocusObserver);
            foregroundObserver = nullptr;
            listFocusObserver = nullptr;
            DestroyWindow(dispatcher);
            dispatcher = nullptr;
            return false;
        }
    }
    return true;
}
} // namespace

void ScheduleEditorSync()
{
    if (closing || syncPosted)
        return;
    // A restored tab can open in the background, and its later activation in
    // a torn frame may arrive only through the foreground observer.
    if (!dispatcher && (!gGeneral || !HasOpenResultsFS() || !EnsureDispatcher()))
        return;
    syncPosted = PostMessageW(dispatcher, syncMessage, 0, 0) != FALSE;
}

HWND ResultLayoutWindow(BOOL left)
{
    HWND list = ListOnSide(gGeneral->GetMainWindowHWND(), left ? PANEL_LEFT : PANEL_RIGHT);
    HWND header = list ? FindHeader(list) : nullptr;
    return header ? header : list;
}

// The Detailed list's vertical scrollbar uses row indices. Samandarin
// reads its 32-bit track position instead of WM_VSCROLL's 16-bit argument.
static HWND ResultScrollBar(CPluginFSInterfaceAbstract* fs)
{
    for (const auto& b : bindings) {
        if (b.fs != fs || !b.header || !IsWindowVisible(b.header)) continue;
        HWND bar = FindWindowExW(b.list, nullptr, L"ScrollBar", nullptr);
        if (bar && (GetWindowLongPtrW(bar, GWL_STYLE) & SBS_VERT)) return bar;
    }
    return nullptr;
}

int ResultScrollPosition(CPluginFSInterfaceAbstract* fs)
{
    const HWND bar = ResultScrollBar(fs);
    SCROLLINFO info{sizeof(info), SIF_POS};
    return bar && GetScrollInfo(bar, SB_CTL, &info) ? info.nPos : -1;
}

void RestoreResultScroll(CPluginFSInterfaceAbstract* fs, int position)
{
    const HWND bar = ResultScrollBar(fs);
    SCROLLINFO info{sizeof(info), SIF_POS};
    if (!bar || GetCapture() == bar || !GetScrollInfo(bar, SB_CTL, &info) || info.nPos == position) return;
    info.nPos = position;
    SetScrollInfo(bar, SB_CTL, &info, FALSE);
    SendMessageW(GetParent(bar), WM_VSCROLL, SB_THUMBPOSITION, reinterpret_cast<LPARAM>(bar));
}

void ShowEditorForActivePanel()
{
    focusRequest = gGeneral->GetPanelPluginFS(PANEL_SOURCE);
    ScheduleEditorSync();
}

void ForgetEditorFS(CPluginFSInterfaceAbstract* fs)
{
    ForgetNativeOrder(fs);
    if (focusRequest == fs) focusRequest = nullptr;
    bindings.remove_if([&](Binding& b) {
        if (b.fs != fs) return false;
        Unbind(b);
        return true;
    });
}

void ShutdownEditor()
{
    closing = true;
    RestoreDirectoryCommands(nullptr);
    if (sortFrame) StopObservingWindow(sortFrame, SortFrameProc);
    sortFrame = nullptr;
    if (editKeyHook) UnhookWindowsHookEx(editKeyHook);
    editKeyHook = nullptr;
    if (foregroundObserver) UnhookWinEvent(foregroundObserver);
    foregroundObserver = nullptr;
    if (listFocusObserver) UnhookWinEvent(listFocusObserver);
    listFocusObserver = nullptr;
    focusRequest = nullptr;
    for (auto& b : bindings) Unbind(b);
    bindings.clear();
    if (dispatcher) DestroyWindow(dispatcher);
    dispatcher = nullptr;
    waitingForPointer = false;
    syncPosted = false;
    if (classAtom) UnregisterClassW(editorClass, gInstance);
    classAtom = 0;
    closing = false;
}
