#pragma once

#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <unordered_map>

enum class PathState : unsigned char { Pending, Unknown, Present, Missing };

struct AvailabilityPage {
    std::mutex mutex;
    std::unordered_map<std::wstring, PathState> paths;
    PathState State(const std::wstring& path);
    std::atomic<bool> changed{false};
};

// The first paint queues the path once for this query.
void CheckResultPath(const std::shared_ptr<AvailabilityPage>& page, const std::wstring& path);
inline constexpr unsigned defaultNetworkRetrySeconds = 5;
inline constexpr unsigned maxNetworkRetrySeconds = 3600;
unsigned NetworkRetrySeconds();
void SetNetworkRetrySeconds(unsigned seconds);
bool AvailabilityChecksEnabled();
bool SetAvailabilityChecksEnabled(bool enabled);
void StopAvailabilityChecks();
