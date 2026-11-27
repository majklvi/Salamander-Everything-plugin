# Result identity regression

This test runs the production filesystem and listing adapters with a simulated panel and search replies. Two rows named `report.pdf` belong to different folders. The test reverses their order, simulates the host restoring a mark by basename, and checks that the selected path and caret return to the original item. It also covers removing one namesake and replacing a directory with a file at the same path.

The simulated detailed view calls the same column callbacks that Samandarin uses to draw cells. It checks the default columns, all ten optional fields, local timestamps, missing and zero values, and the host's per-row size and modification-time callbacks. A page containing only hidden and system items verifies that displaying their attributes preserves both indexed results. The test also changes the visible fields during a pending search and opens a new search with every optional field hidden.

Use the x64 MSVC toolchain and the pinned Samandarin headers from the [build guide](../../docs/build-and-test.md#build). Run these commands from the repository root.

```powershell
$sdk = (Resolve-Path 'out/samandarin/src/plugins/shared').Path
cmake -S tests/result-identity -B out/result-identity -G "Visual Studio 18 2026" -A x64 "-DSAMANDARIN_SDK_DIR:PATH=$sdk"
cmake --build out/result-identity --config Release --target result_identity
ctest --test-dir out/result-identity -C Release -V
```

The executable prints each check and CTest reports the overall pass or failure.
