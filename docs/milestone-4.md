# Milestone 4 — Result identity, file actions and query sorting

Open Samandarin opened and selected the intended copy of two identically named files. For a 603-file search, reversing the native Name header replaced all 200 paths on the first page. All four tested query orders selected the same 200 paths as Everything. Samandarin's own collation and tie-breaking affected their positions within the page.

## Using the results

In Detailed view, use **Name** and **Parent folder** together to choose a result. **Down** moves from the search field into the results. With a result focused, **Enter** opens a file with its Windows association or navigates the same Samandarin panel to a directory. **Plugins → Everything Search → Open containing folder** navigates the source panel to the item's parent and places the caret on the corresponding disk entry.

Each row carries its original UTF-16 name, full path and file or folder type. Opening, revealing and restoring selection use this stored identity. The listing owns the row data through native sorting and refresh. A user can act on a result only while it belongs to the filesystem's current listing. Windows may report that a target has disappeared or changed type since Everything indexed it. [Listing implementation](../src/result_listing.cpp) · [File actions](../src/actions.cpp)

Samandarin 0.16 drops the optional wide display name when adding plugin filesystem rows. In the native listing, unrepresentable characters are displayed as code points such as `[U+6771]`. The exact string is retained for actions. Disk navigation uses the same UTF-8 conversion that the host's own wide-path entry point uses. After navigation the plugin asks the host's disk-item path service for the full UTF-16 path before marking the row. [Host filesystem row handling](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/zip.cpp#L6910-L6935) · [Host disk navigation](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/fileswn2.cpp#L2922-L2957) · [Disk-item path service](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/zip.cpp#L756-L830)

## Test environment

Tests ran on November 27, 2026 in an isolated Open Samandarin 0.16 x64 with the running default Everything 1.5.0.1424b index. The test files shared the basename `sam4-duplicate.txt` in two different folders, `alpha-parent` and `beta-parent`. Each contained a distinct marker so the open document could be checked independently of the displayed basename. The normal Release plugin was compiled with MSVC 19.51 and the Samandarin SDK at `7382688b9ba1c9471a27b19f36beba65825b2940`.

| Binary | SHA-256 |
| --- | --- |
| Everything Search 0.4.0, x64 Release | `0D920FED29D8FA5E14217BC3A0CBA3FC1CA9E27829FBCC49EE5FA4AE4D1F98B0` |
| Separate build recording the host's 200 full paths | `5AE4664867015EF50CC15AE202EB5A4B3CD726BFDECCB00B65455623EC3B789A` |
| Open Samandarin host executable | `EDB9DDFC172571536B4DA2FE5577F188885F4CB8E5892CB645552669A68AB6C7` |

## Enter on files with the same name

**Passed in the Release build.** The search `exact:sam4-duplicate.txt` displayed two rows with the same Name, distinct Parent folder values and a count of **2 of 2 results**. Enter on the Beta row opened a Notepad tab containing `BETA document / distinct second copy.` Enter on the Alpha row opened a separate tab containing `ALPHA document / distinct first copy.` Both documents stayed open so their contents could be compared. Windows used its existing text-file association. An earlier process check also confirmed that the path passed to Notepad for Alpha ended in `alpha-parent\sam4-duplicate.txt`.

## Open containing folder and select the disk file

**Passed for both copies in the Release build.** On the Alpha result, **Open containing folder** changed the left panel to `alpha-parent`. The caret and the selected mark were on `sam4-duplicate.txt`. The disk status showed 62 bytes in one selected file, and its sibling `sam4-offline.txt` was unmarked.

In the right search panel the Beta row appeared first. Choosing it by its Parent folder and invoking the same command opened `beta-parent` on the right. Again, only `sam4-duplicate.txt` was marked and focused, with 62 bytes selected. The neighboring files stayed unmarked. The two disk panels simultaneously showed their respective copies selected in different directories.

**Refresh preserved identity.** With Beta focused and marked in the results, Ctrl+R left Beta focused and the only selected row. The status showed **2 of 2 results | 1 selected**. Narrowing the query to `"\alpha-parent\" exact:sam4-duplicate.txt` left only Alpha, unmarked. Its status showed **1 of 1 results** with no selected count.

## Directory, Unicode and long-path actions

| Indexed result | Enter in the Release build | Open containing folder in the Release build |
| --- | --- | --- |
| Directory `sam4-folder-marker` | The native panel opened that directory and showed its child `sam4-inside.txt`. | Enter-only check. |
| `sam4-東京-🎵.txt` | The search field and the listing rendered the Japanese characters and music symbol literally on this host. Notepad displayed `Unicode Tokyo music QA document.` | The native `beta-parent` panel displayed the same literal name, focused and marked that file alone, and reported 32 bytes selected. |
| `sam4-long.txt` under four nested directories, full path 294 UTF-16 code units | Notepad displayed `Opened the long path document.` | The native panel opened the fourth-level directory and marked only `sam4-long.txt`, reporting 30 bytes selected. |

For a stale local result, the tester listed two copies of `sam4-offline.txt`, then renamed just the Beta copy on disk. Both Enter and Open containing folder on its still-visible row showed **The indexed item is unavailable.**, the full original `beta-parent\sam4-offline.txt` path and Windows' file-not-found message. The existing Alpha copy remained untouched.

## Sorting a query larger than one page

The test query `"\5.0-samandarin-0.2\" file:` matched 603 files. An independent Everything SDK client requested offset 0 and limit 200 for each ordering. It confirmed all four requested sort IDs. Reversing either Name or Parent folder replaced all 200 full paths on page one.

| Everything sort | First entries in the SDK reference page |
| --- | --- |
| Name ascending | Three copies of `1-1.tab` from `convert\centeuro`, `convert\cyrillic` and `convert\westeuro` |
| Name descending | `z-touch.json`, `zip.spl`, `zip.chm` |
| Parent folder ascending | The first directory was the distribution root, starting with `api-ms-win-core-console-l1-1-0.dll` |
| Parent folder descending | `utils\sqlite.dll`, `utils\salmon.exe`, then `plugins\zip\zip2sfx\sample.set` |

**Native Name header reversal passed in the Release build.** With the search field focused, the right panel showed **200 of 603 results**, Name ascending, with three `1-1.tab` rows followed by `7za.dll`. A single click on the native Name header changed the arrow and the Everything page status to descending. The first three visible rows became `z-touch.json`, `zip.spl` and `zip.chm`, matching the SDK reference. Each came from outside the original 200. After the refresh, the search edit had keyboard focus.

Clicking **Parent folder** also fetched new pages. Ascending began in the distribution root at `api-ms-win-core-console-l1-1-0.dll`. Descending began in `utils`. Samandarin displayed `salmon.exe` followed by `sqlite.dll`, reversing the reference client's order *within the same parent*. After the click, typing `e` without refocusing appended it to the field (`file:e`). Choosing **Query order → Name, A to Z** then fetched the Name-ascending page while leaving the native Parent-descending header in force. The first row in that local display was `DEPENDENCIES.md`.

### Comparison of all 200 full paths

A separate build recorded the native list through `GetPanelItem` and resolved each visible row back to its stored UTF-16 path. A separate process queried the official Everything SDK for the same expression, page size and sort ID. The comparison used each full path once, preserving duplicates with different parents.

| Everything query order | Native header | Paths shared with SDK page | Paths at the exact SDK position |
| --- | --- | ---: | ---: |
| Name ascending | Name ascending | 200/200 | 127/200 |
| Name descending | Name descending | 200/200 | 186/200 |
| Parent folder ascending | Parent folder ascending | 200/200 | 164/200 |
| Parent folder descending | Parent folder descending | 200/200 | 28/200 |
| Name ascending via menu | Parent folder descending | 200/200 | 0/200 |

Both reversals had **zero full paths in common** with their respective ascending pages. Native row order reflects the host's regional comparison and handling of ties. For example, both Name-ascending views began with `1-1.tab`, but their first copies came from different directories. On this Czech Windows configuration the clients also placed names containing `ch` differently. Exact positional parity with Everything is therefore limited to the counts shown above.

**Query order** requests a new first page from Everything and advances the query generation. The worker checks the actual sort ID in the response before publishing the page. The status line reports the loaded and total counts and the order used to fetch the page. Samandarin then sorts the loaded rows using its own comparison and directory-grouping rules.

The stock 0.16 SDK has no getter or notification for the native header's sort direction. On a completed header click, the plugin lets the host handle the click and observes the file rows. If the reordered rows identify exactly one supported Name or Parent folder order, the plugin requests that order from Everything. A divider drag, unchanged order or ambiguous comparison leaves the existing page in place. **Plugins → Everything Search → Query order** provides an explicit choice in those cases. [Native header observer](../src/header_order.cpp) · [Everything result-list sort API](https://www.voidtools.com/support/everything/sdk/everything_getresultlistsort/)

## Additional checks

- Thirteen filesystem checks exercised the shipped listing code with a simulated host. They covered identical display names, full-path dispatch, the lifetime of an old listing, and restoration of the caret and selected marks after a refresh or reorder. When a marked item disappeared, its mark did not transfer to its surviving namesake. Replacing a directory with a file at the same path also cleared the mark. Samandarin still places its caret on an available row. [Run the identity regression](../tests/result-identity/README.md)
- A controlled stale-index row pointed to a nonexistent share on `localhost`. The production action code called the real Windows attribute API and received `ERROR_BAD_NET_NAME` (67). Open and Reveal both reported the full UNC target. A local file with the same basename was present and received neither action. The test replaced the launcher, dialog and host navigation with recorders.
- Nine header-order checks covered the four supported orders, repeated parent values, ambiguous orderings and lists with fewer than two files. In a separate worker test, a private IPC peer returned ascending order for a descending request. The plugin rejected the page, reported the unsupported order and accepted a subsequent correctly sorted reply.

## Reproduce with another index

1. In two indexed directories, create text files with the same basename and different contents. Search for `exact:` followed by that basename. Use **Parent folder** to choose each row, press Enter and read its contents in the associated application.
2. Open the search again and choose **Open containing folder** for each copy. Check the disk panel's full directory path, caret and absence of a selection mark. If testing refresh, mark one copy, refresh the results, and verify the chosen parent before invoking the command.
3. Choose a file-only query with at least 400 distinct names. In Everything SDK v2, request offset 0, limit 200 and Name ascending, then repeat with Name descending. Record the *full paths* of both reference pages, including the first result of each.
4. Run the same query in the plugin. Compare the status count and the visible leading rows before and after reversing the Name header. An entry from outside the original 200 demonstrates a new page. Use **Query order** to request a particular page explicitly when the current data does not distinguish the native header order.
