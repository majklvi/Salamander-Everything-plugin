# Milestone 2 — Detached panels and keyboard navigation

In stock Open Samandarin 0.16 x64, one click on **Name** sorted the results in both an individually torn-off tab and the detached right panel. In each window the editor still had focus immediately after the click, and the next character extended the query. Everything Search 0.2.0 uses the same three sample entries as milestone 1. [Build and installation](build-and-test.md#build)

Each search keeps its query in its live filesystem instance. An individually torn-off tab leaves its original side's tab array, and its filesystem and list survive the move. Detaching the whole right panel keeps its tabs on the right side and marks that side as detached. The plugin checks the SDK's active filesystem, side and frame together before moving the list or placing the editor. [Tab snapshot flags](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/zip.cpp#L1139-L1177) · [Host frame selection](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/mainwnd.h#L1032-L1058) · [Moving an individual tab](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/mainwnd1.cpp#L3830-L3875)

## Build and test environment

Tests used a portable copy of stock Open Samandarin 0.16 x64 with its own profile on November 27, 2026. The host executable's SHA-256 was `EDB9DDFC172571536B4DA2FE5577F188885F4CB8E5892CB645552669A68AB6C7`. The x64 Release plugin was built with MSVC 19.51, CMake 4.4.3, Windows SDK 10.0.26100 and Samandarin SDK revision `7382688b9ba1c9471a27b19f36beba65825b2940`. Its SHA-256 was `06668569FE23B4E629A1E5DC79331AFBAB0966C63C4FB7E044D16F52EDFC250D`. PE inspection confirmed AMD64 and the three required SDK exports. [Host release](https://github.com/KRtkovo-eu-AI/salamander/releases/tag/5.0-samandarin-0.16)

Windows reported one 2370 × 1200 display at 96 × 96 effective DPI. The main frame, torn-off tab and detached right panel each reported 96 DPI and `PER_MONITOR_AWARE_V2`. A move between physical monitors and a live change of monitor DPI remain unverified. The plugin recalculates its row height from the current heading and the host's Detailed-view font and icon sizing rules whenever it handles a DPI or layout change. [Host header metrics](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/filesbx1.cpp#L2810) · [Host row metrics](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/fileswn2.cpp#L5429)

## Torn-off tab

**Passed on the Release build.** An unlocked results tab containing `invoic` was torn out of the detached right panel. A third top-level window opened with the same query and both files, in the order `invoice-a.pdf`, `invoice-b.pdf`. With the edit focused, one left click on **Name** put `invoice-b.pdf` first. A focus read immediately after the click still identified the edit containing `invoic`. Typing `e` there produced `invoice` and preserved B–A order. There was no intervening click or command.

On build `6F712026`, dragging the torn window's title bar moved its screen origin from (979, 26) to (1049, 51). The popup moved by the same 70 pixels horizontally and 25 vertically. Its offset within the frame stayed at (1, 54), its size stayed 845 × 19, and the query and rows remained visible.

## Detached right panel

**Passed on the Release build.** The right panel was in its own window. With `invoic` in the focused edit, the panel listed `invoice-b.pdf` followed by `invoice-a.pdf`. One left click on **Name** put `invoice-a.pdf` first. Before the next key was sent, the focused control was still the edit and its value was still `invoic`. Typing `e` changed it to `invoice`. Both files remained visible in A–B order.

## Navigation and layout in the host

The following observations were recorded on the preceding 0.2.0 build, SHA-256 `336CC9A7A416CD8AF8B42088EFD2C8DD87C7165686C26ABB62C06F87BB9BAD2F`.

| Action | Observation |
| --- | --- |
| Open a restored search | On host startup, the left search tab displayed its editor without invoking **Open search**. Entering `note` returned only the `notes` folder. |
| Switch panels with Tab | From the torn-off editor containing `invoice`, one Tab activated the main left results list. Its field still read `note` and its sole result was `notes`. Keyboard focus was in that list. The inactive torn tab returned the editor strip to its native list. |
| Leave the editor with Esc | Esc moved focus from the editor to the host list (control ID 950) and preserved `invoice`. |
| Down or Enter as the query changes | Starting from `note`, typing `x` immediately followed by Down left `notex` focused in the editor with the empty-list message visible. Enter followed by `x` produced `notexx` there. Starting from `notex`, Backspace immediately followed by Down focused the list and selected its sole match, `notes`. |
| Maximize and restore | The main frame measured 916 × 836, then 2370 × 1152, then 916 × 836 pixels. The editor measured 912 × 19, 2368 × 19 and 912 × 19 respectively. The native heading and the sole result remained fully visible. |
| Drag a column divider | In the detached right panel, the host's **Automatic Column Width** setting initially prevented resizing. After turning it off, dragging the Ext/Attr divider from x = 102 to x = 229 moved Attr about 126 pixels. A–B order stayed unchanged, the editor kept focus, and the next `x` produced `invoicex` with an empty list. |
| Close a torn-off search | Choosing **Close Tab** when closing the torn window removed that window. The main frame and the detached right panel remained. The other right-side search still contained `invoice` and both result files. |

On build `6F712026`, **Reattach Left** moved the torn search back as a left tab alongside the independent `note` search. The reattached tab kept `invoice` and both files. Closing both unlocked search tabs restored the disk header at y = 178 with no gap. Returning the detached right panel to a local drive restored its disk header at y = 180. Plugins Manager then changed Everything Search from **Loaded: Yes** to **Loaded: No**, disabled **Unload**, and the host stayed open.

## Activating an inactive torn-off tab

The host can focus a result on mouse-up. The plugin watches native list-focus changes as well as foreground changes, then checks the active filesystem again. When a mouse button is down, a button message is queued or a native control holds capture, the plugin waits before moving the newly active list. A header click while the editor already has focus uses the existing geometry. [Host list activation](https://github.com/KRtkovo-eu-AI/salamander/blob/7382688b9ba1c9471a27b19f36beba65825b2940/src/fileswn9.cpp#L894-L920)

On the Release build, Tab switched from the torn tab to the main window, where `note` returned the folder `notes`. Returning to the torn window restored its `invoice` field automatically and left focus in the native results. Name remained sorted in descending order.

The desktop test driver activates its target window before sending a click. The point (31, 108) covered the second file, `invoice-a.pdf`, in the inactive layout. After activation restored the strip, the click selected the first file, `invoice-b.pdf`, at that point. The live test therefore leaves the target of a single physical click **from an inactive window** unverified. In six separate tests with simulated Win32 input state, the Release binding code kept the header and row in place through a held left or right button, native capture or a queued button message. It restored the strip on release, restored it immediately on idle activation, and discarded a pending rebind when the source changed.

## Scrolling with a longer listing

The three shipping samples fit in the panel without scrolling. A separate build of the final source added 80 files named `scroll-001.txt` through `scroll-080.txt` (plugin SHA-256 `5CAAB4C76E4633C7B1AF1DD63E2C1745974F1C6A8E20E6334F389E6EA1C25176`). In the detached right panel, a mouse-wheel scroll moved the first visible entry from `scroll-001.txt` to `scroll-016.txt` and the last visible entry from `scroll-030.txt` to `scroll-045.txt`. The focused field still contained `scroll`. Its screen rectangle stayed at (980, 196), 845 × 19 pixels. Within the detached window, the Name header stayed at y = 199 and the top row at y = 218. The test copy was then returned to the three-sample Release binary.

## Deferred keyboard navigation

A separate harness compiled the plugin's editor and filesystem sources against the same SDK and controlled when the host made the refreshed rows available. All 15 checks passed. For both Down and Enter, it exercised an early notification while the previous rows were still present, replacement by an empty list, replacement by one file, immediate entry into a current list, and cancellation by new text, focus loss, Escape or Tab. Removing the binding also cancelled a pending handoff. The harness simulated the host and focus calls. The table above records the corresponding keystrokes exercised in the real host.

## Reproduce the first-click test

1. In Detailed view, keep the Name heading visible. For a torn-off tab, open an **unlocked second tab**, run **Everything Search → Open search**, then detach that tab through its context menu. For the whole right panel, use **Right → Detach into separate window** and open the search on that side.
2. Focus the search field and type `invoic`. Both invoice files should appear. Record their order.
3. Press and release the left mouse button **once** on Name. Observe the order and keyboard focus before sending another key. Type the single character `e` without clicking the editor or otherwise reactivating it.
4. Check the new field value, both result names and their order. A successful sort and continued keyboard input are separate observations.
