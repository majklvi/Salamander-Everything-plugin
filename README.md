# Everything search in an Open Samandarin panel

Everything Search 0.4.2 is a native x64 plugin for Open Samandarin. Type an Everything expression above the panel's column headings to see up to 200 matches from the selected Everything instance. Detailed view shows **Name** and **Date modified** by default. Use **Plugins → Everything Search → Settings…** to show additional information from the index, including creation and access times, file attributes and extensions. [Build and install](docs/build-and-test.md) · [Available columns and verification](docs/result-metadata.md)

Enable **Parent folder** to distinguish identical names. Press **Enter** on a result to open it, or press **Shift+F3** to open its containing folder in the same panel and focus the item. [File action and sort tests](docs/milestone-4.md)

Use **F3** to view a result and **F4** to edit it. **F5** copies, **F6** moves and **F8/Del** deletes the marked items. Right-click a result for its menu. These operations preserve the Everything run count. [File operations and verification](docs/result-metadata.md#file-operations-in-search-results)

Press **Ctrl+F12** in an Everything panel and select **Use Everything Filter** to choose a saved filter by name and expression. Everything combines it with the text in the search field before applying the result limit. The filter stays active as you type. **Use filter** applies a Samandarin filename mask to the returned rows. **Do not use filter** clears either choice.

The plugin loads the built-in filters and reads `%APPDATA%\Everything\Filters.csv` at startup and whenever the dialog opens. For a named Everything instance, it checks `Filters-<instance>.csv` first. A saved filter with the same name overrides the built-in definition.

Open **Plugins > Everything Search > Help** for the English offline manual. Press **F1** in Settings or Advanced Properties for help with that dialog. [Read the manual in a browser](help/search.htm) · [Build, install and maintain the help](docs/build-and-test.md#html-help)

## Development

Each search tab keeps its query when the tab or the whole right panel moves into a separate window. Queries use SDK v2 for Everything 1.4 and SDK 3 for Everything 1.5. **Settings → Everything source** selects the version. Automatic prefers a running 1.4 instance. Version 1.5 also supplies a searchable list of additional display properties and a dynamic sort menu. Milestone 4 covers full-path actions, selection across refreshes and ordering queries larger than one page. [Development plan](docs/development-plan.md) · [Detached-window tests](docs/milestone-2.md) · [Live query tests](docs/milestone-3.md)

## Recommendation

**For a fixed search field and clickable column headings, the most direct plugin-only design is to move the entire results list down one row.** The field would sit in the freed space above the native header. Scrolling would leave it in place, and a single match would remain visible below the header. To survive resizing, the plugin would have to reapply the offset whenever Samandarin lays out the panel. This depends on the host's private window layout. [List placement, scrolling, resizing and sorting](docs/panel-overlay.md#moving-the-list-down) · [Keyboard and focus](docs/panel-overlay.md#typing-and-focus)

The plugin uses an **activatable modeless popup** for the editor in that strip. In Open Samandarin 0.16 x64, one click on Name reversed the invoice rows and the next keystroke continued the query. The same interaction worked in the main frame, an individually torn-off tab and the detached right panel. [Main-frame observations](docs/build-and-test.md#observations-in-the-016-host) · [Detached-window observations](docs/milestone-2.md) · [Editor comparison](docs/panel-overlay.md#child-control-or-popup)

| Approach | Where the user types | What appears in the panel | When to choose it |
| --- | --- | --- | --- |
| Salamatrix extension on stock Samandarin | A native modal dialog | A bounded virtual filesystem listing with columns and extension actions | A submitted search is acceptable and a script-based extension is preferred. |
| Native plugin that moves the list on stock Samandarin | A dedicated strip above the original column headings | A smaller live results list with its own header and scrollbars | The editor and the sortable header must remain accessible together, and depending on private host layout is acceptable. |
| Native plugin with an overlay on stock Samandarin | A modeless window over the parent row or the visible column header | A live virtual listing, with the first match visible under the editor | Use the parent row while editing at the top, or the header during scrolling. Positioning depends on the host's private layout. |
| Native filesystem plugin on stock Samandarin | A plugin-owned window, or the shared command line for submitted queries | A virtual listing backed by the Everything SDK | A separate input window is acceptable. A modeless window could drive live results while it remains open. |
| Native plugin with a host change | A fixed editor in its own strip inside the active panel | Live results in a plugin filesystem, or host-owned disk rows if that integration is added | Choose this for a supported layout that keeps the field, column headers and scrolled results usable together. |

**A native filesystem plugin can handle searching, opening a match and showing its containing folder with either editor design.** It could also implement F5/F6 itself. To reuse Samandarin's disk-panel handling of selection, copy/move and shell actions, the host would need to accept external results by full path. Branch View provides a model for that work. I found no public API that imports an Everything result set into it. [Panel and file-operation details](docs/samandarin-integration.md#what-branch-view-already-solves)

The default search covers the folders and drives indexed by Everything. The worker sends a bounded Unicode query while the user continues typing. The runtime test used the SDK v2 interface of Everything 1.5.0.1424b. [Everything integration](docs/everything-integration.md) · [Live test environment](docs/milestone-3.md)

The source review covers the released Samandarin 0.16 and the 0.17 branch at commit `37e9544`. Runtime observations apply to the host and builds identified in each test report. [0.16 release](https://github.com/KRtkovo-eu-AI/salamander/releases/tag/5.0-samandarin-0.16) · [Reviewed 0.17 revision](https://github.com/KRtkovo-eu-AI/salamander/commit/37e954410f65d2aa3009028cccd9e99c8505efbc)
