#pragma once

#include <array>
#include <iterator>
#include <string_view>
#include <memory>
#include "plugin.h"
#include "search.h"

std::string DisplayText(std::wstring_view text, size_t limit);
void BeginHeaderSortObservation();
unsigned EndHeaderSortObservation();

#include "availability.h"

struct ResultPage;
struct ResultRow {
    SearchHit hit;
    std::string displayName;
    std::string displayParent;
    unsigned rank;
    const ResultPage* owner;

};

struct ResultPage {
    explicit ResultPage(SearchReply&& reply, std::shared_ptr<AvailabilityPage> checks = {});
    ResultPage(const ResultPage&) = delete;
    ResultPage& operator=(const ResultPage&) = delete;
    std::shared_ptr<AvailabilityPage> availability;
    std::vector<ResultRow> rows;
    std::uint32_t total;
    std::uint32_t sort;
    std::vector<Property> properties;
};

// The host can use the previous listing until its refresh finishes. Each
// listing keeps its backing page alive independently of the live FS.
bool ListResultPage(const std::shared_ptr<const ResultPage>& page,
                    CSalamanderDirectoryAbstract* directory, CPluginDataInterfaceAbstract*& data,
                    bool panelFilter = false, const std::wstring& filterName = {});
bool TakeNativeOrderUpdate(CPluginDataInterfaceAbstract* data, unsigned sort);
bool HasFilteredRanks(CPluginDataInterfaceAbstract* data);
void SetRankingFilteredRows(bool enabled);
const ResultPage* GetListingPage(CPluginDataInterfaceAbstract* data);
const SearchHit* FindResultHit(const ResultPage* page, const CFileData* file);
