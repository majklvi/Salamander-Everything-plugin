#pragma once

#include <windows.h>

class CPluginFSInterfaceAbstract;

bool HandleNativeSortCommand(UINT command);
void ForgetNativeOrder(CPluginFSInterfaceAbstract* fs);
void ApplyNativeOrder(HWND frame, int panel);
void ObserveResultHeader(HWND header, CPluginFSInterfaceAbstract* fs);
void StopObservingResultHeader(HWND header);
void ApplyRankedOrder(HWND frame, CPluginFSInterfaceAbstract* fs);
