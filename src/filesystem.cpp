#include "plugin.h"
#include "filesystem.h"
#include "search.h"
#include "result_listing.h"
#include "actions.h"
#include "metadata_columns.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <list>
#include <new>
#include <optional>
#include <set>
#include <strsafe.h>
#include <utility>

namespace {

unsigned visibleColumns = defaultMetadataColumns;

bool IsRoot(const char* path) {
    return path && (*path == '\0' || std::strcmp(path, "\\") == 0);
}

// Samandarin builds its directory line from the filesystem's current path.
std::string FilterPath(const EverythingFilter& filter) {
    return "\\" + DisplayText(filter.name, MAX_PATH - 2);
}

struct PanelSelection {
    int topIndex = -1;
    std::wstring topPath;
    bool topIsDirectory = false;
    std::wstring caret;
    bool caretIsDirectory = false;
    // A directory can be replaced by a file at exactly the same path.
    std::array<std::set<std::wstring>, 2> marked;
};

class ResultsFS final : public CPluginFSInterfaceAbstract {
public:
    explicit ResultsFS(std::uint64_t instance) {
        key.instance = instance;
        key.metadataColumns = visibleColumns;
        key.source = preferredSource;
        key.limit = configuredResultLimit;
        key.properties = visibleProperties;
    }

    std::uint64_t Instance() const { return key.instance; }
    const std::wstring& Query() const { return query; }
    EverythingFilter filter;
    bool HasSearch() const { return !query.empty() || !filter.name.empty(); }
    std::wstring EffectiveQuery() const {
        if (filter.expression.empty()) return query.empty() && !filter.name.empty() ? L"*" : query;
        if (query.empty()) return filter.expression;
        return L"<" + query + L"> <" + filter.expression + L">";
    }
    bool ListingCurrent() const { return listingCurrent; }
    std::uint32_t Sort() const { return key.sort; }

    bool CopyHit(const CFileData* file, SearchHit& out) const {
        if (!pending && listingCurrent) {
            if (const auto* hit = FindResultHit(page.get(), file)) {
                out = *hit;
                return true;
            }
        }
        return false;
    }

    void ChangeSort(std::uint32_t sort) {
        if (sort == key.sort) return;
        key.sort = sort;
        const auto* property = PropertyForOrder(sort);
        key.sortProperty = property ? property->canonical : L"";
        focusFirstRank = UsesRankedNames(sort);
        if (HasSearch())
            Filter(query, true);
    }

    void ChangeColumns(unsigned mask) {
        key.metadataColumns = mask;
        key.source = preferredSource;
        key.limit = configuredResultLimit;
        key.properties = visibleProperties;
        if (key.source == Source::Stable14 && !key.sortProperty.empty()) {
            key.sort = 1;
            key.sortProperty.clear();
        }
        if (!HasSearch())
            gGeneral->PostRefreshPanelFS(this);
        else
            Filter(query, true);
    }

    // PANEL_SOURCE resolves the individually detached tab as well. A physical
    // side can point at a different live FS, so verify it before reading rows.
    int Panel() {
        for (int panel : {PANEL_SOURCE, PANEL_LEFT, PANEL_RIGHT})
            if (gGeneral->GetPanelPluginFS(panel) == this) return panel;
        return -1;
    }

    void RememberSelection(bool preserveScroll = false) {
        if (saved) return;
        const int panel = Panel();
        if (panel < 0) return;
        const ResultPage* displayed = GetListingPage(gGeneral->GetPanelPluginData(panel));
        if (!displayed || displayed->rows.empty()) return;
        PanelSelection selection;
        if (preserveScroll) {
            int index = ResultScrollPosition(this);
            selection.topIndex = index;
            if (index >= 0) {
                if (const auto* top = FindResultHit(displayed, gGeneral->GetPanelItem(panel, &index, nullptr))) {
                    selection.topPath = top->path;
                    selection.topIsDirectory = top->directory;
                }
            }
        }
        if (const auto* focus = FindResultHit(displayed, gGeneral->GetPanelFocusedItem(panel, nullptr))) {
            selection.caret = focus->path;
            selection.caretIsDirectory = focus->directory;
        }
        int i = 0;
        while (const CFileData* file = gGeneral->GetPanelSelectedItem(panel, &i, nullptr))
            if (const auto* hit = FindResultHit(displayed, file)) selection.marked[hit->directory].insert(hit->path);
        saved = std::move(selection);
    }

    void RestoreSelection() {
        if ((!saved && !focusFirstRank) || pending || !listingCurrent) return;
        const int panel = Panel();
        if (panel < 0 || GetListingPage(gGeneral->GetPanelPluginData(panel)) != page.get())
            return;
        if (focusFirstRank && page && !page->rows.empty()) {
            const auto& first = page->rows.front().hit;
            if (!saved) saved.emplace();
            saved->caret = first.path;
            saved->caretIsDirectory = first.directory;
            focusFirstRank = false;
        }
        if (!saved) return;
        PanelSelection selection = std::move(*saved);
        saved.reset();
        int i = 0;
        bool repaint = false;
        while (const CFileData* file = gGeneral->GetPanelItem(panel, &i, nullptr)) {
            const auto* hit = FindResultHit(page.get(), file);
            if (!hit) continue;
            if (selection.topIndex >= 0 && hit->directory == selection.topIsDirectory &&
                hit->path == selection.topPath)
                selection.topIndex = i - 1;
            const bool mark = selection.marked[hit->directory].count(hit->path) != 0;
            if (mark != (file->Selected != 0)) {
                gGeneral->SelectPanelItem(panel, file, mark);
                repaint = true;
            }
            if (hit->directory == selection.caretIsDirectory && hit->path == selection.caret &&
                gGeneral->GetPanelFocusedItem(panel, nullptr) != file)
                gGeneral->SetPanelFocusedItem(panel, file, FALSE);
        }
        if (repaint) gGeneral->RepaintChangedItems(panel);
        if (selection.topIndex >= 0) RestoreResultScroll(this, selection.topIndex);
    }

    void ForgetScroll() {
        if (saved) saved->topIndex = -1;
    }

    void Filter(const std::wstring& text, bool retry = false) {
        if (query == text && !retry)
            return;
        ForgetScroll();
        RememberSelection();
        if (!retry)
            query = text;
        const auto expression = EffectiveQuery();
        if (!AvailabilityChecksEnabled() || !HasSearch() || expression != availabilityQuery ||
            key.source != availabilitySource) {
            availability.reset();
            availabilityQuery = expression;
            availabilitySource = key.source;
        }
        ++key.generation;
        polling = false;
        pending = HasSearch();
        page.reset();
        state = SearchState::Ready;
        listingCurrent = false;
        gGeneral->PostRefreshPanelFS(this);
        if (pending)
            SubmitSearch(key, EffectiveQuery());
        else
            CancelSearch(key.instance);
    }

    void RefreshAvailability() {
        if (!page) return;
        const int panel = shortenResultLabels ? Panel() : -1;
        const bool resized = panel >= 0 && ResultLayoutResized(gGeneral->GetPanelPluginData(panel));
        if ((page->availability && page->availability->changed.exchange(false)) || resized)
            gGeneral->PostRefreshPanelFS(this);
    }

    void Poll() {
        if (!HasSearch() || pending || polling) return;
        polling = true;
        ++key.generation;
        SubmitSearch(key, EffectiveQuery());
    }

    void RetryIfNeeded() {
        if (HasSearch() && !pending && state != SearchState::Ready)
            Filter(query, true);
    }

    bool Accept(SearchReply&& reply) {
        // The FS owns a live tab even while another panel or tab has focus.
        // A close/reopen creates a fresh instance ID, even at the same address.
        if ((!pending && !polling) || !(reply.key == key)) return false;
        if (polling) {
            polling = false;
            if (state == reply.state && (state != SearchState::Ready ||
                (page && page->total == reply.total && page->rows.size() == reply.hits.size() &&
                 std::equal(page->rows.begin(), page->rows.end(), reply.hits.begin(),
                            [](const ResultRow& row, const SearchHit& hit) { return row.hit == hit; }))))
                return true;
            RememberSelection(true);
            page.reset();
        }
        state = reply.state;
        if (state == SearchState::Ready) {
            try {
                if (AvailabilityChecksEnabled() && !availability)
                    availability = std::make_shared<AvailabilityPage>();
                page = std::make_shared<ResultPage>(std::move(reply), availability);
            } catch (const std::bad_alloc&) {
                state = SearchState::Unavailable;
            }
        }
        pending = false;
        listingCurrent = false;
        gGeneral->PostRefreshPanelFS(this);
        return true;
    }

    BOOL WINAPI GetCurrentPath(char* path) override {
        return SUCCEEDED(StringCchCopyA(path, MAX_PATH, FilterPath(filter).c_str()));
    }
    BOOL WINAPI GetRootPath(char* path) override {
        path[0] = '\\';
        path[1] = '\0';
        return TRUE;
    }
    BOOL WINAPI GetFullName(CFileData& file, int, char* buffer, int size) override {
        return size > 0 && SUCCEEDED(StringCchPrintfA(buffer, size, "\\%s", file.Name));
    }
    BOOL WINAPI GetFullFSPath(HWND, const char*, char*, int, BOOL&) override { return FALSE; }
    BOOL WINAPI IsCurrentPath(int currentName, int name, const char* path) override {
        return currentName == name && path && (filter.name.empty() ? IsRoot(path) : FilterPath(filter) == path);
    }
    BOOL WINAPI IsOurPath(int currentName, int name, const char*) override {
        return currentName == name;
    }
    BOOL WINAPI ChangePath(int currentName, char*, int name, const char* path,
                           char* cutName, BOOL* wasCut, BOOL, int mode) override {
        if (currentName != name || !path) return FALSE;
        const bool root = IsRoot(path);
        bool recognized = root || IsCurrentPath(currentName, name, path);
        EverythingFilter target = filter;
        if (root) target = {};
        if (!recognized) {
            for (const auto& candidate : FilterCatalog()) {
                if (FilterPath(candidate) == path) {
                    recognized = true;
                    target = candidate;
                    break;
                }
            }
        }
        if (cutName)
            *cutName = '\0';
        if (wasCut)
            *wasCut = !recognized;
        // After ListCurrentPath fails the SDK retries ChangePath. The root has
        // no accessible parent to offer, so let the host recover.
        if (listingFailed) {
            listingFailed = false;
            return FALSE;
        }
        if (!recognized && mode == 3) return FALSE;
        if (!recognized) target = {};
        if (target.name != filter.name || target.expression != filter.expression) {
            filter = std::move(target);
            Filter(query, true);
        }
        return TRUE;
    }
    BOOL WINAPI ListCurrentPath(CSalamanderDirectoryAbstract* listing,
                                CPluginDataInterfaceAbstract*& data, int& icons, BOOL) override {
        data = nullptr;
        icons = pitFromPlugin;
        // 0.16 retains the old FS listing during this call. Its normal
        // post-refresh merge uses basenames; take the full-path identities
        // before the host installs the new list, including for a manual refresh.
        RememberSelection(true);
        char masks[MAX_GROUPMASK];
        const int panel = Panel();
        const bool filtered = panel >= 0 && gGeneral->GetFilterFromPanel(panel, masks, sizeof(masks));
        if (!ListResultPage(page, listing, data, filtered, filter.name)) {
            listingFailed = true;
            listingCurrent = false;
            return FALSE;
        }
        listingCurrent = !pending;
        ScheduleEditorSync();
        return TRUE;
    }
    BOOL WINAPI GetNoItemsInPanelText(char* buffer, int size) override {
        const char* message = "No matches.";
        if (!HasSearch())
            message = "Type an Everything query.";
        else if (pending)
            message = "Searching Everything...";
        else if (state == SearchState::LoadingIndex)
            message = "Everything is loading its index. Run Open search to retry.";
        else if (state == SearchState::Unavailable)
            message = "Everything is unavailable. Run Open search to retry.";
        else if (state == SearchState::UnsupportedSort)
            message = "Everything could not return this query order. Choose another column in Settings.";
        return size > 0 && SUCCEEDED(StringCchCopyA(buffer, size, message));
    }
    BOOL WINAPI TryCloseOrDetach(BOOL, BOOL, BOOL& detach, int) override {
        detach = FALSE;
        return TRUE;
    }
    void WINAPI Event(int event, DWORD) override {
        if (event == FSE_OPENED || event == FSE_PATHCHANGED || event == FSE_ATTACHED ||
            event == FSE_DETACHED || event == FSE_CLOSEORDETACHCANCELED)
            ScheduleEditorSync();
    }
    void WINAPI ReleaseObject(HWND) override {}

    DWORD WINAPI GetSupportedServices() override {
        return FS_SERVICE_VIEWFILE | FS_SERVICE_CONTEXTMENU | FS_SERVICE_COPYFROMFS |
               FS_SERVICE_MOVEFROMFS | FS_SERVICE_DELETE | FS_SERVICE_OPENACTIVEFOLDER;
    }

    std::vector<SearchHit> SelectedHits(int count) {
        std::vector<SearchHit> hits;
        const int panel = Panel();
        if (panel < 0) return hits;
        if (!count) {
            SearchHit hit;
            if (CopyHit(gGeneral->GetPanelFocusedItem(panel, nullptr), hit)) hits.push_back(std::move(hit));
        } else {
            int index = 0;
            while (const auto* file = gGeneral->GetPanelSelectedItem(panel, &index, nullptr)) {
                SearchHit hit;
                if (!CopyHit(file, hit)) return {};
                hits.push_back(std::move(hit));
            }
            if (hits.size() != static_cast<size_t>(count)) return {};
        }
        return hits;
    }
    BOOL WINAPI GetChangeDriveOrDisconnectItem(const char*, char*&, HICON&, BOOL&) override { return FALSE; }
    HICON WINAPI GetFSIcon(BOOL& destroy) override { destroy = FALSE; return nullptr; }
    void WINAPI GetDropEffect(const char*, const char*, DWORD, DWORD, DWORD* effect) override {
        *effect = DROPEFFECT_NONE;
    }
    void WINAPI GetFSFreeSpace(CQuadWord* value) override { *value = QW_MAX; }
    BOOL WINAPI GetNextDirectoryLineHotPath(const char*, int, int&) override { return FALSE; }
    void WINAPI CompleteDirectoryLineHotPath(char*, int) override {}
    BOOL WINAPI GetPathForMainWindowTitle(const char*, int, char*, int) override { return FALSE; }
    void WINAPI ShowInfoDialog(const char*, HWND) override {}
    BOOL WINAPI ExecuteCommandLine(HWND, char*, int&, int&) override { return FALSE; }
    BOOL WINAPI QuickRename(const char*, int, HWND, CFileData&, BOOL, char*, BOOL& cancel) override {
        cancel = TRUE;
        return FALSE;
    }
    void WINAPI AcceptChangeOnPathNotification(const char*, const char*, BOOL) override {}
    BOOL WINAPI CreateDir(const char*, int, HWND, char*, BOOL& cancel) override { cancel = TRUE; return FALSE; }
    void WINAPI ViewFile(const char*, HWND parent, CSalamanderForViewFileOnFSAbstract* viewer,
                         CFileData& file) override {
        SearchHit hit;
        if (CopyHit(&file, hit)) ViewResult(parent, viewer, hit);
    }
    BOOL WINAPI Delete(const char*, int mode, HWND parent, int, int files, int dirs, BOOL& cancel) override {
        if (mode == 1) {
            pendingOperation = SelectedHits(files + dirs);
            cancel = pendingOperation.empty();
            return cancel; // Otherwise request the host's normal confirmation.
        }
        auto hits = std::move(pendingOperation);
        cancel = mode != 2 || !OperateOnResults(parent, ResultOperation::Delete, hits, nullptr);
        return TRUE;
    }
    BOOL WINAPI CopyOrMoveFromFS(BOOL copy, int mode, const char*, HWND parent, int, int files,
                                 int dirs, char* target, BOOL& mask, BOOL& cancel, HWND) override {
        mask = FALSE;
        if (mode == 1) {
            pendingOperation = SelectedHits(files + dirs);
            cancel = pendingOperation.empty();
            return cancel; // Host dialog defaults to the opposite panel.
        }
        if (mode == 2 || mode == 4) {
            cancel = mode == 2; // Parse the destination, or let the user correct it.
            return FALSE;
        }
        auto hits = std::move(pendingOperation);
        cancel = mode != 3 || !OperateOnResults(parent, copy ? ResultOperation::Copy : ResultOperation::Move,
                                                hits, target);
        if (!cancel) target[0] = 0;
        return TRUE;
    }
    BOOL WINAPI CopyOrMoveFromDiskToFS(BOOL, int, const char*, HWND, const char*, SalEnumSelection2,
                                       void*, int, int, char*, BOOL* cancel) override {
        if (cancel)
            *cancel = TRUE;
        return FALSE;
    }
    BOOL WINAPI ChangeAttributes(const char*, HWND, int, int, int) override { return FALSE; }
    void WINAPI ShowProperties(const char*, HWND, int, int, int) override {}
    void WINAPI ContextMenu(const char*, HWND parent, int x, int y, int type, int, int files, int dirs) override {
        if (type == fscmItemsInPanel) {
            const auto hits = SelectedHits(files + dirs);
            if (!hits.empty()) ResultContextMenu(parent, x, y, hits);
        }
    }
    BOOL WINAPI HandleMenuMsg(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result) override {
        return ForwardResultMenuMessage(msg, wp, lp, result);
    }
    BOOL WINAPI OpenFindDialog(const char*, int) override { return FALSE; }
    void WINAPI OpenActiveFolder(const char*, HWND) override {
        // Changing to disk can close this filesystem. Let the host finish its callback first.
        if (gGeneral->GetPanelPluginFS(PANEL_SOURCE) == this)
            gGeneral->PostMenuExtCommand(openContainingFolder, FALSE);
    }
    void WINAPI GetAllowedDropEffects(int, const char*, DWORD* effects) override {
        if (effects)
            *effects = DROPEFFECT_NONE;
    }
    void WINAPI ShowSecurityInfo(HWND) override {}

private:
    std::vector<SearchHit> pendingOperation; // Exact paths captured before the modal host dialog.
    SearchKey key{};
    std::wstring query;
    std::wstring availabilityQuery;
    Source availabilitySource{};
    std::shared_ptr<AvailabilityPage> availability;
    std::shared_ptr<const ResultPage> page;
    SearchState state = SearchState::Ready;
    std::optional<PanelSelection> saved;
    bool pending = false;
    bool polling = false;
    bool listingFailed = false;
    bool listingCurrent = false;
    bool focusFirstRank = false;
};

// The SDK hands FS pointers back to us. Validate identity by address before
// downcasting because other plugins' filesystems can occupy either panel.
std::list<ResultsFS> instances;
std::uint64_t nextInstance = 0;

auto FindInstance(CPluginFSInterfaceAbstract* fs) {
    return std::find_if(instances.begin(), instances.end(),
                        [fs](const ResultsFS& instance) { return &instance == fs; });
}

class FilesystemProvider final : public CPluginInterfaceForFSAbstract {
public:
    CPluginFSInterfaceAbstract* WINAPI OpenFS(const char*, int) override {
        try {
            instances.emplace_back(++nextInstance);
            return &instances.back();
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    }
    void WINAPI CloseFS(CPluginFSInterfaceAbstract* fs) override {
        const auto it = FindInstance(fs);
        if (it != instances.end()) {
            CancelSearch(it->Instance());
            ForgetEditorFS(fs);
            instances.erase(it);
        }
    }
    void WINAPI ExecuteOnFS(int panel, CPluginFSInterfaceAbstract* fs, const char*, int,
                            CFileData& file, int isDir) override {
        SearchHit hit;
        if (isDir != 2 && GetResultHit(fs, &file, hit))
            OpenResult(gGeneral->GetPanelPluginFS(PANEL_SOURCE) == fs ? PANEL_SOURCE : panel, hit);
    }
    BOOL WINAPI DisconnectFS(HWND parent, BOOL inPanel, int panel, CPluginFSInterfaceAbstract* fs,
                             const char*, int) override {
        if (!inPanel)
            return gGeneral->CloseDetachedFS(parent, fs);
        gGeneral->DisconnectFSFromPanel(parent, panel);
        return gGeneral->GetPanelPluginFS(panel) != fs;
    }
    void WINAPI ConvertPathToInternal(const char*, int, char*) override {}
    void WINAPI ConvertPathToExternal(const char*, int, char*) override {}
    void WINAPI ExecuteChangeDriveMenuItem(int) override {}
    BOOL WINAPI ChangeDriveMenuItemContextMenu(HWND, int, int, int, CPluginFSInterfaceAbstract*,
                                             const char*, int, BOOL, BOOL&, BOOL&, int&, void*&) override {
        return FALSE;
    }
    void WINAPI ExecuteChangeDrivePostCommand(int, int, void*) override {}
    void WINAPI EnsureShareExistsOnServer(int, const char*, const char*) override {}
};

} // namespace

CPluginInterfaceForFSAbstract* GetFilesystemProvider() {
    static FilesystemProvider provider;
    return &provider;
}

bool HasOpenResultsFS() {
    return !instances.empty();
}

unsigned VisibleMetadataColumns() {
    return visibleColumns;
}

void RefreshAvailabilityIcons() {
    for (auto& fs : instances) fs.RefreshAvailability();
}

bool RefreshOpenSearches() {
    bool active = false;
    for (auto& fs : instances) {
        if (fs.HasSearch()) {
            active = true;
            fs.Poll();
        }
    }
    return active;
}

void RefreshSourceSettings() {
    for (auto& fs : instances) fs.ChangeColumns(visibleColumns);
}

void SetVisibleMetadataColumns(unsigned mask) {
    mask &= allMetadataColumns;
    if (mask == visibleColumns) return;
    visibleColumns = mask;
    for (auto& fs : instances) fs.ChangeColumns(mask);
}

bool IsResultsFS(CPluginFSInterfaceAbstract* fs) {
    return FindInstance(fs) != instances.end();
}

bool IsListingCurrent(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    return it != instances.end() && it->ListingCurrent();
}

const std::wstring& GetQuery(CPluginFSInterfaceAbstract* fs) {
    static const std::wstring empty;
    const auto it = FindInstance(fs);
    return it != instances.end() ? it->Query() : empty;
}

void SetQuery(CPluginFSInterfaceAbstract* fs, const std::wstring& text) {
    const auto it = FindInstance(fs);
    if (it != instances.end())
        it->Filter(text);
}

EverythingFilter GetEverythingFilter(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    return it == instances.end() ? EverythingFilter{} : it->filter;
}

void SetEverythingFilter(CPluginFSInterfaceAbstract* fs, EverythingFilter filter) {
    const auto it = FindInstance(fs);
    if (it == instances.end() || (it->filter.name == filter.name && it->filter.expression == filter.expression)) return;
    it->filter = std::move(filter);
    it->Filter(it->Query(), true);
}

void RetryFailedSearch(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    if (it != instances.end())
        it->RetryIfNeeded();
}

std::uint32_t GetResultSort(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    return it == instances.end() ? 1 : it->Sort();
}

void SetResultSort(CPluginFSInterfaceAbstract* fs, std::uint32_t sort) {
    const auto it = FindInstance(fs);
    if (it != instances.end()) it->ChangeSort(sort);
}

bool GetResultHit(CPluginFSInterfaceAbstract* fs, const CFileData* row, SearchHit& hit) {
    const auto it = FindInstance(fs);
    return it != instances.end() && it->CopyHit(row, hit);
}

void RememberResultSelection(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    if (it != instances.end()) it->RememberSelection();
}

void RestoreResultSelection() {
    for (auto& fs : instances) fs.RestoreSelection();
}

void ForgetResultScroll(CPluginFSInterfaceAbstract* fs) {
    const auto it = FindInstance(fs);
    if (it != instances.end()) it->ForgetScroll();
}

void DeliverSearchReply(SearchReply&& reply) {
    const auto it = std::find_if(instances.begin(), instances.end(), [&](const ResultsFS& fs) {
        return fs.Instance() == reply.key.instance;
    });
    if (it != instances.end())
        it->Accept(std::move(reply));
}
