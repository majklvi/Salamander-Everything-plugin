#include "modern_search.h"
#include "result_order.h"
#include <everything_ipc.h>
#define EVERYTHING3_USERAPI
#include <Everything3.h>
#include <memory>
#include <mutex>
#include <limits>
#include <string_view>

namespace {
using Client = std::unique_ptr<EVERYTHING3_CLIENT, decltype(&Everything3_DestroyClient)>;
Client Connect(const Sources& sources)
{
    return Client(sources.modern ? Everything3_ConnectW(sources.modernInstance.empty() ?
                  nullptr : sources.modernInstance.c_str()) : nullptr, Everything3_DestroyClient);
}

std::mutex activeLock;
EVERYTHING3_CLIENT* activeClient = nullptr;
std::uint64_t activeInstance = 0;

// Only the query worker owns the client. ShutdownClient may interrupt it from the UI thread.
struct ActiveQuery {
    ActiveQuery(EVERYTHING3_CLIENT* client, std::uint64_t instance, HANDLE stop) {
        std::lock_guard lock(activeLock);
        activeClient = client;
        activeInstance = instance;
        if (stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) Everything3_ShutdownClient(client);
    }
    ~ActiveQuery() {
        std::lock_guard lock(activeLock);
        activeClient = nullptr;
    }
};

BOOL CALLBACK FindEndpoint(HWND hwnd, LPARAM data)
{
    wchar_t buffer[256]{};
    if (!GetClassNameW(hwnd, buffer, 256)) return TRUE;
    constexpr std::wstring_view prefix = EVERYTHING_IPC_WNDCLASSW;
    std::wstring_view name(buffer);
    if (name.substr(0, prefix.size()) != prefix) return TRUE;
    auto suffix = name.substr(prefix.size());
    std::wstring instance;
    if (!suffix.empty()) {
        const auto start = suffix.find(L'(');
        if (start == suffix.npos || suffix.back() != L')') return TRUE;
        instance = suffix.substr(start + 1, suffix.size() - start - 2);
    }
    DWORD_PTR major{}, minor{};
    if (!SendMessageTimeoutW(hwnd, EVERYTHING_WM_IPC, EVERYTHING_IPC_GET_MAJOR_VERSION, 0,
                             SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 300, &major) || major != 1 ||
        !SendMessageTimeoutW(hwnd, EVERYTHING_WM_IPC, EVERYTHING_IPC_GET_MINOR_VERSION, 0,
                             SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 300, &minor)) return TRUE;
    auto& sources = *reinterpret_cast<Sources*>(data);
    if (minor == 4 && (!sources.stable || instance.empty())) {
        sources.stable = hwnd;
        sources.stableInstance = instance;
    }
    if (minor == 5 && (!sources.modern || instance.empty())) {
        sources.modern = true;
        sources.modernInstance = std::move(instance);
    }
    return TRUE;
}

struct Field { unsigned flag; DWORD property; unsigned ascending; };
constexpr Field fields[] = {
    {0, EVERYTHING3_PROPERTY_ID_NAME, 1}, {ColumnParent, EVERYTHING3_PROPERTY_ID_PATH, 3},
    {ColumnSize, EVERYTHING3_PROPERTY_ID_SIZE, 5}, {ColumnExtension, EVERYTHING3_PROPERTY_ID_EXTENSION, 7},
    {ColumnCreated, EVERYTHING3_PROPERTY_ID_DATE_CREATED, 11}, {ColumnModified, EVERYTHING3_PROPERTY_ID_DATE_MODIFIED, 13},
    {ColumnAttributes, EVERYTHING3_PROPERTY_ID_ATTRIBUTES, 15}, {ColumnFileList, EVERYTHING3_PROPERTY_ID_FILE_LIST_FULL_PATH, 17},
    {ColumnRunCount, EVERYTHING3_PROPERTY_ID_RUN_COUNT, 19}, {ColumnRecentlyChanged, EVERYTHING3_PROPERTY_ID_DATE_RECENTLY_CHANGED, 21},
    {ColumnAccessed, EVERYTHING3_PROPERTY_ID_DATE_ACCESSED, 23}, {ColumnDateRun, EVERYTHING3_PROPERTY_ID_DATE_RUN, 25}
};
template<class Read> std::wstring ReadText(Read read)
{
    auto size = read(nullptr, 0);
    if (!size) return {};
    std::wstring text(size + 1, L'\0');
    text.resize(read(text.data(), text.size()));
    return text;
}
template<class T> std::optional<T> Value(T value)
{
    if (value == std::numeric_limits<T>::max()) return {};
    return value;
}
}

void InterruptModernSearch(std::uint64_t instance)
{
    std::lock_guard lock(activeLock);
    if (activeClient && (!instance || instance == activeInstance)) Everything3_ShutdownClient(activeClient);
}

Sources DiscoverSources()
{
    Sources sources;
    EnumWindows(FindEndpoint, reinterpret_cast<LPARAM>(&sources));
    return sources;
}

std::vector<Property> ReadModernCatalog(const Sources& sources)
{
    std::vector<Property> catalog;
    auto client = Connect(sources);
    if (!client) return catalog;
    for (DWORD id = 0;; ++id) {
        auto title = ReadText([&](wchar_t* p, size_t n){ return Everything3_GetPropertyNameW(client.get(), id, p, n); });
        if (title.empty()) break;
        auto builtin = std::find_if(std::begin(fields), std::end(fields), [=](auto f){ return f.property == id; });
        if (builtin != std::end(fields)) {
            catalog.push_back({{}, std::move(title), builtin->ascending});
        } else {
            auto canonical = ReadText([&](wchar_t* p, size_t n){ return Everything3_GetPropertyCanonicalNameW(client.get(), id, p, n); });
            if (!canonical.empty()) catalog.push_back(RegisterProperty(canonical, title));
        }
    }
    return catalog;
}

bool RecordModernOpen(const Sources& sources, const std::wstring& path)
{
    auto client = Connect(sources);
    return client && Everything3_IncRunCountFromFilenameW(client.get(), path.c_str()) != 0;
}

SearchReply QueryModern(const Sources& sources, const SearchKey& key, const std::wstring& query, HANDLE stop)
{
    SearchReply reply{key, SearchState::Unavailable, {}, 0};
    auto client = Connect(sources);
    if (!client) return reply;
    ActiveQuery active(client.get(), key.instance, stop);
    if (!Everything3_IsDBLoaded(client.get())) {
        if (Everything3_GetLastError() == EVERYTHING3_OK) reply.state = SearchState::LoadingIndex;
        return reply;
    }
    std::unique_ptr<EVERYTHING3_SEARCH_STATE, decltype(&Everything3_DestroySearchState)>
        state(Everything3_CreateSearchState(), Everything3_DestroySearchState);
    if (!state) return reply;
    auto* s = state.get();
    DWORD sort = EVERYTHING3_INVALID_PROPERTY_ID;
    if (!key.sortProperty.empty()) sort = Everything3_FindPropertyW(client.get(), key.sortProperty.c_str());
    for (const auto& f : fields) {
        if (key.sort == f.ascending || key.sort == f.ascending + 1) sort = f.property;
        if ((!f.flag || (key.metadataColumns & f.flag)) && !Everything3_AddSearchPropertyRequest(s, f.property)) return reply;
    }
    const bool ascending = key.sort >= firstPropertyOrder ? key.sort % 2 == 0 : key.sort % 2 == 1;
    if (sort == EVERYTHING3_INVALID_PROPERTY_ID || !Everything3_SetSearchSort(s, sort, ascending)) {
        reply.state = SearchState::UnsupportedSort;
        return reply;
    }
    std::vector<std::pair<unsigned,DWORD>> extra;
    for (const auto& p : key.properties) {
        const auto id = Everything3_FindPropertyW(client.get(), p.canonical.c_str());
        if (id == EVERYTHING3_INVALID_PROPERTY_ID) continue;
        if (!Everything3_AddSearchPropertyRequestFormatted(s, id)) return reply;
        extra.emplace_back(p.order, id);
    }
    if (!Everything3_AddSearchPropertyRequest(s, EVERYTHING3_PROPERTY_ID_FULL_PATH) ||
        !Everything3_SetSearchTextW(s, query.c_str())) return reply;
    // SDK 3.0.1.12 can return FALSE after successfully setting the viewport.
    Everything3_SetSearchViewportOffset(s, key.offset);
    const size_t viewport = key.limit ? key.limit : SIZE_MAX;
    Everything3_SetSearchViewportCount(s, viewport);
    if (Everything3_GetSearchViewportCount(s) != viewport || Everything3_GetSearchViewportOffset(s) != key.offset) return reply;
    std::unique_ptr<EVERYTHING3_RESULT_LIST, decltype(&Everything3_DestroyResultList)>
        list(Everything3_Search(client.get(), s), Everything3_DestroyResultList);
    auto* r = list.get();
    if (!r) return reply;
    if (!Everything3_GetResultListSortCount(r) || Everything3_GetResultListSortPropertyId(r, 0) != sort ||
        !!Everything3_GetResultListSortAscending(r, 0) != ascending) {
        reply.state = SearchState::UnsupportedSort;
        return reply;
    }
    reply.total = static_cast<unsigned>(std::min<size_t>(Everything3_GetResultListCount(r), UINT_MAX));
    const auto count = Everything3_GetResultListViewportCount(r);
    if (count > viewport || count > reply.total) return reply;
    reply.hits.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        SearchHit h{};
        h.source = Source::Modern15;
        h.name = ReadText([&](wchar_t* p, size_t n){ return Everything3_GetResultNameW(r,i,p,n); });
        h.path = ReadText([&](wchar_t* p, size_t n){ return Everything3_GetResultFullPathNameW(r,i,p,n); });
        if (h.name.empty() || h.path.empty()) return reply;
        h.directory = Everything3_IsFolderResult(r,i) != FALSE;
        const auto mask = key.metadataColumns;
        if (mask & ColumnSize) h.size = Value(Everything3_GetResultSize(r,i));
        if (mask & ColumnModified) h.modified = Value(Everything3_GetResultDateModified(r,i));
        if (mask & ColumnCreated) h.created = Value(Everything3_GetResultDateCreated(r,i));
        if (mask & ColumnAccessed) h.accessed = Value(Everything3_GetResultDateAccessed(r,i));
        if (mask & ColumnDateRun) h.dateRun = Value(Everything3_GetResultDateRun(r,i));
        if (mask & ColumnRecentlyChanged) h.recentlyChanged = Value(Everything3_GetResultDateRecentlyChanged(r,i));
        if (mask & ColumnRunCount) h.runCount = Value(Everything3_GetResultRunCount(r,i));
        if (mask & ColumnAttributes) h.attributes = Value(Everything3_GetResultAttributes(r,i));
        if (mask & ColumnFileList) h.fileList = ReadText([&](wchar_t* p,size_t n){ return Everything3_GetResultFilelistFilenameW(r,i,p,n); });
        if (mask & ColumnExtension) h.extension = ReadText([&](wchar_t* p,size_t n){ return Everything3_GetResultExtensionW(r,i,p,n); });
        for (const auto& [order,id] : extra)
            h.propertyValues[order] = ReadText([&](wchar_t* p,size_t n){ return Everything3_GetResultPropertyTextFormattedW(r,i,id,p,n); });
        reply.hits.push_back(std::move(h));
    }
    reply.properties = key.properties;
    reply.state = SearchState::Ready;
    return reply;
}
