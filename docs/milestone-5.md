# Milestone 5 — distribution and release checks

The x64 package targets stock Open Samandarin 0.16 on Windows. Extract the ZIP next to salamand.exe and follow the [installation instructions](INSTALL.txt). Build the Release target with a help compiler configured, then run:

```powershell
cpack --config out/build/CPackConfig.cmake -C Release -B out/release
```

The ZIP contains the SPL, the English CHM, the English installation guide and both Everything SDK license texts. It includes no host configuration files. The fixed search field requires Detailed view with column headings visible.

## Installation and lifecycle

- **Clean installation.** Plugins and Extensions Manager initially displayed zero installed plugins. Adding the extracted SPL showed Everything Search 0.5.0, Loaded Yes. Ctrl+Shift+E opened the search. The automatic source used Everything 1.4.1.1032 and returned the two expected files for the query below. F1 in the editor opened the English Getting started topic and the five-topic Contents tree.
- **Unload.** After leaving the search and closing its Help viewer, Unload and Save Configuration changed Loaded to No (zero loaded plugins). An exclusive read/write open of the 0.5.0 SPL succeeded while Samandarin was running.
- **Upgrade.** A separate portable profile started with 0.4.2 loaded. After leaving both search panels, unloading and saving, extraction replaced the files in place. Test in Plugins Manager loaded 0.5.0 using the existing registration. Settings retained Everything 1.5, Size, Extension, Date modified and the limit of four. Querying and F1 both worked.
- **Help file in use.** On a second extraction Windows reported the CHM in use after the viewer had closed. The final install guide explains how to complete that update after exiting the affected host. Only INSTALL.txt changed between the two archives. The SPL, CHM and license files were byte-identical. A separate 0.4.2 profile received the final ZIP with the host stopped. All five extracted files matched, and the resulting host displayed four of seven matches for exact:salamand.exe.
- **Runtime prerequisite.** With the named portable 1.4 test instance stopped, a real host explicitly configured for 1.4 detected the installed executable and asked the user to start it with IPC enabled. The separate 1.5 instance stayed running. The test instance was then restarted. Both versions are installed on this machine. The no-installation branch was verified separately by exercising the production first-open check with empty installation lookup results and no running endpoints. That regression checks the actual official download URL, link label and one-time display. An installation-free Windows desktop was not used.
- **Automated regressions.** The result identity, SDK metadata and missing-source tests all passed.

## Search response

Both indexes were ready before testing. Each first sample was the first nonempty search after loading the plugin in a new host process. Each repeat cleared the query before pressing Q again. The same single-key query, q, makes the last key unambiguous. Everything 1.4 used the named portable fixture; Everything 1.5.0.1424b used the workstation index. Both tests used ascending name order and the 100 ms debounce.

| Everything | Indexed entries (query *) | Matches for q | Timing page limit | First (ms) | Five repeats (ms) | Repeat median (ms) |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| 1.4.1.1032 | 5 | 2 | 4 | 418 | 113, 127, 127, 144, 186 | 127 |
| 1.5.0.1424b | 3,127 | 37 | 4 | 140 | 135, 127, 133, 127, 121 | 127 |

A separate Release timing build used QueryPerformanceCounter in the editor and the host panel subclass. It started on Q key-down (or the edit notification if the input path supplied text directly). After the current generation had usable rows, the probe timed completion of the native list paint. On the first ready editor sync it requested an immediate redraw if a completed paint had not yet been observed. Values are rounded to milliseconds. This measures application rendering and excludes display scanout. One first sample per source does not establish a general cold-start distribution.

As a cross-check, the uninstrumented packaged binary returned the correct rendered rows in each first screenshot. Upper bounds measured from before sending the key through receipt of that screenshot were 420 ms first and 435, 668, 436 ms repeated for 1.4 (default limit 200), and 469 ms first and 489, 674, 432 ms repeated for 1.5 (saved limit four). These bounds include Computer Use and image capture overhead. The measurements support retaining the initial debounce. Index sizes and page limits here do not characterize performance on a large production index.

## Release identity

| File | SHA-256 |
| --- | --- |
| EverythingSearch-0.5.0-x64.zip | 49F27E95D8F9DD08930273B888500F66F7378AE767886A99327E1DDDFB3BF961 |
| EverythingSearch.spl | 3886869639CDE8049D9522B6B1C2455B00470A65979AB71E527E33963713D05C |
| EverythingSearch.chm | 3052241F3BDAF405B98D6DE14E6251D928F5B30FDFB1CAD548D52D4B726D4A27 |
