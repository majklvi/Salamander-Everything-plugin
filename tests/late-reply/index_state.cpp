#include <windows.h>
#define EVERYTHINGUSERAPI
#include <Everything.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

extern HINSTANCE gInstance;

namespace {
// Both SDK calls run on the same worker. A normal probe uses the real SDK.
thread_local std::optional<DWORD> probeError;
}

extern "C" BOOL EVERYTHINGAPI TestIsDBLoaded()
{
    probeError.reset();
    wchar_t module[32768];
    const DWORD length = GetModuleFileNameW(gInstance, module, ARRAYSIZE(module));
    if (length && length < ARRAYSIZE(module)) {
        const auto path = std::filesystem::path(module).parent_path() / L"m3-readiness.txt";
        std::ifstream file(path);
        std::string setting;
        if (file >> setting) {
            if (setting == "loading") probeError = EVERYTHING_OK;
            if (setting == "unavailable") probeError = EVERYTHING_ERROR_IPC;
        }
    }
    return probeError ? FALSE : Everything_IsDBLoaded();
}

extern "C" DWORD EVERYTHINGAPI TestGetLastError()
{
    if (probeError) return *probeError;
    return Everything_GetLastError();
}
