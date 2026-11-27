#include "plugin.h"
#include "actions.h"
#include "spl_gui.h"
#include "search.h"

#include <wrl/client.h>
#include <shobjidl.h>
#include <filesystem>
#include <shellapi.h>
#include <vector>

namespace {
namespace fs = std::filesystem;

void Report(const std::wstring& reason, const std::wstring& path, DWORD error = 0)
{
    std::wstring text = reason + L"\n\n" + path;
    wchar_t description[512]{};
    if (error && FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                nullptr, error, 0, description, ARRAYSIZE(description), nullptr))
        text += std::wstring(L"\n\n") + description;
    MessageBoxW(gGeneral->GetMainWindowHWND(), text.c_str(), L"Everything Search", MB_OK | MB_ICONWARNING);
}

std::wstring Extended(const std::wstring& path)
{
    return path.compare(0, 2, L"\\\\") == 0 ? L"\\\\?\\UNC\\" + path.substr(2) : L"\\\\?\\" + path;
}

std::wstring DiskSpelling(const std::wstring& path)
{
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0)
        return L"\\\\" + path.substr(8);
    if (path.compare(0, 4, L"\\\\?\\") == 0)
        return path.substr(4);
    return path;
}

bool ResolveTarget(const SearchHit& hit, fs::path& target)
{
    std::wstring path = DiskSpelling(hit.path);
    if (hit.directory && path.size() == 2 && path[1] == L':')
        path += L'\\';
    target = path;
    if (!target.is_absolute() || path.compare(0, 4, L"\\\\.\\") == 0) {
        Report(L"The index entry has no absolute disk path.", hit.path);
        return false;
    }
    if (target.has_relative_path() && !target.has_filename())
        target = target.parent_path();
    const DWORD attributes = GetFileAttributesW(Extended(target.native()).c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        Report(L"The indexed item is unavailable.", hit.path, GetLastError());
        return false;
    }
    if (((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != hit.directory) {
        Report(L"The indexed item has changed type. Search again before opening it.", hit.path);
        return false;
    }
    return true;
}

bool OpenDiskFolder(int panel, const fs::path& directory)
{
    // Samandarin 0.16's own ChangePathToDiskW passes UTF-8 to this entry point.
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.c_str(), -1,
                                         nullptr, 0, nullptr, nullptr);
    if (!size) {
        Report(L"Samandarin could not represent the folder path.", directory.native(), GetLastError());
        return false;
    }
    if (size > 32767) {
        Report(L"This folder exceeds Samandarin's disk navigation limit.", directory.native());
        return false;
    }
    std::string utf8(size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, directory.c_str(), -1,
                             utf8.data(), size, nullptr, nullptr))
        return false;
    return gGeneral->ChangePanelPathToDisk(panel, utf8.c_str()) != FALSE;
}

bool FocusDiskTarget(int panel, const fs::path& target)
{
    CSalamanderServiceQuery query{SALAMANDER_SERVICE_PANEL_ITEM_PATHS,
                                  SALAMANDER_PANEL_ITEM_PATHS_VERSION_1_0, 0};
    CSalamanderServiceResult service{};
    if (!gGeneral->QueryService(&query, &service) || !service.Interface)
        return false;
    auto* paths = static_cast<CSalamanderPanelItemPathsAbstract*>(service.Interface);
    std::vector<wchar_t> path(32768);
    int index = 0;
    while (const CFileData* row = gGeneral->GetPanelItem(panel, &index, nullptr)) {
        if (!paths->GetItemFullPath(panel, row, path.data(), static_cast<int>(path.size())))
            continue;
        if (DiskSpelling(path.data()) != target.native())
            continue;
        gGeneral->SetPanelFocusedItem(panel, row, FALSE);
        return true;
    }
    return false;
}
}

void OpenResult(int panel, const SearchHit& hit)
{
    fs::path target;
    if (!ResolveTarget(hit, target))
        return;
    gGeneral->SetUserWorkedOnPanelPath(panel);
    if (hit.directory) {
        OpenDiskFolder(panel, target);
        return;
    }
    SHELLEXECUTEINFOW open{};
    open.cbSize = sizeof(open);
    open.hwnd = gGeneral->GetMainWindowHWND();
    open.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    open.lpFile = target.c_str();
    open.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&open))
        Report(L"Windows could not open the indexed file.", target.native(), GetLastError());
    else
        RecordOpenedFile(hit.path, hit.source);
}

void RevealResult(int panel, const SearchHit& hit)
{
    fs::path target;
    if (!ResolveTarget(hit, target))
        return;
    const fs::path parent = target.parent_path();
    // A volume or UNC share root has no containing directory on disk.
    const bool shareRoot = target.root_name().native().compare(0, 2, L"\\\\") == 0 &&
                           !parent.has_relative_path();
    if (shareRoot || parent == target) {
        OpenDiskFolder(panel, target);
        return;
    }
    gGeneral->SetUserWorkedOnPanelPath(panel);
    if (OpenDiskFolder(panel, parent) && !FocusDiskTarget(panel, target))
        Report(L"The item is not visible in the open disk panel.", target.native());
}
using Microsoft::WRL::ComPtr;

void ViewResult(HWND parent, CSalamanderForViewFileOnFSAbstract* viewer, const SearchHit& hit)
{
    fs::path path;
    if (hit.directory || !ResolveTarget(hit, path)) return;
    const auto name = path.u8string();
    const BOOL opened = viewer ? viewer->OpenViewer(parent, name.c_str(), nullptr, nullptr)
                               : gGeneral->OpenFileInConfiguredViewer(parent, name.c_str());
    if (!opened)
        Report(L"Could not open the file in the configured viewer.", path.native());
}

void EditResult(HWND parent, const SearchHit& hit)
{
    fs::path path;
    if (hit.directory || !ResolveTarget(hit, path)) return;
    SHELLEXECUTEINFOW edit{sizeof(edit)};
    edit.hwnd = parent;
    edit.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    edit.nShow = SW_SHOWNORMAL;
    edit.lpVerb = L"edit";
    edit.lpFile = path.c_str();
    if (ShellExecuteExW(&edit)) return;
    DWORD error = GetLastError();
    if (error == ERROR_NO_ASSOCIATION) {
        wchar_t system[MAX_PATH];
        if (GetSystemDirectoryW(system, ARRAYSIZE(system))) {
            const auto notepad = fs::path(system) / L"notepad.exe";
            const auto argument = L"\"" + path.native() + L"\"";
            edit.lpVerb = L"open";
            edit.lpFile = notepad.c_str();
            edit.lpParameters = argument.c_str();
            if (ShellExecuteExW(&edit)) return;
            error = GetLastError();
        }
    }
    Report(L"Could not edit the file.", path.native(), error);
}

namespace {
HRESULT ShellSelection(const std::vector<SearchHit>& hits, ComPtr<IShellItemArray>& selection)
{
    if (hits.empty()) return E_INVALIDARG;
    std::vector<PCIDLIST_ABSOLUTE> ids;
    ids.reserve(hits.size());
    HRESULT hr = S_OK;
    for (const auto& hit : hits) {
        fs::path path;
        if (!ResolveTarget(hit, path)) { hr = E_ABORT; break; }
        PIDLIST_ABSOLUTE id = nullptr;
        hr = SHParseDisplayName(path.c_str(), nullptr, &id, 0, nullptr);
        if (FAILED(hr)) break;
        ids.push_back(id);
    }
    if (SUCCEEDED(hr))
        hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()), ids.data(), &selection);
    for (auto id : ids) CoTaskMemFree(const_cast<PIDLIST_ABSOLUTE>(id));
    return hr;
}

struct ShellResultsView {
    ComPtr<IExplorerBrowser> browser;
    ~ShellResultsView() { if (browser) browser->Destroy(); }
};

// Ask the actual disk folder for its item menu, as the disk panel does.
// A virtual results folder is only needed when the items have different parents.
HRESULT SelectionContextMenu(HWND parent, IShellItemArray* items, ShellResultsView& view,
                             ComPtr<IContextMenu>& shell)
{
    DWORD count = 0;
    HRESULT hr = items->GetCount(&count);
    if (FAILED(hr) || !count) return FAILED(hr) ? hr : E_INVALIDARG;
    std::vector<PIDLIST_ABSOLUTE> ids(count);
    std::vector<PCUITEMID_CHILD> children;
    ComPtr<IShellFolder> diskFolder;
    PIDLIST_ABSOLUTE commonParent = nullptr;
    bool siblings = true;
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; ++i) {
        ComPtr<IShellItem> item;
        hr = items->GetItemAt(i, &item);
        if (SUCCEEDED(hr)) hr = SHGetIDListFromObject(item.Get(), &ids[i]);
        if (FAILED(hr)) break;
        if (i == 0) {
            commonParent = ILCloneFull(ids[i]);
            if (!commonParent) { hr = E_OUTOFMEMORY; break; }
            ILRemoveLastID(commonParent);
            hr = SHBindToParent(ids[i], IID_PPV_ARGS(&diskFolder), nullptr);
        } else if (!ILIsParent(commonParent, ids[i], TRUE)) {
            siblings = false;
            break;
        }
        children.push_back(ILFindLastID(ids[i]));
    }
    if (SUCCEEDED(hr) && siblings)
        hr = diskFolder->GetUIObjectOf(parent, count, children.data(), IID_IContextMenu, nullptr,
                                       reinterpret_cast<void**>(shell.GetAddressOf()));
    CoTaskMemFree(commonParent);
    for (auto id : ids) CoTaskMemFree(id);
    if (siblings) return hr;
    children.clear();

    hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&view.browser));
    RECT bounds{}; // The results view supplies shell identities only.
    FOLDERSETTINGS settings{FVM_DETAILS, FWF_NOICONS};
    if (SUCCEEDED(hr)) hr = view.browser->SetOptions(EBO_NAVIGATEONCE | EBO_NOPERSISTVIEWSTATE);
    if (SUCCEEDED(hr)) hr = view.browser->Initialize(parent, &bounds, &settings);
    if (SUCCEEDED(hr)) hr = view.browser->FillFromObject(nullptr, EBF_NODROPTARGET);
    ComPtr<IFolderView> folderView;
    if (SUCCEEDED(hr)) hr = view.browser->GetCurrentView(IID_PPV_ARGS(&folderView));
    ComPtr<IShellFolder> folder;
    if (SUCCEEDED(hr)) hr = folderView->GetFolder(IID_PPV_ARGS(&folder));
    ComPtr<IResultsFolder> results;
    if (SUCCEEDED(hr)) hr = folder.As(&results);
    children.reserve(count);
    for (DWORD i = 0; SUCCEEDED(hr) && i < count; ++i) {
        ComPtr<IShellItem> item;
        hr = items->GetItemAt(i, &item);
        PIDLIST_ABSOLUTE id = nullptr;
        if (SUCCEEDED(hr)) hr = SHGetIDListFromObject(item.Get(), &id);
        PITEMID_CHILD child = nullptr;
        if (SUCCEEDED(hr)) hr = results->AddIDList(id, &child);
        CoTaskMemFree(id);
        if (SUCCEEDED(hr)) children.push_back(child);
    }
    if (SUCCEEDED(hr))
        hr = folder->GetUIObjectOf(parent, count, children.data(), IID_IContextMenu, nullptr,
                                   reinterpret_cast<void**>(shell.GetAddressOf()));
    for (auto child : children) CoTaskMemFree(const_cast<PITEMID_CHILD>(child));
    return hr;
}

ComPtr<IContextMenu2> activeMenu2;
ComPtr<IContextMenu3> activeMenu3;
}

bool OperateOnResults(HWND parent, ResultOperation action, const std::vector<SearchHit>& hits,
                      const char* destination)
{
    if (hits.empty()) return false;
    ComPtr<IShellItemArray> items;
    HRESULT hr = ShellSelection(hits, items);
    ComPtr<IFileOperation> op;
    if (SUCCEEDED(hr)) hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&op));
    if (SUCCEEDED(hr)) hr = op->SetOwnerWindow(parent);
    if (SUCCEEDED(hr)) hr = op->SetOperationFlags(FOF_ALLOWUNDO | FOFX_EARLYFAILURE | FOF_WANTNUKEWARNING);
    if (SUCCEEDED(hr) && action == ResultOperation::Delete) hr = op->DeleteItems(items.Get());
    if (SUCCEEDED(hr) && action != ResultOperation::Delete) {
        // Samandarin uses UTF-8 for disk paths and accepts legacy ACP input.
        UINT cp = CP_UTF8;
        int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, destination, -1, nullptr, 0);
        if (!n) { cp = CP_ACP; n = MultiByteToWideChar(cp, 0, destination, -1, nullptr, 0); }
        std::wstring path(n, L'\0');
        ComPtr<IShellItem> target;
        if (!n || !MultiByteToWideChar(cp, 0, destination, -1, path.data(), n)) hr = E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&target));
        if (SUCCEEDED(hr)) hr = action == ResultOperation::Copy ? op->CopyItems(items.Get(), target.Get())
                                                                 : op->MoveItems(items.Get(), target.Get());
    }
    BOOL aborted = TRUE;
    if (SUCCEEDED(hr)) {
        hr = op->PerformOperations();
        if (SUCCEEDED(hr)) hr = op->GetAnyOperationsAborted(&aborted);
        ScheduleFileOperationRefresh(); // The index can lag behind a completed move.
        gGeneral->PostRefreshPanelPath(PANEL_TARGET);
    }
    if (FAILED(hr) && hr != E_ABORT && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
        Report(L"Windows could not complete the file operation.", L"", hr);
    return SUCCEEDED(hr) && !aborted;
}

void ResultContextMenu(HWND parent, int x, int y, const std::vector<SearchHit>& hits)
{
    ComPtr<IShellItemArray> items;
    HRESULT hr = ShellSelection(hits, items);
    if (FAILED(hr)) {
        if (hr != E_ABORT) Report(L"Could not prepare the file menu.", L"", hr);
        return;
    }
    ShellResultsView view;
    ComPtr<IContextMenu> shell;
    hr = SelectionContextMenu(parent, items.Get(), view, shell);
    if (FAILED(hr)) {
        Report(L"Could not load the Windows context menu for this selection.", L"", hr);
        return;
    }
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    constexpr UINT firstVerb = 1;
    UINT flags = CMF_NORMAL | CMF_EXPLORE | CMF_SYNCCASCADEMENU;
    if (GetKeyState(VK_SHIFT) & 0x8000) flags |= CMF_EXTENDEDVERBS;
    hr = shell->QueryContextMenu(menu, 0, firstVerb, 0x7fff, flags);
    if (FAILED(hr)) {
        DestroyMenu(menu);
        Report(L"Windows could not populate the context menu.", L"", hr);
        return;
    }
    auto* popup = gGUI->CreateMenuPopup();
    if (!popup) { DestroyMenu(menu); return; }
    popup->SetTemplateMenu(menu);
    shell.As(&activeMenu2);
    shell.As(&activeMenu3);
    const UINT cmd = popup->Track(MENU_TRACK_RETURNCMD | MENU_TRACK_RIGHTBUTTON, x, y, parent, nullptr);
    activeMenu3.Reset();
    activeMenu2.Reset();
    gGUI->DestroyMenuPopup(popup);
    if (cmd >= firstVerb) {
        CMINVOKECOMMANDINFO invoke{sizeof(invoke)};
        invoke.hwnd = parent;
        invoke.lpVerb = MAKEINTRESOURCEA(cmd - firstVerb);
        invoke.nShow = SW_SHOWNORMAL;
        hr = shell->InvokeCommand(&invoke);
        if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_CANCELLED))
            Report(L"Windows could not execute this menu command.", L"", hr);
        ScheduleFileOperationRefresh();
    }
    DestroyMenu(menu);
}

BOOL ForwardResultMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)
{
    if (activeMenu3) return activeMenu3->HandleMenuMsg2(msg, wp, lp, result) == S_OK;
    if (activeMenu2 && activeMenu2->HandleMenuMsg(msg, wp, lp) == S_OK) {
        if (result) *result = 0;
        return TRUE;
    }
    return FALSE;
}
