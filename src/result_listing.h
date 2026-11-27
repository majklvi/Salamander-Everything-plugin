#pragma once

#include <array>
#include <iterator>
#include <memory>
#include "plugin.h"
#include "search.h"

void BeginHeaderSortObservation();
unsigned EndHeaderSortObservation();

struct ResultRow {
    SearchHit hit;
    std::string displayName;
    std::string displayParent;
};

struct ResultPage {
    explicit ResultPage(SearchReply&& reply);
    std::vector<ResultRow> rows;
    std::uint32_t total;
    std::uint32_t sort;
    std::vector<Property> properties;
};

// The host can use the previous listing until its refresh finishes. Each
// listing keeps its backing page alive independently of the live FS.
bool ListResultPage(const std::shared_ptr<const ResultPage>& page,
                    CSalamanderDirectoryAbstract* directory, CPluginDataInterfaceAbstract*& data);
const ResultPage* GetListingPage(CPluginDataInterfaceAbstract* data);
const SearchHit* FindResultHit(const ResultPage* page, const CFileData* file);
