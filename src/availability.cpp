#include "availability.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace {
bool checksEnabled = true; // UI thread preference. Workers only own queued jobs.
constexpr ULONGLONG retryMs = 30000;
struct Job {
    std::weak_ptr<AvailabilityPage> page;
    size_t index;
    std::wstring path;
};

bool ShareUnavailable(DWORD error) {
    return error == ERROR_BAD_NET_NAME || error == ERROR_NETWORK_UNREACHABLE ||
           error == ERROR_HOST_UNREACHABLE;
}

PathState Classify(DWORD error) {
    if (!error) return PathState::Present;
    if (ShareUnavailable(error) || error == ERROR_BAD_NETPATH || error == ERROR_NOT_CONNECTED ||
        error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_INVALID_DRIVE ||
        error == ERROR_DEV_NOT_EXIST || error == ERROR_NOT_READY || error == ERROR_DEVICE_NOT_CONNECTED)
        return PathState::Missing;
    return PathState::Unknown;
}

DWORD ProbePath(std::wstring path, bool shareRoot) {
    // GetFileAttributesEx cannot query a share root, even when it is online.
    if (shareRoot) {
        if (path.back() != L'\\') path += L'\\';
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW((path + L"*").c_str(), &data);
        if (find == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : error;
        }
        FindClose(find);
        return ERROR_SUCCESS;
    }
    if (path.size() >= MAX_PATH && path.compare(0, 4, LR"(\\?\)") != 0) {
        if (path.compare(0, 2, LR"(\\)") == 0) path = LR"(\\?\UNC\)" + path.substr(2);
        else if (path[1] == L':') path = LR"(\\?\)" + path;
    }
    WIN32_FILE_ATTRIBUTE_DATA info{};
    return GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info) ? ERROR_SUCCESS : GetLastError();
}

struct Lane {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> queue;
    std::thread worker;
    bool stopping = false;

    void Run() {
        SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
        std::unordered_map<std::wstring, ULONGLONG> unavailableShares;
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping) return;
                job = std::move(queue.front());
                queue.pop_front();
            }
            if (job.page.expired()) continue;
            std::wstring share;
            if (job.path.compare(0, 2, L"\\\\") == 0 && job.path.compare(0, 4, L"\\\\?\\") != 0) {
                auto serverEnd = job.path.find(L'\\', 2);
                if (serverEnd != job.path.npos)
                    share = job.path.substr(0, job.path.find(L'\\', serverEnd + 1));
            }
            const auto now = GetTickCount64();
            auto cached = unavailableShares.find(share);
            PathState state = PathState::Missing;
            if (cached == unavailableShares.end() || cached->second <= now) {
                const bool root = !share.empty() && (job.path.size() == share.size() ||
                                     (job.path.size() == share.size() + 1 && job.path.back() == L'\\'));
                DWORD error = ProbePath(job.path, root);
                state = Classify(error);
                if (!share.empty() && ShareUnavailable(error)) {
                    if (unavailableShares.size() >= 128) unavailableShares.clear();
                    unavailableShares[share] = now + retryMs;
                }
            }
            if (auto page = job.page.lock()) {
                page->entries[job.index].nextCheck = GetTickCount64() + retryMs;
                if (page->entries[job.index].state.exchange(state) != state)
                    page->changed = true;
            }
        }
    }

    void CancelPending() {
        std::lock_guard lock(mutex);
        queue.clear();
        if (worker.joinable()) CancelSynchronousIo(worker.native_handle());
    }

    void Stop() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            queue.clear();
        }
        ready.notify_one();
        if (worker.joinable()) {
            while (WaitForSingleObject(worker.native_handle(), 50) == WAIT_TIMEOUT)
                CancelSynchronousIo(worker.native_handle());
            worker.join();
        }
        stopping = false;
    }
};
Lane localPaths, networkPaths;
}

void CheckResultPath(const std::shared_ptr<AvailabilityPage>& page, size_t index, const std::wstring& path) {
    if (!checksEnabled || !page) return;
    auto& entry = page->entries[index];
    auto now = GetTickCount64();
    const auto next = entry.nextCheck.load();
    if (next > now) return;
    auto& lane = path.compare(0, 2, L"\\\\") == 0 ? networkPaths : localPaths;
    try {
        if (!next) page->observed.push_back(index);
        entry.nextCheck = MAXULONGLONG; // Includes queued and in-flight probes.
        std::lock_guard lock(lane.mutex);
        if (!lane.worker.joinable()) lane.worker = std::thread([&lane] { lane.Run(); });
        lane.queue.push_back({page, index, path});
    } catch (const std::exception&) {
        entry.nextCheck = now + retryMs;
        return; // Keep the generic icon if resources are exhausted.
    }
    lane.ready.notify_one();
}

bool AvailabilityChecksEnabled() {
    return checksEnabled;
}

bool SetAvailabilityChecksEnabled(bool enabled) {
    if (checksEnabled == enabled) return false;
    checksEnabled = enabled;
    if (!enabled) {
        localPaths.CancelPending();
        networkPaths.CancelPending();
    }
    return true;
}

void StopAvailabilityChecks() {
    networkPaths.Stop();
    localPaths.Stop();
}
