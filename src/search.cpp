#define EVERYTHINGUSERAPI
#include "search.h"
#include "modern_search.h"
extern "C" void Everything_SetTargetWindow(HWND);

#include <windows.h>
#include <process.h>
#include <Everything.h>
#include <everything_ipc.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

extern HINSTANCE gInstance;
static_assert(leastOpenedSort == EVERYTHING_SORT_RUN_COUNT_ASCENDING);
static_assert(mostOpenedSort == EVERYTHING_SORT_RUN_COUNT_DESCENDING);

namespace {
constexpr wchar_t uiClass[] = L"EverythingSearch.Results.Dispatch";
constexpr wchar_t ipcClass[] = L"EverythingSearch.SDK2.Replies";
constexpr UINT resultsMessage = WM_APP + 17;
constexpr UINT_PTR fileOperationTimer = 1;
constexpr UINT_PTR indexPollTimer = 2;
constexpr UINT_PTR availabilityTimer = 3;
constexpr UINT indexPollMs = 3000;
bool indexPollArmed = false;
constexpr UINT runsChangedMessage = WM_APP + 18;
constexpr DWORD debounce = 100;
constexpr DWORD replyTimeout = 15000;
constexpr DWORD identityFields = EVERYTHING_REQUEST_FILE_NAME | EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME;

struct WireField {
    DWORD flag;
    unsigned column;
    size_t bytes; // Zero denotes a length-prefixed, NUL-terminated UTF-16 string.
};

// Match the field walk in the pinned SDK's _Everything_GetRequestData.
constexpr WireField wireFields[] = {
    {EVERYTHING_REQUEST_FILE_NAME, 0, 0},
    {EVERYTHING_REQUEST_PATH, 0, 0},
    {EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME, 0, 0},
    {EVERYTHING_REQUEST_EXTENSION, ColumnExtension, 0},
    {EVERYTHING_REQUEST_SIZE, ColumnSize, sizeof(LARGE_INTEGER)},
    {EVERYTHING_REQUEST_DATE_CREATED, ColumnCreated, sizeof(FILETIME)},
    {EVERYTHING_REQUEST_DATE_MODIFIED, ColumnModified, sizeof(FILETIME)},
    {EVERYTHING_REQUEST_DATE_ACCESSED, ColumnAccessed, sizeof(FILETIME)},
    {EVERYTHING_REQUEST_ATTRIBUTES, ColumnAttributes, sizeof(DWORD)},
    {EVERYTHING_REQUEST_FILE_LIST_FILE_NAME, ColumnFileList, 0},
    {EVERYTHING_REQUEST_RUN_COUNT, ColumnRunCount, sizeof(DWORD)},
    {EVERYTHING_REQUEST_DATE_RUN, ColumnDateRun, sizeof(FILETIME)},
    {EVERYTHING_REQUEST_DATE_RECENTLY_CHANGED, ColumnRecentlyChanged, sizeof(FILETIME)},
    {EVERYTHING_REQUEST_HIGHLIGHTED_FILE_NAME, 0, 0},
    {EVERYTHING_REQUEST_HIGHLIGHTED_PATH, 0, 0},
    {EVERYTHING_REQUEST_HIGHLIGHTED_FULL_PATH_AND_FILE_NAME, 0, 0},
};

constexpr DWORD RequestedFields(unsigned columns)
{
    DWORD flags = identityFields;
    for (const auto& field : wireFields)
        if (field.column & columns) flags |= field.flag;
    return flags;
}

struct Request {
    SearchKey key;
    std::wstring query;
    ULONGLONG due;
};

// The UI owns the lifetime of the handles and windows. The worker exclusively
// owns the SDK globals. Pending edits and completed snapshots are the handoff.
HWND ui = nullptr;
HANDLE stop = nullptr;
HANDLE wake = nullptr;
HANDLE worker = nullptr;
std::mutex queues;
std::deque<Request> pending;
std::deque<SearchReply> completed;
std::deque<std::pair<std::wstring, Source>> openedFiles;

void Publish(SearchReply reply)
{
    {
        std::lock_guard lock(queues);
        completed.push_back(std::move(reply));
    }
    PostMessageW(ui, resultsMessage, 0, 0);
}

LRESULT CALLBACK UiProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_TIMER && wp == availabilityTimer) {
        RefreshAvailabilityIcons();
        return 0;
    }
    if (msg == WM_TIMER && wp == indexPollTimer) {
        if (!RefreshOpenSearches()) {
            KillTimer(hwnd, indexPollTimer);
            KillTimer(hwnd, availabilityTimer);
            indexPollArmed = false;
        }
        return 0;
    }
    if (msg == WM_TIMER && wp == fileOperationTimer) {
        KillTimer(hwnd, fileOperationTimer);
        RefreshOpenSearches();
        return 0;
    }
    if (msg == runsChangedMessage) {
        RefreshOpenSearches();
        return 0;
    }
    if (msg == resultsMessage) {
        std::deque<SearchReply> batch;
        {
            std::lock_guard lock(queues);
            batch.swap(completed);
        }
        for (auto& reply : batch)
            DeliverSearchReply(std::move(reply));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The SDK trusts offsets and lengths. Validate every returned field against its
// own row before passing the buffer to the SDK, even if the field was unsolicited.
bool ValidPage(const COPYDATASTRUCT& data, const SearchKey& key)
{
    if (!data.lpData || data.cbData < sizeof(EVERYTHING_IPC_LIST2))
        return false;
    const auto* bytes = static_cast<const BYTE*>(data.lpData);
    const auto* list = static_cast<const EVERYTHING_IPC_LIST2*>(data.lpData);
    const size_t size = data.cbData;
    const DWORD flags = list->request_flags;
    DWORD knownFlags = 0;
    for (const auto& field : wireFields) knownFlags |= field.flag;
    if ((key.limit && list->numitems > key.limit) || (flags & identityFields) != identityFields || (flags & ~knownFlags) ||
        list->offset != key.offset ||
        (list->numitems && (list->offset > list->totitems || list->numitems > list->totitems - list->offset)) ||
        list->numitems > (size - sizeof(*list)) / sizeof(EVERYTHING_IPC_ITEM2))
        return false;
    const auto* items = reinterpret_cast<const EVERYTHING_IPC_ITEM2*>(list + 1);
    const size_t tableEnd = sizeof(*list) + sizeof(*items) * list->numitems;
    std::vector<size_t> offsets;
    offsets.reserve(static_cast<size_t>(list->numitems) + 1);
    for (DWORD i = 0; i < list->numitems; ++i) {
        const size_t offset = items[i].data_offset;
        if (offset < tableEnd || offset >= size || offset % alignof(wchar_t))
            return false;
        offsets.push_back(offset);
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.push_back(size);
    for (DWORD i = 0; i < list->numitems; ++i) {
        size_t pos = offsets[i];
        const size_t end = offsets[i + 1];
        for (const auto& field : wireFields) {
            if (!(flags & field.flag)) continue;
            if (field.bytes) {
                if (end - pos < field.bytes) return false;
                pos += field.bytes;
                continue;
            }
            if (end - pos < sizeof(DWORD)) return false;
            DWORD chars;
            std::memcpy(&chars, bytes + pos, sizeof(chars));
            pos += sizeof(chars);
            const size_t units = (end - pos) / sizeof(wchar_t);
            if (chars >= units) return false;
            pos += (static_cast<size_t>(chars) + 1) * sizeof(wchar_t);
            if (bytes[pos - 1] || bytes[pos - 2]) return false;
        }
    }
    return true;
}

using GetDate = BOOL (EVERYTHINGAPI *)(DWORD, FILETIME*);

std::optional<std::uint64_t> ReadDate(DWORD index, GetDate getter)
{
    FILETIME date{};
    if (!getter(index, &date)) return {};
    const auto ticks = (std::uint64_t{date.dwHighDateTime} << 32) | date.dwLowDateTime;
    if (ticks == UINT64_MAX) return {};
    return ticks;
}

void ReadMetadata(DWORD index, DWORD flags, SearchHit& hit)
{
    if (flags & EVERYTHING_REQUEST_SIZE) {
        LARGE_INTEGER size{};
        if (Everything_GetResultSize(index, &size) && size.QuadPart != -1)
            hit.size = static_cast<std::uint64_t>(size.QuadPart);
    }
    if (flags & EVERYTHING_REQUEST_DATE_MODIFIED) hit.modified = ReadDate(index, Everything_GetResultDateModified);
    if (flags & EVERYTHING_REQUEST_DATE_CREATED) hit.created = ReadDate(index, Everything_GetResultDateCreated);
    if (flags & EVERYTHING_REQUEST_DATE_ACCESSED) hit.accessed = ReadDate(index, Everything_GetResultDateAccessed);
    if (flags & EVERYTHING_REQUEST_ATTRIBUTES) {
        const DWORD attributes = Everything_GetResultAttributes(index);
        if (attributes != INVALID_FILE_ATTRIBUTES) hit.attributes = attributes;
    }
    if (flags & EVERYTHING_REQUEST_EXTENSION) {
        if (const auto* value = Everything_GetResultExtensionW(index); value && *value) hit.extension = value;
    }
    if (flags & EVERYTHING_REQUEST_FILE_LIST_FILE_NAME) {
        if (const auto* value = Everything_GetResultFileListFileNameW(index); value && *value) hit.fileList = value;
    }
    if (flags & EVERYTHING_REQUEST_RUN_COUNT) hit.runCount = Everything_GetResultRunCount(index);
    if (flags & EVERYTHING_REQUEST_DATE_RUN) hit.dateRun = ReadDate(index, Everything_GetResultDateRun);
    if (flags & EVERYTHING_REQUEST_DATE_RECENTLY_CHANGED)
        hit.recentlyChanged = ReadDate(index, Everything_GetResultDateRecentlyChanged);
}

struct Receiver {
    std::optional<SearchKey> active;
    DWORD serial = 0;
    ULONGLONG expires = 0;

    void Finish(SearchState state)
    {
        const SearchKey key = *active;
        active.reset();
        Publish({key, state, {}, 0});
    }

    LRESULT Reply(UINT msg, WPARAM wp, LPARAM lp)
    {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lp);
        if (!active || !data || data->dwData != serial) return FALSE;
        if (!ValidPage(*data, *active) || !Everything_IsQueryReply(msg, wp, lp, serial) ||
            Everything_GetLastError() != EVERYTHING_OK ||
            (Everything_GetResultListRequestFlags() & identityFields) != identityFields) {
            Finish(SearchState::Unavailable);
            return FALSE;
        }
        // SDK v2 may deliver a different order than the one it was asked for.
        // Such a bounded page cannot represent the requested whole-query sort.
        if (Everything_GetResultListSort() != active->sort) {
            Finish(SearchState::UnsupportedSort);
            return TRUE;
        }
        SearchReply reply{*active, SearchState::Ready, {}, Everything_GetTotResults()};
        const DWORD count = Everything_GetNumResults();
        const DWORD flags = Everything_GetResultListRequestFlags() & RequestedFields(active->RequestedColumns());
        reply.hits.reserve(count);
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t* name = Everything_GetResultFileNameW(i);
            const DWORD length = Everything_GetResultFullPathNameW(i, nullptr, 0);
            std::wstring path(static_cast<size_t>(length) + 1, L'\0');
            if (!name || !length || Everything_GetResultFullPathNameW(i, path.data(), length + 1) != length) {
                Finish(SearchState::Unavailable);
                return FALSE;
            }
            path.resize(length);
            reply.hits.push_back({name, std::move(path),
                Everything_IsFolderResult(i) != FALSE || Everything_IsVolumeResult(i) != FALSE});
            ReadMetadata(i, flags, reply.hits.back());
        }
        active.reset();
        Publish(std::move(reply));
        return TRUE;
    }

    void Start(HWND hwnd, const Request& request)
    {
        active = request.key;
        Everything_Reset();
        if (!hwnd || !Everything_IsDBLoaded()) {
            Finish(hwnd && Everything_GetLastError() == EVERYTHING_OK ?
                SearchState::LoadingIndex : SearchState::Unavailable);
            return;
        }
        if (++serial == 0) ++serial;
        Everything_SetSearchW(request.query.c_str());
        if (Everything_GetLastError() != EVERYTHING_OK) {
            Finish(SearchState::Unavailable);
            return;
        }
        Everything_SetReplyWindow(hwnd);
        Everything_SetReplyID(serial);
        Everything_SetSort(request.key.sort);
        Everything_SetOffset(request.key.offset);
        Everything_SetMax(request.key.limit ? request.key.limit : MAXDWORD);
        Everything_SetRequestFlags(RequestedFields(request.key.RequestedColumns()));
        expires = GetTickCount64() + replyTimeout;
        // Windows may dispatch a synchronous reply inside the submission.
        if (!Everything_QueryW(FALSE) && active)
            Finish(SearchState::Unavailable);
    }
};

LRESULT CALLBACK IpcProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (msg == WM_COPYDATA) {
        auto* receiver = reinterpret_cast<Receiver*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (receiver) {
            try {
                return receiver->Reply(msg, wp, lp);
            } catch (const std::bad_alloc&) {
                if (receiver->active) receiver->Finish(SearchState::Unavailable);
                return FALSE;
            }
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

unsigned __stdcall Run(void*)
{
    Receiver receiver;
    HWND hwnd = CreateWindowExW(0, ipcClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, gInstance, &receiver);
    const HANDLE events[] = {stop, wake};
    for (;;) {
        const bool stopping = WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
        std::deque<std::pair<std::wstring, Source>> launches;
        {
            std::lock_guard lock(queues);
            launches.swap(openedFiles);
        }
        bool changed = false;
        for (const auto& [path, source] : launches) {
            const auto sources = DiscoverSources();
            if (source == Source::Modern15) changed |= RecordModernOpen(sources, path);
            else if (sources.stable) {
                Everything_SetTargetWindow(sources.stable);
                changed |= Everything_IncRunCountFromFileNameW(path.c_str()) != 0;
            }
        }
        if (stopping) break;
        if (changed) PostMessageW(ui, runsChangedMessage, 0, 0);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
        const ULONGLONG now = GetTickCount64();
        DWORD delay = INFINITE;
        if (receiver.active) {
            if (now >= receiver.expires) {
                receiver.Finish(SearchState::Unavailable);
                continue;
            }
            delay = static_cast<DWORD>(receiver.expires - now);
        } else {
            std::optional<Request> request;
            {
                std::lock_guard lock(queues);
                if (!pending.empty()) {
                    if (pending.front().due <= now) {
                        request = std::move(pending.front());
                        pending.pop_front();
                    } else {
                        delay = static_cast<DWORD>(pending.front().due - now);
                    }
                }
            }
            if (request) {
                const auto sources = DiscoverSources();
                if (sources.UseModern(request->key.source)) {
                    try {
                        Publish(QueryModern(sources, request->key, request->query, stop));
                    } catch (const std::bad_alloc&) {
                        Publish({request->key, SearchState::Unavailable, {}, 0});
                    }
                } else if (sources.stable && !request->key.sortProperty.empty())
                    Publish({request->key, SearchState::UnsupportedSort, {}, 0});
                else {
                    Everything_SetTargetWindow(sources.stable);
                    receiver.Start(hwnd, *request);
                }
                continue;
            }
        }
        MsgWaitForMultipleObjectsEx(2, events, delay, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    if (hwnd) DestroyWindow(hwnd);
    Everything_CleanUp();
    return 0;
}

bool Register(const wchar_t* name, WNDPROC proc)
{
    WNDCLASSW wc{};
    wc.hInstance = gInstance;
    wc.lpszClassName = name;
    wc.lpfnWndProc = proc;
    return RegisterClassW(&wc) != 0;
}

bool EnsureStarted()
{
    if (worker) return true;
    if (Register(uiClass, UiProc) && Register(ipcClass, IpcProc)) {
        ui = CreateWindowExW(0, uiClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, gInstance, nullptr);
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (ui && stop && wake)
            worker = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, Run, nullptr, 0, nullptr));
    }
    if (worker) return true;
    ShutdownSearch();
    return false;
}

void RemovePending(std::uint64_t instance)
{
    pending.erase(std::remove_if(pending.begin(), pending.end(), [instance](const Request& r) {
        return r.key.instance == instance;
    }), pending.end());
}
} // namespace

void ScheduleFileOperationRefresh()
{
    if (ui && !SetTimer(ui, fileOperationTimer, 800, nullptr)) PostMessageW(ui, runsChangedMessage, 0, 0);
}

void SubmitSearch(SearchKey key, const std::wstring& query)
{
    if (!EnsureStarted()) {
        DeliverSearchReply({key, SearchState::Unavailable, {}, 0});
        return;
    }
    InterruptModernSearch(key.instance);
    // SDK v2 returns snapshots. Poll while queries are open so delayed index
    // updates and later saves in external editors reach the same result panel.
    if (!indexPollArmed) {
        indexPollArmed = SetTimer(ui, indexPollTimer, indexPollMs, nullptr) != 0;
        SetTimer(ui, availabilityTimer, 250, nullptr);
    }
    {
        std::lock_guard lock(queues);
        RemovePending(key.instance);
        pending.push_back({key, query, GetTickCount64() + debounce});
    }
    SetEvent(wake);
}

void CancelSearch(std::uint64_t instance)
{
    InterruptModernSearch(instance);
    std::lock_guard lock(queues);
    RemovePending(instance);
    // An in-flight or already posted reply still has its original immutable key.
}

void RecordOpenedFile(const std::wstring& path, Source source)
{
    if (!EnsureStarted()) return;
    {
        std::lock_guard lock(queues);
        openedFiles.emplace_back(path, source);
    }
    SetEvent(wake);
}

void ShutdownSearch()
{
    if (worker) {
        SetEvent(stop);
        InterruptModernSearch(0);
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
        worker = nullptr;
    }
    if (stop) { CloseHandle(stop); stop = nullptr; }
    if (wake) { CloseHandle(wake); wake = nullptr; }
    if (ui) { DestroyWindow(ui); ui = nullptr; }
    indexPollArmed = false;
    UnregisterClassW(ipcClass, gInstance);
    UnregisterClassW(uiClass, gInstance);
    pending.clear();
    completed.clear();
}
