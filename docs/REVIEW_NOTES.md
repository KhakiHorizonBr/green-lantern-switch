# Hardware evidence and current candidate — 2026-10-14

## Current blocker

Log 035 initializes NVK on Tegra X1, creates a 1280x720 swapchain and reports
`Renderer ready: 160 MB stream, 1 shared memory binding(s)`.
Both native GPU XHostThreads then fail in `pthread_create(stack_size=524288)` with errno 12.
The function table initializes afterward, but there is no guest ring/swap/frame evidence.

At each failure, successful Horizon queries report **13 / 96 threads** and **1281 / 3189 MiB**
of process memory resources. These counters rule out reaching those two reported quotas at that
instant. They do not measure the largest available allocation in the fixed malloc heap.

Log 033 used 2 MiB helper host stacks; both helpers failed. Log 034 used 1 MiB and logged a failure
only for the ring helper. Log 035 used 512 KiB and both failed again. Smaller stacks did not reliably
solve startup; absence of a creation-error line in 034 is not a frame-success result.

The linked libnx binary was inspected: `__syscall_thread_create` returns ENOMEM after failure in
`threadCreate`, `svcSetThreadCoreMask`, or `threadStart`, and on pthread metadata allocation failure.
The errno alone therefore cannot identify the failing operation.
`threadCreate` also allocates page-aligned stack/TLS/reent backing and maps it with `svcMapMemory`.
TLS in the previous executable was 0x540 bytes, so this was not a huge TLS allocation.

## Current correction candidate (not hardware validated)

- Reserve two page-aligned stack backings of 1088 KiB each before NVK initialization. Only the
  named native VSync/ring helper creations use these buffers; guest execution keeps 2 MiB host stacks.
- libnx retains responsibility for stack mapping, thread creation, TLS, priorities and joining.
  The reserve provides backing, and each slot is returned only after successful `threadClose`.
- Report the raw Results for stack mapping, kernel thread creation, core-mask assignment and start.
  Failed stack maps also report source memory type, attributes and permissions.
- Check both GPU worker `Create()` results. Previously these were ignored and GPU setup returned
  success with missing workers. Startup now propagates failure and stops an already-started VSync
  worker if ring creation fails.
- Report malloc usage/free/top statistics alongside the existing Horizon resource counters.

The underlying failure in 035 is **not yet identified**. Early stack backing addresses an aligned
allocation/source-page hypothesis; it does not prove that hypothesis. This candidate is an actual
allocation/order change combined with error reporting, not a confirmed fix.

NRO: `green_lantern_native_renderer_reserved_threads.nro`, 53,356,741 bytes.
SHA-256: `536ED663A17562F5990C53259F8BAF8658B112997B6657BED26C850B1958E15A`.
Cross-build/package succeeded. The linked executable calls all five diagnostic wrappers.
Previous NROs are preserved locally. The NRO contains generated game code and is not in this repo.

## How to interpret the next run

- `Both GPU worker threads created successfully`: this startup blocker is bypassed. Look next
  for the guest ring, swaps and rendering; thread creation alone does not demonstrate a game frame.
- `stack map raw failure`: inspect the raw kernel Result and source-page metadata.
- `threadCreate raw failure` without a stack-map/kernel-create failure: inspect allocation or
  argument validation and malloc stats; the reserve is logged explicitly.
- `thread core-mask raw failure` / `threadStart raw failure`: investigate that operation and
  process capabilities rather than treating errno 12 as a RAM capacity measurement.

## Earlier Xenos renderer path

A 256 MiB early guest-backing arena bypassed the original aligned-allocation failure for a 35 MiB
guest request. It later exhausted on a 16 MiB slab with only 12 MiB free. A 384 MiB arena instead
hit NVK OOM on a depth target earlier. The 320 MiB arena was built as a compromise; its original
Xenos NRO has not been hardware tested. The current native experiment also uses the 320 MiB arena.

Current fixed malloc heap: 1280 MiB. STREAMDATA test cap: 32 MiB. Native stream: 160 MiB.
The Xenos non-sparse shared buffer (512 MiB) explains much of the old path's graphics memory use;
the native experiment is a different renderer and should not be assumed to have the same budget.

## Questions for the Project8 developer

1. Did your port encounter libnx stack backing/map failures after NVK setup? Is early explicit
   backing sufficient, or must heap page attributes / stack mirror placement be handled specially?
2. How do you budget the libnx heap, guest backing and NVK's host-visible allocations in title
   takeover? Do you preserve a dedicated host allocation reserve as well as guest slabs?
3. Are there SDK/version differences affecting libnx core-mask/start calls or guest MMIO registration
   before GPU worker creation? The raw diagnostics should now distinguish these failure sites.

Hardware launches use title takeover (R while opening a game), rather than Album applet mode.
