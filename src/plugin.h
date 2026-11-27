#pragma once

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <ostream>
#include <string>

#include "spl_com.h"
#include "spl_base.h"
#include "spl_gen.h"
#include "spl_fs.h"
#include "spl_menu.h"
#include "spl_vers.h"

extern CSalamanderGeneralAbstract* gGeneral;
extern HINSTANCE gInstance;
extern CSalamanderGUIAbstract* gGUI;

constexpr int openContainingFolder = 2;
#include "../help/topics.h"

bool UseHostDarkTheme();
void CheckSearchPrerequisite(HWND parent);
void ShowSearchSettings(HWND parent);
void ShowEditorForActivePanel();
void ScheduleEditorSync();
void ForgetEditorFS(CPluginFSInterfaceAbstract* fs);
void ShutdownEditor();
