# File details in Everything Search

## Settings

**Layout check in Open Samandarin 0.16 x64.** All three file-date checkboxes could be checked together in the dark dialog. The open order selector showed twelve distinct columns, from Name to Parent folder, with a separate Descending checkbox below it. The SDK regression used synthetic replies with 275 matches to verify that the returned 200 rows retain their order for all twelve columns in both directions.

The settings window follows Samandarin's dark-mode setting. In Open Samandarin 0.16 x64, the dark title bar, dialog background, section heading, checkbox labels, date and order selectors, and OK and Cancel buttons were verified visually. Light and Windows high-contrast modes use native control rendering and were not visually tested.

Open **Plugins → Everything Search → Settings…**, or select **Everything Search** in **Plugins Manager** and click **Configure…**. Both commands open the same native dialog. Column choices apply to all search panels and use the existing saved preferences. **Current search order** applies to the active search and is available when a results panel is active. Choose **OK** to apply the changes together or **Cancel** to discard them.

**Verified in Open Samandarin 0.16 x64.** Cancelling a change to Size left it unchecked on reopening. Applying Size, Parent folder, Date created and Least opened first updated an open two-result query. Both paths remained visible, the new headers appeared, and the status line reported the selected order. Opening Configure in Plugins Manager showed the same selections.

## Compact columns in 0.4.2

Detailed view starts with **Name** and **Date modified**. Each optional column has its own checkbox. Select any combination of **Date modified**, **Date created** and **Date accessed**, or clear all three. Enable **Parent folder** to distinguish duplicate names.

**Current search order** lists Name and all eleven optional columns, including hidden columns. Select a column for ascending order or check **Descending** under the list to reverse it. Everything orders the full query before returning up to 200 results. If Everything reports a different sort, the panel shows that the selected order is unavailable.

Dates follow the same Windows local-time conversion and regional short-date format as the disk panel. Size uses Samandarin's configured bytes, whole KB or mixed units via its SDK formatting functions. Empty values stay blank.

**Verified in Open Samandarin 0.16 x64.** The initial result headers were Name and Date modified. After enabling Size, the first result for `exact:salamand.exe` showed `3 420 160` and `19.05.2026`. Opening its containing folder showed the same size and date in the disk panel. In the earlier single-date layout, selecting Date created and then Date accessed replaced the previous header each time. The accessed date of that file was `27.11.2026`. The live comparison used the host's bytes setting. The listing regression also checks dispatch to the SDK's whole-KB and mixed-unit formatters.

### Changing size units while a search is open

In Open Samandarin 0.16 x64 with Everything Search 0.4.2, the left panel showed one match for `exact:api-ms-win-core-console-l1-1-0.dll \5.0-samandarin-0.2\`. The right disk panel showed the same file in its containing directory. With both Size columns visible, the tester changed **Options → Configuration → Panels → Show sizes** and compared the two cells immediately after confirming each setting.

| Show sizes setting | Search result | Disk panel |
| --- | ---: | ---: |
| In Bytes (initial) | `22 056` | `22 056` |
| In Kilo Bytes | `22 KB` | `22 KB` |
| In short format (mixed units) | `21,5 KB` | `21,5 KB` |
| In Bytes (restored) | `22 056` | `22 056` |

Both panels updated on confirmation, including the rounding to whole KB and the decimal comma in short format. The original query stayed open with one result throughout. No manual refresh or query edit was needed.

### Recording file opens in Everything

When Windows accepts a request to open a file from the results list, the plugin sends its exact indexed Unicode path to `Everything_IncRunCountFromFileNameW`. Everything increments **Run count** and updates **Date run**. Recording works with both columns hidden. A successful update refreshes open searches, including queries that filter on run history. Folder navigation, **Open containing folder** and failed file opens do not increment the count. If Everything is unavailable at the time of recording, the file stays open and the plugin does not retry the increment. [Everything SDK reference](https://www.voidtools.com/support/everything/sdk/everything_incruncountfromfilename/)

**Verified in Open Samandarin 0.16 x64 on November 27, 2026.** A new indexed text file showed a run count of `0`. Pressing Enter on its result opened that file in Notepad. The still-open search updated to `1` without a manual refresh, and **Date run** showed `27.11.2026`. After the test file was removed, pressing Enter on its stale result produced a file-not-found message. A separate SDK read of the exact path still returned `1`.

### Run count header

Open **Plugins → Everything Search → Settings…** and check **Run count**. From the default name order, click **Run count** once for most opened first and again for least opened first. Each click sends a new query to Everything. The server applies the requested order to all matches and returns up to 200. The status line shows the current Everything order. Samandarin's native header arrow can move to **Name** when the plugin uses the numbered names to preserve the exact SDK order.

**Verified in Open Samandarin 0.16 x64.** With the query field focused, one click switched the status to *most opened first* even though every displayed count was zero. The next click switched to *least opened first*. With a temporary indexed text file added, the panel showed 200 of 2,639 matches. In ascending order the first result was `zutil.obj` with zero runs. Clicking again brought the test file with 17 runs to position 001. Independent SDK queries over the same index found the test file outside the first 200 in both name order and run-count ascending (19), and first in run-count descending (20). The test file and its run history were cleared after verification.

### File operations in search results

| Action on a result | Current behavior |
| --- | --- |
| Enter or double-click a file | Opens its exact path with the Windows file association and records one run on success. |
| Plugins → Everything Search → Open containing folder | Opens the disk panel and selects the file. This command records no run. |
| File context menu | Right-click an item or press Shift+F10. Windows supplies the top-level menu for the marked items, or for the focused result when nothing is marked. For results from different folders, the plugin groups their full shell identities in a temporary Windows results view. |
| View (F3) | Opens the focused file with Samandarin's viewer for that file type. |
| Edit (F4) | Opens the focused file with its Windows Edit association. Files without an Edit association use Windows Notepad. Samandarin 0.16 exposes no filesystem editor callback. Press F4 with focus in the list. |
| Copy (F5) or move (F6) | Opens the host destination dialog, initially pointing to the opposite disk panel. Windows handles collisions and progress. |
| Delete (F8 or Del) | Uses Samandarin's delete confirmation setting and the Windows shell's undo/recycling support. |

File operations use the full paths captured from the result rows, including when the names are identical or carry a rank prefix. Bulk actions use the marked rows. View and Edit act on one file. Only the plugin's Enter/double-click file-open action records a run.

**Verified in Open Samandarin 0.16 x64.** F3 opened the internal viewer on the indexed text file's full path. F4 opened the same file in Notepad. F5 copied both to the opposite disk panel. F6 moved both originals there after the test copies were renamed. Both destination files matched their respective source contents. The SDK returned zero runs for all four fixture paths after viewing, editing, opening the menu, copying and moving.

**Shell menu verification.** In Samandarin, right-clicking a text-file result showed Open, Edit, Print, Copy as path and Properties directly. A Windows shell integration check exercised the final menu-building code with two files named `copy.txt` under different directories. The combined menu included Copy as path and Properties. Computer Use could not activate the final host window for a repeat of the multi-selection gesture.

**F8 deletion.** On November 27, 2026, confirming the two-file deletion in Open Samandarin 0.16 x64 removed both `Desktop\sam-action-29c1\alpha\same.txt` and `Desktop\sam-action-29c1\beta\same.txt` from their original disk locations. Both parent folders still existed. The panel regression also checks that the operation retains both original paths if the selection changes while the confirmation is open.

**Search results.** A fresh Everything SDK query for `file: path:sam-action-29c1 same.txt` returned zero matches. The running test build still displayed one stale row after its automatic refresh. Reissuing the identical query in the open panel cleared the row and the status line showed **0 of 0 results**.

**Run count.** The SDK returned `0` for each original full path before deletion. Reading each path again after deletion also returned `0`.

### Automatic refresh after file operations

The plugin requests an updated page shortly after copy, move, delete and shell menu operations. While a nonempty query is open, it also checks Everything every three seconds. This picks up index updates that arrive after the first check, including changes made outside the plugin. The panel stays usable during the check. It replaces the listing only when the results, order, requested metadata, total count or search status change.

**Verified in Open Samandarin 0.16 x64 on November 27, 2026.** Each check below used the same query before and after the operation, with no manual refresh.

| Operation | Observed result |
| --- | --- |
| F8, two files named `same.txt` | Selected both matches in `alpha` and `beta` and confirmed deletion. Both original paths disappeared from disk. The open panel changed from **2 of 2** to **0 of 0** and showed **No matches**. |
| F5, copy within the search scope | Copied `alpha\copy.txt` into the opposite panel's `target` folder. The search changed from one row to two; both files existed on disk. |
| F6, move outside the search scope | Moved `alpha\move.txt` into `target` while the query matched only the source folder. The search changed from one row to zero, and the destination panel showed the moved file. |

### Most opened first

Choose **Plugins → Everything Search → Settings… → Current search order → Most opened first**. Everything sorts all matches by descending run count before returning the first 200. This works with **Run count** hidden. The panel focuses the top result and prefixes each displayed name with its position in the SDK reply, so ties retain Everything's order. Folders share the ranked list and have a trailing backslash. Open and Reveal use the original path. Changing the query in the same search keeps the selected order.

**Verified in Open Samandarin 0.16 x64.** A query matched 605 files. Two test files with counts of 17 and 9 were outside the first 200 paths in Everything's Name-ascending reply. Choosing **Most opened first** in the Release plugin brought those files to positions 001 and 002, with the first file focused. The panel reported 200 of 605 results. A separate instrumented build captured all 200 full paths from the native panel. Each matched the path at the same position in an independent SDK query for run-count descending (sort ID 20), including the zero-count ties.

## Historical behavior and verification in 0.4.1

In Open Samandarin's **Detailed** view, Everything Search 0.4.1 shows **Name**, **Size**, **Date modified** and **Parent folder** by default. Open **Plugins → Everything Search → Result columns** to choose which of the ten optional fields to request across all search panels. If an enabled column is missing, right-click the header and check **Choose Columns** for the current Samandarin view. The host applies its saved column order and visibility. On a narrow panel, use the horizontal scrollbar to reach the columns on the right. [Samandarin's handling of plugin columns](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/salamdr4.cpp#L3095-L3179)

| Column | Value from Everything |
| --- | --- |
| Size | Exact byte count. An indexed empty file shows `0 B`. Folder sizes depend on the value Everything supplies. |
| Date modified | Last write time for the indexed item. |
| Date created | Creation time for the indexed item. |
| Date accessed | Last access time recorded in the index. |
| Attributes | Windows file attributes, such as `Read-only, Archive`, `Directory` or `Compressed`. |
| Extension | Extension supplied by Everything, for example `txt`. An extensionless item has an empty cell. |
| File list | Name of the Everything file list that supplied the result, when applicable. |
| Run count | Number of runs recorded by Everything. A returned count of zero displays as `0`. |
| Date run | Last run recorded by Everything. |
| Date recently changed | Time from Everything's recently changed history. **Date modified** shows the filesystem's last write time. |

Dates use the Windows local time zone, including the daylight-saving offset applicable on that date, and display as `YYYY-MM-DD HH:MM:SS`. A blank cell means the index supplied no displayable value for that item. The search keeps the row, including its full path for opening or revealing it. The plugin requests the enabled metadata in the same SDK query as the filenames. It draws the rows from that reply without opening individual files or contacting their parent shares. [SDK request flags](https://www.voidtools.com/support/everything/sdk/everything_setrequestflags/) · [Returned fields](https://www.voidtools.com/support/everything/sdk/everything_getresultlistrequestflags/)

Use **Name** or **Parent folder** to change the sort order. The ten metadata columns display values only. **Plugins → Everything Search → Query order** explicitly selects the order Everything uses for its 200-result page. [Whole-query sorting](milestone-4.md#sorting-a-query-larger-than-one-page)

## Verification in Open Samandarin

On November 27, 2026, an isolated Open Samandarin 0.16 x64 queried the running default Everything 1.5.0.1424b instance. The expression `samcol-` returned two text files and one directory. The expected file sizes, timestamps and attributes were read independently from Windows and from the Everything SDK.

| Result | Size | Date modified (Prague) | Date created (Prague) | Attributes | Extension |
| --- | ---: | --- | --- | --- | --- |
| `samcol-known.txt` | 36 B | 2025-01-02 14:45:09 | 2024-07-01 11:12:13 | Read-only, Archive | txt |
| `samcol-zero.txt` | 0 B | 2025-01-02 14:45:09 | 2024-07-01 11:12:13 | Archive | txt |

Both open result panels gained the enabled columns. The directory showed **Directory** and an empty extension. The empty file's **Date accessed** was `2026-03-28 22:43:07`. The summer creation time and winter modification time matched their respective local offsets. On all three rows **Run count** displayed `0`, and **File list**, **Date run** and **Date recently changed** were blank, matching the SDK reply.

With `samcol-` in one panel and `exact:samcol-zero.txt` in the other, turning off **Size** removed it from both panels and kept their counts at three and one. **Ctrl+R** preserved the columns. After **Options → Save Configuration** and a host restart, both panels retained the nine enabled fields. Enabling **Size** again restored the byte counts.

**Hidden files.** A fourth match, `samcol-hidden.txt`, was 23 bytes with the Windows attributes **Hidden, Archive**. With Samandarin's **Hide files and directories with hidden or system attribute** option enabled, the final build displayed all four indexed matches. Switching the **Attributes** column off and back on kept all four rows. The hidden file again showed `23 B` and `Hidden, Archive`.

**Sorting with the extra columns.** With all ten fields enabled on a 603-file query, reversing **Name** loaded the descending page beginning with `z-touch.json`, `zip.spl` and `zip.chm`. Selecting ascending **Parent folder** loaded the distribution-root DLLs. Both orders reported 200 of 603 results.

The first Release build used for the two-panel column checks was `5CFD2456D0FDC58B42732FC6152B60A850B4FDA69B8601C2E1B73C632B97090C`. The restart and 603-file sort checks ran on `ABA0C8381E4A3219A80B4D696A7973F21AA0F275A88AF59C489F992F245DF5AE`. The final x64 Release SHA-256 is **`1C65A71FF88F07BE2D3FDFE9DBE2C95387D8C4C196E67BA88CA2822D20BDE118`**. The final host run repeated the metadata comparison and the hidden-file column toggle.

## Repeatable regression tests

The SDK test passed 18 checks using controlled Unicode replies and the production receiver. Cases included a 5,000,000,000-byte file, Unicode file-list names, a missing size, genuine zeros and replies containing only the required identity fields. On a 200-row page with 275 total matches, the receiver kept each size with its path for all four supported query orders. The simulated panel passed 23 checks, including the native cell callbacks, changing columns while a query was in flight, opening a search with all optional fields hidden, and displaying hidden and system attributes without losing either match. [SDK test](../tests/sdk-metadata/main.cpp) · [Panel test instructions](../tests/result-identity/README.md)

Run the SDK test from the repository root on Windows.

```powershell
cmake -S tests/sdk-metadata -B out/sdk-metadata-regression -G "Visual Studio 18 2026" -A x64
cmake --build out/sdk-metadata-regression --config Release
ctest --test-dir out/sdk-metadata-regression -C Release -V
```
