#include "result_listing.h"
#include "result_order.h"
#include "settings_resource.h"

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

int TextWidth(HDC dc, std::string_view text)
{
    SIZE extent{};
    GetTextExtentPoint32A(dc, text.data(), static_cast<int>(text.size()), &extent);
    return extent.cx;
}

// Count complete Unicode characters from each end before encoding. The SDK
// byte limit also applies on code pages that need a [U+xxxx] escape.
std::string ResultLabel(std::wstring_view text, HDC dc, int pixels, size_t limit)
{
    const size_t count = text.size();
    if (count <= limit) {
        auto full = DisplayText(text, 10 * count + 3);
        if (full.size() <= limit && (!dc || TextWidth(dc, full) <= pixels)) return full;
    }
    static const auto marker = [] {
        auto value = DisplayText(L"\u2026", 16);
        return value == "[U+2026]" ? std::string("...") : value;
    }();
    if (dc && TextWidth(dc, marker) > pixels) return {};
    auto split = [&](size_t pos) {
        return pos && pos < count && text[pos] >= 0xdc00 && text[pos] <= 0xdfff &&
               text[pos-1] >= 0xd800 && text[pos-1] <= 0xdbff;
    };
    size_t low = 0, high = (std::min)(count, limit);
    std::string best = marker;
    while (low < high) {
        size_t keep = (low + high + 1) / 2;
        size_t head = keep / 2, tail = count - (keep - head);
        if (split(head)) --head;
        if (split(tail)) ++tail;
        auto value = DisplayText(text.substr(0, head), head * 10 + 3) + marker +
                     DisplayText(text.substr(tail), (count - tail) * 10 + 3);
        if (value.size() <= limit && (!dc || TextWidth(dc, value) <= pixels)) {
            best = std::move(value);
            low = keep;
        } else high = keep - 1;
    }
    return best;
}

struct LabelLayout {
    HWND window = nullptr;
    HDC dc = nullptr;
    HFONT font = nullptr;
    int viewport = 0, name = 0, parent = 0;
    UINT dpi = 0;
    ~LabelLayout() { if (dc) DeleteDC(dc); if (font) DeleteObject(font); }
    int Width() const {
        RECT rect{};
        return GetClientRect(window, &rect) ? rect.right : 0;
    }
};
CPluginDataInterfaceAbstract** transferListing = nullptr;
LabelLayout* CurrentLayout();

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
    case ColumnSize:
    case ColumnModified:
    case ColumnExtension:       return {}; // Standard panel columns.
    case ColumnCreated:         return DisplayDate(hit.created);
    case ColumnAccessed:        return DisplayDate(hit.accessed);
    case ColumnAttributes:      return hit.attributes ? DisplayAttributes(*hit.attributes) : "";
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
int* transferIsDir = nullptr;

char* transferBuffer = nullptr;
int* transferLength = nullptr;
DWORD* transferColumn = nullptr;

const ResultRow* ListingRow(const CFileData* file)
{
    return file ? reinterpret_cast<const ResultRow*>(file->PluginData) : nullptr;
}

int WINAPI PlaceholderIndex()
{
    const auto* row = ListingRow(transferFile ? *transferFile : nullptr);
    if (!row) return transferIsDir && *transferIsDir ? 1 : 0;
    const auto& availability = row->owner->availability;
    const int base = row->hit.directory ? 1 : 0;
    if (!availability) return base;
    const auto state = availability->State(row->hit.path);
    if (state == PathState::Pending) CheckResultPath(availability, row->hit.path);
    switch (state) {
    case PathState::Missing: return 2 + base;
    case PathState::Pending: return 4 + base;
    default: return base;
    }
}

// Column zero is Parent folder. Each metadata column uses its one-based
// catalog index, independent of which other columns the user has enabled.
// Samandarin 0.16 sets sortKeyFlag only while gathering native sort keys
// for a custom column carrying separateSortKey. Painting uses the base ID.
constexpr DWORD separateSortKey = 0x20000000;
constexpr DWORD sortKeyFlag = 0x80000000;
constexpr DWORD rankColumn = 99;
constexpr DWORD compactNameColumn = 100;
constexpr DWORD compactStemColumn = 101;
bool rankingFilteredRows = false;
bool collectingHeaderSort = false;
unsigned observedColumnSort = 0;
DWORD BaseColumn(DWORD data) { return data & ~(separateSortKey | sortKeyFlag); }

unsigned HeaderColumnSort(DWORD raw)
{
    if (!(raw & sortKeyFlag)) return 0;
    const DWORD id = BaseColumn(raw);
    if (id >= firstPropertyOrder) return id;
    if (id == compactNameColumn || id == compactStemColumn) return EVERYTHING_SORT_NAME_ASCENDING;
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
    if (row && (column == compactNameColumn || column == compactStemColumn)) {
        std::wstring_view name = row->hit.name;
        const bool sortKey = (raw & sortKeyFlag) != 0;
        if (!sortKey && column == compactStemColumn && !row->hit.directory) {
            const auto dot = name.rfind(L'.');
            if (dot != name.npos && dot != 0 && dot + 1 < name.size()) name = name.substr(0, dot);
        }
        std::string prefixText, suffix;
        // Preserve the existing rank prefix and folder suffix only in listings that use them.
        if (!sortKey && UsesRankedNames(row->owner->sort) && (*transferFile)->Name == row->displayName) {
            const auto prefix = row->displayName.find("  ");
            if (prefix != std::string::npos) prefixText = row->displayName.substr(0, prefix + 2);
            if (row->hit.directory) suffix = "\\";
        }
        const auto* layout = CurrentLayout();
        const auto value = !sortKey && layout && layout->dc
            ? prefixText + ResultLabel(name, layout->dc, layout->name - TextWidth(layout->dc, prefixText + suffix),
                                       MAX_PATH - 5 - prefixText.size() - suffix.size()) + suffix
            : prefixText + DisplayText(name, (sortKey ? TRANSFER_BUFFER_MAX - 1 : MAX_PATH - 5) - prefixText.size() - suffix.size()) + suffix;
        std::memcpy(transferBuffer, value.data(), value.size());
        *transferLength = static_cast<int>(value.size());
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
    std::string text;
    const auto* layout = CurrentLayout();
    if (column) text = FormatMetadata(row->hit, metadataColumns[column - 1].flag);
    else if (!(raw & sortKeyFlag) && layout && layout->dc)
        text = ResultLabel(ParentOf(row->hit), layout->dc, layout->parent, TRANSFER_BUFFER_MAX - 1);
    else text = row->displayParent;
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
    ~ListingData() { if (placeholders) ImageList_Destroy(placeholders); }
    HIMAGELIST placeholders = nullptr;
    std::shared_ptr<const ResultPage> page;
    std::string filterLabel;
    LabelLayout layout;
    bool filteredRanks = false;
    bool filtered = false;
    bool nativeOrderApplied = false;

    void WINAPI SetupView(BOOL left, CSalamanderViewAbstract* view, const char*, const CFileData*) override {
        DWORD* rowData;
        view->GetTransferVariables(transferFile, transferIsDir, transferBuffer, transferLength, rowData, transferListing, transferColumn);
        view->SetPluginSimpleIconCallback(PlaceholderIndex);
        const bool detailed = view->GetViewMode() == VIEW_MODE_DETAILED;
        if (shortenResultLabels) {
            // Samandarin 0.16 supports a display callback on column zero in Brief and
            // Detailed views. Its bundled Salamatrix FS installs it the same way.
            if (auto* name = const_cast<CColumn*>(view->GetColumn(0))) {
                name->GetText = ColumnText;
                name->ID = COLUMN_ID_CUSTOM;
                name->CustomData = separateSortKey | (detailed && (VisibleMetadataColumns() & ColumnExtension)
                                                    ? compactStemColumn : compactNameColumn);
            }
        }
        if (!detailed) { ConfigureLabels(view, left); return; }
        for (int i = view->GetColumnsCount() - 1; i > 0; --i)
            view->DeleteColumn(i);
        const unsigned visible = VisibleMetadataColumns();
        if (visible & ColumnExtension)
            view->InsertStandardColumn(1, COLUMN_ID_EXTENSION);
        for (DWORD i = 0; i < std::size(metadataColumns); ++i) {
            const auto& field = metadataColumns[i];
            if (!(visible & field.flag) || field.flag == ColumnExtension) continue;
            if (field.flag == ColumnSize || field.flag == ColumnModified)
                view->InsertStandardColumn(view->GetColumnsCount(), field.flag == ColumnSize ? COLUMN_ID_SIZE : COLUMN_ID_DATE);
            else
                InsertResultColumn(view, left, field.flag == ColumnParent ? 0 : i + 1, field.title, field.flag == ColumnRunCount);
        }
        if (page) for (const auto& property : page->properties) {
            const auto title = DisplayText(property.title, sizeof(CColumn::Name) - 1);
            InsertResultColumn(view, left, property.order, title.c_str(), false);
        }
        bool hasCustom = false;
        for (int i = 1; i < view->GetColumnsCount(); ++i)
            hasCustom |= view->GetColumn(i)->ID == COLUMN_ID_CUSTOM;
        if (filteredRanks && !hasCustom)
            InsertResultColumn(view, left, rankColumn, "Rank", true);
        ConfigureLabels(view, left);
    }

    void ConfigureLabels(CSalamanderViewAbstract* view, BOOL left) {
        if (!shortenResultLabels || !page) return;
        layout.window = ResultLayoutWindow(left);
        layout.viewport = layout.Width();
        if (!layout.viewport) return;
        const UINT dpi = GetDpiForWindow(layout.window);
        if (!layout.dc || layout.dpi != dpi) {
            if (layout.dc) DeleteDC(layout.dc);
            if (layout.font) DeleteObject(layout.font);
            layout.font = nullptr;
            layout.dpi = dpi;
            layout.dc = CreateCompatibleDC(nullptr);
            if (!layout.dc) return;
            LOGFONTW lf{};
            if (SystemParametersInfoForDpi(SPI_GETICONTITLELOGFONT, sizeof(lf), &lf, 0, dpi))
                layout.font = CreateFontIndirectW(&lf);
            SelectObject(layout.dc, layout.font ? layout.font : GetStockObject(DEFAULT_GUI_FONT));
        }
        auto* name = const_cast<CColumn*>(view->GetColumn(0));
        CColumn* parent = nullptr;
        int available = layout.viewport;
        for (int i = 1; i < view->GetColumnsCount(); ++i) {
            auto* col = const_cast<CColumn*>(view->GetColumn(i));
            if (col->ID == COLUMN_ID_CUSTOM && BaseColumn(col->CustomData) == 0) parent = col;
            else available -= col->Width ? col->Width : TextWidth(layout.dc, col->Name) + MulDiv(32, dpi, 96);
        }
        // The host clips fixed cells using SPACE_WIDTH (10 px at 96 DPI),
        // with extra space for the icon and focus frame in column zero.
        const int pad = MulDiv(10, dpi, 96), namePad = pad + MulDiv(20, dpi, 96);
        int nameWidth = TextWidth(layout.dc, name->Name) + namePad;
        int parentWidth = parent ? TextWidth(layout.dc, parent->Name) + pad : 0;
        for (const auto& row : page->rows) {
            nameWidth = (std::max)(nameWidth, TextWidth(layout.dc, row.displayName) + namePad);
            if (parent) parentWidth = (std::max)(parentWidth, TextWidth(layout.dc, row.displayParent) + pad);
        }
        const int minimum = MulDiv(84, dpi, 96);
        available = (std::max)(available, minimum * (parent ? 2 : 1));
        const auto savedName = columnWidths[left != FALSE].find(compactNameColumn);
        if (savedName != columnWidths[left != FALSE].end() && savedName->second.fixed) {
            name->FixedWidth = TRUE;
            name->Width = savedName->second.width;
        }
        const bool fixedName = name->FixedWidth && name->Width;
        const bool fixedParent = parent && parent->FixedWidth && parent->Width;
        if (fixedName) nameWidth = name->Width;
        if (fixedParent) parentWidth = parent->Width;
        if (!fixedName && !fixedParent && nameWidth + parentWidth > available) {
            nameWidth = (std::min)(nameWidth, (std::max)(available / 2, available - parentWidth));
            parentWidth = (std::min)(parentWidth, available - nameWidth);
        } else if (!fixedName) nameWidth = (std::min)(nameWidth, available - parentWidth);
        else if (!fixedParent) parentWidth = (std::min)(parentWidth, available - nameWidth);
        if (!fixedName) nameWidth = (std::max)(minimum, nameWidth);
        if (parent && !fixedParent) parentWidth = (std::max)(minimum, parentWidth);
        layout.name = (std::max)(1, nameWidth - namePad);
        layout.parent = (std::max)(1, parentWidth - pad);
        // The 0.16 host measures custom column zero without its file icon.
        // Supply the calculated cell width so the retained suffix fits as well.
        if (view->GetViewMode() == VIEW_MODE_DETAILED && !fixedName) {
            name->Width = static_cast<WORD>((std::clamp)(nameWidth, namePad + 12, 65535));
            name->FixedWidth = TRUE;
        }
    }
    void WINAPI ColumnFixedWidthShouldChange(BOOL left, const CColumn* column, int fixed) override {
        auto& widths = columnWidths[left != FALSE];
        if (column->ID == COLUMN_ID_CUSTOM)
            widths[BaseColumn(column->CustomData)] = {column->Width, fixed != 0};
    }
    void WINAPI ColumnWidthWasChanged(BOOL left, const CColumn* column, int width) override {
        auto& widths = columnWidths[left != FALSE];
        if (column->ID == COLUMN_ID_CUSTOM) {
            const auto id = BaseColumn(column->CustomData);
            widths[id].width = width;
            const auto dpi = layout.window ? GetDpiForWindow(layout.window) : 96;
            if (id == 0) layout.parent = width - MulDiv(10, dpi, 96);
            if (id == compactNameColumn || id == compactStemColumn) {
                const int pixels = width - MulDiv(30, dpi, 96);
                if (pixels != layout.name) widths[compactNameColumn] = {static_cast<unsigned>(width), true};
                layout.name = pixels;
            }
        }
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
    HIMAGELIST WINAPI GetSimplePluginIcons(int size) override {
        if (placeholders) ImageList_Destroy(placeholders);
        const int pixels = size == SALICONSIZE_48 ? 48 : size == SALICONSIZE_32 ? 32 : 16;
        placeholders = ImageList_Create(pixels, pixels, ILC_COLOR32 | ILC_MASK, 8, 0);
        if (!placeholders) return nullptr;
        for (int kind : {SALICON_NONASSOCIATED, SALICON_DIRECTORY}) {
            HICON icon = gGeneral->GetSalamanderIcon(kind, size);
            int index = icon ? ImageList_AddIcon(placeholders, icon) : -1;
            if (icon) DestroyIcon(icon);
            if (index < 0) {
                ImageList_Destroy(placeholders);
                placeholders = nullptr;
                break;
            }
        }
        // Ordinary FS refreshes copy the old IconOverlayIndex by basename.
        // Keep badges in our file/folder placeholders instead. The paint
        // callback selects a variant by the current state of the exact path.
        for (int resource : {IDI_UNAVAILABLE, IDI_CHECKING}) {
            if (!placeholders) break;
            HICON badge = static_cast<HICON>(LoadImageW(gInstance, MAKEINTRESOURCEW(resource),
                                                        IMAGE_ICON, pixels, pixels, LR_DEFAULTCOLOR));
            const int slot = badge ? ImageList_AddIcon(placeholders, badge) : -1;
            if (badge) DestroyIcon(badge);
            bool ok = slot >= 0 && ImageList_SetOverlayImage(placeholders, slot, 1);
            for (int base = 0; ok && base < 2; ++base) {
                HICON decorated = ImageList_GetIcon(placeholders, base, ILD_TRANSPARENT | INDEXTOOVERLAYMASK(1));
                ok = decorated && ImageList_AddIcon(placeholders, decorated) >= 0;
                if (decorated) DestroyIcon(decorated);
            }
            if (ok) ok = ImageList_Remove(placeholders, slot) != FALSE;
            if (!ok) {
                ImageList_Destroy(placeholders);
                placeholders = nullptr;
            }
        }
        return placeholders;
    }
    BOOL WINAPI HasSimplePluginIcon(CFileData& file, BOOL) override {
        const auto* row = ListingRow(&file);
        return !row || (row->owner->availability &&
                        row->owner->availability->State(row->hit.path) != PathState::Present);
    }
    HICON WINAPI GetPluginIcon(const CFileData* file, int size, BOOL& destroy) override {
        destroy = FALSE;
        const auto* row = ListingRow(file);
        if (!row) return nullptr;
        // Only the host icon reader resolves the path. A PIDL preserves its Unicode spelling.
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(init) && init != RPC_E_CHANGED_MODE) return nullptr;
        PIDLIST_ABSOLUTE item = nullptr;
        HICON icon = nullptr;
        if (SUCCEEDED(SHParseDisplayName(row->hit.path.c_str(), nullptr, &item, 0, nullptr))) {
            gGeneral->GetFileIcon(reinterpret_cast<const char*>(item), TRUE, &icon, size, FALSE, row->hit.directory);
            CoTaskMemFree(item);
        }
        if (SUCCEEDED(init)) CoUninitialize();
        destroy = icon != nullptr;
        return icon;
    }
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
LabelLayout* CurrentLayout() {
    auto* listing = transferListing ? static_cast<ListingData*>(*transferListing) : nullptr;
    return listing ? &listing->layout : nullptr;
}
}

bool ResultLayoutResized(CPluginDataInterfaceAbstract* data) {
    if (!data || !shortenResultLabels) return false;
    auto& layout = static_cast<ListingData*>(data)->layout;
    const int width = layout.Width();
    if (!width || (width == layout.viewport && GetDpiForWindow(layout.window) == layout.dpi)) return false;
    layout.viewport = width;
    return true;
}

bool TakeNativeOrderUpdate(CPluginDataInterfaceAbstract* data, unsigned sort)
{
    auto* listing = static_cast<ListingData*>(data);
    if (!listing || !listing->page || listing->page->sort != sort || listing->nativeOrderApplied) return false;
    listing->nativeOrderApplied = true;
    return true;
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

ResultPage::ResultPage(SearchReply&& reply, std::shared_ptr<AvailabilityPage> checks)
    : availability(std::move(checks)), total(reply.total), sort(reply.key.sort)
{
    properties = std::move(reply.properties);
    rows.reserve(reply.hits.size());
    const int rankWidth = std::max(3, static_cast<int>(std::to_string(reply.key.offset + reply.hits.size()).size()));
    for (auto& hit : reply.hits) {
        std::string prefix;
        if (UsesRankedNames(sort)) {
            char rank[24];
            sprintf_s(rank, "%0*llu  ", rankWidth, static_cast<unsigned long long>(reply.key.offset) + rows.size() + 1);
            prefix = rank;
        }
        const size_t room = MAX_PATH - 5 - prefix.size() - (UsesRankedNames(sort) && hit.directory ? 1 : 0);
        auto name = prefix + DisplayText(hit.name, room);
        if (UsesRankedNames(sort) && hit.directory) name += "\\";
        auto parent = DisplayText(ParentOf(hit), TRANSFER_BUFFER_MAX - 1);
        ResultRow row{std::move(hit), std::move(name), std::move(parent),
                      static_cast<unsigned>(reply.key.offset + rows.size() + 1), this};
        rows.push_back(std::move(row));
    }
}

const SearchHit* FindResultHit(const ResultPage* page, const CFileData* file)
{
    if (!page || !file) return nullptr;
    // PluginData identifies an element of this immutable, contiguous page.
    // Validate its range and alignment before looking up the original path.
    const auto base = reinterpret_cast<DWORD_PTR>(page->rows.data());
    if (file->PluginData < base) return nullptr;
    const auto offset = file->PluginData - base;
    const auto index = offset / sizeof(ResultRow);
    if (offset % sizeof(ResultRow) || index >= page->rows.size()) return nullptr;
    return &page->rows[index].hit;
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
    DWORD valid = VALID_DATA_EXTENSION | VALID_DATA_ICONOVERLAY;
    if (page) {
        for (const auto& row : page->rows) {
            if (row.hit.size) valid |= VALID_DATA_SIZE;
            if (row.hit.modified) valid |= VALID_DATA_DATE | VALID_DATA_TIME;
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
            row.Size.SetUI64(item.hit.size.value_or(0));
            row.SizeValid = item.hit.size.has_value();
            const auto ticks = item.hit.modified.value_or(0);
            row.LastWrite = {static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
            row.NameLen = static_cast<unsigned>(name.size());
            char* dot = item.hit.name.find_last_of(L'.') == std::wstring::npos ? nullptr : std::strrchr(row.Name, '.');
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
