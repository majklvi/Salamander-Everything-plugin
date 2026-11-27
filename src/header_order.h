#pragma once

#include <windows.h>

class CPluginFSInterfaceAbstract;

void ObserveResultHeader(HWND header, CPluginFSInterfaceAbstract* fs);
void StopObservingResultHeader(HWND header);
void ApplyRankedOrder(HWND frame, CPluginFSInterfaceAbstract* fs);
