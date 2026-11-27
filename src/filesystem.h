#pragma once

#include <cstdint>
#include <string>

class CPluginFSInterfaceAbstract;
class CPluginInterfaceForFSAbstract;
class CPluginDataInterfaceAbstract;
struct CFileData;
struct SearchHit;

// Accessed on the host's UI thread. The returned query belongs to its live FS.
CPluginInterfaceForFSAbstract* GetFilesystemProvider();
bool HasOpenResultsFS();
bool IsResultsFS(CPluginFSInterfaceAbstract* fs);
bool IsListingCurrent(CPluginFSInterfaceAbstract* fs);
const std::wstring& GetQuery(CPluginFSInterfaceAbstract* fs);
void SetQuery(CPluginFSInterfaceAbstract* fs, const std::wstring& text);
void RetryFailedSearch(CPluginFSInterfaceAbstract* fs);
std::uint32_t GetResultSort(CPluginFSInterfaceAbstract* fs);
void SetResultSort(CPluginFSInterfaceAbstract* fs, std::uint32_t sort);
bool GetResultHit(CPluginFSInterfaceAbstract* fs, const CFileData* row, SearchHit& hit);
void RememberResultSelection(CPluginFSInterfaceAbstract* fs);
void RestoreResultSelection();
void ReleaseResultListing(CPluginDataInterfaceAbstract* data);
