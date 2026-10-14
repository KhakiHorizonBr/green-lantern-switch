# Green Lantern native renderer experiment

As of 2026-10-14, hardware log 035 reaches renderer initialization but both native GPU helper
threads fail to create. The current reserved-threads candidate provides two early 1088 KiB
stack backings, checks startup results and logs raw libnx failures. It builds successfully and
awaits hardware validation; no game frame has been displayed by this Switch experiment.

Experimental Switch-only adaptation of the native Vulkan renderer from the user-provided
The Simpsons Game NX source. Upstream attribution and GPL-3.0-or-later notices are retained
in the source headers. This is a separate build variant; it does not replace the default
Xenos renderer or the archived arena test NRO.

The Simpsons-specific precompiled pixel-shader replacements are not intended for Green Lantern.
The first compile uses an empty embedded-shader table; Green Lantern shader compatibility must
come from the generic ReXGlue SPIR-V translator and will be evaluated separately.

The compatibility layer adds the Vulkan device-dispatch entries and the optional
`VertexFetchObserver` hook needed by this renderer to the experimental ReXGlue checkout.
The Simpsons-only texture fast path is disabled because its translator modification bits do
not exist in this Green Lantern SDK snapshot. Optional ring progress and presenter counters
from the upstream port are also omitted; the guest read pointer and local swap stats are used.

The NRO is built with the current 320 MiB guest-backing arena and the remaining runtime
settings held constant. Rendering is not yet validated on Switch hardware. Use the separate NRO in
`artifacts` and keep the archived `green_lantern_switch_game.arena-320.nro` for rollback.
