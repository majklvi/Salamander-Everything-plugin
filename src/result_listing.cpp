#include "result_listing.h"
#include "result_order.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string_view>

// The 0.16 SDK deliberately discards CFileData::NameW in AddFile/AddDir.
// Escape unrepresentable characters and truncate at a character boundary.
std::string DisplayText(std::wstring_view text, size_t limit)
{
    const bool utf8 = GetACP() == CP_UTF8;
    const DWORD flags = utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    std::string result;
    size_t roomForEllipsis = 0;
    for (size_t i = 0; i < text.size();) {
        const unsigned lead = text[i];
        const bool pair = lead >= 0xD800 && lead <= 0xDBFF && i + 1 < text.size() &&
                          text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF;
        const int units = pair ? 2 : 1;
        char bytes[16];
        BOOL usedDefault = FALSE;
        int count = WideCharToMultiByte(CP_ACP, flags, text.data() + i, units,
                                        bytes, sizeof(bytes), nullptr, utf8 ? nullptr : &usedDefault);
        if (!count || usedDefault) {
            const unsigned code = pair ? 0x10000 + ((lead - 0xD800) << 10) + text[i + 1] - 0xDC00 : lead;
            count = sprintf_s(bytes, "[U+%04X]", code);
        }
        if (result.size() + count > limit) {
            result.resize(roomForEllipsis);
            result += "...";
            break;
        }
        result.append(bytes, count);
        if (result.size() <= limit - 3)
            roomForEllipsis = result.size();
        i += units;
    }
    return result;
}

namespace {

std::wstring_view ParentOf(const SearchHit& hit)
{
    const auto end = hit.path.find_last_of(L"\\/");
    if (end == std::wstring::npos) return {};
    const bool root = end == 0 || hit.path[end - 1] == L':';
    return std::wstring_view(hit.path).substr(0, end + (root ? 1 : 0));
}

bool LocalTime(const std::optional<std::uint64_t>& ticks, SYSTEMTIME& local)
{
    if (!ticks) return false;
    FILETIME fileTime{static_cast<DWORD>(*ticks), static_cast<DWORD>(*ticks >> 32)};
    FILETIME localFileTime{};
    return FileTimeToLocalFileTime(&fileTime, &localFileTime) &&
           FileTimeToSystemTime(&localFileTime, &local);
}

std::string DisplayDate(const std::optional<std::uint64_t>& ticks)
{
    SYSTEMTIME local{};
    if (!LocalTime(ticks, local)) return {};
    char text[TRANSFER_BUFFER_MAX];
    if (!GetDateFormatA(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &local, nullptr, text, sizeof(text)))
        sprintf_s(text, "%u.%u.%u", local.wDay, local.wMonth, local.wYear);
    return text;
}

std::string DisplaySize(const SearchHit& hit)
{
    if (!hit.size) return {};
    int mode = 0;
    gGeneral->GetConfigParameter(SALCFG_SIZEFORMAT, &mode, sizeof(mode), nullptr);
    CQuadWord bytes{};
    bytes.SetUI64(*hit.size);
    char text[128];
    return mode == 0 ? gGeneral->NumberToStr(text, bytes)
                     : gGeneral->PrintDiskSize(text, bytes, mode == 1 ? 3 : 0);
}

std::string DisplayAttributes(std::uint32_t bits)
{
    if (!bits) return "None";
    constexpr struct { DWORD flag; const char* name; } attributes[] = {
        {FILE_ATTRIBUTE_READONLY, "Read-only"}, {FILE_ATTRIBUTE_HIDDEN, "Hidden"},
        {FILE_ATTRIBUTE_SYSTEM, "System"}, {FILE_ATTRIBUTE_DIRECTORY, "Directory"},
        {FILE_ATTRIBUTE_ARCHIVE, "Archive"}, {FILE_ATTRIBUTE_DEVICE, "Device"},
        {FILE_ATTRIBUTE_NORMAL, "Normal"}, {FILE_ATTRIBUTE_TEMPORARY, "Temporary"},
        {FILE_ATTRIBUTE_SPARSE_FILE, "Sparse"}, {FILE_ATTRIBUTE_REPARSE_POINT, "Reparse point"},
        {FILE_ATTRIBUTE_COMPRESSED, "Compressed"}, {FILE_ATTRIBUTE_OFFLINE, "Offline"},
        {FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "Not content indexed"}, {FILE_ATTRIBUTE_ENCRYPTED, "Encrypted"},
        {FILE_ATTRIBUTE_INTEGRITY_STREAM, "Integrity stream"}, {FILE_ATTRIBUTE_VIRTUAL, "Virtual"},
        {FILE_ATTRIBUTE_NO_SCRUB_DATA, "No scrub data"}, {FILE_ATTRIBUTE_PINNED, "Pinned"},
        {FILE_ATTRIBUTE_UNPINNED, "Unpinned"}, {FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS, "Recall on data access"},
    };
    std::string text;
    for (const auto& attribute : attributes) {
        if (bits & attribute.flag) {
            if (!text.empty()) text += ", ";
            text += attribute.name;
            bits &= ~attribute.flag;
        }
    }
    if (bits) {
        char hex[12];
        sprintf_s(hex, "0x%08X", bits);
        if (!text.empty()) text += ", ";
        text += hex;
    }
    return text;
}

std::string FormatMetadata(const SearchHit& hit, MetadataColumn flag)
{
    switch (flag) {
    case ColumnSize:            return DisplaySize(hit);
    case ColumnModified:        return DisplayDate(hit.modified);
    case ColumnCreated:         return DisplayDate(hit.created);
    case ColumnAccessed:        return DisplayDate(hit.accessed);
    case ColumnAttributes:      return hit.attributes ? DisplayAttributes(*hit.attributes) : "";
    case ColumnExtension:       return hit.extension ? DisplayText(*hit.extension, TRANSFER_BUFFER_MAX - 1) : "";
    case ColumnFileList:        return hit.fileList ? DisplayText(*hit.fileList, TRANSFER_BUFFER_MAX - 1) : "";
    case ColumnRunCount:        return hit.runCount ? std::to_string(*hit.runCount) : "";
    case ColumnDateRun:         return DisplayDate(hit.dateRun);
    case ColumnRecentlyChanged: return DisplayDate(hit.recentlyChanged);
    case ColumnParent:          return {};
    }
    return {};
}

// Pointers to global host transfer slots, filled immediately before GetText.
const CFileData** transferFile = nullptr;
char* transferBuffer = nullptr;
int* transferLength = nullptr;
DWORD* transferColumn = nullptr;

const ResultRow* ListingRow(const CFileData* file)
{
    return file ? reinterpret_cast<const ResultRow*>(file->PluginData) : nullptr;
}

// Column zero is Parent folder. Each metadata column uses its one-based
// catalog index, independent of which other columns the user has enabled.
// Samandarin 0.16 sets sortKeyFlag only while gathering native sort keys
// for a custom column carrying separateSortKey. Painting uses the base ID.
constexpr DWORD separateSortKey = 0x20000000;
constexpr DWORD sortKeyFlag = 0x80000000;
constexpr DWORD rankColumn = 99;
bool rankingFilteredRows = false;
bool collectingHeaderSort = false;
unsigned observedColumnSort = 0;
DWORD BaseColumn(DWORD data) { return data & ~(separateSortKey | sortKeyFlag); }

unsigned HeaderColumnSort(DWORD raw)
{
    if (!(raw & sortKeyFlag)) return 0;
    const DWORD id = BaseColumn(raw);
    if (id >= firstPropertyOrder) return id;
    if (!id) return EVERYTHING_SORT_PATH_ASCENDING;
    return id <= std::size(metadataColumns) ? metadataColumns[id - 1].ascending : 0;
}

void WINAPI ColumnText()
{
    *transferLength = 0;
    const auto* row = ListingRow(*transferFile);
    const DWORD raw = *transferColumn;
    if (collectingHeaderSort && (raw & sortKeyFlag)) observedColumnSort = HeaderColumnSort(raw);
    const DWORD column = BaseColumn(raw);
    if (row && (column == rankColumn || (rankingFilteredRows && (raw & sortKeyFlag)))) {
        *transferLength = sprintf_s(transferBuffer, TRANSFER_BUFFER_MAX, "%u", row->rank);
        return;
    }
    if (row && column >= firstPropertyOrder) {
        const auto it = row->hit.propertyValues.find(column);
        if (it != row->hit.propertyValues.end()) {
            const auto value = DisplayText(it->second, TRANSFER_BUFFER_MAX - 1);
            std::memcpy(transferBuffer, value.data(), value.size());
            *transferLength = static_cast<int>(value.size());
        }
        return;
    }
    if (!row || column > std::size(metadataColumns)) return;
    const auto text = column ? FormatMetadata(row->hit, metadataColumns[column - 1].flag) : row->displayParent;
    *transferLength = static_cast<int>(text.size());
    std::memcpy(transferBuffer, text.data(), text.size());
}

struct ColumnWidth { unsigned width = 0; bool fixed = false; };
std::map<DWORD, ColumnWidth> columnWidths[2];

void InsertResultColumn(CSalamanderViewAbstract* view, BOOL left, DWORD id, const char* title, bool numeric)
{
    const auto& width = columnWidths[left != FALSE][id];
    CColumn column{};
    strncpy_s(column.Name, title, _TRUNCATE);
    if (id) _snprintf_s(column.Description, _TRUNCATE, "%s from the Everything index", title);
    else strcpy_s(column.Description, "Indexed folder containing the result");
    column.GetText = ColumnText;
    column.CustomData = separateSortKey | id;
    column.LeftAlignment = !numeric;
    column.SupportSorting = TRUE;
    column.FixedWidth = width.fixed;
    column.Width = width.width;
    view->InsertColumn(view->GetColumnsCount(), &column);
}

class ListingData final : public CPluginDataInterfaceAbstract {
public:
    explicit ListingData(std::shared_ptr<const ResultPage> value) : page(std::move(value)) {}
    std::shared_ptr<const ResultPage> page;
    std::string filterLabel;
    bool filteredRanks = false;
    bool filtered = false;

    void WINAPI SetupView(BOOL left, CSalamanderViewAbstract* view, const char*, const CFileData*) override {
        if (view->GetViewMode() != VIEW_MODE_DETAILED) return;
        int* isDir; DWORD* rowData; CPluginDataInterfaceAbstract** pluginData;
        view->GetTransferVariables(transferFile, isDir, transferBuffer, transferLength, rowData, pluginData, transferColumn);
        for (int i = view->GetColumnsCount() - 1; i > 0; --i)
            view->DeleteColumn(i);
        const unsigned visible = VisibleMetadataColumns();
        for (DWORD i = 0; i < std::size(metadataColumns); ++i) {
            const auto& field = metadataColumns[i];
            if (visible & field.flag)
                InsertResultColumn(view, left, field.flag == ColumnParent ? 0 : i + 1, field.title, field.flag == ColumnSize || field.flag == ColumnRunCount);
        }
        if (page) for (const auto& property : page->properties) {
            const auto title = DisplayText(property.title, sizeof(CColumn::Name) - 1);
            InsertResultColumn(view, left, property.order, title.c_str(), false);
        }
        if (filteredRanks && view->GetColumnsCount() == 1)
            InsertResultColumn(view, left, rankColumn, "Rank", true);
    }
    void WINAPI ColumnFixedWidthShouldChange(BOOL left, const CColumn* column, int fixed) override {
        auto& widths = columnWidths[left != FALSE];
        if (column->ID == COLUMN_ID_CUSTOM)
            widths[BaseColumn(column->CustomData)] = {column->Width, fixed != 0};
    }
    void WINAPI ColumnWidthWasChanged(BOOL left, const CColumn* column, int width) override {
        auto& widths = columnWidths[left != FALSE];
        if (column->ID == COLUMN_ID_CUSTOM)
            widths[BaseColumn(column->CustomData)].width = width;
    }
    BOOL WINAPI GetInfoLineContent(int panel, const CFileData*, BOOL, int files, int dirs, BOOL,
                                   const CQuadWord&, char* buffer, DWORD*, int& hotCount) override {
        if (!page) return FALSE;
        hotCount = 0;
        unsigned shown = static_cast<unsigned>(page->rows.size());
        if (filtered && gGeneral->GetPanelPluginData(panel) == this) {
            shown = 0;
            int index = 0;
            while (gGeneral->GetPanelItem(panel, &index, nullptr)) ++shown;
        }
        std::string order = "Name ascending";
        if (page->sort == EVERYTHING_SORT_NAME_DESCENDING) order = "Name descending";
        for (const auto& column : metadataColumns) {
            if (page->sort == column.ascending || page->sort == column.descending) {
                order = column.title;
                order += page->sort == column.descending ? " descending" : " ascending";
                break;
            }
        }
        if (const auto* property = PropertyForOrder(page->sort)) {
            order = DisplayText(property->title, 160);
            order += page->sort == property->order ? " ascending" : " descending";
        }
        if (page->sort == mostOpenedSort) order = "most opened first";
        if (page->sort == leastOpenedSort) order = "least opened first";
        if (!filterLabel.empty()) order += " | Filter: " + filterLabel;
        if (files || dirs)
            sprintf_s(buffer, 1000, "%u of %u results | %d selected | Everything page by %s",
                      shown, page->total, files + dirs, order.c_str());
        else
            sprintf_s(buffer, 1000, "%u of %u results | Everything page by %s",
                      shown, page->total, order.c_str());
        return TRUE;
    }
    // PluginData points into the immutable page. Releasing this interface
    // disposes of every row in one step after the host has finished using it.
    BOOL WINAPI CallReleaseForFiles() override { return FALSE; }
    BOOL WINAPI CallReleaseForDirs() override { return FALSE; }
    void WINAPI ReleasePluginData(CFileData&, BOOL) override {}
    void WINAPI GetFileDataForUpDir(const char*, CFileData&) override {}
    BOOL WINAPI GetFileDataForNewDir(const char*, CFileData&) override { return FALSE; }
    HIMAGELIST WINAPI GetSimplePluginIcons(int) override { return nullptr; }
    BOOL WINAPI HasSimplePluginIcon(CFileData&, BOOL) override { return TRUE; }
    HICON WINAPI GetPluginIcon(const CFileData*, int, BOOL& destroy) override { destroy = FALSE; return nullptr; }
    int WINAPI CompareFilesFromFS(const CFileData* a, const CFileData* b) override {
        const auto* first = reinterpret_cast<const ResultRow*>(a->PluginData);
        const auto* second = reinterpret_cast<const ResultRow*>(b->PluginData);
        return first->hit.path.compare(second->hit.path);
    }
    BOOL WINAPI CanBeCopiedToClipboard() override { return FALSE; }
    BOOL WINAPI GetByteSize(const CFileData* file, BOOL, CQuadWord* bytes) override {
        const auto* row = ListingRow(file);
        if (!row || !row->hit.size) return FALSE;
        bytes->SetUI64(*row->hit.size);
        return TRUE;
    }
    BOOL WINAPI GetLastWriteDate(const CFileData* file, BOOL, SYSTEMTIME* date) override {
        const auto* row = ListingRow(file);
        SYSTEMTIME local{};
        if (!row || !LocalTime(row->hit.modified, local)) return FALSE;
        date->wYear = local.wYear;
        date->wMonth = local.wMonth;
        date->wDay = local.wDay;
        date->wDayOfWeek = local.wDayOfWeek;
        return TRUE;
    }
    BOOL WINAPI GetLastWriteTime(const CFileData* file, BOOL, SYSTEMTIME* time) override {
        const auto* row = ListingRow(file);
        SYSTEMTIME local{};
        if (!row || !LocalTime(row->hit.modified, local)) return FALSE;
        time->wHour = local.wHour;
        time->wMinute = local.wMinute;
        time->wSecond = local.wSecond;
        time->wMilliseconds = local.wMilliseconds;
        return TRUE;
    }
};
}

bool HasFilteredRanks(CPluginDataInterfaceAbstract* data)
{
    return data && static_cast<ListingData*>(data)->filteredRanks;
}

void SetRankingFilteredRows(bool enabled)
{
    rankingFilteredRows = enabled;
}

void BeginHeaderSortObservation()
{
    collectingHeaderSort = true;
    observedColumnSort = 0;
    // Even on an empty listing the host sets the transfer slot when building
    // its sort cache. Discard any marker left over from an earlier click.
    if (transferColumn) *transferColumn &= ~sortKeyFlag;
}

unsigned EndHeaderSortObservation()
{
    collectingHeaderSort = false;
    return observedColumnSort ? observedColumnSort : transferColumn ? HeaderColumnSort(*transferColumn) : 0;
}

ResultPage::ResultPage(SearchReply&& reply) : total(reply.total), sort(reply.key.sort)
{
    properties = std::move(reply.properties);
    rows.reserve(reply.hits.size());
    for (auto& hit : reply.hits) {
        std::string prefix;
        if (UsesRankedNames(sort)) {
            char rank[24];
            sprintf_s(rank, "%03llu  ", static_cast<unsigned long long>(reply.key.offset) + rows.size() + 1);
            prefix = rank;
        }
        auto name = prefix + DisplayText(hit.name, MAX_PATH - 5 - prefix.size());
        if (UsesRankedNames(sort) && hit.directory) name += "\\";
        auto parent = DisplayText(ParentOf(hit), TRANSFER_BUFFER_MAX - 1);
        ResultRow row{std::move(hit), std::move(name), std::move(parent),
                      static_cast<unsigned>(reply.key.offset + rows.size() + 1)};
        rows.push_back(std::move(row));
    }
}

const SearchHit* FindResultHit(const ResultPage* page, const CFileData* file)
{
    if (!page || !file) return nullptr;
    for (const auto& row : page->rows)
        if (reinterpret_cast<DWORD_PTR>(&row) == file->PluginData) return &row.hit;
    return nullptr;
}

const ResultPage* GetListingPage(CPluginDataInterfaceAbstract* data)
{
    return data ? static_cast<ListingData*>(data)->page.get() : nullptr;
}

void ReleaseResultListing(CPluginDataInterfaceAbstract* data)
{
    delete static_cast<ListingData*>(data);
}

bool ListResultPage(const std::shared_ptr<const ResultPage>& page, CSalamanderDirectoryAbstract* directory,
                    CPluginDataInterfaceAbstract*& data, bool panelFilter, const std::wstring& filterName)
{
    data = nullptr;
    auto listing = std::unique_ptr<ListingData>(new (std::nothrow) ListingData(page));
    if (!listing) return false;
    listing->filterLabel = DisplayText(filterName, 160);
    listing->filtered = panelFilter;
    listing->filteredRanks = panelFilter && page && UsesRankedNames(page->sort);
    DWORD valid = VALID_DATA_EXTENSION;
    if (page) {
        for (const auto& row : page->rows) {
            if (row.hit.size) valid |= VALID_DATA_PL_SIZE;
            if (row.hit.modified) valid |= VALID_DATA_PL_DATE | VALID_DATA_PL_TIME;
        }
    }
    directory->SetValidData(valid);
    if (page) {
        for (const auto& item : page->rows) {
            CFileData row{};
            // Ctrl+F12 compares the native Name and Ext. Keep rank decorations
            // out of both fields while a panel mask is active.
            const auto& name = listing->filteredRanks ? DisplayText(item.hit.name, MAX_PATH - 5)
                                                     : item.displayName;
            row.Name = gGeneral->DupStr(name.c_str());
            if (!row.Name) { directory->Clear(listing.get()); return false; }
            row.NameLen = static_cast<unsigned>(name.size());
            char* dot = std::strrchr(row.Name, '.');
            row.Ext = dot ? dot + 1 : row.Name + row.NameLen;
            // Samandarin filters its native rows by hidden/system attributes.
            // Keep indexed attributes in the display column so enabling it
            // preserves the membership of the Everything result page.
            row.Attr = item.hit.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
            row.IconOverlayIndex = ICONOVERLAYINDEX_NOTUSED;
            row.PluginData = reinterpret_cast<DWORD_PTR>(&item);
            // A single group preserves Everything's ranking across folders and files.
            // Open and Reveal use the original type and full path from the snapshot.
            const BOOL added = item.hit.directory && !UsesRankedNames(page->sort) ? directory->AddDir(nullptr, row, listing.get())
                                                  : directory->AddFile(nullptr, row, listing.get());
            if (!added) {
                gGeneral->Free(row.Name);
                directory->Clear(listing.get());
                return false;
            }
        }
    }
    data = listing.release();
    return true;
}
