#include "plugin.h"
#include "availability.h"
#include "settings_resource.h"
#include "filesystem.h"
#include "search.h"
#include "actions.h"
#include "metadata_columns.h"
#include "modern_search.h"
#include "spl_gui.h"

int SalamanderVersion = 0;
HINSTANCE gInstance = nullptr;
CSalamanderGUIAbstract* gGUI = nullptr;
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
constexpr int settingsCommand = 5;
constexpr int helpCommand = 6;
char fsName[MAX_PATH]{};
constexpr char columnsSetting[] = "ResultColumnsV2";

HICON LoadSourceIcon()
{
    const auto sources = DiscoverSources();
    HWND endpoint = sources.UseModern(preferredSource) ? sources.modern : sources.stable;
    DWORD pid = 0;
    if (!endpoint || !GetWindowThreadProcessId(endpoint, &pid)) return nullptr;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return nullptr;
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    const BOOL found = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    HICON icon = nullptr;
    if (found)
        SHDefExtractIconW(path.c_str(), 0, 0, &icon, nullptr, 16);
    return icon;
}

void RegisterMenuIcon(CSalamanderConnectAbstract* host)
{
    HICON icon = LoadSourceIcon();
    if (!icon) return;
    auto* list = gGUI->CreateIconList();
    if (list) {
        if (list->Create(16, 16, 1) && list->ReplaceIcon(0, icon)) {
            host->SetIconListForGUI(list);
            host->SetPluginIcon(0);
            host->SetPluginMenuAndToolbarIcon(0);
        } else {
            gGUI->DestroyIconList(list);
        }
    }
    DestroyIcon(icon);
}

bool FocusedHit(SearchHit& hit)
{
    auto* fs = gGeneral->GetPanelPluginFS(PANEL_SOURCE);
    return IsResultsFS(fs) && GetResultHit(fs, gGeneral->GetPanelFocusedItem(PANEL_SOURCE, nullptr), hit);
}

class SearchMenu final : public CPluginInterfaceForMenuExtAbstract {
public:
    DWORD WINAPI GetMenuItemState(int id, DWORD) override {
        if (id == openSearch || id == settingsCommand || id == helpCommand) return MENU_ITEM_STATE_ENABLED;
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
        } else if (id == helpCommand) {
            gGeneral->OpenHtmlHelp(parent, HHCDisplayContext, IDH_SEARCH, FALSE);
        } else if (id == settingsCommand) {
            ShowSearchSettings(parent);
        } else if (id == openContainingFolder) {
            RevealFocusedResult(PANEL_SOURCE);
        }
        return FALSE; // Preserve the user's selection.
    }
    BOOL WINAPI HelpForMenuItem(HWND parent, int id) override {
        int topic;
        switch (id) {
        case openSearch:
        case helpCommand: topic = IDH_SEARCH; break;
        case openContainingFolder: topic = IDH_ACTIONS; break;
        case settingsCommand: topic = IDH_SETTINGS; break;
        default: return FALSE;
        }
        return gGeneral->OpenHtmlHelp(parent, HHCDisplayContext, topic, FALSE);
    }
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
        RegisterMenuIcon(host);
        host->AddMenuItem(-1, "&Open search", SALHOTKEY('E', HOTKEYF_CONTROL | HOTKEYF_SHIFT), openSearch, FALSE,
                          MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        host->AddMenuItem(-1, "Open &containing folder", 0, openContainingFolder, TRUE,
                          MENU_EVENT_THIS_PLUGIN_FS, MENU_EVENT_THIS_PLUGIN_FS, MENU_SKILLLEVEL_ALL);
        host->AddMenuItem(-1, "&Settings...", 0, settingsCommand, FALSE,
                          MENU_EVENT_TRUE, MENU_EVENT_TRUE, MENU_SKILLLEVEL_ALL);
        host->AddMenuItem(-1, "&Help", 0, helpCommand, FALSE,
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
        StopAvailabilityChecks();
        return TRUE;
    }
    CPluginInterfaceForMenuExtAbstract* WINAPI GetInterfaceForMenuExt() override { return &menu; }
    CPluginInterfaceForFSAbstract* WINAPI GetInterfaceForFS() override { return GetFilesystemProvider(); }
    CPluginInterfaceForArchiverAbstract* WINAPI GetInterfaceForArchiver() override { return nullptr; }
    CPluginInterfaceForViewerAbstract* WINAPI GetInterfaceForViewer() override { return nullptr; }
    CPluginInterfaceForThumbLoaderAbstract* WINAPI GetInterfaceForThumbLoader() override { return nullptr; }
    void WINAPI ReleasePluginDataInterface(CPluginDataInterfaceAbstract* data) override { ReleaseResultListing(data); }
    void WINAPI LoadConfiguration(HWND, HKEY key, CSalamanderRegistryAbstract* registry) override {
        DWORD shorten = 0;
        const bool hasShortenSetting = key && registry->GetValue(key, "ShortenResultLabels", REG_DWORD,
                                                                &shorten, sizeof(shorten));
        shortenResultLabels = hasShortenSetting ? shorten != 0 : defaultShortenResultLabels;
        DWORD delay = defaultNetworkRetrySeconds;
        if (!key || !registry->GetValue(key, "NetworkRetrySeconds", REG_DWORD, &delay, sizeof(delay)) ||
            delay > maxNetworkRetrySeconds) delay = defaultNetworkRetrySeconds;
        SetNetworkRetrySeconds(delay);
        DWORD hide = 0;
        hideUnavailableResults = key && registry->GetValue(key, "HideUnavailableResults", REG_DWORD,
                                                           &hide, sizeof(hide)) && hide != 0;
        DWORD checkPaths = 1;
        const bool hasCheckSetting = key && registry->GetValue(key, "CheckAvailability", REG_DWORD,
                                                              &checkPaths, sizeof(checkPaths));
        SetAvailabilityChecksEnabled(!hasCheckSetting || checkPaths != 0);
        DWORD limit = defaultResultLimit;
        if (key && registry->GetValue(key, "ResultLimit", REG_DWORD, &limit, sizeof(limit)) && limit <= INT_MAX)
            configuredResultLimit = limit;
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
        const DWORD shorten = shortenResultLabels;
        registry->SetValue(key, "ShortenResultLabels", REG_DWORD, &shorten, sizeof(shorten));
        const DWORD delay = NetworkRetrySeconds();
        registry->SetValue(key, "NetworkRetrySeconds", REG_DWORD, &delay, sizeof(delay));
        const DWORD hide = hideUnavailableResults;
        registry->SetValue(key, "HideUnavailableResults", REG_DWORD, &hide, sizeof(hide));
        const DWORD checkPaths = AvailabilityChecksEnabled();
        registry->SetValue(key, "CheckAvailability", REG_DWORD, &checkPaths, sizeof(checkPaths));
        registry->SetValue(key, "ResultLimit", REG_DWORD, &configuredResultLimit, sizeof(configuredResultLimit));
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
                                  EVERYTHING_SEARCH_VERSION, "Michal Várady",
                                  "Search the Everything index in a fixed panel field.", "EverythingSearch",
                                  nullptr, "everything")) {
        return nullptr;
    }
    host->SetPluginHomePageURL("https://github.com/majklvi/Salamander-Everything-plugin");
    gGeneral = host->GetSalamanderGeneral();
    gGUI = host->GetSalamanderGUI();
    gGeneral->SetHelpFileName("EverythingSearch.chm");
    gGeneral->GetPluginFSName(fsName, 0);
    RefreshFilterCatalog();
    static SearchPlugin plugin;
    return &plugin;
}
