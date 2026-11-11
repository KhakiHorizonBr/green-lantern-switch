# Horizon runtime boot probe

This is an isolated first-runtime milestone. It does not replace the known-good
green-screen NRO and does not launch guest game code.

When linked with the Horizon-capable ReXGlue runtime, it will:

1. Mount SD and check `sdmc:/switch/green_lantern/game/default.xex`.
2. Initialize ReXGlue using the generated `PPCImageConfig` and HLE kernel.
3. Set `RuntimeConfig::tool_mode` so GPU/Xenos startup is skipped.
4. Load `game:\default.xex` into guest memory, then wait for +.
5. Write phase/status messages to `sdmc:/switch/green_lantern_runtime_boot.log`.

Put the user's own legally dumped game files under
`sdmc:/switch/green_lantern/game/`; game files are not included in this source
tree. A successful XEX load only validates runtime setup and image loading. It
does not mean the game can execute or render on Switch.

Current state: the Horizon headless SDK configuration compiles and archives
`librexruntime.a` for AArch64. The final probe executable does not link yet:
ReXGlue still needs Horizon thread/fiber and exception-handling implementations,
plus explicit core/filesystem object and project memory/clock bridge wiring.
No probe ELF or NRO has been produced. This probe is an intermediate loader
diagnostic, not a playable game build.
