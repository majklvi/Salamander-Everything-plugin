#include "plugin.h"
#include "modern_search.h"

#include <vector>

namespace {
// A portable copy may live anywhere. Check running endpoints first, then the
// installer registration and conventional locations for a stopped executable.
bool MatchesEverything(const std::wstring& path, Source source)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return false;
    std::vector<BYTE> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(*info) || HIWORD(info->dwFileVersionMS) != 1) return false;
    const auto minor = LOWORD(info->dwFileVersionMS);
    return (minor == 4 && source != Source::Modern15) || (minor == 5 && source != Source::Stable14);
}

bool Installed(Source source)
{
    for (auto root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        for (DWORD view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
            for (const auto* name : {L"Everything", L"Everything 1.5a"}) {
                const std::wstring key = std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") + name;
                wchar_t location[1024];
                DWORD bytes = sizeof(location);
                if (RegGetValueW(root, key.c_str(), L"InstallLocation", RRF_RT_REG_SZ | view,
                                 nullptr, location, &bytes) == ERROR_SUCCESS &&
                    MatchesEverything(std::wstring(location) + L"\\Everything.exe", source)) return true;
            }
        }
    }
    for (auto variable : {L"ProgramFiles", L"ProgramFiles(x86)", L"LOCALAPPDATA"}) {
        wchar_t directory[1024];
        auto length = GetEnvironmentVariableW(variable, directory, 1024);
        if (!length || length >= 1024) continue;
        for (auto suffix : {L"\\Everything\\Everything.exe", L"\\Everything 1.5a\\Everything.exe"})
            if (MatchesEverything(std::wstring(directory) + suffix, source)) return true;
    }
    return false;
}
}

void CheckSearchPrerequisite(HWND parent)
{
    static std::optional<Source> checked;
    if (checked == preferredSource) return;
    checked = preferredSource; // Modal message boxes can reenter the editor sync.
    const auto sources = DiscoverSources();
    if (sources.UseModern(preferredSource) ? sources.modern : sources.stable) return;
    std::string name = preferredSource == Source::Modern15 ? "Everything 1.5" :
                       preferredSource == Source::Stable14 ? "Everything 1.4" : "Everything 1.4 or 1.5";
    MSGBOXEX_PARAMS box{};
    box.HParent = parent;
    box.Caption = "Everything Search";
    box.Flags = MB_OK | MB_ICONINFORMATION;
    std::string message;
    if (Installed(preferredSource)) {
        message = "Start " + name + " in this Windows session with IPC enabled, then open the search again.";
    } else {
        message = "No installation of " + name + " was detected. Install Everything or start your portable copy, then open the search again.";
        box.URL = "https://www.voidtools.com/downloads/";
        box.URLText = "Download Everything from voidtools";
    }
    box.Text = message.c_str();
    gGeneral->SalMessageBoxEx(&box);
}
