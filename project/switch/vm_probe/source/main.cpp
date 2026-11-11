#include <switch.h>

#include <cerrno>
#include <cstdio>

#include "../../runtime/sparse_guest_pages.h"

size_t __nx_heap_size = 0x40000000;
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

namespace {
constexpr u64 kPageSize = 0x1000;
constexpr u64 kHeapTarget = 0x40000000;
constexpr u64 kGuestWindowSize = 0x120000000;
constexpr u64 kValidatedAliasOffset = 0xA0000000;
constexpr u64 kPhysicalChunkSizes[] = {
    0x01000000,
    0x04000000, 0x04000000, 0x04000000, 0x04000000,
    0x04000000, 0x04000000, 0x04000000,
    0x01000000, 0x01000000, 0x01000000,
};
constexpr size_t kPhysicalChunkCount = sizeof(kPhysicalChunkSizes) / sizeof(kPhysicalChunkSizes[0]);
constexpr size_t kPhysicalAliasCount = 5;
constexpr size_t kExpectedPhysicalViews = kPhysicalChunkCount * (kPhysicalAliasCount - 1) + 1;
constexpr u64 kPhysicalAliasOffsets[kPhysicalAliasCount] = {
    0x7F000000, 0xA0000000, 0xC0000000, 0xE0000000, 0x100000000,
};

void report(std::FILE* log, const char* message, Result result) {
  std::printf("%s: 0x%08X\n", message, static_cast<unsigned>(result));
  std::fflush(stdout);
  if (log) {
    std::fprintf(log, "%s: 0x%08X\n", message, static_cast<unsigned>(result));
    std::fflush(log);
  }
  consoleUpdate(NULL);
}

bool find_free_page(void** out_address, std::FILE* log) {
  u64 aslr_base = 0, aslr_size = 0, heap_base = 0, heap_size = 0;
  Result rc = svcGetInfo(&aslr_base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
  report(log, "query ASLR base", rc);
  if (R_FAILED(rc)) return false;
  rc = svcGetInfo(&aslr_size, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
  report(log, "query ASLR size", rc);
  if (R_FAILED(rc) || aslr_base + aslr_size < aslr_base) return false;
  const Result heap_base_rc = svcGetInfo(&heap_base, InfoType_HeapRegionAddress,
                                         CUR_PROCESS_HANDLE, 0);
  const Result heap_size_rc = svcGetInfo(&heap_size, InfoType_HeapRegionSize,
                                         CUR_PROCESS_HANDLE, 0);
  report(log, "query heap region base", heap_base_rc);
  report(log, "query heap region size", heap_size_rc);
  const bool valid_heap = R_SUCCEEDED(heap_base_rc) && R_SUCCEEDED(heap_size_rc) &&
                          heap_base + heap_size >= heap_base;
  const u64 heap_end = valid_heap ? heap_base + heap_size : 0;
  const u64 aslr_end = aslr_base + aslr_size;
  u64 best_span_base = 0;
  u64 best_span_size = 0;

  // Reuse the free-span strategy from the successful 512 MiB test. The exact
  // AliasRegion base itself was rejected, while the interior A-view location
  // inside this proven 4.5 GiB span accepted the full alias set.
  for (u64 cursor = aslr_base; cursor < aslr_end;) {
    MemoryInfo info{};
    u32 page_info = 0;
    rc = svcQueryMemory(&info, &page_info, cursor);
    if (R_FAILED(rc) || info.size == 0 || info.addr + info.size <= cursor) {
      report(log, "query memory while scanning", rc);
      return false;
    }
    const u64 region_end = info.addr + info.size;
    if (info.type == MemType_Unmapped) {
      const u64 span_begin = info.addr > aslr_base ? info.addr : aslr_base;
      const u64 span_end = region_end < aslr_end ? region_end : aslr_end;
      const bool overlaps_heap = valid_heap && span_begin < heap_end && span_end > heap_base;
      if (!overlaps_heap && span_end >= span_begin && span_end - span_begin >= kGuestWindowSize &&
          span_end - span_begin > best_span_size) {
        best_span_base = (span_begin + kPageSize - 1) & ~(kPageSize - 1);
        best_span_size = span_end - span_begin;
      }
    }
    cursor = region_end;
  }
  if (best_span_base == 0 || best_span_size < kGuestWindowSize ||
      best_span_base + kValidatedAliasOffset < best_span_base) return false;
  // Return the base of the contiguous arena. The physical-alias probe adds
  // each guest offset itself, so the entire 4.5 GiB ReXGlue layout stays in
  // this free range rather than shifting beyond its end.
  *out_address = reinterpret_cast<void*>(best_span_base);
  if (log) {
    std::fprintf(log,
                 "selected validated guest arena: base 0x%016llX size 0x%llX sample interior 0x%016llX\n",
                 static_cast<unsigned long long>(best_span_base),
                 static_cast<unsigned long long>(best_span_size),
                 static_cast<unsigned long long>(best_span_base + kValidatedAliasOffset));
    std::fflush(log);
  }
  return true;
}

bool is_range_unmapped(void* address, u64 size, std::FILE* log, const char* label) {
  MemoryInfo info{};
  u32 page_info = 0;
  const Result rc = svcQueryMemory(&info, &page_info,
                                  reinterpret_cast<uintptr_t>(address));
  report(log, label, rc);
  const u64 range_begin = reinterpret_cast<uintptr_t>(address);
  const u64 range_end = range_begin + size;
  const bool unmapped = R_SUCCEEDED(rc) && info.type == MemType_Unmapped &&
                        info.addr <= range_begin && info.addr + info.size >= range_end;
  if (log) {
    std::fprintf(log, "%s: %s (type 0x%02X, queried addr 0x%016llX, region size 0x%llX, expected end 0x%016llX)\n",
                 label, unmapped ? "yes" : "no", info.type,
                 static_cast<unsigned long long>(info.addr),
                 static_cast<unsigned long long>(info.size),
                 static_cast<unsigned long long>(range_end));
    std::fflush(log);
  }
  return unmapped;
}

bool probe_sparse_guest_pages(void* address, void* mirror_address, std::FILE* log) {
  constexpr size_t kProbePages = 8192;
  constexpr size_t kExpectedSlabs =
      (kProbePages + green_lantern::horizon::SparseGuestPages::kPagesPerSlab - 1) /
      green_lantern::horizon::SparseGuestPages::kPagesPerSlab;
  constexpr size_t kProbeSize = kProbePages * green_lantern::horizon::SparseGuestPages::kPageSize;
  green_lantern::horizon::SparseGuestPages allocator;
  bool pass = true;
  for (u32 round = 0; round < 2 && pass; ++round) {
    const Result commit_result = allocator.Commit(address, kProbeSize, mirror_address);
    report(log, round == 0 ? "sparse pages round 1 commit 8192 pages"
                           : "sparse pages round 2 commit 8192 pages",
           commit_result);
    if (R_FAILED(commit_result)) return false;
    pass &= allocator.mapped_slabs() == kExpectedSlabs;

    for (size_t page = 0; page < kProbePages; ++page) {
      auto* words = reinterpret_cast<volatile u32*>(
          static_cast<u8*>(address) + page * green_lantern::horizon::SparseGuestPages::kPageSize);
      auto* mirror_words = reinterpret_cast<volatile u32*>(
          static_cast<u8*>(mirror_address) +
          page * green_lantern::horizon::SparseGuestPages::kPageSize);
      const void* page_address = static_cast<u8*>(address) +
                                 page * green_lantern::horizon::SparseGuestPages::kPageSize;
      if (words[0] != 0 || !allocator.IsCommitted(page_address)) {
        pass = false;
        break;
      }
      words[0] = 0x50414745u ^ (round << 16) ^ static_cast<u32>(page);
      if (mirror_words[0] != (0x50414745u ^ (round << 16) ^ static_cast<u32>(page))) {
        pass = false;
        break;
      }
      mirror_words[1] = ~static_cast<u32>(page + round);
      if (words[1] != ~static_cast<u32>(page + round)) { pass = false; break; }
    }
    if (log) {
      std::fprintf(log, "sparse pages round %u: committed=%u/%u mapped slabs=%u marker and zero checks=%s\n",
                   round + 1, static_cast<unsigned>(allocator.committed_pages()),
                   static_cast<unsigned>(kProbePages),
                   static_cast<unsigned>(allocator.mapped_slabs()), pass ? "pass" : "fail");
      std::fflush(log);
    }
    const void* release_address = (round == 0) ? address : mirror_address;
    const Result release_result = allocator.Decommit(const_cast<void*>(release_address), kProbeSize);
    report(log, round == 0 ? "sparse pages round 1 decommit 8192 pages"
                           : "sparse pages round 2 decommit 8192 pages",
           release_result);
    pass &= R_SUCCEEDED(release_result) && allocator.committed_pages() == 0 &&
            allocator.mapped_slabs() == 0;
    pass &= is_range_unmapped(address, kProbeSize, log, "sparse guest page range released");
    pass &= is_range_unmapped(mirror_address, kProbeSize, log, "sparse guest mirror range released");
  }
  if (log) {
    std::fprintf(log, "sparse guest page allocator lifecycle: %s\n", pass ? "pass" : "check");
    std::fflush(log);
  }
  return pass;
}

bool probe_sparse_guest_capacity(void* address, std::FILE* log) {
  constexpr size_t kTargetSize = 0x20000000;
  constexpr size_t kChunkSize = 0x04000000;
  constexpr size_t kPages = kTargetSize / green_lantern::horizon::SparseGuestPages::kPageSize;
  constexpr size_t kChunks = kTargetSize / kChunkSize;
  constexpr size_t kSlabs = kTargetSize / green_lantern::horizon::SparseGuestPages::kSlabSize;
  green_lantern::horizon::SparseGuestPages allocator;
  bool pass = true;

  if (log) {
    std::fprintf(log,
                 "\n512 MiB sparse virtual capacity: target pages=%u slabs=%u; guest window 0x10000000-0x2FFFFFFF\n",
                 static_cast<unsigned>(kPages), static_cast<unsigned>(kSlabs));
    std::fflush(log);
  }

  // Keep the game's full 512 MiB physical guest store mapped while exercising
  // 512 MiB of virtual commitments, so peak unique backing reaches 1 GiB.
  green_lantern::horizon::SharedMemoryBlock physical[kPhysicalChunkCount];
  auto* arena_base = static_cast<u8*>(address) - 0x10000000;
  size_t physical_created = 0;
  size_t physical_views = 0;
  size_t physical_offset = 0;
  bool physical_ok = true;
  for (size_t chunk = 0; chunk < kPhysicalChunkCount && physical_ok; ++chunk) {
    Result result = physical[chunk].Create(kPhysicalChunkSizes[chunk]);
    char label[96]{};
    std::snprintf(label, sizeof(label), "capacity physical chunk %u/%u create",
                  static_cast<unsigned>(chunk + 1), static_cast<unsigned>(kPhysicalChunkCount));
    report(log, label, result);
    if (R_FAILED(result)) {
      physical_ok = false;
      break;
    }
    ++physical_created;
    const size_t first_view = chunk == 0 ? 0 : 1;
    for (size_t view = first_view; view < kPhysicalAliasCount; ++view) {
      void* view_address = arena_base + kPhysicalAliasOffsets[view] + physical_offset;
      result = physical[chunk].MapAlias(view_address);
      std::snprintf(label, sizeof(label), "capacity physical chunk %u/%u alias %u/%u",
                    static_cast<unsigned>(chunk + 1), static_cast<unsigned>(kPhysicalChunkCount),
                    static_cast<unsigned>(view + 1), static_cast<unsigned>(kPhysicalAliasCount));
      report(log, label, result);
      if (R_FAILED(result)) {
        physical_ok = false;
        break;
      }
      ++physical_views;
    }
    physical_offset += kPhysicalChunkSizes[chunk];
  }
  if (!physical_ok || physical_offset != 0x20000000 ||
      physical_views != kExpectedPhysicalViews) {
    for (size_t i = physical_created; i > 0; --i) physical[i - 1].Close();
    return false;
  }
  if (log) {
    std::fprintf(log, "concurrent physical backing ready: %u chunks, %u aliases (512 MiB)\n",
                 static_cast<unsigned>(physical_created), static_cast<unsigned>(physical_views));
    std::fflush(log);
  }

  // Commit in bounded batches to expose progress and identify a resource ceiling.
  for (size_t chunk = 0; chunk < kChunks; ++chunk) {
    const Result result = allocator.Commit(static_cast<u8*>(address) + chunk * kChunkSize,
                                           kChunkSize);
    char label[96]{};
    std::snprintf(label, sizeof(label), "capacity commit chunk %u/%u (64 MiB)",
                  static_cast<unsigned>(chunk + 1), static_cast<unsigned>(kChunks));
    report(log, label, result);
    if (R_FAILED(result)) {
      if (log) {
        std::fprintf(log, "capacity failure detail: stage=%s slab_address=0x%016llX\n",
                     allocator.last_failure_stage(),
                     static_cast<unsigned long long>(allocator.last_failure_address()));
        std::fflush(log);
      }
      pass = false;
      break;
    }
    pass &= allocator.committed_pages() == ((chunk + 1) * kChunkSize /
                                            green_lantern::horizon::SparseGuestPages::kPageSize);
    pass &= allocator.mapped_slabs() == ((chunk + 1) * kChunkSize /
                                         green_lantern::horizon::SparseGuestPages::kSlabSize);
  }

  if (pass) {
    for (size_t page = 0; page < kPages; ++page) {
      auto* words = reinterpret_cast<volatile u32*>(
          static_cast<u8*>(address) + page * green_lantern::horizon::SparseGuestPages::kPageSize);
      if (words[0] != 0) {
        pass = false;
        break;
      }
      words[0] = 0x512BACC0u ^ static_cast<u32>(page);
      if (words[0] != (0x512BACC0u ^ static_cast<u32>(page))) {
        pass = false;
        break;
      }
    }
  }

  if (log) {
    std::fprintf(log, "512 MiB committed pages=%u/%u slabs=%u/%u zero/write check=%s\n",
                 static_cast<unsigned>(allocator.committed_pages()), static_cast<unsigned>(kPages),
                 static_cast<unsigned>(allocator.mapped_slabs()), static_cast<unsigned>(kSlabs),
                 pass ? "pass" : "fail");
    std::fflush(log);
  }

  for (size_t chunk = 0; chunk < kChunks; ++chunk) {
    const Result result = allocator.Decommit(static_cast<u8*>(address) + chunk * kChunkSize,
                                             kChunkSize);
    char label[96]{};
    std::snprintf(label, sizeof(label), "capacity decommit chunk %u/%u (64 MiB)",
                  static_cast<unsigned>(chunk + 1), static_cast<unsigned>(kChunks));
    report(log, label, result);
    if (R_FAILED(result)) {
      pass = false;
      break;
    }
  }
  pass &= allocator.committed_pages() == 0 && allocator.mapped_slabs() == 0;
  pass &= is_range_unmapped(address, kTargetSize, log, "512 MiB sparse guest range released");
  for (size_t i = physical_created; i > 0; --i) {
    const Result result = physical[i - 1].Close();
    char label[96]{};
    std::snprintf(label, sizeof(label), "capacity physical chunk %u/%u release",
                  static_cast<unsigned>(i), static_cast<unsigned>(physical_created));
    report(log, label, result);
    pass &= R_SUCCEEDED(result);
  }
  if (log) {
    std::fprintf(log, "512 MiB sparse virtual capacity: %s\n", pass ? "pass" : "fail");
    std::fflush(log);
  }
  return pass;
}
}  // namespace

int main(int, char**) {
  consoleInit(nullptr);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad{};
  padInitializeDefault(&pad);

  fsdevMountSdmc();
  std::FILE* log = std::fopen("sdmc:/green_lantern_vm_probe.log", "w");
  std::printf("Green Lantern - VM page reuse probe\n\n");
  std::printf("Log: %s\n", log ? "sdmc:/green_lantern_vm_probe.log" : "SD log open failed");
  u64 alias_base = 0, alias_size = 0;
  const Result alias_base_rc = svcGetInfo(&alias_base, InfoType_AliasRegionAddress,
                                          CUR_PROCESS_HANDLE, 0);
  const Result alias_size_rc = svcGetInfo(&alias_size, InfoType_AliasRegionSize,
                                          CUR_PROCESS_HANDLE, 0);
  std::printf("Alias region: 0x%016llX + 0x%llX\n",
              static_cast<unsigned long long>(alias_base),
              static_cast<unsigned long long>(alias_size));
  std::fflush(stdout);
  consoleUpdate(NULL);
  if (log) {
    std::fprintf(log, "Alias region base: result 0x%08X value 0x%016llX\n",
                 static_cast<unsigned>(alias_base_rc), static_cast<unsigned long long>(alias_base));
    std::fprintf(log, "Alias region size: result 0x%08X value 0x%016llX\n",
                 static_cast<unsigned>(alias_size_rc), static_cast<unsigned long long>(alias_size));
    std::fflush(log);
  }

  if (envHasHeapOverride() && envGetHeapOverrideSize() > kHeapTarget) {
    void* new_heap = nullptr;
    const Result rc = svcSetHeapSize(&new_heap, kHeapTarget);
    report(log, "shrink loader heap to 1 GiB", rc);
    if (R_SUCCEEDED(rc)) {
      fake_heap_start = static_cast<char*>(new_heap);
      fake_heap_end = fake_heap_start + kHeapTarget;
    }
  }

  void* address = nullptr;
  bool overall_pass = R_SUCCEEDED(alias_base_rc) && R_SUCCEEDED(alias_size_rc) &&
                      find_free_page(&address, log);
  if (!overall_pass) {
    std::printf("Could not locate a safe free span.\n");
  } else {
    green_lantern::horizon::SetSparseGuestArenaBase(address);
    void* sparse_page_test = static_cast<u8*>(address) + 0x80000000;
    void* sparse_page_mirror = static_cast<u8*>(address) + 0x90000000;
    overall_pass = probe_sparse_guest_pages(sparse_page_test, sparse_page_mirror, log);
    if (overall_pass) {
      overall_pass = probe_sparse_guest_capacity(static_cast<u8*>(address) + 0x10000000, log);
    }
    std::printf("Testing 512 MiB backing in %u chunks / five views\n",
                static_cast<unsigned>(kPhysicalChunkCount));
    for (size_t round = 0; round < 2 && overall_pass; ++round) {
      Handle handles[kPhysicalChunkCount]{};
      void* views[kPhysicalChunkCount][kPhysicalAliasCount]{};
      bool mapped[kPhysicalChunkCount][kPhysicalAliasCount]{};
      size_t created = 0, mapped_count = 0, verified = 0, unmapped = 0, closed = 0;
      char label[96]{};
      if (log) {
        std::fprintf(log, "\n512 MiB alias round %u begin\n", static_cast<unsigned>(round + 1));
        std::fflush(log);
      }
      size_t backing_offset = 0;
      for (size_t chunk = 0; chunk < kPhysicalChunkCount && overall_pass; ++chunk) {
        const size_t chunk_size = kPhysicalChunkSizes[chunk];
        Result rc = svcCreateSharedMemory(&handles[chunk], chunk_size, Perm_Rw, Perm_DontCare);
        std::snprintf(label, sizeof(label), "round %u create chunk %u size 0x%zX",
                      static_cast<unsigned>(round + 1), static_cast<unsigned>(chunk), chunk_size);
        report(log, label, rc);
        if (R_FAILED(rc)) { overall_pass = false; break; }
        ++created;

        const size_t first_alias = chunk == 0 ? 0 : 1;
        for (size_t view = first_alias; view < kPhysicalAliasCount; ++view) {
          const u64 guest_offset = kPhysicalAliasOffsets[view] + backing_offset;
          views[chunk][view] = static_cast<u8*>(address) + guest_offset;
          rc = svcMapSharedMemory(handles[chunk], views[chunk][view], chunk_size, Perm_Rw);
          std::snprintf(label, sizeof(label), "round %u map chunk %u view %u",
                        static_cast<unsigned>(round + 1), static_cast<unsigned>(chunk),
                        static_cast<unsigned>(view));
          report(log, label, rc);
          if (R_FAILED(rc)) { overall_pass = false; break; }
          mapped[chunk][view] = true;
          ++mapped_count;
        }

        if (overall_pass) {
          const size_t probe_offsets[] = {0x1000, chunk_size / 2, chunk_size - sizeof(u32)};
          for (size_t n = 0; n < 3; ++n) {
            const u32 value = 0x474C0000u | (static_cast<u32>(round) << 12) |
                              (static_cast<u32>(chunk) << 4) | static_cast<u32>(n);
            *reinterpret_cast<volatile u32*>(static_cast<u8*>(views[chunk][1]) + probe_offsets[n]) = value;
            for (size_t view = 2; view < kPhysicalAliasCount; ++view) {
              const u32 observed = *reinterpret_cast<volatile u32*>(
                  static_cast<u8*>(views[chunk][view]) + probe_offsets[n]);
              if (observed != value) overall_pass = false;
              else ++verified;
            }
            if (chunk == 0) {
              const u32 observed = *reinterpret_cast<volatile u32*>(
                  static_cast<u8*>(views[chunk][0]) + probe_offsets[n]);
              if (observed != value) overall_pass = false;
              else ++verified;
            }
          }
        }
        backing_offset += chunk_size;
      }

      for (size_t chunk = 0; chunk < kPhysicalChunkCount; ++chunk) {
        for (size_t view = 0; view < kPhysicalAliasCount; ++view) {
          if (!mapped[chunk][view]) continue;
          if (log) {
            std::fprintf(log, "round %u about to unmap chunk %u view %u at %p\n",
                         static_cast<unsigned>(round + 1), static_cast<unsigned>(chunk),
                         static_cast<unsigned>(view), views[chunk][view]);
            std::fflush(log);
          }
          const Result rc = svcUnmapSharedMemory(handles[chunk], views[chunk][view],
                                                 kPhysicalChunkSizes[chunk]);
          std::snprintf(label, sizeof(label), "round %u unmap chunk %u view %u",
                        static_cast<unsigned>(round + 1), static_cast<unsigned>(chunk),
                        static_cast<unsigned>(view));
          report(log, label, rc);
          if (R_SUCCEEDED(rc)) { ++unmapped; mapped[chunk][view] = false; }
          else overall_pass = false;
        }
      }
      bool all_ranges_free = true;
      for (size_t chunk = 0; chunk < kPhysicalChunkCount; ++chunk) {
        const size_t first_alias = chunk == 0 ? 0 : 1;
        for (size_t view = first_alias; view < kPhysicalAliasCount; ++view) {
          if (mapped[chunk][view]) { all_ranges_free = false; continue; }
          all_ranges_free &= is_range_unmapped(views[chunk][view],
                                               kPhysicalChunkSizes[chunk], log,
                                               "physical alias released");
        }
      }
      overall_pass &= all_ranges_free;
      for (size_t chunk = 0; chunk < created; ++chunk) {
        const Result close_rc = svcCloseHandle(handles[chunk]);
        std::snprintf(label, sizeof(label), "round %u close chunk %u handle",
                      static_cast<unsigned>(round + 1), static_cast<unsigned>(chunk));
        report(log, label, close_rc);
        if (R_SUCCEEDED(close_rc)) ++closed;
        else overall_pass = false;
      }
      if (log) {
        std::fprintf(log,
                     "round %u totals: chunks created %u/%u mapped views %u expected %u verified alias words %u unmapped views %u/%u all ranges free %s handles closed %u/%u\n",
                     static_cast<unsigned>(round + 1), static_cast<unsigned>(created),
                     static_cast<unsigned>(kPhysicalChunkCount), static_cast<unsigned>(mapped_count),
                     static_cast<unsigned>(kExpectedPhysicalViews), static_cast<unsigned>(verified),
                     static_cast<unsigned>(unmapped), static_cast<unsigned>(kExpectedPhysicalViews),
                     all_ranges_free ? "yes" : "no", static_cast<unsigned>(closed),
                     static_cast<unsigned>(created));
        std::fflush(log);
      }
    }
  }
  std::printf("\n512 MiB physical alias release/reuse: %s\n", overall_pass ? "PASS" : "CHECK LOG");
  if (log) {
    std::fprintf(log, "\n512 MiB physical alias release/reuse: %s\n", overall_pass ? "pass" : "check");
    std::fflush(log);
  }

  std::printf("\nPress + to return to Sphaira.\n");
  if (log) std::fclose(log);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(NULL);
  }
  consoleExit(NULL);
  return 0;
}
