# Placing a plugin-owned search editor in a Samandarin panel

**A native plugin could move the entire results list down by one row on an unmodified Samandarin.** That would give the editor a fixed strip above the original column header, leaving even a single match visible below it. The public filesystem SDK and Salamatrix expose no way to reserve that space. The design depends on private Win32 window layout observed in release 0.16 and the reviewed 0.17 source. It has not been tested in a running host. [Public view interface][view-api] · [Salamatrix UI limits](samandarin-integration.md#salamatrix-on-an-unmodified-host) · [0.16 panel layout][panel-size16] · [0.17 panel layout][panel-size]

## Moving the list down

In Detailed view with column headings enabled, the panel would read from top to bottom as **path bar → search editor → original column headings → results → status line**. The plugin would lower the top edge of the file-list window by the height of the editor and keep its bottom edge fixed. The header and scrollbars belong to the list and move with it. The editor could be a sibling window under the same panel or an activatable popup aligned with the empty strip. A flat virtual results folder needs no placeholder entry. Even with zero hits the editor has a place, and one hit fits as soon as there is room for one data row below the header. [File-list window][list] · [Header creation][header-create] · [List layout][layout]

### Scrolling

The editor would stay in place during vertical and horizontal scrolling. In Detailed view the host scrolls only the item rectangle (`FilesRect`) when the top item changes. Horizontal scrolling shifts the row contents and the header labels together, keeping the columns aligned. Neither operation reaches an editor outside the list. Wheel input over the header is forwarded to the list. Wheel input over the new editor has its own target and would need a separate behavior decision. [Vertical scrolling][scroll] · [Keyboard navigation][ensure-visible] · [Horizontal scrolling][horizontal] · [Wheel over the header][header-wheel]

### Resizing the panel

A one-time move would last only until the host next lays out the panel. Resizing the frame, dragging the panel splitter or changing DPI can make Samandarin assign the list its full rectangle again. A detached tab also explicitly triggers panel layout. The plugin would need to take each fresh rectangle assigned by the host, preserve its width and bottom edge, and apply the offset **once**. Reusing the already shortened rectangle would progressively shrink the list. The strip height should follow the current panel font and DPI. When there is too little height for the field, header and one result, the proposed behavior is to collapse the field. [Panel sizing and DPI][panel-size] · [Splitter layout][splitter] · [Detached-tab layout][detached-layout] · [Detailed row height][row-height]

One candidate is to observe the private list window on its owning UI thread and adjust the pending geometry in `WM_WINDOWPOSCHANGING`. The host would still process the resulting size normally and recalculate the visible rows and scrollbars. The plugin would ignore changes that only affect visibility or stacking order. A sibling editor moves with its parent. A top-level popup also needs to track moves of the frame, even at constant size. When leaving the search tab or unloading, release the window binding and restore the list to the host's **current** layout. [Win32 position changes](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-windowposchanging) · [Win32 size notification](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-windowposchanged) · [Window subclassing](https://learn.microsoft.com/en-us/windows/win32/api/commctrl/nf-commctrl-setwindowsubclass) · [List resize handler][list-size] · [Visible rows and scrollbars][page-size]

### Sorting from the header

The original header would remain visible and clickable. It handles its own mouse coordinates, captures a drag on a column divider, and dispatches a click to the standard or plugin-column sort handler. Clicking Name twice should still reverse the order. The editor must leave the header's entire hit area clear and let the user click or drag immediately after typing. Its query can stay visible while the host handles the click. A result refresh should leave focus where the user put it. This applies to Detailed view with the header enabled. If Everything returns only a bounded page, the host can sort the loaded rows. Ordering the full set requires requesting results from Everything in the chosen order where supported. [Header mouse handling][header-click] · [Sort dispatch][header-sort] · [Everything paging and ordering](everything-integration.md)

## Child control or popup?

**For the fixed strip on stock Samandarin, prefer a borderless, activatable modeless popup with a native edit control.** Either design should allow the first click on a sortable heading to sort. The deciding factor is whether the user can edit a query without invoking file commands. Keep the popup on the host UI thread, owned by the frame displaying the results. Leave that frame enabled and confine the popup to the strip. [Host message loop][pump17] · [Win32 modeless dialogs][modeless] · [First click on a column heading](#first-click-on-a-column-heading)

| Design in the fixed strip | Focus while typing | Practical consequence |
| --- | --- | --- |
| Panel child, alongside the list | The edit has the caret and receives ordinary text. The host frame stays active. Its message loop can translate Delete, keypad `+`, F5 or F8 into panel commands before the edit sees the key. | The child follows the panel naturally. Subclassing the edit cannot recover a key already translated by the frame. The reviewed SDK exposes no per-control exemption. [Shortcut tables][accelerators] · [Accelerator translation][win-accelerators] |
| Owned, activatable popup | The popup becomes the active top-level window and its edit holds keyboard focus. The host skips its frame accelerators when its caption is inactive and the active window is outside its registered detached-tab frames. That lookup compares window handles directly. | Keep the edit alive across result updates so its caret, selection and composition survive. Track the popup's bounds and visibility with the results tab. The design relies on actual activation, so a `WS_EX_NOACTIVATE` popup is unsuitable. [Release 0.16 message loop][pump16] · [0.17 message loop][pump17] · [Detached-frame lookup][detached-lookup] · [Win32 window styles][ex-styles] |

Samandarin gives its built-in Quick Rename editor a specific exemption. The host checks its own Quick Rename windows, and its command-line edit mode also uses a recognized host control. Giving a plugin child keyboard focus does not register it for either behavior. For the popup, verify both that its edit has focus and that the host actually sees its frame as inactive. The main frame records this on `WM_NCACTIVATE`. [Quick Rename placement][rename] · [Quick Rename recognition][quick-rename-check] · [Main caption state][caption-flag] · [Host message loop][pump17]

### First click on a column heading

With the **panel child**, typing already has the results frame active. A press inside a sortable heading goes straight to the native header. With the **popup** active, Windows first asks the clicked window how to handle mouse activation. The header delegates that question to the default procedure, which passes it to the parent file list. For an ordinary left press in the client area, with the UI unlocked and the main window unminimized, the list returns `MA_NOACTIVATE`. Windows delivers the original press and leaves the popup active on this path. The list and header code is identical in the two reviewed revisions. [Header default handling][header-default] · [List mouse activation in 0.16][list-activate16] · [List mouse activation in 0.17][list-activate17] · [Win32 mouse activation][mouse-activate]

The header captures the mouse on button-down. Releasing over the same sortable heading while it still has capture triggers **one sort on that first click**. A divider starts a width drag. The header does not request keyboard focus, so the next character can continue the query in either design. With the popup, keep the owner enabled, leave the header exposed, and let it handle the original click. Replaying the click could reverse the sort a second time. [Header press][header-click] · [Header release and sort][header-sort] · [Capture loss][header-capture-loss] · [Row caret after sorting][sort-caret]

This is the expected path from source. If activation reaches a detached frame, its handler can activate the panel and preserve the click. If the main frame actually regains activation, it can restore focus to its list or command line. Preserve the query in either case and respect the resulting focus. A later Everything reply should never move it back to the editor. A divider drag that leaves the header also merits a separate check because Win32 limits mouse capture for background windows. [Detached tab activation][detached-click] · [Detached panel activation][detached-side-click] · [Main-frame focus restoration][restore] · [Win32 mouse capture][capture]

## Typing and focus

**Start editing through a plugin command or an explicit click.** Capture the result tab and its filesystem instance on the host UI thread. Activate the intended tab if needed, verify that it is the source, then give the plugin's edit keyboard focus. This matters when clicking an overlay on the inactive side. An ordinary click on a file runs Samandarin's panel-activation logic. A click delivered to the overlay can bypass it. While the file list has focus, typing should retain Samandarin's ordinary Quick Search behavior. [Panel mouse handling][mouse] · [SDK tab activation][activate] · [Win32 keyboard focus](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setfocus)

| Input or transition | Proposed behavior |
| --- | --- |
| Letters, Ctrl+C / Ctrl+V, Backspace, selection and caret keys | Let the native edit handle text and clipboard operations. Its plugin-owned parent receives `EN_CHANGE` and reads the entire value after edits, including paste and replacement selections. For a child overlay, a thin plugin-owned parent is important because the host list handles its own edit notifications. Preserve IME composition, including Enter or Escape used by the IME. [Edit notifications](https://learn.microsoft.com/en-us/windows/win32/controls/en-change) · [Host notification handler][notifications] |
| Delete, numpad operators, F5/F8 | In a direct child edit, Delete can map to file deletion, numpad `+` to panel selection, F5 to file copy and F8 to file deletion. The enabled host command and current selection determine the effect. In the popup, Delete should edit text, the numpad should enter query characters, and file commands should only run once the user returns to the results. [Host shortcut tables][accelerators] |
| Down or Enter in the editor | When current matches are available, focus the first real match in the original results tab. Dismiss an editor covering a row or header. A field in its own strip can remain visible. Consume this first key so moving into the list cannot also open a file. Enter **in the list** then opens the focused result. With no current match, keep the editor available. |
| Escape or click a result | Escape dismisses the editor and returns to its results tab. A click on a visible match should reach normal panel selection. Preserve the query for explicit re-entry. This keeps the caret from jumping back during navigation. |
| Tab or click the opposite panel | Plain Tab normally switches sides in the **list's** key handler. While the popup owns input, dismiss it and invoke the public `ChangePanel()` once on the UI thread. A mouse click on the other panel can follow the host's usual route. Keep that panel active even when an old query finishes. [List Tab handling][tab-key] · [SDK panel switching][sdk-switch] · [Host focus transfer][switch] |

## Where the first row goes

The file list is a custom `SalamanderItemsBox`. The SDK exposes the view mode and top index. I found no public call returning a particular tab's list-window handle, item rectangle or reserved strip. A plugin would have to identify the visible list belonging to its own tab and position the popup in screen coordinates. Detailed view is the clearest initial target. Its first data row starts below an optional header, and row height depends on the current layout. Brief, Icons, Tiles and Thumbnails group items differently. Recalculate or dismiss on splitter movement, resizing, font/DPI changes and view changes. If the target cannot be identified, the same editor can appear as a separate window. [List creation][list] · [View API][view] · [Row geometry][geometry] · [Header and scrollbar layout][layout]

### Reserving the first row in a virtual folder

**Use a real parent row when the results have a parent.** For example, a plugin could expose a search home and a `Results` subfolder. The native filesystem SDK gives a directory entry named `..` special treatment. The host inserts it at index zero, allows only one, and excludes it from selection. When the editor is dismissed, Enter on `..` should navigate back to the search home. Keep this entry out of Everything's match count. With zero hits, explicitly show “No matches” because the host's empty-list check counts the parent row. [Native SDK contract][up-sdk] · [Insertion and duplicate check][up-insert] · [Selection][up-select] · [Parent navigation][up-enter] · [Empty-list check][empty]

| Entry used beneath the editor | Effect of changing the sort |
| --- | --- |
| Genuine `..` directory | Standard sorts skip index zero, including reverse Name, Size or Time. Sortable plugin columns such as Parent folder also skip it. Either a file or a folder can be the sole match on the second row. [Standard sorting][sort-standard] · [Plugin-column sorting][sort-custom] |
| Ordinary placeholder file or directory | The host sorts after reading the listing. Insertion order and a name such as `! Search` cannot guarantee first place. A dummy directory can precede all hits when it is the only directory and every hit is a file. A folder hit can compete with it. The dummy also participates in selection and file commands unless the plugin handles it explicitly. [Listing and sort][empty] · [Directories and files][sort-standard] · [Selection][up-select] |

The 0.16 release also excludes the parent entry from both standard and custom-column sorting. In Salamatrix, the framework adds `..` itself for nonempty virtual paths and rejects a script-supplied item with that name. The overlay still needs its own native editor. [Release 0.16 sorting][sort16] · [Salamatrix parent entry][smx-up] · [Item validation][smx-name]

### What scrolling does

In Detailed view, `TopIndex` identifies the item displayed in the top data row. With `..` and one match, the two fit together if the panel has room for two complete data rows. The host then clamps the scroll position to the beginning. A longer listing makes the difference visible. For example, suppose two rows fit and the result files are `A`, `B` and `C`. [Row positioning][geometry] · [Scroll limits][bounds]

| Action | Rows drawn by the host, top to bottom | What the user sees with the editor fixed over the top row |
| --- | --- | --- |
| Sort by Name ascending at the top | `..`, `A` | Editor, `A` |
| Reverse Name order at the top | `..`, `C` | Editor, `C` |
| Scroll down one row in that reverse order | `C`, `B` | Editor, `B`. The top match `C` is covered. |

The wheel, scrollbar and keyboard navigation can all move `TopIndex`. The host paints, hit-tests and keeps the focused item visible in that same scrolling area. For the parent-row design, dismiss the popup when `..` leaves view and preserve the query. An editor that follows the item would scroll away with it. Forcing the list back to zero after each scroll would prevent normal browsing. [Scroll handling][scroll] · [Keyboard scrolling][ensure-visible] · [Mouse hit testing][hit]

### Keeping the editor visible throughout scrolling

- **On stock Samandarin, temporarily cover the visible column header in Detailed view.** The host already puts this strip above the scrolling area. Every match remains visible in its normal row, including the sole hit, and sorting still applies to the results. Dismiss the editor to click or resize a covered column header. This is the simplest plugin-only option when the field must remain visible while browsing. [Header placement][layout]
- **For simultaneous access to the editor and header, give the editor its own strip.** This costs roughly one visible data row and keeps every remaining row accessible. [Moving the list down](#moving-the-list-down)

## Switching tabs and receiving results

- **Switch or swap panels.** Keep query state by filesystem instance and tab ID. A tab being displayed on the left or right does not by itself make it the source. Panel, path and tab notifications report a side and are occasions to recheck the binding. Hide the popup when leaving that tab. When returning from another application or a modal dialog, allow the host to restore focus to its list or command line. Reopen the editor on explicit user intent. [SDK tab identity and flags][tab-info] · [Plugin events][events] · [Host focus restoration][restore]
- **Refresh while typing.** Replace the snapshot only for the matching live tab and query generation, then request `PostRefreshPanelFS` for that filesystem instance. The host posts a refresh to the matching panel. Its inactive-window throttling condition excludes this explicit request, although a suspended refresh can still be deferred. No edit recreation, tab activation or `SetFocus` is needed on each reply. This also preserves the caret and text selection. [Refresh routing][route] · [Host refresh conditions][refresh-conditions]
- **Close or tear off a tab.** A normal tab switch hides the previous panel window. Closing can destroy it before the tab-change notification. Tearing off an individual tab reparents its panel into another frame and removes it from the side's tab collection. Withdraw the popup on an uncertain binding, discard stale replies and reacquire the frame only after validating the same live tab. A missing side entry alone does not establish closure. Release plugin windows and callbacks before unloading the plugin. [Tab visibility and closure][tab-close] · [Torn-off panel][tear] · [Tab lookup and activation][activate]

## What would settle feasibility in a prototype?

| Exercise | Pass condition |
| --- | --- |
| Type `žluťoučký`, paste a path, replace a selection, use AltGr, IME, Delete and numpad operators. Press F5/F8 while editing. | The query matches the committed edit text, and no file command is dispatched. Repeat with focus in the results to check normal panel shortcuts. |
| Search on the right, click the left panel, press Tab, and finish a delayed query. Then test Down, Escape and re-entry with zero or one match. | The intended panel owns the next keystroke. No double switch, accidental open or focus theft occurs. The sole match is visible on handoff. |
| Switch tabs on one side, swap sides, close the search tab, tear off a tab, and try the separately detached right panel. Alt+Tab away and back. | The popup never remains over an unrelated panel and a delayed reply cannot reactivate the old tab. |
| Use a results subfolder with zero hits, one file, one folder, then enough mixed hits to scroll. Reverse Name, Size and a plugin column. Scroll by wheel, scrollbar and Page Down, then return Home. | The match count excludes `..`. At the top the editor leaves the sole match visible when two rows fit. On scrolling, a parent-row editor withdraws before it hides a match. A header editor stays aligned and leaves every visible data row accessible. |
| Move the splitter, toggle the header, change view and move the window between monitors with different DPI. | The editor follows its chosen strip or withdraws, and the original panel layout is restored on exit. |
| With a separate strip, try zero hits, one hit and a long listing. Scroll by wheel, Page Down and scrollbar thumb, then scroll horizontally. | A sole hit is visible below the header. On a long list the field stays fixed, the top visible match is fully exposed, and the headings stay aligned with their columns. |
| With each editor design, type a query with several results. Click and release once on Name, observe the order, then type one more character without clicking the edit. Repeat with a sortable Parent folder column in the main frame, a detached tab and the detached right panel. | That single click changes the sort exactly once. Record which window is active, which control has keyboard focus and where the next character goes. On the normal header path it appends to the query. A second, separate click on the same heading reverses the order. |
| Release a delayed Everything reply while editing and again after clicking a result. Drag a column divider immediately after editing, including a drag outside the header. | The reply preserves the caret and selection while editing and leaves focus on the list after the result click. The divider changes width without sorting or leaving capture stuck. |
| Repeatedly resize the panel and drag the splitter, shrink below one usable row, move the frame at constant size, then leave the results tab. | Exactly one strip is reserved when space permits. The field tracks the panel without drift or overlap. Leaving search restores the full current list rectangle. |

[rename]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn5.cpp#L5297-L5315
[caption-flag]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd3.cpp#L11022-L11038
[pump16]: https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/salamdr1.cpp#L5927-L5964
[pump17]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/salamdr1.cpp#L5935-L5971
[mouse]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn9.cpp#L872-L895
[activate]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/zip.cpp#L1199-L1238
[notifications]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L1683-L1691
[accelerators]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/salamand.rc#L67-L148
[tab-key]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn0.cpp#L1333-L1337
[sdk-switch]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_gen.h#L1731-L1743
[switch]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd4.cpp#L1713-L1748
[list]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswnb.cpp#L1197-L1209
[view]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_com.h#L553-L578
[geometry]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L704-L741
[layout]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L2767-L2829
[tab-info]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_gen.h#L1080-L1097
[events]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_base.h#L464-L484
[restore]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd3.cpp#L11041-L11100
[route]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/zip.cpp#L3162-L3205
[refresh-conditions]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswnb.cpp#L703-L727
[tab-close]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd3.cpp#L644-L753
[tear]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd1.cpp#L3854-L3909
[up-sdk]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_com.h#L319-L333
[up-insert]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/zip.cpp#L7310-L7334
[up-select]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn0.cpp#L684-L722
[up-enter]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_fs.h#L844-L854
[empty]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn3.cpp#L2254-L2273
[sort-standard]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn3.cpp#L2544-L2602
[sort-custom]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn3.cpp#L2693-L2742
[bounds]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L2660-L2681
[scroll]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L1290-L1355
[ensure-visible]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L746-L778
[hit]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L2179-L2188
[view-api]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/shared/spl_com.h#L553-L654
[panel-size]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswnb.cpp#L143-L215
[list-size]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L1457-L1466
[panel-size16]: https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/fileswnb.cpp#L110-L182
[header-create]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L2541-L2549
[horizontal]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L1189-L1211
[header-wheel]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx2.cpp#L578-L583
[splitter]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd3.cpp#L10947-L10964
[detached-layout]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd1.cpp#L4052-L4065
[row-height]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn2.cpp#L5850-L5855
[page-size]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L2890-L2917
[header-click]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx2.cpp#L760-L789
[header-sort]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx2.cpp#L793-L829
[modeless]: https://learn.microsoft.com/en-us/windows/win32/dlgbox/about-dialog-boxes#modeless-dialog-boxes
[win-accelerators]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-translateacceleratorw
[ex-styles]: https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles
[mouse-activate]: https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-mouseactivate
[capture]: https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture
[quick-rename-check]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd4.cpp#L2542-L2551
[detached-lookup]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd1.cpp#L3771-L3784
[header-default]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx2.cpp#L837-L841
[list-activate16]: https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/filesbx1.cpp#L1734-L1748
[list-activate17]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx1.cpp#L1734-L1748
[header-capture-loss]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/filesbx2.cpp#L570-L576
[sort-caret]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/fileswn2.cpp#L1633-L1640
[detached-click]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd1.cpp#L4534-L4552
[detached-side-click]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/mainwnd1.cpp#L4770-L4793
[sort16]: https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/fileswn3.cpp#L2516-L2714
[smx-up]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L2244-L2262
[smx-name]: https://github.com/KRtkovo-eu-AI/salamander/blob/37e954410f65d2aa3009028cccd9e99c8505efbc/src/plugins/salamatrix/salamatrix_packages.cpp#L5293-L5299
