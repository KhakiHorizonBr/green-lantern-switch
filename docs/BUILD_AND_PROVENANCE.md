# Reconstructing the review snapshot

ReXGlue base: https://github.com/rexglue/rexglue-sdk.git at
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.

Copy the contents of `sdk-overlay/` onto a checkout of that revision, preserving relative paths.
The overlay is the authoritative review snapshot; applying `patches/rexglue-sdk.patch` alone omits
newly added Switch files. Upstream dependencies are supplied separately. SDK vendored headers
contained in the overlay retain their notices.

The native graphics-system and renderer sources were adapted from the locally supplied
The Simpsons Game NX sources. Game-specific embedded pixel shader replacements are excluded.
The generic ReXGlue translator is used; Green Lantern shader compatibility is unvalidated.
The optional vertex-fetch observer and Vulkan dispatch compatibility edits are in the SDK overlay.
Project8 Switch and its memory/renderer architecture were also studied as references.

References supplied by the project owner:

- https://github.com/R-drg/project8-sw
- https://github.com/R-drg/the-simpsons-game-nx
- https://github.com/danfromtico/mesa-switch
- https://github.com/artslay/KisakCOD_nx

Local build uses devkitA64/libnx, a Mesa/NVK static SDK, and CMake/Ninja. Hardware log 035 reports
Mesa 26.2.3 (git-d4a00ea0ab), NVK GM20B, Vulkan instance/device API 1.4.354.
The toolchain path/environment is local configuration, not part of the review snapshot.

The CMake file retains its original relative worktree layout. A full local reproduction requires
placing the patched SDK at the path expected by `project/switch/generated_full/CMakeLists.txt`,
or changing `REXGLUE_SDK_ROOT`, and generating `project/generated/hinted/sources.cmake` locally.
Use the analysis manifest with a legally obtained Xbox 360 executable/assets held locally.

The original worktree contains earlier compatibility edits outside this snapshot's generated game
sources. This repository is intended for source review of the platform/renderer blocker, and does
not promise a one-command complete reproduction of the working Windows build or all historic tests.

Switch cache settings for the current candidate:

```text
GREEN_LANTERN_NATIVE_RENDERER=ON
GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB=320
GREEN_LANTERN_SWITCH_STREAMDATA_TEST_CAP_MIB=32
REXGLUE_PLATFORM_SWITCH=ON
REXGLUE_USE_VULKAN=ON
```

The stock loader heap is shrunk to 1280 MiB when a title-takeover heap override is larger.
Guest backing and native-worker stacks are reserved before NVK/runtime initialization.

Files intentionally omitted: Xbox/PC executable files and assets, generated PPC-to-C++ functions,
shader caches extracted from games, saves, compiled SDK/driver libraries, build trees and NROs.
