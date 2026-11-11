# Native macOS / Apple Silicon build

This build targets Apple Silicon (`arm64`) and uses ReXGlue's Vulkan renderer
through MoltenVK. The Windows D3D12 build and the separate Switch project are
unchanged. A successful build is not yet proof that every game effect renders
correctly on MoltenVK.

## Requirements

- A Mac with Apple Silicon and Xcode 16 or newer (AppleClang 16+).
- CMake 3.25+, Ninja, and Git.
- A ReXGlue SDK build that includes its `mac-arm64` preset and MoltenVK support.
- The user's own extracted game data in the sibling `game` directory, or a
  path supplied with `GREEN_LANTERN_GAME_DATA_DIR`.

Install the command-line tools and build utilities on the Mac:

```sh
xcode-select --install
brew install cmake ninja
```

## Use the SDK checkout from this project

The Windows workspace's SDK checkout contains local, uncommitted changes
(including platform and Vulkan compatibility work), so a generic clone may
not match this project. Copy the complete folder
`work/rexglue-sdk/work/rexglue-sdk-horizon-reference` from the workspace,
including its `thirdparty` contents/submodules, to the Mac. Keep it somewhere
stable and pass its new absolute path with `REXSDK_DIR`; this tells CMake to use
that exact source tree rather than whichever SDK happens to be installed.

The `mac-arm64` preset is present in that SDK checkout. The game project can
configure/build the SDK in-tree, so a separate install step is not required.

## Build Green Lantern

Keep the project directory next to its `game` directory (the default layout).
From the project's `project` directory:

```sh
cmake --preset mac-arm64 -DREXSDK_DIR="/absolute/path/to/rexglue-sdk-horizon-reference"
cmake --build --preset mac-arm64-release --target green_lantern
```

The executable is produced under `out/build/mac-arm64/Release/`. The SDK's
post-build steps stage the runtime libraries and MoltenVK alongside it.
If game data is elsewhere, configure with both paths (then run the same build
command above):

```sh
cmake --preset mac-arm64 \
  -DREXSDK_DIR="/absolute/path/to/rexglue-sdk-horizon-reference" \
  -DGREEN_LANTERN_GAME_DATA_DIR="/path/to/your/game-data"
```

Do not redistribute the extracted game data or a build containing it.

## First run

Run the executable from Terminal so startup diagnostics remain visible. Share
the complete terminal output if configuration, linking, graphics startup, or
the first scene fails. In particular, a MoltenVK feature/extension error would
be a graphics compatibility issue to investigate separately from the PPC
recompilation.
