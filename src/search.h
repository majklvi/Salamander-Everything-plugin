#pragma once

#include "metadata_columns.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <tuple>

inline constexpr std::uint32_t mostOpenedSort = 20; // EVERYTHING_SORT_RUN_COUNT_DESCENDING

inline constexpr std::uint32_t leastOpenedSort = 19; // EVERYTHING_SORT_RUN_COUNT_ASCENDING

inline constexpr bool UsesRankedNames(std::uint32_t sort) {
    return sort != 1; // Preserve the SDK sequence when the host would sort by Name.
}

struct SearchKey {
    std::uint64_t instance;
    std::uint64_t generation;
    std::uint32_t sort = 1; // Everything name ascending
    std::uint32_t offset = 0;
    std::uint32_t limit = 200;
    unsigned metadataColumns = defaultMetadataColumns;

    bool operator==(const SearchKey& rhs) const {
        return instance == rhs.instance && generation == rhs.generation &&
               sort == rhs.sort && offset == rhs.offset && limit == rhs.limit &&
               metadataColumns == rhs.metadataColumns;
    }
};

struct SearchHit {
    std::wstring name;
    std::wstring path; // Exact full pathname from the index.
    bool directory;
    std::optional<std::uint64_t> size;
    // UTC, in 100 ns intervals since the Windows FILETIME epoch.
    std::optional<std::uint64_t> modified, created, accessed;
    std::optional<std::uint32_t> attributes;
    std::optional<std::wstring> extension, fileList;
    std::optional<std::uint32_t> runCount;
    std::optional<std::uint64_t> dateRun, recentlyChanged;

    bool operator==(const SearchHit& other) const {
        return std::tie(name, path, directory, size, modified, created, accessed, attributes,
                        extension, fileList, runCount, dateRun, recentlyChanged) ==
               std::tie(other.name, other.path, other.directory, other.size, other.modified,
                        other.created, other.accessed, other.attributes, other.extension,
                        other.fileList, other.runCount, other.dateRun, other.recentlyChanged);
    }
};

enum class SearchState { Ready, LoadingIndex, Unavailable, UnsupportedSort };

struct SearchReply {
    SearchKey key;
    SearchState state;
    std::vector<SearchHit> hits;
    std::uint32_t total;
};

// Called on Samandarin's UI thread. No filesystem pointers cross threads.
void SubmitSearch(SearchKey key, const std::wstring& query);
void RecordOpenedFile(const std::wstring& path);
bool RefreshOpenSearches();
void ScheduleFileOperationRefresh();
void CancelSearch(std::uint64_t instance);
void ShutdownSearch();
void DeliverSearchReply(SearchReply&& reply);
