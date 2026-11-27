#include "plugin.h"
#include "filesystem.h"
#include "search.h"
#include "actions.h"
#include "metadata_columns.h"

int SalamanderVersion = 0;
HINSTANCE gInstance = nullptr;
CSalamanderGeneralAbstract* gGeneral = nullptr;

bool UseHostDarkTheme()
{
    BOOL dark = FALSE;
    gGeneral->GetConfigParameter(SALCFG_USEWINDOWSDARKMODE, &dark, sizeof(dark), nullptr);
    HIGHCONTRASTW contrast{sizeof(contrast), 0, nullptr};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON)) dark = FALSE;
    return dark != FALSE;
}

namespace {

constexpr int openSearch = 1;
constexpr int openContainingFolder = 2;
constexpr int settingsCommand = 5;
char fsName[MAX_PATH]{};
constexpr char columnsSetting[] = "ResultColumnsV2";

bool FocusedHit(SearchHit& hit)
{
    auto* fs = gGeneral->GetPanelPluginFS(PANEL_SOURCE);
    return IsResultsFS(fs) && GetResultHit(fs, gGeneral->GetPanelFocusedItem(PANEL_SOURCE, nullptr), hit);
}

class SearchMenu final : public CPluginInterfaceForMenuExtAbstract {
public:
    DWORD WINAPI GetMenuItemState(int id, DWORD) override {
        if (id == openSearch || id == settingsCommand) return MENU_ITEM_STATE_ENABLED;
        if (id == openContainingFolder) {
            SearchHit hit;
            return FocusedHit(hit) ? MENU_ITEM_STATE_ENABLED : 0;
        }
        return 0;
    }
    BOOL WINAPI ExecuteMenuItem(CSalamanderForOperationsAbstract*, HWND parent, int id, DWORD) override {
        if (id == openSearch &&
            (IsResultsFS(gGeneral->GetPanelPluginFS(PANEL_SOURCE)) ||
             gGeneral->ChangePanelPathToPluginFS(PANEL_SOURCE, fsName, "\\"))) {
            RetryFailedSearch(gGeneral->GetPanelPluginFS(PANEL_SOURCE));
            ShowEditorForActivePanel();
        } else if (id == settingsCommand) {
            ShowSearchSettings(parent);
        } else if (id == openContainingFolder) {
            SearchHit hit;
            if (FocusedHit(hit))
                RevealResult(PANEL_SOURCE, hit);
        }
        return FALSE; // Preserve the user's selection.
    }
    BOOL WINAPI HelpForMenuItem(HWND, int) override { return FALSE; }
    void WINAPI BuildMenu(HWND, CSalamanderBuildMenuAbstract*) override {}
};

class SearchPlugin final : public CPluginInterfaceAbstract {
public:
    void WINAPI About(HWND parent) override {
        MessageBoxA(parent, "Everything Search " EVERYTHING_SEARCH_VERSION
                    "\nSearch the Everything index in the active panel.",
                    "Everything Search", MB_OK | MB_ICONINFORMATION);
    }
    void WINAPI Connect(HWND, CSalamanderConnectAbstract* host) override {
        host->AddMenuItem(-1, "&Open search", SALHOTKEY('E', HOTKEYF_CONTROL | HOTKEYF_SHIFT), openSearch, FALSE,
                          MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        host->AddMenuItem(-1, "Open &containing folder", 0, openContainingFolder, TRUE,
                          MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
        host->AddMenuItem(-1, "&Settings...", 0, settingsCommand, FALSE,
                          MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
    }
    void WINAPI Event(int event, DWORD) override {
        switch (event) {
        case PLUGINEVENT_PANELACTIVATED:
        case PLUGINEVENT_PANELSSWAPPED:
        case PLUGINEVENT_PATHCHANGED:
        case PLUGINEVENT_TABCHANGED:
        case PLUGINEVENT_CONFIGURATIONCHANGED:
        case PLUGINEVENT_SETTINGCHANGE:
            ScheduleEditorSync();
        }
    }
    BOOL WINAPI Release(HWND, BOOL) override {
        ShutdownEditor();
        ShutdownSearch();
        return TRUE;
    }
    CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() override { return &menu; }
    CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override { return GetFilesystemProvider(); }
    CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() override { return nullptr; }
    CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() override { return nullptr; }
    CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() override { return nullptr; }
    void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* data) override { ReleaseResultListing(data); }
    void WINAPI LoadConfiguration(HWND, HKEY key, CSalamanderRegistryAbstract* registry) override {
        DWORD source = 0;
        if (key && registry->GetValue(key, "EverythingSource", REG_DWORD, &source, sizeof(source)) &&
            source <= static_cast<DWORD>(Source::Modern15))
            preferredSource = static_cast<Source>(source);
        DWORD bytes = 0;
        if (key && registry->GetSize(key, "Everything15Properties", REG_BINARY, bytes) &&
            bytes && bytes % sizeof(wchar_t) == 0 && bytes <= 4 * 1024 * 1024) {
            std::wstring saved(bytes / sizeof(wchar_t), L'\0');
            if (registry->GetValue(key, "Everything15Properties", REG_BINARY, saved.data(), bytes)) {
                for (size_t pos = 0, end; (end = saved.find(L'\0', pos)) != saved.npos && end > pos; pos = end + 1) {
                    auto name = saved.substr(pos, end - pos);
                    visibleProperties.push_back(RegisterProperty(name, name));
                }
            }
        }
        DWORD mask = defaultMetadataColumns;
        if (key && !registry->GetValue(key, columnsSetting, REG_DWORD, &mask, sizeof(mask))) {
            DWORD legacy = 0;
            mask = defaultMetadataColumns;
            if (registry->GetValue(key, "VisibleMetadataColumns", REG_DWORD, &legacy, sizeof(legacy)) &&
                legacy != (ColumnSize | ColumnModified))
                mask = legacy | ColumnParent;
        }
        SetVisibleMetadataColumns(mask);
    }
    void WINAPI SaveConfiguration(HWND, HKEY key, CSalamanderRegistryAbstract* registry) override {
        const DWORD mask = VisibleMetadataColumns();
        if (!key) return;
        const auto source = static_cast<DWORD>(preferredSource);
        registry->SetValue(key, "EverythingSource", REG_DWORD, &source, sizeof(source));
        std::wstring saved;
        for (const auto& p : visibleProperties) {
            saved += p.canonical;
            saved.push_back(L'\0');
        }
        registry->SetValue(key, "Everything15Properties", REG_BINARY, saved.c_str(),
                           static_cast<DWORD>((saved.size() + 1) * sizeof(wchar_t)));
        registry->SetValue(key, columnsSetting, REG_DWORD, &mask, sizeof(mask));
    }
    void WINAPI Configuration(HWND parent) override { ShowSearchSettings(parent); }
    void WINAPI ClearHistory(HWND) override {}
    void WINAPI AcceptChangeOnPathNotification(const char*, BOOL) override {}
    void WINAPI PasswordManagerEvent(HWND, int) override {}
private:
    SearchMenu menu;
};

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH)
        gInstance = instance;
    return TRUE;
}

extern "C" int WINAPI SalamanderPluginGetReqVer() { return LAST_VERSION_OF_SALAMANDER; }
extern "C" int WINAPI SalamanderPluginGetSDKVer() { return LAST_VERSION_OF_SALAMANDER; }

extern "C" CPluginInterfaceAbstract* WINAPI SalamanderPluginEntry(CSalamanderPluginEntryAbstract* host) {
    SalamanderVersion = host->GetVersion();
    if (SalamanderVersion < LAST_VERSION_OF_SALAMANDER ||
        !host->SetBasicPluginData("Everything Search", FUNCTION_FILESYSTEM | FUNCTION_CONFIGURATION | FUNCTION_LOADSAVECONFIGURATION,
                                  EVERYTHING_SEARCH_VERSION, "majklvi",
                                  "Search the Everything index in a fixed panel field.", "EverythingSearch",
                                  nullptr, "everything")) {
        return nullptr;
    }
    gGeneral = host->GetSalamanderGeneral();
    gGeneral->GetPluginFSName(fsName, 0);
    RefreshFilterCatalog();
    static SearchPlugin plugin;
    return &plugin;
}
