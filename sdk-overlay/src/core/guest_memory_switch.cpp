/* See guest_memory_switch.h for the design and the reason behind each decision. */

#if defined(__SWITCH__)

#include "guest_memory_switch.h"

#include <malloc.h>
#include <string.h>
#include <switch.h>

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

#include <rex/logging.h>

namespace {

constexpr size_t kPageSize  = 0x1000;
/*
 * Large chunks are requested 2 MB aligned, so the kernel can use block descriptors instead of a
 * page table.
 */
constexpr size_t kBlockSize = 0x200000;
/* Keep host allocations/code-memory objects bounded for large guest commits. */
constexpr size_t kMaxPoolChunkSize = 0x1000000;
/* The Switch game reserves this once, before NVK and its worker threads start. */
constexpr size_t kMaxBackingArenaFreeRanges = 1024;

constexpr size_t AlignDown(size_t v, size_t a) { return v & ~(a - 1); }
constexpr size_t AlignUp(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

/* Backing slabs amortize Horizon code-memory objects across many small guest commits. */
struct Pool {
    void* backing = nullptr;
    size_t arena_offset = 0;
    bool from_arena = false;
    uint8_t* shadow = nullptr;
    size_t length = 0;
    size_t live_chunks = 0;
    std::map<size_t, size_t> free_ranges;
};

/* A committed guest range uses a subrange of a backing slab. */
struct Chunk {
    size_t offset;      /* offset in the mapping */
    size_t length;
    void*  shadow;      /* the codeAlias: always RW, a back door */
    Pool*  pool;
    size_t pool_offset;
};

struct ArenaFreeRange {
    size_t offset = 0;
    size_t length = 0;
};

/* A view: a span of the window that mirrors the mapping from `offset`. */
struct View {
    uint8_t* base;
    size_t   offset;
    size_t   length;
};

/* A span already mapped in this operation, so it can be undone if another fails. */
struct Tramo {
    size_t   win_off;
    uint8_t* src;
    size_t   len;
};

/*
 * The window size, without the lock
 *
 * InGuestWindow() is called on every fault (tens of thousands per second in a race) and only needs
 * to know whether the address falls inside the window. Reading it from State meant taking the global
 * lock, the same one the mappings use, and in the profile the thread that faulted most spent 22.8%
 * of its time in svcArbitrateLock because of this.
 *
 * base and size are set once in RexGmInit and only change again in RexGmShutdown, so an atomic copy
 * is enough and cannot get out of sync.
 */
std::atomic<size_t> g_size_rapido{0};

/*
 * The base goes in its own atomic and is published after the size. That way, if a thread sees the
 * base, the size is already set: the other way round (as it used to be) the window could be seen
 * half-initialized, with a valid base and size 0, and then InGuestWindow said no and a legitimate
 * guest fault ended in a fatal stop.
 */
std::atomic<uint8_t*> g_base_rapido{nullptr};

struct State {
    /*
     * Exclusive lock, and it cannot be replaced by a reader/writer one.
     *
     * shared_mutex was tried and it hung the game. The idea was sound on paper: the three calls of
     * the fault path only read, so they could run concurrently. But the performance profiler suspends
     * threads to sample them (svcSetThreadActivity in switch_perf.cpp) and then takes this same lock
     * to read the memory counters.
     *
     * With shared_mutex that is fatal: libstdc++ gives writers preference, so if a thread holding the
     * shared lock is suspended, the first writer to arrive queues up, and all following readers queue
     * behind it. The whole process stops. It hung exactly like that in a test: all 17 threads idle and
     * the profiler unable to write a single report.
     *
     * What did pay off from that attempt is in exception_handler_switch.cpp: asking once per fault
     * instead of five times. That reduces lock traffic without changing how it blocks.
     */
    std::mutex mutex;
    uint8_t* base = nullptr;
    size_t   size = 0;
    VirtmemReservation* reservation = nullptr;

    /* One early, 2 MiB-aligned malloc allocation backs all guest slabs. */
    uint8_t* backing_arena = nullptr;
    size_t backing_arena_size = 0;
    std::array<ArenaFreeRange, kMaxBackingArenaFreeRanges> arena_free_ranges{};
    size_t arena_free_range_count = 0;

    std::vector<View> views;
    /* chunks sorted by offset: allows finding the one that covers an offset */
    std::map<size_t, Chunk> chunks;
    std::vector<std::unique_ptr<Pool>> pools;
    /*
     * Logical permissions per page of the window, keyed by offset from base.
     * Only pages that are not REX_GM_WRITE are stored; REX_GM_WRITE is the common
     * case, so the table stays small. A page with an entry here is unmapped from
     * the window, whether it is committed or not.
     */
    std::map<size_t, RexGmAccess> protection;
    std::set<size_t> readonly_logical;
    /*
     * Which (view index, chunk offset) pairs are mapped. A chunk is not mapped into
     * all five views when it is committed: it enters each view the first time it is
     * accessed there. The real ceiling on Horizon is the process limit on mappable
     * memory, and mapping too much exhausts it.
     */
    std::set<std::pair<size_t, size_t>> view_mapped;

    size_t   committed = 0;
    size_t   mapped = 0;
    uint32_t last_result = 0;
    uint32_t last_stage = 0;
};

State& S() {
    static State s;
    return s;
}

Handle Proc() { return envGetOwnProcessHandle(); }

bool Intersect(size_t a, size_t al, size_t b, size_t bl, size_t* lo, size_t* hi) {
    const size_t l = a > b ? a : b;
    const size_t ea = a + al, eb = b + bl;
    const size_t h = ea < eb ? ea : eb;
    if (l >= h) return false;
    *lo = l;
    *hi = h;
    return true;
}

/* Finds the chunk that contains an offset of the mapping. */
Chunk* ChunkAt(State& s, size_t offset) {
    auto it = s.chunks.upper_bound(offset);
    if (it == s.chunks.begin()) return nullptr;
    --it;
    Chunk& c = it->second;
    return (offset >= c.offset && offset < c.offset + c.length) ? &c : nullptr;
}

/* The view that contains a window address. */
const View* ViewFor(State& s, uint64_t addr) {
    for (const View& v : s.views) {
        const uint64_t lo = reinterpret_cast<uint64_t>(v.base);
        if (addr >= lo && addr < lo + v.length) return &v;
    }
    return nullptr;
}

RexGmAccess AccessOf(State& s, size_t page_off) {
    auto it = s.protection.find(page_off);
    return it == s.protection.end() ? REX_GM_WRITE : it->second;
}

/*
 * Read-only guest pages kept mapped
 *
 * Horizon cannot make these pages read-only, so a read-only guest page was
 * unmapped and every read of it became an emulated fault. Project 8's video
 * decoder reads read-only tables: about 200,000 emulated reads a second, most of
 * a core, with GPU write watching already off. With this on, such pages stay
 * mapped read/write and only the bookkeeping says read-only. A guest write to
 * one then succeeds instead of faulting; on the console it would have been a
 * game bug. It must stay off while pages are write-watched, because watching
 * relies on read-only pages faulting on write.
 */
bool g_ro_mapped = false;
bool g_cpu_ro_mapped = false;

// GPU write watches cover physical backing, including its virtual aliases.
// Decide by backing offset, never merely by the guest virtual address (0x7F...
// is an alias of physical memory). Refuse mixed-view and boundary-crossing ranges.
bool IsCpuOnlyReadRange(State& s, uint64_t address, size_t length) {
    if (!length) return false;
    const View* first = ViewFor(s, address);
    const View* last = ViewFor(s, address + length - 1);
    if (!first || first != last) return false;
    const size_t offset = first->offset +
        static_cast<size_t>(address - reinterpret_cast<uint64_t>(first->base));
    constexpr size_t kPhysicalBacking = 0x100000000ull;
    return offset < kPhysicalBacking && length <= kPhysicalBacking - offset;
}

RexGmAccess ReportedAccessOf(const State& s, size_t page_off) {
    auto it = s.protection.find(page_off);
    if (it != s.protection.end()) return it->second;
    return s.readonly_logical.count(page_off) ? REX_GM_READ : REX_GM_WRITE;
}

/* Is there committed backing behind this window address? */
bool IsCommittedWindowAddress(State& s, uint64_t addr) {
    const View* v = ViewFor(s, addr);
    if (!v) return false;
    const size_t off = v->offset + static_cast<size_t>(addr - reinterpret_cast<uint64_t>(v->base));
    return ChunkAt(s, off) != nullptr;
}

/*
 * Unmaps [win_off, win_off+len) from the window, skipping pages with logical
 * protection, which are already unmapped. `src` is the shadow address that
 * corresponds to win_off.
 *
 * Walk: `it` always points to the first protected page >= cur. Keys are
 * page-aligned and increasing, so skipping a protected page and advancing the
 * iterator keeps that invariant, and the loop always makes progress.
 */
/*
 * Read-only page: try it, and if it is refused, go back to unmapping
 *
 * Why. Watching writes by unmapping the page has an expensive side effect: the
 * guest cannot read from it either, and every read is an exception. Measured in
 * a race: 108,000 emulated reads per second at about 9 us each, which is
 * practically the whole core of the main thread.
 *
 * With the page read-only, watching works the same (the write faults and the
 * handler services it) but reads are free.
 *
 * Why it is not a given. svcSetProcessMemoryPermission only accepts memory whose
 * state allows permission changes. The shadow is mapped with
 * svcMapProcessCodeMemory, which does allow it (it is already used there, with
 * Perm_Rw). The views are mapped with svcMapProcessMemory, and whether Horizon
 * allows it there too is not documented anywhere.
 *
 * So it is tried once. The first page that needs watching is tried with
 * permissions; if the kernel refuses, that is recorded and from then on pages
 * are unmapped as before. Nothing is left half done: if the call fails, it has
 * not changed anything.
 */
enum class SoloLectura { kSinProbar, kSi, kNo };
SoloLectura g_solo_lectura = SoloLectura::kSinProbar;

/* Returns true if the page was protected through permissions. */
bool ProtegerConPermisos(uint8_t* pagina, bool solo_lectura) {
    if (g_solo_lectura == SoloLectura::kNo) return false;

    const Result rc = svcSetProcessMemoryPermission(
        Proc(), reinterpret_cast<u64>(pagina), kPageSize, solo_lectura ? Perm_R : Perm_Rw);
    if (R_FAILED(rc)) {
        if (g_solo_lectura == SoloLectura::kSinProbar) {
            g_solo_lectura = SoloLectura::kNo;  // no se vuelve a intentar
        }
        return false;
    }
    g_solo_lectura = SoloLectura::kSi;
    return true;
}
void UnmapWindowRange(State& s, size_t win_off, uint8_t* src, size_t len) {
    if (g_solo_lectura == SoloLectura::kSi) {
        /* No holes here: everything was mapped, watched or not. */
        if (R_SUCCEEDED(svcUnmapProcessMemory(s.base + win_off, Proc(),
                                              reinterpret_cast<u64>(src), len))) {
            s.mapped -= len;
        }
        return;
    }
    const size_t end = win_off + len;
    size_t cur = win_off;
    auto it = s.protection.lower_bound(cur);
    while (cur < end) {
        if (it != s.protection.end() && it->first == cur) {
            cur += kPageSize;
            ++it;
            continue;
        }
        const size_t stop = (it != s.protection.end() && it->first < end) ? it->first : end;
        if (R_SUCCEEDED(svcUnmapProcessMemory(s.base + cur, Proc(),
                                              reinterpret_cast<u64>(src + (cur - win_off)),
                                              stop - cur))) {
            s.mapped -= stop - cur;
        }
        cur = stop;
    }
}

/*
 * When watched pages are read-only instead of unmapped, the whole range is mapped
 * at once and the watched pages then get their permission lowered. Simpler and
 * faster than skipping them.
 */
bool MapWindowRangeSoloLectura(State& s, size_t win_off, uint8_t* src, size_t len) {
    const Result rc =
        svcMapProcessMemory(s.base + win_off, Proc(), reinterpret_cast<u64>(src), len);
    if (R_FAILED(rc)) {
        s.last_result = rc;
        return false;
    }
    s.mapped += len;
    for (auto it = s.protection.lower_bound(win_off);
         it != s.protection.end() && it->first < win_off + len; ++it) {
        ProtegerConPermisos(s.base + it->first, true);
    }
    return true;
}

/*
 * Maps [win_off, win_off+len) honoring the logical protection. If a call fails,
 * it undoes what this call had mapped and returns false: it never leaves a range
 * half mapped.
 */
bool MapWindowRange(State& s, size_t win_off, uint8_t* src, size_t len) {
    if (g_solo_lectura == SoloLectura::kSi) {
        return MapWindowRangeSoloLectura(s, win_off, src, len);
    }
    const size_t end = win_off + len;
    size_t cur = win_off;
    auto it = s.protection.lower_bound(cur);
    while (cur < end) {
        if (it != s.protection.end() && it->first == cur) {
            cur += kPageSize;
            ++it;
            continue;
        }
        const size_t stop = (it != s.protection.end() && it->first < end) ? it->first : end;
        const Result rc = svcMapProcessMemory(s.base + cur, Proc(),
                                              reinterpret_cast<u64>(src + (cur - win_off)),
                                              stop - cur);
        if (R_FAILED(rc)) {
            s.last_result = rc;
            /*
             * The unmap walks the same way as this loop, so it removes exactly what was
             * mapped, no more and no less.
             */
            UnmapWindowRange(s, win_off, src, cur - win_off);
            return false;
        }
        s.mapped += stop - cur;
        cur = stop;
    }
    return true;
}

/*
 * Maps the range [offset, offset+length) of a chunk into every view that covers
 * it. This is where the 360 mirrors come from.
 *
 * All or nothing: if it fails in one view, it undoes the previous ones. The
 * alternative (leaving a view unmapped and carrying on) would make every access
 * to it be emulated one by one by the exception handler: it would work, but at a
 * fraction of the speed and without any warning. That is exactly the bug the
 * first version had.
 */
bool ViewIndexFor(State& s, uint64_t addr, size_t* out) {
    for (size_t i = 0; i < s.views.size(); i++) {
        const uint64_t lo = reinterpret_cast<uint64_t>(s.views[i].base);
        if (addr >= lo && addr < lo + s.views[i].length) { *out = i; return true; }
    }
    return false;
}

/* Maps a chunk into one view: the view of the faulting access. */
bool MapChunkIntoView(State& s, size_t view_index, const Chunk& c) {
    const View& v = s.views[view_index];
    size_t lo, hi;
    if (!Intersect(c.offset, c.length, v.offset, v.length, &lo, &hi)) return false;
    const size_t win_off = static_cast<size_t>(v.base - s.base) + (lo - v.offset);
    uint8_t* src = static_cast<uint8_t*>(c.shadow) + (lo - c.offset);
    if (!MapWindowRange(s, win_off, src, hi - lo)) return false;
    s.view_mapped.insert({view_index, c.offset});
    return true;
}

void UnmapChunkFromViews(State& s, const Chunk& c) {
    for (size_t i = 0; i < s.views.size(); i++) {
        auto it = s.view_mapped.find({i, c.offset});
        if (it == s.view_mapped.end()) continue;  /* never entered this view */
        const View& v = s.views[i];
        size_t lo, hi;
        if (!Intersect(c.offset, c.length, v.offset, v.length, &lo, &hi)) continue;
        const size_t win_off = static_cast<size_t>(v.base - s.base) + (lo - v.offset);
        uint8_t* src = static_cast<uint8_t*>(c.shadow) + (lo - c.offset);
        UnmapWindowRange(s, win_off, src, hi - lo);
        s.view_mapped.erase(it);
    }
}

void ReturnArenaRange(State& s, size_t offset, size_t range_length) {
    size_t begin = offset;
    size_t length = range_length;
    size_t insert_at = 0;
    while (insert_at < s.arena_free_range_count &&
           s.arena_free_ranges[insert_at].offset < begin) {
        ++insert_at;
    }
    if (insert_at > 0) {
        ArenaFreeRange& previous = s.arena_free_ranges[insert_at - 1];
        if (previous.offset + previous.length == begin) {
            begin = previous.offset;
            length += previous.length;
            --insert_at;
            for (size_t i = insert_at; i + 1 < s.arena_free_range_count; ++i)
                s.arena_free_ranges[i] = s.arena_free_ranges[i + 1];
            --s.arena_free_range_count;
        }
    }
    if (insert_at < s.arena_free_range_count &&
        begin + length == s.arena_free_ranges[insert_at].offset) {
        length += s.arena_free_ranges[insert_at].length;
        for (size_t i = insert_at; i + 1 < s.arena_free_range_count; ++i)
            s.arena_free_ranges[i] = s.arena_free_ranges[i + 1];
        --s.arena_free_range_count;
    }
    if (s.arena_free_range_count < s.arena_free_ranges.size()) {
        for (size_t i = s.arena_free_range_count; i > insert_at; --i)
            s.arena_free_ranges[i] = s.arena_free_ranges[i - 1];
        s.arena_free_ranges[insert_at] = {begin, length};
        ++s.arena_free_range_count;
    } else {
        REXLOG_ERROR("Switch guest backing arena free-list metadata exhausted while returning "
                     "offset=0x{:X}, size={} MiB", begin, length >> 20);
    }
}

void ReturnPoolBacking(State& s, Pool& pool) {
    if (pool.from_arena) ReturnArenaRange(s, pool.arena_offset, pool.length);
    else free(pool.backing);
}

void ReleaseChunk(const Chunk& c) {
    Pool& pool = *c.pool;
    size_t begin = c.pool_offset;
    size_t length = c.length;
    auto next = pool.free_ranges.lower_bound(begin);
    if (next != pool.free_ranges.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == begin) {
            begin = prev->first;
            length += prev->second;
            pool.free_ranges.erase(prev);
        }
    }
    next = pool.free_ranges.lower_bound(begin);
    if (next != pool.free_ranges.end() && begin + length == next->first) {
        length += next->second;
        pool.free_ranges.erase(next);
    }
    pool.free_ranges.emplace(begin, length);
    --pool.live_chunks;
    if (pool.live_chunks != 0) return;

    svcUnmapProcessCodeMemory(Proc(), reinterpret_cast<u64>(pool.shadow),
                              reinterpret_cast<u64>(pool.backing), pool.length);
    State& s = S();
    /* Return the range only after the code-memory alias no longer borrows it. */
    ReturnPoolBacking(s, pool);
    for (auto it = s.pools.begin(); it != s.pools.end(); ++it) {
        if (it->get() == &pool) {
            s.pools.erase(it);
            break;
        }
    }
}

bool AllocateArenaRange(State& s, size_t length, size_t* out_offset) {
    size_t largest_free_range = 0;
    for (size_t i = 0; i < s.arena_free_range_count; ++i) {
        ArenaFreeRange& range = s.arena_free_ranges[i];
        largest_free_range = std::max(largest_free_range, range.length);
        if (range.length < length) continue;
        *out_offset = range.offset;
        range.offset += length;
        range.length -= length;
        if (range.length == 0) {
            for (size_t j = i; j + 1 < s.arena_free_range_count; ++j)
                s.arena_free_ranges[j] = s.arena_free_ranges[j + 1];
            --s.arena_free_range_count;
        }
        return true;
    }
    REXLOG_ERROR("Switch guest backing arena exhausted: requested={} MiB, largest_free={} MiB, "
                 "arena={} MiB, free_ranges={}",
                 length >> 20, largest_free_range >> 20, s.backing_arena_size >> 20,
                 s.arena_free_range_count);
    return false;
}

bool AllocatePoolRange(State& s, size_t length, Pool** out_pool, size_t* out_offset) {
    const size_t pool_size = AlignUp(length > kBlockSize ? length : kBlockSize, kBlockSize);
    for (const auto& candidate : s.pools) {
        if (candidate->length < pool_size) continue;
        for (auto it = candidate->free_ranges.begin(); it != candidate->free_ranges.end(); ++it) {
            if (it->second < length) continue;
            const size_t offset = it->first;
            const size_t remaining = it->second - length;
            candidate->free_ranges.erase(it);
            if (remaining) candidate->free_ranges.emplace(offset + length, remaining);
            ++candidate->live_chunks;
            *out_pool = candidate.get();
            *out_offset = offset;
            return true;
        }
    }

    std::unique_ptr<Pool> pool;
    try {
        pool = std::make_unique<Pool>();
    } catch (...) {
        s.last_result = REX_GM_ERR_NOMEM;
        s.last_stage = 1;
        return false;
    }
    pool->length = pool_size;
    if (s.backing_arena) {
        if (!AllocateArenaRange(s, pool_size, &pool->arena_offset)) {
            s.last_result = REX_GM_ERR_NOMEM;
            s.last_stage = 1;
            return false;
        }
        pool->from_arena = true;
        pool->backing = s.backing_arena + pool->arena_offset;
    } else {
        pool->backing = memalign(kBlockSize, pool_size);
        if (!pool->backing) {
            s.last_result = REX_GM_ERR_NOMEM;
            s.last_stage = 1;
            return false;
        }
    }
    memset(pool->backing, 0, pool_size);

    virtmemLock();
    pool->shadow = static_cast<uint8_t*>(virtmemFindCodeMemory(pool_size, kBlockSize));
    Result rc = pool->shadow
                    ? svcMapProcessCodeMemory(Proc(), reinterpret_cast<u64>(pool->shadow),
                                              reinterpret_cast<u64>(pool->backing), pool_size)
                    : 0;
    virtmemUnlock();
    if (!pool->shadow) {
        /* The backing range has not been mapped; return it to the arena. */
        ReturnPoolBacking(s, *pool);
        s.last_result = REX_GM_ERR_NOCODEVA;
        s.last_stage = 2;
        return false;
    }
    if (R_FAILED(rc)) {
        ReturnPoolBacking(s, *pool);
        s.last_result = rc;
        s.last_stage = 3;
        return false;
    }
    rc = svcSetProcessMemoryPermission(Proc(), reinterpret_cast<u64>(pool->shadow), pool_size,
                                       Perm_Rw);
    if (R_FAILED(rc)) {
        svcUnmapProcessCodeMemory(Proc(), reinterpret_cast<u64>(pool->shadow),
                                  reinterpret_cast<u64>(pool->backing), pool_size);
        ReturnPoolBacking(s, *pool);
        s.last_result = rc;
        s.last_stage = 4;
        return false;
    }

    pool->free_ranges.emplace(length, pool_size - length);
    pool->live_chunks = 1;
    Pool* raw_pool = pool.get();
    try {
        s.pools.emplace_back(std::move(pool));
    } catch (...) {
        svcUnmapProcessCodeMemory(Proc(), reinterpret_cast<u64>(raw_pool->shadow),
                                  reinterpret_cast<u64>(raw_pool->backing), raw_pool->length);
        ReturnPoolBacking(s, *raw_pool);
        s.last_result = REX_GM_ERR_NOMEM;
        s.last_stage = 1;
        return false;
    }
    *out_pool = raw_pool;
    *out_offset = 0;
    return true;
}

}  // namespace

extern "C" {



bool RexGmBackingArenaInit(size_t size) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.backing_arena) return s.backing_arena_size == size;
    if (size == 0 || size % kBlockSize != 0) {
        REXLOG_ERROR("Switch guest backing arena size must be a nonzero multiple of 2 MiB; got {}",
                     size);
        return false;
    }
    s.backing_arena = static_cast<uint8_t*>(memalign(kBlockSize, size));
    if (!s.backing_arena) {
        const struct mallinfo heap = mallinfo();
        REXLOG_ERROR("Switch guest backing arena reservation failed: requested={} MiB, "
                     "malloc_free={} MiB",
                     size >> 20, static_cast<size_t>(heap.fordblks) >> 20);
        return false;
    }
    s.backing_arena_size = size;
    s.arena_free_ranges[0] = {0, size};
    s.arena_free_range_count = 1;
    const struct mallinfo heap = mallinfo();
    REXLOG_INFO("Switch guest backing arena reserved: base=0x{:X}, size={} MiB, "
                "malloc_free_after={} MiB",
                reinterpret_cast<uintptr_t>(s.backing_arena), size >> 20,
                static_cast<size_t>(heap.fordblks) >> 20);
    return true;
}

uint8_t* RexGmInit(size_t size) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.base) return s.base;

    virtmemLock();
    s.base = static_cast<uint8_t*>(virtmemFindAslr(size, kBlockSize));
    if (s.base) s.reservation = virtmemAddReservation(s.base, size);
    virtmemUnlock();

    if (!s.base || !s.reservation) {
        s.base = nullptr;
        return nullptr;
    }
    s.size = size;
    g_size_rapido.store(size, std::memory_order_release);
    g_base_rapido.store(s.base, std::memory_order_release);
    return s.base;
}

void RexGmShutdown(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);

    for (auto& kv : s.chunks) {
        UnmapChunkFromViews(s, kv.second);
        ReleaseChunk(kv.second);
    }
    s.chunks.clear();
    s.pools.clear();
    s.views.clear();
    s.protection.clear();
    s.readonly_logical.clear();
    s.committed = 0;
    s.mapped = 0;

    if (s.reservation) {
        virtmemLock();
        virtmemRemoveReservation(s.reservation);
        virtmemUnlock();
        s.reservation = nullptr;
    }
    s.base = nullptr;
    s.size = 0;
    /*
     * Reverse order from startup: the base is cleared first, so nobody gets in with
     * a size that is no longer valid.
     */
    g_base_rapido.store(nullptr, std::memory_order_release);
    g_size_rapido.store(0, std::memory_order_release);
    free(s.backing_arena);
    s.backing_arena = nullptr;
    s.backing_arena_size = 0;
    s.arena_free_range_count = 0;
}

uint8_t* RexGmBase(void) { return g_base_rapido.load(std::memory_order_acquire); }

size_t RexGmSize(void) { return g_size_rapido.load(std::memory_order_acquire); }

bool RexGmWindowToOffset(uint64_t window_address, size_t* offset_out) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;
    const View* v = ViewFor(s, window_address);
    if (!v) return false;
    if (offset_out) {
        *offset_out =
            v->offset + static_cast<size_t>(window_address - reinterpret_cast<uint64_t>(v->base));
    }
    return true;
}

size_t RexGmCommittedBytes(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.committed;
}

size_t RexGmPoolBytes(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    size_t total = 0;
    for (const auto& pool : s.pools) total += pool->length;
    return total;
}

size_t RexGmChunkCount(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.chunks.size();
}

size_t RexGmPoolCount(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.pools.size();
}

size_t RexGmViewMappingCount(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.view_mapped.size();
}

size_t RexGmMappedBytes(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.mapped;
}

int RexGmModoProteccion(void) {
    switch (g_solo_lectura) {
        case SoloLectura::kSi: return 1;
        case SoloLectura::kNo: return 2;
        default: return 0;
    }
}

uint32_t RexGmLastResult(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.last_result;
}

uint32_t RexGmLastStage(void) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.last_stage;
}

bool RexGmAddView(uint8_t* base, size_t offset, size_t length) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;
    if (base < s.base || static_cast<size_t>(base - s.base) + length > s.size)
        return false;  /* outside the reserved window */

    const View v{base, offset, length};

    /*
     * Nothing is mapped here: what is already committed enters this view the first
     * time it is touched.
     */
    s.views.push_back(v);
    return true;
}

bool RexGmCommit(size_t offset, size_t length, RexGmAccess access) {
    /*
     * Memory is always committed accessible. Restricting permissions is the job of
     * RexGmProtect, which works on window addresses and not on mapping offsets (and
     * it has to, because the same offset can be mirrored in several windows with
     * different permissions: the 360 does exactly that).
     *
     * `access` stays in the signature because the natural caller is
     * AllocFixed(kCommit, access), and removing it would invite forgetting to apply
     * it. Pages that already had logical protection keep it.
     */
    (void)access;
    if (length == 0) return true;

    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.last_result = 0;
    s.last_stage = 0;
    if (!s.base) return false;

    const size_t begin = AlignDown(offset, kPageSize);
    const size_t end   = AlignUp(offset + length, kPageSize);

    size_t cur = begin;
    while (cur < end) {
        if (Chunk* existente = ChunkAt(s, cur)) {
            cur = existente->offset + existente->length;
            continue;
        }

        /* Where the gap ends: the next chunk, or the end. */
        size_t hueco_fin = end;
        auto sig = s.chunks.upper_bound(cur);
        if (sig != s.chunks.end() && sig->second.offset < hueco_fin)
            hueco_fin = sig->second.offset;

        size_t piece_offset = cur;
        while (piece_offset < hueco_fin) {
            const size_t piece_size =
                std::min(hueco_fin - piece_offset, kMaxPoolChunkSize);
            Pool* pool = nullptr;
            size_t pool_offset = 0;
            if (!AllocatePoolRange(s, piece_size, &pool, &pool_offset)) return false;
            uint8_t* shadow = pool->shadow + pool_offset;
            memset(shadow, 0, piece_size);

            /*
             * Not mapped into the views: each view gets it on first access
             * (RexGmFaultIn). Mapping it into all five 360 views multiplies by five the
             * use of the process limit on mappable memory. Large guest commits are
             * represented by bounded adjacent chunks to avoid one enormous host
             * allocation and code-memory mapping.
             */
            const Chunk c{piece_offset, piece_size, shadow, pool, pool_offset};
            s.chunks[piece_offset] = c;
            s.committed += piece_size;
            piece_offset += piece_size;
        }

        cur = hueco_fin;
    }
    return true;
}

bool RexGmFaultIn(uint64_t window_address) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;

    size_t vi = 0;
    if (!ViewIndexFor(s, window_address, &vi)) return false;

    /*
     * Page with logical protection: it is unmapped on purpose (write watching, MMIO,
     * read-only). Mapping the chunk would not bring it in (protected pages are
     * skipped), so answering yes would leave the fault repeating forever. The next
     * steps of the handler deal with it.
     */
    const size_t win_off =
        static_cast<size_t>(window_address - reinterpret_cast<uint64_t>(s.base));
    if (s.protection.find(AlignDown(win_off, kPageSize)) != s.protection.end()) return false;

    const size_t off = s.views[vi].offset +
                       static_cast<size_t>(window_address -
                                           reinterpret_cast<uint64_t>(s.views[vi].base));
    Chunk* c = ChunkAt(s, off);
    if (!c) return false;                                       /* sin confirmar */
    if (s.view_mapped.count({vi, c->offset}) != 0) return false; /* ya estaba */
    return MapChunkIntoView(s, vi, *c);
}

bool RexGmDecommit(size_t offset, size_t length) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;

    const size_t begin = AlignDown(offset, kPageSize);
    const size_t end   = AlignUp(offset + length, kPageSize);

    for (auto it = s.chunks.begin(); it != s.chunks.end();) {
        const Chunk c = it->second;
        /*
         * Only chunks entirely contained in the range are freed: splitting a chunk
         * would also mean splitting its backing, and the backing is a single heap
         * allocation. Partial chunks stay committed, which is conservative but correct.
         */
        if (c.offset >= begin && c.offset + c.length <= end) {
            UnmapChunkFromViews(s, c);
            ReleaseChunk(c);
            s.committed -= c.length;
            it = s.chunks.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool RexGmProtect(uint8_t* address, size_t length, RexGmAccess access, RexGmAccess* out_old) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;

    const uint64_t addr  = reinterpret_cast<uint64_t>(address);
    const uint64_t wbase = reinterpret_cast<uint64_t>(s.base);
    if (addr < wbase || addr + length > wbase + s.size) return false;

    const size_t begin = AlignDown(static_cast<size_t>(addr - wbase), kPageSize);
    const size_t end   = AlignUp(static_cast<size_t>(addr - wbase) + length, kPageSize);

    if (out_old) *out_old = ReportedAccessOf(s, begin);

    const bool ro_mapped = access == REX_GM_READ &&
        (g_ro_mapped || (g_cpu_ro_mapped && IsCpuOnlyReadRange(s, addr, length)));
    for (size_t p = begin; p < end; p += kPageSize) {
        if (ro_mapped) s.readonly_logical.insert(p);
        else s.readonly_logical.erase(p);
    }
    const bool debe_estar = (access == REX_GM_WRITE) || ro_mapped;

    /*
     * Fast path, and the common one: asking for full access on a range that has no
     * protected page. AllocFixed(kCommit) does it for every allocation of the game
     * heap, and without this it would walk the range page by page (131,072
     * iterations for the 512 MB of physical memory) to do nothing.
     */
    if (debe_estar) {
        auto it = s.protection.lower_bound(begin);
        if (it == s.protection.end() || it->first >= end) return true;
    }

    for (size_t p = begin; p < end; p += kPageSize) {
        const bool estaba = (AccessOf(s, p) == REX_GM_WRITE);

        if (estaba != debe_estar) {
            /*
             * The view is looked up per page: a range can cross the boundary
             * between two views with different offsets.
             */
            if (const View* v = ViewFor(s, wbase + p)) {
                const size_t off_mapeo =
                    v->offset + (p - static_cast<size_t>(v->base - s.base));
                size_t vi = 0;
                Chunk* c = ChunkAt(s, off_mapeo);
                /*
                 * If the chunk has not entered this view yet, there is nothing to map
                 * or unmap: the permission is recorded and the first access will bring
                 * the chunk in with the right permission already.
                 */
                const bool en_vista = c != nullptr && ViewIndexFor(s, wbase + p, &vi) &&
                                      s.view_mapped.count({vi, c->offset}) != 0;
                if (en_vista) {
                    const u64 src = reinterpret_cast<u64>(
                        static_cast<uint8_t*>(c->shadow) + (off_mapeo - c->offset));
                    /*
                     * Permissions are tried first, which keeps the page readable. If the
                     * kernel refuses, it falls back to the usual unmap. ProtegerConPermisos
                     * remembers whether it works.
                     */
                    if (!ProtegerConPermisos(s.base + p, estaba)) {
                        const Result rc =
                            estaba ? svcUnmapProcessMemory(s.base + p, Proc(), src, kPageSize)
                                   : svcMapProcessMemory(s.base + p, Proc(), src, kPageSize);
                        if (R_FAILED(rc)) {
                            s.last_result = rc;
                            return false;
                        }
                        if (estaba) s.mapped -= kPageSize;
                        else s.mapped += kPageSize;
                    }
                }
                /*
                 * Not committed: nothing to map, but the permission is still recorded
                 * and RexGmCommit will honor it on commit.
                 */
            }
        }

        if (debe_estar) s.protection.erase(p);
        else s.protection[p] = access;
    }
    return true;
}

bool RexGmQueryProtect(uint8_t* address, size_t* length, RexGmAccess* out_access) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return false;

    const uint64_t addr  = reinterpret_cast<uint64_t>(address);
    const uint64_t wbase = reinterpret_cast<uint64_t>(s.base);
    if (addr < wbase || addr >= wbase + s.size) return false;

    const size_t begin = AlignDown(static_cast<size_t>(addr - wbase), kPageSize);

    /*
     * An uncommitted page is reported as having no access, even if its logical
     * permission is the default one.
     *
     * This matters a lot: after a fault, the SDK's MMIO handler queries this, and if
     * it sees the page as accessible it concludes that another thread already
     * unprotected it and retries the instruction. If the page does not really exist,
     * the retry faults again... forever. A silent hang.
     */
    if (!IsCommittedWindowAddress(s, wbase + begin)) {
        if (out_access) *out_access = REX_GM_NONE;
        if (length) *length = kPageSize;
        return true;
    }

    const RexGmAccess a = ReportedAccessOf(s, begin);
    if (out_access) *out_access = a;

    /*
     * On input *length is the maximum to walk (0 = one page); on output, the size of
     * the committed range with the same permission.
     */
    size_t maximo = (length && *length) ? *length : kPageSize;
    if (begin + maximo > s.size) maximo = s.size - begin;

    size_t p = begin;
    const size_t tope = begin + maximo;
    while (p < tope && ReportedAccessOf(s, p) == a && IsCommittedWindowAddress(s, wbase + p))
        p += kPageSize;
    if (length) *length = p - begin;
    return true;
}

void* RexGmShadowFor(uint64_t window_address) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.base) return nullptr;

    const View* v = ViewFor(s, window_address);
    if (!v) return nullptr;

    const size_t off_mapeo =
        v->offset + static_cast<size_t>(window_address - reinterpret_cast<uint64_t>(v->base));
    Chunk* c = ChunkAt(s, off_mapeo);
    if (!c) return nullptr;

    return static_cast<uint8_t*>(c->shadow) + (off_mapeo - c->offset);
}

}  // extern "C"

#endif /* __SWITCH__ */

#if defined(__SWITCH__)
extern "C" void RexGmSetReadOnlyMapped(bool enabled) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    g_ro_mapped = enabled;
}
extern "C" void RexGmSetCpuReadOnlyMapped(bool enabled) {
    State& s = S();
    std::lock_guard<std::mutex> lock(s.mutex);
    g_cpu_ro_mapped = enabled;
}
#endif
