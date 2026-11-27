#include "test_host.h"
#include "search.h"

#include <iostream>

namespace {
TestHost testHost;
SearchKey submitted{};
std::wstring submittedText;
SearchHit opened;
}

// Compile the actual adapter against the simulated panel. This limits the
// stand-in to the SDK methods used here, without introducing a shipping hook.
#define gGeneral (&testHost)
#include "../../src/result_listing.cpp"
#include "../../src/filesystem.cpp"
#undef gGeneral

const std::vector<EverythingFilter>& FilterCatalog() {
    static const std::vector<EverythingFilter> filters{{L"Documents", L"ext:pdf | ext:txt"}};
    return filters;
}

void ForgetEditorFS(CPluginFSInterfaceAbstract*) {}
void ScheduleEditorSync() {}
void SubmitSearch(SearchKey key, const std::wstring& text) { submitted = key; submittedText = text; }
void CancelSearch(std::uint64_t) {}
void OpenResult(int, const SearchHit& hit) { opened = hit; }

namespace {
std::vector<SearchHit> operationHits;
ResultOperation lastOperation{};
}
void ViewResult(HWND, CSalamanderForViewFileOnFSAbstract*, const SearchHit& hit) { operationHits = {hit}; }
bool OperateOnResults(HWND, ResultOperation operation, const std::vector<SearchHit>& hits, const char*) {
    operationHits = hits;
    lastOperation = operation;
    return !hits.empty();
}
void ResultContextMenu(HWND, int, int, const std::vector<SearchHit>& hits) { operationHits = hits; }
BOOL ForwardResultMenuMessage(UINT, WPARAM, LPARAM, LRESULT*) { return FALSE; }

namespace {
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
    std::cout << "PASS " << message << '\n';
}

bool DisplaysRocket(const std::string& shown)
{
    if (shown.find("[U+1F680]") != std::string::npos) return true;
    wchar_t decoded[MAX_PATH]{};
    return MultiByteToWideChar(CP_ACP, 0, shown.c_str(), -1, decoded, MAX_PATH) &&
           std::wstring_view(decoded).find(L"🚀") != std::wstring_view::npos;
}

void RunChecks()
{
    auto* provider = GetFilesystemProvider();
    auto* fs = provider->OpenFS("everything", 0);
    if (!fs) throw std::runtime_error("OpenFS failed");
    testHost.active = fs;
    Check((fs->GetSupportedServices() & FS_SERVICE_OPENACTIVEFOLDER) != 0,
          "Shift+F3 is available in the result panel");
    fs->OpenActiveFolder("everything", nullptr);
    Check(testHost.postedCommand == openContainingFolder, "Shift+F3 posts the containing-folder command");

    const SearchHit alpha{L"report.pdf", LR"(C:\Alpha\report.pdf)", false};
    const SearchHit beta{L"report.pdf", LR"(C:\Beta\report.pdf)", false};
    const std::wstring segment(140, L'x');
    const SearchHit unicode{L"漢字🚀.txt", L"C:\\" + segment + L"\\" + segment + L"\\漢字🚀.txt", false};
    SetQuery(fs, L"report");
    DeliverSearchReply({submitted, SearchState::Ready, {alpha, beta, unicode}, 400});
    TestDirectory original;
    testHost.Publish(original);
    RestoreResultSelection();

    Check(std::strcmp(original.files[0].Name, original.files[1].Name) == 0,
          "duplicate basenames remain equal on screen");
    SearchHit hit;
    Check(GetResultHit(fs, &original.files[0], hit) && hit.path == alpha.path,
          "first duplicate retains its full path");
    provider->ExecuteOnFS(PANEL_LEFT, fs, "everything", 0, original.files[1], 0);
    Check(opened.path == beta.path, "native ExecuteOnFS dispatches the second duplicate's own full path");
    Check(GetResultHit(fs, &original.files[2], hit) && hit.path == unicode.path && hit.path.size() > MAX_PATH,
          "long Unicode hit round-trips its exact UTF-16 path");
    Check(DisplaysRocket(original.files[2].Name),
          "supplementary character survives display conversion or has an explicit Unicode escape");
    const auto& oldBeta = *reinterpret_cast<const ResultRow*>(original.files[1].PluginData);
    Check(oldBeta.displayParent == R"(C:\Beta)", "Parent folder preserves the indexed parent");
    char line[1000]{};
    int hotCount = 0;
    CQuadWord size{};
    const bool hasStatus = original.data->GetInfoLineContent(PANEL_LEFT, nullptr, FALSE, 0, 0, FALSE,
                                                             size, line, nullptr, hotCount) != FALSE;
    Check(hasStatus && std::string_view(line).find("3 of 400 results") != std::string_view::npos,
          "status reports loaded and total counts");

    original.files[0].Selected = original.files[1].Selected = TRUE;
    fs->ContextMenu("everything", nullptr, 0, 0, fscmItemsInPanel, PANEL_LEFT, 2, 0);
    Check(operationHits.size() == 2 && operationHits[0].path == alpha.path && operationHits[1].path == beta.path,
          "multi-selection menu receives both namesakes with their own paths");
    BOOL cancelled = FALSE;
    Check(!fs->Delete("everything", 1, nullptr, PANEL_LEFT, 2, 0, cancelled) && !cancelled,
          "delete uses the host confirmation for the exact selection");
    original.files[0].Selected = original.files[1].Selected = FALSE;
    original.files[2].Selected = TRUE;
    Check(fs->Delete("everything", 2, nullptr, PANEL_LEFT, 1, 0, cancelled) && !cancelled &&
          lastOperation == ResultOperation::Delete && operationHits.size() == 2 &&
          operationHits[0].path == alpha.path && operationHits[1].path == beta.path,
          "confirmed delete retains both original paths after the selection changes");
    original.files[2].Selected = FALSE;
    fs->ViewFile("everything", nullptr, nullptr, original.files[1]);
    Check(operationHits.size() == 1 && operationHits[0].path == beta.path, "view receives only the focused namesake");

    // Beta was marked on page one. After reversal it moves to row zero. The
    // legacy basename merge put the mark and caret on Alpha at the old index.
    testHost.caret = 1;
    original.files[1].Selected = TRUE;
    SetResultSort(fs, 2);
    Check(!GetResultHit(fs, &original.files[1], hit), "a stale visible row cannot launch during requery");
    Check(oldBeta.hit.path == beta.path, "the previous listing keeps its backing page alive");
    DeliverSearchReply({submitted, SearchState::Ready, {beta, alpha}, 2});
    TestDirectory reversed;
    testHost.Publish(reversed);
    testHost.caret = 1;
    reversed.files[1].Selected = TRUE;
    RestoreResultSelection();
    Check(testHost.caret == 0 && reversed.files[0].Selected && !reversed.files[1].Selected,
          "after reordering, caret and selection follow Beta despite the host's basename merge");

    TestDirectory refresh;
    testHost.Publish(refresh);
    testHost.caret = 1;
    refresh.files[1].Selected = TRUE;
    RestoreResultSelection();
    Check(testHost.caret == 0 && refresh.files[0].Selected && !refresh.files[1].Selected,
          "manual refresh restores the exact file after a basename merge");

    SetQuery(fs, L"Alpha");
    DeliverSearchReply({submitted, SearchState::Ready, {alpha}, 1});
    TestDirectory onlyAlpha;
    testHost.Publish(onlyAlpha);
    onlyAlpha.files[0].Selected = TRUE;
    RestoreResultSelection();
    Check(!onlyAlpha.files[0].Selected, "absent Beta selection cannot migrate to its surviving namesake");

    SetResultSort(fs, 1);
    const SearchHit oldDirectory{L"item", LR"(C:\Alpha\item)", true};
    const SearchHit replacement{oldDirectory.name, oldDirectory.path, false};
    SetQuery(fs, L"item");
    DeliverSearchReply({submitted, SearchState::Ready, {oldDirectory}, 1});
    TestDirectory asDirectory;
    testHost.Publish(asDirectory);
    RestoreResultSelection();
    asDirectory.dirs[0].Selected = TRUE;
    SetQuery(fs, L"exact:item");
    DeliverSearchReply({submitted, SearchState::Ready, {replacement}, 1});
    TestDirectory asFile;
    testHost.Publish(asFile);
    asFile.files[0].Selected = TRUE;
    RestoreResultSelection();
    Check(!asFile.files[0].Selected, "directory replaced by a file at the same path loses its prior mark");

    SetResultSort(fs, mostOpenedSort);
    Check(submitted.sort == mostOpenedSort && submitted.limit == 200, "rank is requested from Everything");
    DeliverSearchReply({submitted, SearchState::Ready, {beta, oldDirectory, alpha}, 605});
    TestDirectory ranked;
    testHost.Publish(ranked);
    testHost.caret = 2;
    RestoreResultSelection();
    Check(testHost.caret == 0 && ranked.files.size() == 3 && ranked.dirs.empty() &&
          std::string_view(ranked.files[0].Name) == "001  report.pdf",
          "ranked page starts at the first SDK hit and keeps one group");
    testHost.panelFilter = true;
    TestDirectory masked;
    testHost.Publish(masked);
    Check((masked.validData & VALID_DATA_EXTENSION) && std::string_view(masked.files[0].Ext) == "pdf" &&
          std::string_view(masked.files[0].Name) == "report.pdf" && HasFilteredRanks(masked.data),
          "ranked native filter receives the exact basename and extension");
    testHost.panelFilter = false;
    testHost.listing = &ranked;
    provider->ExecuteOnFS(PANEL_LEFT, fs, "everything", 0, ranked.files[1], 0);
    Check(opened.directory && opened.path == oldDirectory.path, "ranked folder keeps its directory action");

    RefreshOpenSearches();
    const auto firstPoll = submitted;
    RefreshOpenSearches();
    Check(submitted == firstPoll && GetResultHit(fs, &ranked.files[0], hit),
          "background refresh keeps rows usable and coalesces overlapping checks");
    DeliverSearchReply({firstPoll, SearchState::Ready, {beta, oldDirectory, alpha}, 605});
    Check(GetResultHit(fs, &ranked.files[0], hit), "unchanged index reply preserves the live listing");
    RefreshOpenSearches();
    DeliverSearchReply({submitted, SearchState::Ready, {}, 0});
    TestDirectory afterDelete;
    testHost.Publish(afterDelete);
    Check(afterDelete.files.empty() && afterDelete.dirs.empty() && IsListingCurrent(fs),
          "a later index update clears the deleted results without a query edit");
    RefreshOpenSearches();
    const auto obsolete = submitted;
    SetQuery(fs, L"new query");
    DeliverSearchReply({obsolete, SearchState::Ready, {alpha}, 1});
    Check(!IsListingCurrent(fs), "late background reply cannot replace a newly typed query");

    SetQuery(fs, L"alpha | beta");
    SetEverythingFilter(fs, {L"Documents", L"ext:pdf | ext:txt"});
    Check(submittedText == L"<alpha | beta> <ext:pdf | ext:txt>" && GetQuery(fs) == L"alpha | beta",
          "saved filter intersects the complete query and leaves editor text intact");
    SetQuery(fs, L"");
    Check(submittedText == L"ext:pdf | ext:txt", "a filter works with an empty query");
    RefreshSourceSettings();
    Check(submittedText == L"ext:pdf | ext:txt", "refresh retains the saved filter");
    char filterPath[MAX_PATH]{};
    Check(fs->GetCurrentPath(filterPath) && std::string_view(filterPath) == "\\Documents" &&
          fs->IsCurrentPath(0, 0, filterPath), "active filter is visible in the panel path");
    SetQuery(fs, L"next");
    SetEverythingFilter(fs, {});
    Check(submittedText == L"next", "switching back to native filters restores the typed query");
    const auto oldLimit = submitted;
    configuredResultLimit = 0;
    RefreshSourceSettings();
    Check(submitted.limit == 0 && submitted.generation > oldLimit.generation,
          "removing the cap resubmits an open search");
    DeliverSearchReply({oldLimit, SearchState::Ready, {alpha}, 1});
    Check(!IsListingCurrent(fs), "changing the cap rejects the old page");
    configuredResultLimit = defaultResultLimit;
    provider->CloseFS(fs);
    testHost.active = nullptr;
    testHost.listing = nullptr;
}

void CheckMetadata()
{
    auto* provider = GetFilesystemProvider();
    auto* fs = provider->OpenFS("everything", 0);
    if (!fs) throw std::runtime_error("OpenFS failed");
    testHost.active = fs;
    SetQuery(fs, L"metadata");
    const SearchKey first = submitted;
    TestDirectory initial;
    testHost.Publish(initial);
    TestView view;
    initial.data->SetupView(TRUE, &view, nullptr, nullptr);
    Check(first.metadataColumns == defaultMetadataColumns && view.columns.size() == 2 &&
          std::string_view(view.columns[1].Name) == "Date modified", "compact default shows one date");

    SetVisibleMetadataColumns(allMetadataColumns);
    const SearchKey all = submitted;
    DeliverSearchReply({first, SearchState::Ready, {{L"stale", LR"(C:\stale)", false}}, 1});
    TestDirectory waiting;
    testHost.Publish(waiting);
    Check(all.instance == first.instance && all.generation > first.generation &&
          all.metadataColumns == allMetadataColumns && waiting.files.empty() && !IsListingCurrent(fs),
          "changing columns rejects a pending completion for the old mask");

    SYSTEMTIME local{}, utc{};
    local.wYear = 2024; local.wMonth = 11; local.wDay = 5;
    local.wHour = 12; local.wMinute = 34; local.wSecond = 56; local.wMilliseconds = 789;
    FILETIME ft{};
    if (!TzSpecificLocalTimeToSystemTimeEx(nullptr, &local, &utc) || !SystemTimeToFileTime(&utc, &ft))
        throw std::runtime_error("Cannot create time-zone-adjusted fixture");
    const std::uint64_t ticks = (std::uint64_t{ft.dwHighDateTime} << 32) | ft.dwLowDateTime;
    constexpr std::uint64_t minute = 60ull * 10000000;
    SearchHit full{L"sample.pdf", LR"(C:\meta\sample.pdf)", false};
    full.size = (1ull << 32) + 1;
    full.modified = ticks;
    full.created = ticks - minute;
    full.accessed = ticks + minute;
    full.attributes = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE | 0x80000000u;
    full.extension = L"pdf";
    full.fileList = LR"(C:\lists\sample.efu)";
    full.runCount = 3;
    full.dateRun = ticks + 2 * minute;
    full.recentlyChanged = ticks + 3 * minute;
    SearchHit zero{L"empty", LR"(C:\meta\empty)", false};
    zero.size = 0;
    zero.attributes = 0;
    zero.runCount = 0;
    SearchHit missing{L"folder", LR"(C:\meta\folder)", true};
    DeliverSearchReply({all, SearchState::Ready, {full, zero, missing}, 3});
    TestDirectory listing;
    testHost.Publish(listing);
    listing.data->SetupView(TRUE, &view, nullptr, nullptr);
    const auto& file = listing.files[0];
    const auto& empty = listing.files[1];
    Check(view.columns.size() == 12 && view.columns.back().SupportSorting &&
          view.Cell(11, file) == R"(C:\meta)", "Parent folder is available on request");
    Check(view.Cell(1, file) == "4294967297" && view.Cell(5, file) == "Read-only, Archive, 0x80000000" &&
          view.Cell(6, file) == "pdf" && view.Cell(7, file) == R"(C:\lists\sample.efu)" &&
          view.Cell(8, file) == "3", "optional cells preserve metadata");
    testHost.sizeMode = 1;
    Check(view.Cell(1, file) == "host KB", "KB uses host whole-kilobyte formatter");
    testHost.sizeMode = 2;
    Check(view.Cell(1, file) == "host mixed", "mixed units use host short formatter");
    testHost.sizeMode = 0;
    char expected[128]{};
    GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, expected, sizeof(expected));
    Check(view.Cell(2, file) == expected && view.Cell(3, file) == expected && view.Cell(4, file) == expected,
          "all three file dates can appear together in regional format");
    Check(view.Cell(1, empty) == "0" && view.Cell(5, empty) == "None" &&
          view.Cell(1, listing.dirs[0]).empty(), "zero and missing values stay distinct");

    CQuadWord bytes{};
    SYSTEMTIME dateTime{};
    dateTime.wHour = 23;
    Check(listing.validData == (VALID_DATA_EXTENSION | VALID_DATA_PL_SIZE | VALID_DATA_PL_DATE | VALID_DATA_PL_TIME) &&
          listing.data->GetByteSize(&file, FALSE, &bytes) && bytes.Value == *full.size &&
          listing.data->GetByteSize(&empty, FALSE, &bytes) && bytes.Value == 0 &&
          !listing.data->GetByteSize(&listing.dirs[0], TRUE, &bytes) &&
          listing.data->GetLastWriteDate(&file, FALSE, &dateTime) && dateTime.wYear == 2024 &&
          dateTime.wDay == 5 && dateTime.wHour == 23 &&
          listing.data->GetLastWriteTime(&file, FALSE, &dateTime) && dateTime.wDay == 5 &&
          dateTime.wHour == 12 && dateTime.wMilliseconds == 789 &&
          !listing.data->GetLastWriteDate(&empty, FALSE, &dateTime),
          "SDK byte-size and modification-time callbacks distinguish each row's available values");

    SetVisibleMetadataColumns(ColumnAttributes);
    SearchHit hidden{L"hidden.dat", LR"(C:\meta\hidden.dat)", false};
    hidden.attributes = FILE_ATTRIBUTE_HIDDEN;
    SearchHit system{L"system", LR"(C:\meta\system)", true};
    system.attributes = FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_DIRECTORY;
    DeliverSearchReply({submitted, SearchState::Ready, {hidden, system}, 2});
    TestDirectory attributed;
    testHost.Publish(attributed);
    attributed.data->SetupView(TRUE, &view, nullptr, nullptr);
    Check(attributed.GetFilesCount() == 1 && attributed.GetDirsCount() == 1 &&
          (attributed.validData & VALID_DATA_ATTRIBUTES) == 0 &&
          attributed.files[0].Attr == FILE_ATTRIBUTE_NORMAL && attributed.dirs[0].Attr == FILE_ATTRIBUTE_DIRECTORY &&
          view.Cell(1, attributed.files[0]) == "Hidden" && view.Cell(1, attributed.dirs[0]) == "System, Directory",
          "indexed hidden and system flags remain visible in cells without triggering host row filtering");

    SetVisibleMetadataColumns(0);
    const SearchKey none = submitted;
    DeliverSearchReply({none, SearchState::Ready, {missing}, 1});
    TestDirectory minimal;
    testHost.Publish(minimal);
    minimal.data->SetupView(TRUE, &view, nullptr, nullptr);
    Check(none.metadataColumns == 0 && minimal.validData == VALID_DATA_EXTENSION && view.columns.size() == 1 &&
          view.columns.front().ID == COLUMN_ID_NAME, "all optional fields can be hidden");
    provider->CloseFS(fs);
    testHost.listing = nullptr;
    testHost.active = provider->OpenFS("everything", 0);
    if (!testHost.active) throw std::runtime_error("OpenFS failed");
    SetQuery(testHost.active, L"next");
    Check(submitted.instance != none.instance && submitted.metadataColumns == 0,
          "a newly opened search inherits the stored column selection");
    provider->CloseFS(testHost.active);
    testHost.active = nullptr;
    SetVisibleMetadataColumns(defaultMetadataColumns);
}
}

int main()
{
    try {
        RunChecks();
        CheckMetadata();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
