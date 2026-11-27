#include "../../src/search.cpp"
#include "../../src/result_order.h"

#include <iostream>
#include <stdexcept>

HINSTANCE gInstance;
void DeliverSearchReply(SearchReply&&) {}
bool RefreshOpenSearches() { return false; }
extern "C" void PrepareUnicodeQuery2(DWORD);

namespace {
using Bytes = std::vector<BYTE>;
constexpr DWORD replyID = 7;
constexpr DWORD allFields = 0x1FFD;

void Check(bool result, const char* name)
{
    if (!result) throw std::runtime_error(name);
    std::cout << "PASS " << name << '\n';
}

template<class T> void Append(Bytes& buffer, T value)
{
    const auto* start = reinterpret_cast<const BYTE*>(&value);
    buffer.insert(buffer.end(), start, start + sizeof(value));
}

void String(Bytes& buffer, const std::wstring& value)
{
    Append(buffer, static_cast<DWORD>(value.size()));
    const auto* start = reinterpret_cast<const BYTE*>(value.c_str());
    buffer.insert(buffer.end(), start, start + (value.size() + 1) * sizeof(wchar_t));
}

Bytes Identity(const std::wstring& path)
{
    Bytes row;
    String(row, L"example.txt");
    String(row, path);
    return row;
}

Bytes Page(DWORD flags, const std::vector<Bytes>& rows)
{
    EVERYTHING_IPC_LIST2 list{};
    list.numitems = list.totitems = static_cast<DWORD>(rows.size());
    list.sort_type = EVERYTHING_SORT_NAME_ASCENDING;
    list.request_flags = flags;
    Bytes page;
    Append(page, list);
    size_t offset = sizeof(list) + sizeof(EVERYTHING_IPC_ITEM2) * rows.size();
    for (const auto& row : rows) {
        Append(page, EVERYTHING_IPC_ITEM2{0, static_cast<DWORD>(offset)});
        offset += row.size();
    }
    for (const auto& row : rows) page.insert(page.end(), row.begin(), row.end());
    return page;
}

SearchReply Receive(Bytes page, unsigned columns = allMetadataColumns, DWORD sort = EVERYTHING_SORT_NAME_ASCENDING, unsigned limit = defaultResultLimit)
{
    PrepareUnicodeQuery2(replyID);
    COPYDATASTRUCT data{replyID, static_cast<DWORD>(page.size()), page.data()};
    Receiver receiver;
    receiver.serial = replyID;
    SearchKey key{1, 9};
    key.sort = sort;
    key.limit = limit;
    key.metadataColumns = columns;
    receiver.active = key;
    receiver.Reply(WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
    if (completed.size() != 1) throw std::runtime_error("expected exactly one delivery");
    SearchReply reply = std::move(completed.front());
    completed.clear();
    return reply;
}

void Test()
{
    Check(RequestedFields(defaultMetadataColumns) == 0x45 && RequestedFields(0) == 5 &&
          RequestedFields(allMetadataColumns) == allFields, "selected columns map to SDK flags");
    SearchKey key{1, 9}, changed = key;
    changed.metadataColumns = ColumnAttributes;
    Check(!(changed == key), "column selection participates in the immutable reply key");

    Bytes identity = Identity(LR"(C:\Alpha\example.txt)");
    Bytes mixed = identity;
    String(mixed, L"ž🚀");
    Append(mixed, UINT64_C(5000000000));
    Append(mixed, UINT64_C(10)); // created
    Append(mixed, UINT64_C(20)); // modified
    Append(mixed, UINT64_C(30)); // accessed
    Append(mixed, DWORD{FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE});
    String(mixed, LR"(C:\索引\例.efu)");
    Append(mixed, DWORD{0});
    Append(mixed, UINT64_C(0)); // FILETIME epoch
    Append(mixed, UINT64_MAX);
    Bytes missing = Identity(LR"(C:\Beta\example.txt)");
    String(missing, L"");
    Append(missing, UINT64_MAX); // unknown size
    for (int i = 0; i < 3; ++i) Append(missing, UINT64_MAX);
    Append(missing, DWORD{INVALID_FILE_ATTRIBUTES});
    String(missing, L"");
    Append(missing, DWORD{123});
    Append(missing, UINT64_MAX);
    Append(missing, UINT64_C(50));
    auto full = Receive(Page(allFields, {mixed, missing}));
    Check(full.state == SearchState::Ready && full.hits.size() == 2, "two-row reply accepted");
    const auto& a = full.hits[0];
    const auto& b = full.hits[1];
    Check(a.path != b.path && a.name == b.name && a.size == UINT64_C(5000000000) &&
          a.created == 10 && a.modified == 20 && a.accessed == 30 && a.attributes == 0x21 &&
          a.extension == L"ž🚀" && a.fileList == LR"(C:\索引\例.efu)" &&
          a.runCount == 0 && a.dateRun == 0 && !a.recentlyChanged,
          "Unicode, 64-bit size, distinct dates, epoch, and zero run count reach the correct row");
    Check(!b.size && !b.created && !b.modified && !b.accessed && !b.attributes &&
          !b.extension && !b.fileList && b.runCount == 123 && !b.dateRun && b.recentlyChanged == 50,
          "empty strings and all-ones sentinels leave fields empty");
    for (const auto& rows : {std::vector<Bytes>{identity}, std::vector<Bytes>{}}) {
        auto reply = Receive(Page(identityFields, rows));
        Check(reply.state == SearchState::Ready && reply.hits.size() == rows.size() &&
              (rows.empty() || !reply.hits[0].size), "identity-only result page accepted");
    }
    const auto largePage = Page(identityFields, std::vector<Bytes>(1250, identity));
    for (unsigned limit : {0u, 1250u, 1249u, defaultResultLimit}) {
        const auto reply = Receive(largePage, 0, EVERYTHING_SORT_NAME_ASCENDING, limit);
        const bool fits = !limit || limit >= 1250;
        Check(fits ? reply.state == SearchState::Ready && reply.hits.size() == 1250
                   : reply.state == SearchState::Unavailable, "configured and unlimited page sizes");
    }
    Bytes partial = identity;
    Append(partial, UINT64_C(0));
    Append(partial, DWORD{0});
    auto subset = Receive(Page(identityFields | EVERYTHING_REQUEST_SIZE | EVERYTHING_REQUEST_ATTRIBUTES, {partial}));
    Check(subset.state == SearchState::Ready && subset.hits[0].size == 0 &&
          subset.hits[0].attributes == 0 && !subset.hits[0].modified, "zero values survive partial replies");
    auto hidden = Receive(Page(allFields, {mixed}), 0);
    Check(hidden.state == SearchState::Ready && !hidden.hits[0].size && !hidden.hits[0].runCount,
          "unsolicited fields are skipped");

    // Everything chooses the page after sorting the whole query. The receiver
    // must keep its order, count and total even when optional fields are absent.
    std::vector<Bytes> sortedRows;
    for (DWORD i = 0; i < 200; ++i) {
        Bytes row = Identity(L"C:\\sorted\\" + std::to_wstring(200 - i) + L"\\example.txt");
        Append(row, static_cast<std::uint64_t>(200 - i));
        sortedRows.push_back(std::move(row));
    }
    Bytes sortedPage = Page(identityFields | EVERYTHING_REQUEST_SIZE, sortedRows);
    std::vector<DWORD> sorts{EVERYTHING_SORT_NAME_ASCENDING, EVERYTHING_SORT_NAME_DESCENDING};
    for (const auto& column : metadataColumns) {
        sorts.push_back(column.ascending);
        sorts.push_back(column.descending);
    }
    for (DWORD sort : sorts) {
        auto* header = reinterpret_cast<EVERYTHING_IPC_LIST2*>(sortedPage.data());
        header->totitems = 275;
        header->sort_type = sort;
        auto reply = Receive(sortedPage, allMetadataColumns, sort);
        bool ok = reply.state == SearchState::Ready && reply.total == 275 && reply.hits.size() == sortedRows.size();
        for (size_t i = 0; ok && i < reply.hits.size(); ++i) {
            ok = reply.hits[i].size == 200 - i && reply.hits[i].path ==
                 L"C:\\sorted\\" + std::to_wstring(200 - i) + L"\\example.txt";
        }
        Check(ok, "SDK-reported order and metadata preserved across the full 200-row page");
    }
    Check(Receive(sortedPage, allMetadataColumns, EVERYTHING_SORT_NAME_ASCENDING).state == SearchState::UnsupportedSort,
          "server sort mismatch rejected");

    mixed.pop_back();
    Check(Receive(Page(allFields, {mixed})).state == SearchState::Unavailable, "truncated last FILETIME rejected");
    Bytes shortRow = identity;
    Append(shortRow, DWORD{1});
    Check(Receive(Page(identityFields | EVERYTHING_REQUEST_SIZE, {shortRow, partial})).state == SearchState::Unavailable,
          "a short row cannot borrow bytes from its neighbor");
    Bytes badString = identity;
    Append(badString, DWORD{0xFFFFFFFF});
    Check(Receive(Page(identityFields | EVERYTHING_REQUEST_EXTENSION, {badString})).state == SearchState::Unavailable,
          "out-of-bounds optional UTF-16 field rejected");
    Bytes badTerminator = identity;
    String(badTerminator, L"txt");
    badTerminator.back() = 1;
    Check(Receive(Page(identityFields | EVERYTHING_REQUEST_EXTENSION, {badTerminator})).state == SearchState::Unavailable,
          "unterminated optional UTF-16 field rejected");
}
}

int main()
{
    try {
        Test();
        Everything_CleanUp();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
