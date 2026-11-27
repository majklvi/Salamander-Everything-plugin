# Everything SDK v2

The bundled client comes from voidtools' SDK archive. The archive SHA-256 was `F5716D9513CCE6B462B5170A0A2E7E081E191D9BCAC6774F5DECC061497DF443`. This directory contains the C implementation, public API header and IPC header. The CMake build links them privately into the plugin and embeds the license as a Windows resource. [SDK overview](https://www.voidtools.com/support/everything/sdk/) · [Upstream archive](https://www.voidtools.com/Everything-SDK.zip) · [License](LICENSE.txt)

The public and IPC headers are the upstream files. In the C implementation, the plugin build removes the DLL export decoration, uses the packaged casing of `everything_ipc.h` and bounds sends to the external process with `SendMessageTimeout`. A send can wait up to 1500 ms. The readiness probe distinguishes a successful reply reporting an unloaded index from a failed send. The plugin's worker handles these calls.
