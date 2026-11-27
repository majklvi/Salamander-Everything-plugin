#include "search.h"

#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <utility>

extern HINSTANCE gInstance;
void ApplySearchReplyInHost(SearchReply&&);

namespace {
std::optional<SearchReply> heldReply;

// Identify the three controlled searches without putting any user paths in the log.
std::string_view Fixture(const SearchReply& reply)
{
    if (reply.state != SearchState::Ready || reply.hits.empty())
        return "other";
    constexpr struct { const char* label; std::wstring_view name; } fixtures[] = {
        {"old", HELD_NAME}, {"current", CURRENT_NAME}, {"other-panel", RELEASE_NAME}
    };
    for (const auto& fixture : fixtures) {
        if (std::all_of(reply.hits.begin(), reply.hits.end(), [&fixture](const SearchHit& hit) {
                return std::wstring_view(hit.name) == fixture.name;
            }))
            return fixture.label;
    }
    return "other";
}

void Record(const char* event, std::string_view fixture, const SearchReply& reply)
{
    wchar_t module[32768];
    const DWORD length = GetModuleFileNameW(gInstance, module, ARRAYSIZE(module));
    if (!length || length >= ARRAYSIZE(module))
        return;
    const auto path = std::filesystem::path(module).parent_path() / L"m3-reply-order.tsv";
    std::ofstream log(path, std::ios::app);
    log << GetTickCount64() << '\t' << event << '\t' << fixture << '\t'
        << reply.key.instance << '\t' << reply.key.generation << '\t'
        << reply.key.sort << '\t' << reply.key.offset << '\t' << reply.key.limit << '\t'
        << reply.hits.size() << '\t' << reply.total << '\n';
}
}

void DeliverSearchReply(SearchReply&& reply)
{
    const auto fixture = Fixture(reply);
    if (fixture == "old") {
        Record("hold", fixture, reply);
        heldReply = std::move(reply);
        return;
    }
    if (fixture != "other")
        Record("deliver", fixture, reply);
    ApplySearchReplyInHost(std::move(reply));
    if (fixture == "other-panel" && heldReply) {
        Record("deliver-held", "old", *heldReply);
        ApplySearchReplyInHost(std::move(*heldReply));
        heldReply.reset();
    }
}
