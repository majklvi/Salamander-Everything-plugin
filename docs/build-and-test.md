# Build and run Everything Search

Everything Search 0.4.2 is a read-only, x64 filesystem plugin for Open Samandarin 0.16. It queries the full Everything application's running default instance. The Everything SDK v2 client and its license are built into the plugin. Install and run an IPC-capable Everything edition in the same Windows session. [Everything SDK](https://www.voidtools.com/support/everything/sdk/) · [File action and sort tests](milestone-4.md)

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

With a result focused in the list, **Enter** opens that file or navigates into that directory. Use **Plugins → Everything Search → Open containing folder** to select the item in its parent disk folder. **Plugins → Everything Search → Settings… → Current search order** selects which 200 results Everything returns. Choose any result column and use the **Descending** checkbox to reverse its order. For most opened first, choose **Run count** and check **Descending**. [Run-count ordering and live tests](result-metadata.md#most-opened-first) · [Ordering and row identity](milestone-4.md)

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
