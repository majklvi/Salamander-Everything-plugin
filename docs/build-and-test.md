# Build and run Everything Search

Everything Search 0.4.2 is a native x64 filesystem plugin for Open Samandarin 0.16. It queries a running Everything 1.4 or 1.5 instance selected in Settings. Both SDK clients and their license notices are built into the plugin. Install and run an IPC-capable Everything edition in the same Windows session. [Everything SDK](https://www.voidtools.com/support/everything/sdk/) · [File action and sort tests](milestone-4.md)

## Build

Use Windows x64 with Visual Studio 2026, its Desktop development with C++ workload, a Windows SDK and CMake 4.2 or later for the Visual Studio 2026 generator. Run these PowerShell commands from the plugin repository. The checkout supplies the headers. CMake checks the contents of the six headers against the pinned revision. [Visual Studio generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html) · [Upstream native plugin SDK](https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940/src/plugins/shared)

```powershell
git clone --filter=blob:none --sparse --branch 5.0-samandarin-0.16 https://github.com/KRtkovo-eu-AI/salamander.git out/samandarin
git -C out/samandarin sparse-checkout set src/plugins/shared
git -C out/samandarin checkout --detach 7382688b9ba1c9471a27b19f36beba65825b2940
$sdk = (Resolve-Path 'out/samandarin/src/plugins/shared').Path
cmake -S . -B out/build -G "Visual Studio 18 2026" -A x64 "-DSAMANDARIN_SDK_DIR:PATH=$sdk"
cmake --build out/build --config Release --target EverythingSearch --parallel 1
```

If you already have the pinned headers, set `$sdk` to that directory and run the last two commands. The loadable file is **`out/build/Release/EverythingSearch.spl`**.

## Install and open

1. Copy the `.spl` to a directory where it can stay. In the x64 host, open **Plugins → Plugins Manager**, choose **Add**, and select the file. [Open Samandarin 0.16 release](https://github.com/KRtkovo-eu-AI/salamander/releases/tag/5.0-samandarin-0.16)
2. Activate the desired panel. Use **Detailed** view with its column headings visible and invoke **Plugins → Everything Search → Open search**. The empty field invites you to type. To move an existing search into its own window, detach its unlocked tab or detach the right panel. [Detached panel instructions](milestone-2.md#reproduce-the-first-click-test)
3. Type an Everything expression such as `ext:pdf invoice` or `exact:salamand.exe`. The results depend on your index. The plugin requests the first 200 hits in name order and clearing the text clears the list. If the panel reports that the index is loading or Everything is unavailable, restore the default instance and invoke **Open search** again to retry the same text. Navigate to a disk folder to leave the search. Use Plugins Manager to unload the plugin before replacing its binary.

Press **Ctrl+Shift+E** to open the search field in the active Detailed panel. From the results, the same shortcut resumes the existing query. While editing, **Down** or **Enter** focuses the first result in the current sort order. An empty result set leaves the caret in the field. **Esc** returns focus to the list and preserves the query. **Tab** switches panels. **Alt+Left** goes back one entry in the active panel's directory history, including while typing.

### Keyboard checks in Open Samandarin 0.16 x64

The x64 Release plugin was tested in the main window of an isolated 0.16 installation on November 27, 2026. Its SHA-256 was `03E580C52DB962BBCD1022E1F35766E8D7624EE15FB3C569358B036552D1318E`.

| Check | Observed result |
| --- | --- |
| Open with Ctrl+Shift+E | From the left disk folder, the shortcut opened the search. Typing immediately entered `exact:salamand.exe` in the focused edit. |
| Enter results with Down | With that query populated, Down moved keyboard focus from the edit to the left native results list (control 950). |
| Resume with Ctrl+Shift+E | From the results list, the shortcut focused the existing edit. Typing appended ` file:` to the original query. |
| Switch with Tab | From the edit, Tab focused the right disk panel. A second Tab returned to the left results list with the query intact. |
| Go back while typing | After resuming the edit and appending `s`, the focused field contained `exact:salamand.exe file:s`. A single Alt+Left returned to the original disk folder, `out\host-test\left`, with the left list focused and the search field gone. |

With a result focused in the list, **Enter** opens that file or navigates into that directory. Use **Plugins → Everything Search → Open containing folder** to focus the item in its parent disk folder. **Plugins → Everything Search → Settings… → Current search order** selects which 200 results Everything returns. Choose any result column and use the **Descending** checkbox to reverse its order. For most opened first, choose **Run count** and check **Descending**. [Run-count ordering and live tests](result-metadata.md#most-opened-first) · [Ordering and row identity](milestone-4.md)

To change which file details appear, use **Plugins → Everything Search → Settings…**. A check mark means the column is enabled in all Everything Search panels. A fresh profile shows Name and Date modified. Date modified, Date created and Date accessed can be enabled independently. Size, Parent folder and the remaining fields are opt-in. Choose **OK** to apply the selected columns to open searches. Samandarin saves the choice with its configuration. [Column descriptions and examples](result-metadata.md)

## Reproduce the focus check

Use two indexed filenames that share a prefix, then type enough of the prefix to show both. Click and release **once** on Name and observe the new order before pressing another key. Type the next shared character without clicking the field. Verify the updated field value and both filenames. Earlier versions used fixed invoice fixtures. [Original focus tests](milestone-2.md) · [Live query tests](milestone-3.md)

## Observations in the 0.16 host

The following observations are from milestone 1, version 0.1.0. Tests ran on Windows in an isolated portable copy of Open Samandarin 0.16 x64, using a separate profile and empty disk folders. The Release artifact built with MSVC 19.51, CMake 4.4.3 and Windows SDK 10.0.26100.0. Its SHA-256 was `04c630e1b21c99a722f56e5f3200a03ac60f2ee6fb62a8d5a4d3e97ab69f4373`. Inspection of the binary confirmed x64 and the three SDK exports, `SalamanderPluginEntry`, `SalamanderPluginGetReqVer` and `SalamanderPluginGetSDKVer`. [Milestone 2 report](milestone-2.md)

| Check | Observed result |
| --- | --- |
| Load and open | The host loaded the plugin and exposed **Everything Search → Open search**. Invoking it on the left opened the results filesystem, with the edit above the original Name heading and all three entries visible. The right panel continued showing its disk folder. |
| First click and next character | With `invoic` in the edit, the order was `invoice-a.pdf`, `invoice-b.pdf`. One click on Name reversed it. The focused control was still the edit. Pressing `e` without clicking again produced `invoice` and kept both rows in the reversed order. |
| Filtering and text editing | `invoice-a` showed one fully visible file. Keypad `+` appended a literal character and produced the empty-list message. Selecting and deleting that suffix restored the single match. Copying the last `a`, moving to the end and pasting produced `invoice-aa`. |
| Resize and switch | Two splitter drags changed the panel width while the header and first row stayed at the same height. Maximizing the frame from 1764 × 836 to 2370 × 1152 and restoring it preserved a single strip. Switching to the opposite panel withdrew the strip. Returning restored the query. |
| Right panel and tabs | Opening search on a second right-side tab and typing `note` showed only the `notes` folder. Its sibling disk tab used the full list rectangle. Returning to the search tab restored the field, query and folder on the right, with keyboard focus still in the list. |
| Change the view | Brief view removed the field and used the freed row. Selecting Detailed again restored one strip, the original query and the sole result. |
| Close, unload and reload | Back returned to the disk folder. The two panel headers aligned again. Plugins Manager changed the plugin's loaded state from Yes to No after unload. Invoking Open search reloaded it and restored the three samples with a fresh focused field. |
| F5 and F8 | On the Release build, both keys left the edit focused with the same query and neither opened a host dialog. The separate diagnostic check below verified delivery. |

Two checks used a separate instrumented build of the same plugin in the isolated host. The diagnostic edit received F5 and F8 as `WM_KEYDOWN` values 116 and 119 while it had focus and the popup was active. The frame's command trace recorded zero Copy or Delete commands during that sequence. For selection, a diagnostic key requested a real filesystem refresh through `PostRefreshPanelFS`. Before the refresh the selection spanned offsets 0 through 9 (exclusive). After `ListCurrentPath` completed and control returned to the message loop, both offsets, keyboard focus and popup activation were unchanged. The Release artifact identified above is the uninstrumented build.

## Native panel filter

Ctrl+F12 opens Samandarin's Filter dialog from the query field or the results. The mask applies to the results returned by Everything (up to 200 by default, configurable under **Maximum results**). To restrict the entire index before Everything selects the page, include the condition in the query field (for example, `ext:pdf`). With a panel mask active, the information line reports the visible count against the total returned by Everything.

In Open Samandarin 0.16 x64, a file-only query for the test installation directory returned 47 files. Opening Ctrl+F12 while the query field had focus and applying `*.exe;vcruntime*` left exactly `salamand.exe` and `vcruntime140.dll`. The information line showed **2 of 47**. Clicking **Size** placed the 124,544-byte DLL first in ascending order and the 3,420,160-byte executable first in descending order. Both rows kept their original filenames throughout.

## Configurable result limit

With a running Everything 1.5 instance, a test using the plugin search worker queried `file: ext:dll` and found 506 matches. Maximum results values of 1, 200, 500 and 0 returned 1, 200, 500 and 506 rows respectively. The SDK v2 reply regression used a 1,250-row fixture to check both bounded and unlimited requests. These checks measured result counts. Panel rendering time was not benchmarked.

### Changing the limit during an open search

In Open Samandarin 0.16 x64 with Everything 1.5, the query `file: ext:dll` stayed open throughout the test. Changing **Maximum results** from 200 to 500 and then to 0 refreshed the panel after each confirmation. With the panel filter disabled, Ctrl+A selected 200, 500 and 506 rows respectively. The total stayed at 506.

### Filtering beyond the first 500

Everything returned `wuceffectsi.dll` at position 506 in ascending name order. At a limit of 500, the exact Ctrl+F12 mask `wuceffectsi.dll` showed zero rows. With that mask still active, changing the limit to 0 immediately displayed the file and the status line read **1 of 506 results**. Disabling the mask exposed all 506 rows. Reapplying it showed the same single file.

## Search field while Settings is open

In Open Samandarin 0.16 x64, opening Everything Search Settings with the original build in the light Open Samandarin scheme hid the search field and exposed an empty strip above the column headers. The plugin had hidden its popup whenever the modal dialog disabled the main frame. The corrected build disables input to the popup and keeps it visible in its reserved row.

In the corrected build, the empty field retained its “Search Everything” prompt behind Settings. After closing Settings, typing `vcruntime` returned 15 results. With the same search open, the host scheme was changed four times in sequence: Windows Dark Mode (experimental), Windows Explorer, Windows Dark Mode (experimental), and Norton Commander. Everything Search Settings was opened after each change. The query remained readable above the headers in all four observations and the panel retained all 15 results. Both dark-mode checks showed a dark field. Windows Explorer and Norton Commander showed a light field, including alongside the blue Norton Commander panel.

## Ctrl+F12 saved Everything filters

Open Samandarin 0.16 x64 was tested with a running Everything 1.5 instance. Without a user CSV, the dialog listed the built-in filters. A second host used an isolated roaming profile so the test could edit Filters.csv while Samandarin remained open. The dropdown displayed both a saved override of Executable and the custom name `Knihovny, "DLL"` with its definition `ext:dll`. Selecting the custom filter with an empty query returned 200 of 506 matches. Typing `vcruntime` retained the filter and returned 15 matches.

With the same panel open, adding `Pouze debug DLL` to the CSV and reopening Ctrl+F12 from the search field exposed the new entry immediately. Selecting its expression `ext:dll *d.dll` narrowed the existing query to 10 results. Switching to the native **Use filter** option with the exact mask `vcruntime140.dll` showed three files. **Do not use filter** restored all 15 results and preserved the query text. These UI checks covered Everything 1.5 only.

### Everything 1.4 verification

Open Samandarin 0.16 x64 used the portable Everything 1.4.1.1032 instance `dual-plugin-qa` with its four-file index. Samandarin read `%APPDATA%\Everything\Filters.csv` from an isolated test profile. The search field stayed empty throughout the checks.

| Ctrl+F12 check | Observed result |
| --- | --- |
| Choose the built-in **Executable** filter. | The panel showed **1 of 1** and listed only `everything.exe`. |
| Choose the custom **QA files** filter with the expression `ext:lng`. | The panel showed **1 of 1** and listed only `Everything.lng`. |
| With the panel open, edit the same CSV entry to `ext:ini;db` and reopen Ctrl+F12. | The dropdown retained **QA files** and immediately displayed its new expression. Pressing Enter applied it without reselecting the filter. The panel showed **2 of 2** and listed `Everything-dual-plugin-qa.ini` and `Everything-dual-plugin-qa.db`. |

### Active filter in the panel title

In Open Samandarin 0.16 x64 with Everything 1.4.1.1032, selecting the custom **QA files** filter updated the directory line and tab caption to `everything:\QA files`. Switching to the built-in **Folder** filter changed both to `everything:\Folder`. Choosing **Do not use filter** restored `everything:\`. All three changes appeared immediately after confirming Ctrl+F12.

### Shift+F3 opens the containing folder

In Open Samandarin 0.16 x64, pressing Shift+F3 from the query field with the single result `everything.exe` opened `out\dual-test-14` in the same panel. The caret rested on `everything.exe`. The row was unmarked and the status line displayed the file details.

## HTML Help

The English user manual lives in `help/`. Its breadcrumb, topic headings, navigation links and typography follow the bundled Open Salamander plugin manuals. The Contents and Index belong to Everything Search so the manual can also be opened on its own.

Install Microsoft HTML Help Workshop or pass the path to `hhc.exe` with `-DHHC_EXECUTABLE:FILEPATH=...` when configuring CMake. With the compiler present, building the plugin also builds the manual. To rebuild just the manual, run:

```powershell
cmake --build out/build --config Release --target EverythingSearchHelp
```

Distribute `out/build/help/english/EverythingSearch.chm` alongside the plugin release. Copy it to the host's `help/english/` directory under the directory containing `salamand.exe`. If the host already uses another help-language directory, put the CHM beside that directory's `salamand.chm`. The plugin registers the filename with the host. Building the plugin alone remains possible without a help compiler.

For a Free Pascal toolchain, set `CHMCMD_EXECUTABLE` to a compiler that writes archive-relative URLs for numeric contexts. Stock chmcmd 3.2.2 embeds the build directory in those URLs. In that version the fix in `chmfilewriter.pas` is to pass `files[i]` as the URL to `Writer.AddContext`. Running `chmls extractalias EverythingSearch.chm` should produce five mappings to HTML filenames that contain no drive or build directory.

### Keeping the help current

Update the corresponding English topic in the same change as any user-visible command, shortcut, dialog or default. Each new topic needs an entry in the project file and contents, suitable index keywords and a link from a related page. For context help, assign a stable ID in `help/topics.h`, map it in the project's ALIAS section and use that ID in the plugin callback. Verify the installed CHM through the plugin menu and F1 from the affected control. Record observed behavior separately from usage instructions.

In Open Samandarin 0.16 x64, pressing F1 with the caret in the search field opened **Getting started** in the Windows HTML Help viewer. The Contents tab listed all five topics and the viewer displayed Index and Search tabs. The compiled archive contained the five numeric context IDs mapped to their respective relative topic filenames.
