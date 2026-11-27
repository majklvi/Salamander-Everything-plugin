# Everything search in an Open Samandarin panel

Feasibility study, 27 November 2026. The proposed interaction is one editable line at the top of a file panel. As the user types, the rows below it change to the matching files and folders from the local Everything index.

## Recommendation

**For a fixed search field and clickable column headings, the most direct plugin-only design is to move the entire results list down one row.** The field would sit in the freed space above the native header. Scrolling would leave it in place, and a single match would remain visible below the header. To survive resizing, the plugin would have to reapply the offset whenever Samandarin lays out the panel. This depends on the host's private window layout. [List placement, scrolling, resizing and sorting](docs/panel-overlay.md#moving-the-list-down) · [Keyboard and focus](docs/panel-overlay.md#typing-and-focus)

Prefer an **activatable modeless popup** for the editor in that strip. An edit embedded in the panel leaves Samandarin's file shortcuts active during typing. With the popup active, the host can skip those shortcuts, and its native header should still sort on the first complete click. On the normal header path the user can then continue typing the query. This prediction still needs a runtime check in the main and detached frames. [Editor comparison and first-click behavior](docs/panel-overlay.md#child-control-or-popup)

| Approach | Where the user types | What appears in the panel | When to choose it |
| --- | --- | --- | --- |
| Salamatrix extension on stock Samandarin | A native modal dialog | A bounded virtual filesystem listing with columns and extension actions | A submitted search is acceptable and a script-based extension is preferred. |
| Native plugin that moves the list on stock Samandarin | A dedicated strip above the original column headings | A smaller live results list with its own header and scrollbars | The editor and the sortable header must remain accessible together, and depending on private host layout is acceptable. |
| Native plugin with an overlay on stock Samandarin | A modeless window over the parent row or the visible column header | A live virtual listing, with the first match visible under the editor | Use the parent row while editing at the top, or the header during scrolling. Positioning depends on the host's private layout. |
| Native filesystem plugin on stock Samandarin | A plugin-owned window, or the shared command line for submitted queries | A virtual listing backed by the Everything SDK | A separate input window is acceptable. A modeless window could drive live results while it remains open. |
| Native plugin with a host change | A fixed editor in its own strip inside the active panel | Live results in a plugin filesystem, or host-owned disk rows if that integration is added | Choose this for a supported layout that keeps the field, column headers and scrolled results usable together. |

**A native filesystem plugin can handle searching, opening a match and showing its containing folder with either editor design.** It could also implement F5/F6 itself. To reuse Samandarin's disk-panel handling of selection, copy/move and shell actions, the host would need to accept external results by full path. Branch View provides a model for that work. I found no public API that imports an Everything result set into it. [Panel and file-operation details](docs/samandarin-integration.md#what-branch-view-already-solves)

The proposed default searches the user's full Everything index across folders and drives. This is a design assumption. Everything SDK v2 is the recommended first backend for compatibility with full Everything 1.4.1 and compatible 1.5 installations. A query can run and return a bounded set of matches while the user continues typing. [Everything integration and verification scenarios](docs/everything-integration.md)

The review covers the released Samandarin 0.16 and source on the 0.17 branch at commit `37e9544`. Conclusions come from source and API documentation. End-to-end keyboard behavior and latency still need measurement in a future prototype. [0.16 release](https://github.com/KRtkovo-eu-AI/salamander/releases/tag/5.0-samandarin-0.16) · [Reviewed 0.17 revision](https://github.com/KRtkovo-eu-AI/salamander/commit/37e954410f65d2aa3009028cccd9e99c8505efbc)
