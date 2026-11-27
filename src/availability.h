#pragma once

#include <windows.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

enum class PathState : unsigned char { Unknown, Present, Missing };

struct AvailabilityEntry {
    std::atomic<PathState> state{PathState::Unknown};
    std::atomic<ULONGLONG> nextCheck{0};
};

struct AvailabilityPage {
    explicit AvailabilityPage(size_t count) : entries(std::make_unique<AvailabilityEntry[]>(count)) {}
    std::unique_ptr<AvailabilityEntry[]> entries;
    std::vector<size_t> observed; // Only touched by the UI thread.
    std::atomic<bool> changed{false};
};

// Called when a placeholder is painted, or a previously painted row needs rechecking.
void CheckResultPath(const std::shared_ptr<AvailabilityPage>& page, size_t index, const std::wstring& path);
void StopAvailabilityChecks();
