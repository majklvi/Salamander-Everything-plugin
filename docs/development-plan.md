# Development plan

The first usable release should let a user open an Everything search in either Samandarin panel, type in a fixed field above the native column headings, sort with one click and open or reveal the chosen file or folder. The proposed first target is a native C++ x64 filesystem plugin for stock Open Samandarin 0.16 on Windows. The reviewed 0.17 revision is a second compatibility target. The fixed strip uses private host window layout, so the first milestone tests it in the real host with sample data. [Samandarin integration](samandarin-integration.md) · [Editor and first-click analysis](panel-overlay.md#child-control-or-popup)

## 1. Prove the editor and first-click sorting

1. Build a standalone, loadable `.spl` against the pinned native SDK. A plugin command opens a read-only virtual filesystem in the active panel. Start with an in-memory list of named sample files and folders and a simple text filter. Make it easy to produce zero, one and several matches. Use the upstream DemoPlug filesystem and exports as references. [0.16 SDK headers](https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940/src/plugins/shared) · [DemoPlug](https://github.com/KRtkovo-eu-AI/salamander/tree/7382688b9ba1c9471a27b19f36beba65825b2940/src/plugins/demoplug)
2. In Detailed view, shorten this results list by one row and place an activatable, modeless popup containing a Unicode edit control in the freed strip. Keep the native header exposed. Refresh the same filesystem instance as the text changes and restore the host layout on exit. [Fixed strip geometry](panel-overlay.md#moving-the-list-down)

**Pass in the main frame.** One match is fully visible. With several matches, the first physical click on Name changes the order exactly once. The next character still edits the query. Delete edits text and F5/F8 issue no file command while editing. Resizing and reopening leave no accumulated offset or stray window. Record build and runtime results separately. If the focus or click check fails, use the observed activation, focus and capture sequence to resolve the UI design before connecting Everything. [First implementation prompt](first-implementation-prompt.md)

## 2. Verify panel ownership and navigation

Keep query state per live tab and filesystem instance. Handle switching sides, resizing, scrolling, moving the frame, DPI changes, detaching a tab, closing it and unloading. Make Down transfer focus to a result, Tab switch sides once and Escape leave editing with the query preserved for re-entry. If a particular view or host version cannot be bound reliably, leave its normal layout intact and document the limitation. [Tab switching and refresh](panel-overlay.md#switching-tabs-and-receiving-results)

**Pass across windows.** Repeat the single-click and next-character check in a torn-off tab and the detached right panel. Drag a column divider, move the frame between monitors and close the active search tab. A second panel keeps its own text, focus stays with the user's last action, and the original list rectangle is restored. Record the actual host revisions and DPI settings tested.

Version 0.2.0 implements this milestone. The host report records both first-click tests and the display configurations exercised. [Milestone 2 results](milestone-2.md)

## 3. Connect live Everything queries

Replace the sample provider with the Unicode Everything SDK v2 on the default instance. Coalesce edits and serialize asynchronous requests through one owner of the SDK state. Copy reply data into an owned snapshot before submitting the next request. Bind each reply to its filesystem instance, tab, query generation, sort and page. Clearing the text or closing a tab invalidates pending results. Start measurements with a 100 ms debounce and a 200-item first page. [Everything query and reply lifecycle](everything-integration.md#from-one-keystroke-to-the-correct-rows)

**Pass with a running index.** An Everything expression such as `ext:pdf invoice` returns matching files across indexed locations. Broad and narrow searches in alternating panels display only their latest results, even when an old reply arrives last. The UI distinguishes zero matches, a loading index and unavailable IPC, and the current query can be retried after recovery.

Version 0.3.0 connects the default Everything instance over SDK v2. The host report identifies the indexed files, both panel queries and the order in which results reached the filesystem. [Milestone 3 results](milestone-3.md)

## 4. Make ordering and file actions correct

Show Name and Parent folder. Retain each row's exact UTF-16 path and file/folder type through filtering, sorting and refresh. Check how the native SDK represents display names, including names outside the system code page. Implement Enter to open a file or navigate to a directory and an explicit **Open containing folder** command. Keep the initial filesystem read-only. [Row identity and native operations](samandarin-integration.md#native-plugin-on-an-unmodified-host)

For partial results, show the loaded and total counts. Verify how the plugin can observe a header sort change. For supported whole-query ordering, request page one from Everything in the new order and invalidate old pages. Where the requested ordering is only available locally, identify it as applying to the loaded rows. [Bounded queries and ordering](everything-integration.md#from-one-keystroke-to-the-correct-rows)

**Pass with ambiguous names and multiple pages.** Open and reveal the intended one of two identically named files in different folders. Refreshing cannot transfer selection to its namesake. Check Unicode and long paths, a directory hit and an unavailable indexed share. For a query larger than one page, reverse the sort so a row from outside the old page should come first. Compare against Everything when claiming whole-query order.

Version 0.4.0 implements full-path actions, selection by identity, and a choice of query order for page one. [Milestone 4 test environment and results](milestone-4.md)

Version 0.4.1 adds ten optional Everything metadata columns. Size and modification time are enabled by default. [Column options and host tests](result-metadata.md)

## 5. Package and measure the first release

Produce the x64 release artifact, include applicable SDK notices and document installation, supported host and Everything versions, and known layout limitations. Test a clean installation, unload and upgrade on the configurations being claimed. Measure time from the last edit to the first usable rows for cold and warm queries, together with the page size, index size and debounce used. Use these measurements to tune the starting values. [Backend compatibility](everything-integration.md#backend-choice) · [End-to-end scenarios](everything-integration.md#what-a-prototype-should-verify)

**Release when** the packaged plugin passes the editor, query, ordering and file-identity checks on its declared configurations, and a user can install it and reproduce a search from the published instructions.
