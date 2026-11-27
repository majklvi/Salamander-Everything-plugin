# Samandarin integration

This analysis compares the released 0.16 tag (`7382688`) with the 26 November 2026 commit on the 0.17 branch (`37e9544`). The Branch View guide, Salamatrix filesystem implementation, Salamatrix UI guide, and native `spl_fs.h` and `spl_com.h` headers are identical at these two revisions. Source links below use the 0.17 commit so their line numbers remain reproducible. [0.16 release sources][release] · [0.17 sources][head]

## What Branch View already solves

Pressing **Ctrl+B** in a disk panel recursively lists files below its current directory. For example, `C:\Music\Live\cover.jpg` and `C:\Music\Studio\cover.jpg` can appear side by side. The Path column distinguishes them. Viewing a match or opening its containing folder uses that row's actual path. Refresh can restore selection by full path even when filenames coincide. This is the relevant precedent for Everything results. [Branch View behavior][branch] · [Path and selection handling][identity]

Branch View starts a host-owned scan at one disk root. Its matched rows are files. A global Everything query can also return directories and can span `C:\`, `D:\` and a UNC share in the same reply. There is no common disk root for those results. The SDK's `Salamander.PanelItemPaths` and `Salamander.DiskSelection` services let plugins **read** paths or capture an existing disk selection. I found no corresponding public service for supplying a new disk listing. Enabling Ctrl+B and filtering its output would still enumerate that subtree and miss matches elsewhere. [Scanner and root][scan] · [Disk services][disk-services] · [Selection contract][selection]

## Salamatrix on an unmodified host

| Capability | Consequence for Everything search |
| --- | --- |
| A manifest can declare a `fileSystems` provider, `listHandler`, typed columns and item actions. The bundled Process Explorer defines an “Open file location” action. | A search command could accept a query in a dialog, make the results available to its listing handler, and present Name and Parent folder in a virtual panel. [Manifest][manifest] · [Process Explorer example][example] |
| Native dialogs support edit controls and `onChange` events addressed by dialog and control ID. The documented UI surface is modal. | The event can track text typed inside the dialog. Neither the panel provider nor the dialog API offers an editor anchored in the live file panel. [Dialog events][events] · [UI limits][ui-limits] |
| The provider caches listings and can refresh asynchronously. The standard script listing path serializes requests per package and starts a one-shot worker. One listing accepts up to 4,096 items. | Request a bounded result set. If using this route, carry query state across handler invocations explicitly. Frequent relisting would need a latency measurement that includes worker startup. [Cache and refresh][cache] · [Worker invocation][worker] · [List dispatch][dispatch] · [Item limits][limits] |
| An item has an ID and a display name. IDs are limited to 255 bytes and names cannot contain path separators. The framework builds a virtual path. Its advertised actions include viewing and context menus. | Keep long physical paths behind short IDs. Folder activation normally enters the virtual tree. Opening a real location requires an explicit action. Standard filesystem copy, move, delete and drag-and-drop are unavailable through this adapter. [Item validation][limits] · [Virtual names][virtual] · [Activation][enter] · [Advertised services][services] |

The two stable catalogs and the unofficial plugin catalog listed no entry named or described as Everything or voidtools on 27 November 2026. [Extension catalog](https://samandarin.krtkovo.eu/catalogs/extensions-stable.json) · [Plugin catalog](https://samandarin.krtkovo.eu/catalogs/plugins-stable.json) · [Unofficial catalog](https://samandarin.krtkovo.eu/catalogs/plugins-unofficial.json)

## Native plugin on an unmodified host

A native filesystem plugin can expose a flat virtual root. Its `ListCurrentPath` callback supplies rows synchronously, so it should read an already prepared snapshot. The plugin can retain an owned UTF-16 path and item type per row, add a parent-folder column, and implement Enter or a location command. When an Everything reply arrives, `PostRefreshPanelFS` requests a reread for the relevant filesystem instance and can be called from a worker. The host can defer the reread. [Listing contract][listing] · [Row data][row] · [View columns][columns] · [Activation callback][execute] · [Refresh contract][refresh]

The view API describes columns, icons and display settings. The native `ExecuteCommandLine` callback receives a **submitted** command from the shared line below both panels. The host's ordinary Quick Search moves the caret among items already in the list. A native plugin could give its editor a fixed strip by moving and shortening the private list window on each panel layout. The existing column header would remain available for sorting. Keyboard behavior depends on whether the editor belongs to the host frame or to an independently active plugin window. [Fixed strip and host layout](panel-overlay.md#moving-the-list-down) · [Typing and focus](panel-overlay.md#typing-and-focus) · [View interface][columns] · [Command line contract][command] · [Quick Search code][quick]

**A correct open action and a correct refresh are separate requirements.** Looking up `report.pdf` through its row data can open the right file. On refresh, the host normally transfers selection using basenames, while Branch View explicitly restores it by full identity. A plugin using virtual rows must either provide distinct host-facing names or manage and verify restoration by full path. Likewise, copying, moving and deleting are explicit filesystem services. Showing a physical path in a column does not supply those operations. [Refresh matching][merge] · [Branch identity restoration][restore] · [Filesystem service flags][flags]

## Proposed host change

1. **For a permanently reserved search row**, give the active tab a host-owned single-line editor above its file list. Expose query changes and tab closure to the provider through an optional, versioned interface. A native Everything plugin can then keep one search state per tab and return virtual rows using the existing filesystem SDK. Typing in the field must keep focus there when a new listing arrives. The editor sits outside the sortable, selectable file rows.
2. **For disk-panel behavior**, add a separate route through which the host accepts an owned set of full UTF-16 paths and item types for a particular tab and query. Reuse Branch View's per-item path resolution and identity-based refresh. Teach that mode about directory hits and unrelated roots. For example, a flat copy to the opposite panel has a clear meaning across drives, while “keep subfolder paths relative to the scan root” requires a real common root. [Branch file-operation behavior][branch] · [Host path resolver][resolver]
3. **For keyboard navigation**, a proposed default is Down to enter the results, Enter on a file to open it, Enter on a directory to navigate there, and a separate “Open containing folder” command to reveal a file in its ordinary disk panel. Escape exits search and restores the previous panel location. Keep these bindings local to search mode and test them with Quick Search, tab switching and detached panels.

The architectural choice is whether opening and revealing matches is sufficient, or whether the first version should also provide the ordinary disk panel's file operations. Both can use the same Everything query backend. Only the second choice needs a host interface for publishing disk rows. [Query backend and verification](everything-integration.md)

[release]: https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940
[head]: https://github.com/KRtkovo-eu-AI/salamander/tree/37e954410f65d2aa3009028cccd9e99c8505efbc
[branch]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/doc/branch-view.md#L12-L64
[identity]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/doc/branch-view.md#L104-L136
[scan]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/branch_view.cpp#L1355-L1407
[disk-services]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_gen.h#L944-L1008
[selection]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/doc/branch-view-plugin-paths.md#L31-L62
[manifest]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_manifest.h#L86-L138
[example]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/extensions/process-explorer/extension.json#L66-L86
[events]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/doc/salamatrix-ui.md#L454-L470
[ui-limits]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/doc/salamatrix-ui.md#L520-L525
[cache]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L1733-L1785
[worker]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L3935-L3949
[dispatch]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L4066-L4087
[limits]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L5269-L5299
[virtual]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L2135-L2151
[enter]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L2599-L2625
[services]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L2346-L2364
[listing]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_fs.h#L319-L335
[row]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_com.h#L203-L239
[columns]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_com.h#L553-L653
[execute]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_fs.h#L844-L861
[refresh]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_gen.h#L2037-L2050
[command]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_fs.h#L448-L461
[quick]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn0.cpp#L1082-L1104
[merge]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn0.cpp#L3185-L3195
[restore]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn0.cpp#L3367-L3385
[flags]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_fs.h#L170-L225
[resolver]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/branch_view.cpp#L891-L929
