#include "availability.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <string_view>

namespace {
bool checksEnabled = true; // UI thread preference. Workers only own queued jobs.
using Clock = std::chrono::steady_clock;
unsigned networkRetrySeconds = defaultNetworkRetrySeconds; // Written under both lane locks; read by workers under their lane lock.
struct Job {
    std::weak_ptr<AvailabilityPage> page;
    std::wstring path;
    Clock::time_point firstFailureAt{};
    unsigned generation = 0;
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

bool IsNetworkPath(const std::wstring& path) {
    std::wstring_view view(path);
    if (view.substr(0, 4) == LR"(\\?\)") {
        view.remove_prefix(4);
        if (_wcsnicmp(view.data(), L"UNC\\", 4) == 0) return true;
    } else if (view.substr(0, 2) == LR"(\\)") return true;
    if (view.size() < 2 || view[1] != L':') return false;
    wchar_t root[] = L"A:\\";
    root[0] = view[0];
    return GetDriveTypeW(root) == DRIVE_REMOTE;
}

bool NeedsNetworkRetry(DWORD error) {
    return Classify(error) == PathState::Missing || error == ERROR_SEM_TIMEOUT ||
           error == ERROR_TIMEOUT || error == ERROR_NETNAME_DELETED ||
           error == ERROR_CONNECTION_UNAVAIL;
}

struct Lane {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> queue;
    std::deque<Job> retries; // A single worker appends deadlines in chronological order.
    unsigned generation = 0;
    std::thread worker;
    bool stopping = false;

    Clock::time_point NextRetry() const {
        return retries.front().firstFailureAt + std::chrono::seconds(networkRetrySeconds);
    }

    void Run() {
        SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                while (!stopping && queue.empty() &&
                       (retries.empty() || Clock::now() < NextRetry())) {
                    if (retries.empty()) ready.wait(lock);
                    else {
                        const auto deadline = NextRetry();
                        ready.wait_until(lock, deadline);
                    }
                }
                if (stopping) return;
                auto& jobs = !retries.empty() && Clock::now() >= NextRetry() ? retries : queue;
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            if (job.page.expired()) continue;
            const auto& path = job.path;
            const bool extendedUNC = _wcsnicmp(path.c_str(), L"\\\\?\\UNC\\", 8) == 0;
            const bool unc = extendedUNC || (path.compare(0, 2, LR"(\\)") == 0 &&
                                               path.compare(0, 4, LR"(\\?\)") != 0);
            const auto serverEnd = unc ? path.find(L'\\', extendedUNC ? 8 : 2) : path.npos;
            const auto shareEnd = serverEnd == path.npos ? path.npos : path.find(L'\\', serverEnd + 1);
            const bool root = serverEnd != path.npos && (shareEnd == path.npos || shareEnd + 1 == path.size());
            const DWORD error = ProbePath(path, root);
            PathState state = Classify(error);
            if (job.firstFailureAt == Clock::time_point{} && NeedsNetworkRetry(error) && IsNetworkPath(path)) {
                std::lock_guard lock(mutex);
                if (stopping || generation != job.generation) continue;
                job.firstFailureAt = Clock::now();
                try {
                    retries.push_back(std::move(job));
                    continue;
                } catch (const std::bad_alloc&) {
                    state = PathState::Unknown;
                }
            }
            if (auto page = job.page.lock()) {
                std::lock_guard lock(page->mutex);
                page->paths.at(job.path) = state;
                page->changed = true; // Also remove the pending badge after an inconclusive check.
            }
        }
    }

    void CancelPending() {
        std::lock_guard lock(mutex);
        ++generation;
        retries.clear();
        queue.clear();
        ready.notify_one();
        if (worker.joinable()) CancelSynchronousIo(worker.native_handle());
    }

    void Stop() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            retries.clear();
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

PathState AvailabilityPage::State(const std::wstring& path) {
    std::lock_guard lock(mutex);
    const auto entry = paths.find(path);
    return entry == paths.end() ? PathState::Pending : entry->second;
}

void CheckResultPath(const std::shared_ptr<AvailabilityPage>& page, const std::wstring& path) {
    if (!checksEnabled || !page) return;
    auto& lane = path.compare(0, 2, L"\\\\") == 0 ? networkPaths : localPaths;
    try {
        std::lock_guard pageLock(page->mutex);
        if (!page->paths.try_emplace(path, PathState::Pending).second) return;
        std::lock_guard lock(lane.mutex);
        if (!lane.worker.joinable()) lane.worker = std::thread([&lane] { lane.Run(); });
        lane.queue.push_back({page, path, {}, lane.generation});
    } catch (const std::exception&) {
        std::lock_guard lock(page->mutex);
        const auto entry = page->paths.find(path);
        if (entry != page->paths.end()) {
            entry->second = PathState::Unknown;
            page->changed = true;
        }
        return;
    }
    lane.ready.notify_one();
}

unsigned NetworkRetrySeconds() {
    return networkRetrySeconds; // UI thread.
}

void SetNetworkRetrySeconds(unsigned seconds) {
    if (seconds > maxNetworkRetrySeconds || seconds == networkRetrySeconds) return;
    {
        std::scoped_lock lock(localPaths.mutex, networkPaths.mutex);
        networkRetrySeconds = seconds;
    }
    localPaths.ready.notify_one();
    networkPaths.ready.notify_one();
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
