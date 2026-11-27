#include "plugin.h"
#include "modern_search.h"
#include <stdexcept>
#include <string_view>

// Exercise the production first-open check on a machine with no registered or
// conventional installation, irrespective of the developer's Windows profile.
namespace {
struct TestHost {
    unsigned calls = 0;
    void SalMessageBoxEx(const MSGBOXEX_PARAMS* message) {
        ++calls;
        if (!message->URL || std::string_view(message->URL) != "https://www.voidtools.com/downloads/" ||
            !message->URLText || std::string_view(message->Text).find("No installation") == std::string_view::npos)
            throw std::runtime_error("Missing installation must offer the official download");
    }
} host;
LSTATUS NoRegistration(HKEY, LPCWSTR, LPCWSTR, DWORD, LPDWORD, PVOID, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
DWORD NoDirectory(LPCWSTR, LPWSTR, DWORD) { return 0; }
Sources endpoints;
}
Sources DiscoverSources() { return endpoints; }
#define gGeneral (&host)
#define RegGetValueW NoRegistration
#define GetEnvironmentVariableW NoDirectory
#include "../../src/prerequisite.cpp"

int main()
{
    preferredSource = Source::Stable14;
    CheckSearchPrerequisite(nullptr);
    CheckSearchPrerequisite(nullptr);
    if (host.calls != 1) return 1;
    preferredSource = Source::Modern15;
    endpoints.modern = reinterpret_cast<HWND>(1);
    CheckSearchPrerequisite(nullptr);
    return host.calls == 1 ? 0 : 1;
}
