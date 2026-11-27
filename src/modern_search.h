#pragma once
#include "search.h"
#include <windows.h>

struct Sources {
    HWND stable = nullptr;
    HWND modern = nullptr;
    std::wstring modernInstance;
    std::wstring stableInstance;
    bool UseModern(Source preference) const {
        return preference == Source::Modern15 || (preference == Source::Automatic && !stable && modern);
    }
};
Sources DiscoverSources();
void InterruptModernSearch(std::uint64_t instance);
std::vector<Property> ReadModernCatalog(const Sources& sources);
SearchReply QueryModern(const Sources& sources, const SearchKey& key, const std::wstring& query, HANDLE stop = nullptr);
bool RecordModernOpen(const Sources& sources, const std::wstring& path);
