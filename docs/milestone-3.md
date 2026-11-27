# Milestone 3 — Live Everything queries

Everything Search 0.3.0 queries the default Everything index from Open Samandarin. In the two-panel test, the left panel kept its single `salamand.exe` result and the right panel kept its single `salmon.exe` result after the plugin received an older 42-row snapshot for the left panel.

## Rapid typing in Samandarin

Tests ran on November 27, 2026, in an isolated portable Open Samandarin 0.16 x64 using the user's running Everything 1.5.0.1424b default instance. The indexed path component `"\5.0-samandarin-0.2\"` selected a software distribution present in this index. It is independent of the version of the running host. An independent read-only query through the public SDK supplied the expected counts.

On the final Release build, scoped `ext:exe` returned `fcremote.exe`, `salamand.exe` and `salmon.exe` in the right panel. The SDK reported three distinct parent folders. After switching left, the tester entered the same scope and `exact:sal`, then typed the remaining `amand.exe` one key at a time in quick succession. Only `salamand.exe` appeared on the left. Its editor kept focus and the right retained its three executables. Switching back with Tab and querying `ext:pdf invoice` left the single left-side result intact.

The path restriction alone matched 714 entries. Selecting all displayed entries counted 89 files and 111 directories, exactly the first page of 200. The index contained no PDFs and `ext:pdf invoice` correctly showed **No matches.** The preceding uninstrumented candidate also handled three rapid query replacements (`ext:spl`, `exact:salmon.exe`, `exact:salamand.exe`) and displayed **Type an Everything query.** after clearing, with the other panel's results intact.

## Switching panels with an older reply arriving last

The SDK client serializes requests. To exercise a guaranteed late delivery, a separate host-test build held a **real, already copied SDK snapshot** just before the normal filesystem callback. The worker and index continued processing newer searches. When the right panel's result arrived, the test delivered it first and then passed the held snapshot to the same production callback. The gate changed only the delivery order. [Host test gate](../tests/late-reply/reply_gate.cpp)

| Delivery order | Instance and generation | Real SDK payload | Observation |
| --- | --- | --- | --- |
| 1. Hold | Left 1, generation 1 | 42 × `english.slg` | The old snapshot was available before the left query changed. |
| 2. Deliver current | Left 1, generation 2 | 1 × `salamand.exe` | The single new file appeared on the left while the right was idle. |
| 3. Deliver other panel | Right 2, generation 1 | 1 × `salmon.exe` | The right search received its own file. |
| 4. Deliver held | Left 1, generation 1 | 42 × `english.slg` | Both panels retained their one correct file. Keyboard focus remained in the right edit with its `exact:salmon.exe` query. |

The four trace records appeared at monotonic clock values 323887921, 323898203, 323915046 and 323915046 ms. The last two calls ran sequentially on the UI thread within the same millisecond. All four records used name ascending, offset 0 and limit 200. The gate is compiled only by the separate test project.

**Rapid edits and a panel switch in the same run.** With another 42-row left reply held at generation 5, the tester entered scoped `ext:spl` and immediately replaced it with `exact:salamand.exe`. Generation 7 supplied the single left row. Switching panels and querying `salmon.exe` delivered right generation 3, followed by the held left generation 5. The left still showed only `salamand.exe`, the right still showed only `salmon.exe`, and the right edit retained focus.

**Clear while a reply is pending.** A separate run held left generation 3, cleared the left edit, and delivered right generation 2 followed by the old left reply. The left kept the empty-query prompt and the right showed only `salmon.exe`.

**Close the owning tab.** A duplicate left tab received instance ID 3. After holding its 42-row reply, the tester closed that tab with Ctrl+Shift+W. The original left tab reappeared. Delivering right generation 7 and then the closed tab's generation 1 left the original left tab on `salamand.exe` and the right tab on `salmon.exe`, with focus in the right edit.

## Index readiness and retry in Samandarin

The separate host-test build can override just the two SDK calls that report database readiness and its error code. With the same right-side `exact:salamand.exe` query, the first check displayed **Everything is loading its index. Run Open search to retry.** After changing the readiness response and invoking **Open search** again, the panel displayed **Everything is unavailable. Run Open search to retry.** Restoring the real probe and invoking the same command returned the actual `salamand.exe` row. The text stayed unchanged, the edit kept focus, and the left panel kept its row.

To reproduce these two states without interrupting a running index, put the single ASCII word `loading`, then `unavailable`, in `m3-readiness.txt` beside the **test** plugin. Remove that file before the final retry. The Release plugin always calls the real SDK. [Test readiness probe](../tests/late-reply/index_state.cpp)

## SDK reply IDs and filesystem regression checks

Six worker checks used a private IPC peer with its own window class. The worker and SDK client handled an absent peer, a loading database and a valid empty response as three different outcomes. A reply with the wrong ID returned **FALSE** and only the matching reply's `correct.txt` reached the caller. Withholding a reply triggered the 15-second timeout after 15,125 ms. Delivering that expired ID during the next request also returned **FALSE**. The next request completed with just `correct.txt`. These checks exercised transport-level timing separately from the live-index test, which reordered snapshots after transport.

Fifteen adapter checks compiled the production filesystem with a simulated host and backend. They covered generation, sort, offset and limit mismatches, updates to a background panel, clearing a query and closing and reopening a filesystem. They also exercised the pending, loading, unavailable and valid-empty states, retrying the same text, and listing a file and a directory after recovery.

With the real default index, a separate message-loop test of the worker returned 200 of 3,108 entries for `*` in 109 ms and 29 of 29 for `ext:exe` in 109 ms. These two single-run timings include the 100 ms debounce and stop when the owned snapshot arrives. Painting the rows in Samandarin is a separate step.

## How a reply reaches a panel

- Each live filesystem has an instance ID. Typing, clearing or retrying advances its generation. An edit invalidates its previous rows immediately. A closed and reopened tab receives a new ID.
- One worker owns the SDK state. It coalesces edits per instance for 100 ms, submits one asynchronous `Everything_QueryW(FALSE)` at a time, and identifies the expected reply by its IPC reply ID. It copies filenames, full UTF-16 paths, file or folder types and the total count before reusing the SDK. [Official query API](https://www.voidtools.com/support/everything/sdk/everything_query/) · [Reply identification](https://www.voidtools.com/support/everything/sdk/everything_isqueryreply/)
- On the host UI thread, the receiving filesystem must still exist, must still be waiting, and must match the snapshot's instance, generation, sort, offset and limit. An accepted reply refreshes only that filesystem. Switching focus does not change the reply's owner. [Filesystem receiver](../src/filesystem.cpp) · [Worker](../src/search.cpp)
- The current page has up to 200 entries. Native header sorting applies to those loaded rows. The snapshot retains full Unicode paths and the total count. The stock 0.16 listing interface displays names through the Windows ANSI code page. Parent-folder display, full-path actions and ordering across pages are scheduled for milestone 4. [Development plan](development-plan.md)

## Reproduce the forced delivery

1. Choose three distinguishable basenames in your default index. Confirm each query returns only that basename within your chosen scope. Put copies in separate indexed folders if you want the old query to have multiple matches.
2. Configure the test project using the same pinned Samandarin SDK headers as the Release build. The names below reproduce the recorded run. [Build prerequisites](build-and-test.md#build)

   ```powershell
   $sdk = (Resolve-Path 'out/samandarin/src/plugins/shared').Path
   cmake -S tests/late-reply -B out/m3-reply-order -G "Visual Studio 18 2026" -A x64 `
     "-DSAMANDARIN_SDK_DIR:PATH=$sdk" '-DHELD_NAME=english.slg' `
     '-DCURRENT_NAME=salamand.exe' '-DRELEASE_NAME=salmon.exe'
   cmake --build out/m3-reply-order --config Release --target EverythingSearch --parallel 1
   ```

3. Load the test build from `out/m3-reply-order/Release/EverythingSearch.spl` in an isolated host. Open search on both sides. Search for the held name on the left, wait for the `hold` record, then replace it with the current name and observe the new row. Switch to the right and search for the release name.
4. Inspect both panels after `deliver-held`. The tab-separated `m3-reply-order.tsv` beside the test plugin records the tick, action, query label, instance ID, generation, sort, offset, limit, returned count and total. For another release event, change the right query and enter the release name again. Return to the normal Release plugin after testing.

## Build identification

| Artifact | SHA-256 |
| --- | --- |
| Everything Search 0.3.0 x64 Release | `2BE975D2EABD3D071673F588F92BFDA9998F36DB0FEA087A05BB6C354A855A04` |
| Separate host-test build with delivery gate | `F7E4BBECCF22F3902A8C651A1BAC83CE48E6481CF76D79341685BFFB84D5698E` |
| Open Samandarin 0.16 x64 executable | `EDB9DDFC172571536B4DA2FE5577F188885F4CB8E5892CB645552669A68AB6C7` |

The build used MSVC 19.51, CMake 4.4.3, Windows SDK 10.0.26100 and the native Samandarin SDK at `7382688b9ba1c9471a27b19f36beba65825b2940`. PE inspection confirmed the three plugin exports, system DLL imports and the embedded Everything SDK license. The official SDK source and local IPC timeout adjustments are documented with the bundled client. [SDK provenance and license](../third_party/everything-sdk-v2/README.md)

The earlier uninstrumented candidate was `AA2CC8D812D9211DDBF0717614C37FD39ADD71D6C2F7D81C2E18C84983CE856F`. The final Release incorporated the additional SDK header copyright notice. Both uninstrumented builds were loaded in Samandarin.
