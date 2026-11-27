#pragma once

struct SearchHit;

// The caller owns the hit; disk navigation can synchronously close the results FS.
void OpenResult(int panel, const SearchHit& hit);
void RevealResult(int panel, const SearchHit& hit);

#include <vector>
class CSalamanderForViewFileOnFSAbstract;
enum class ResultOperation { Copy, Move, Delete };
void ViewResult(HWND parent, CSalamanderForViewFileOnFSAbstract* viewer, const SearchHit& hit);
void EditResult(HWND parent, const SearchHit& hit);
bool OperateOnResults(HWND parent, ResultOperation action, const std::vector<SearchHit>& hits,
                      const char* destination);
void ResultContextMenu(HWND parent, int x, int y, const std::vector<SearchHit>& hits);
BOOL ForwardResultMenuMessage(UINT msg, WPARAM wp, LPARAM lp, LRESULT* result);
