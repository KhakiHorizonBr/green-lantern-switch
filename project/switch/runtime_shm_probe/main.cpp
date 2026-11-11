#include <switch.h>

#include <cstdio>

size_t __nx_heap_size = 0x40000000;
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

namespace {
constexpr size_t kPageSize = 0x1000;
constexpr size_t kProbeSize = 0x01000000;  // Match the 16 MiB guest-memory slabs.
constexpr size_t kAliasCount = 9;
constexpr size_t kReservationSize = kProbeSize * (kAliasCount + 1);
constexpr u64 kHeapTarget = 0x40000000;

struct ProbeState {
  std::FILE* log = nullptr;
  VirtmemReservation* reservation = nullptr;
  Handle shared = INVALID_HANDLE;
  void* source = nullptr;
  void* aliases[kAliasCount]{};
  bool shared_mapped = false;
  size_t process_aliases_mapped = 0;
};

void line(ProbeState& state, const char* text, Result result = 0) {
  std::printf("%s: 0x%08X\n", text, static_cast<unsigned>(result));
  std::fflush(stdout);
  if (state.log) {
    std::fprintf(state.log, "%s: 0x%08X\n", text, static_cast<unsigned>(result));
    std::fflush(state.log);
  }
  consoleUpdate(nullptr);
}

bool find_free_span(void** out_base, ProbeState& state) {
  u64 aslr_base = 0, aslr_size = 0, heap_base = 0, heap_size = 0;
  Result rc = svcGetInfo(&aslr_base, InfoType_AslrRegionAddress, CUR_PROCESS_HANDLE, 0);
  line(state, "query ASLR base", rc);
  if (R_FAILED(rc)) return false;
  rc = svcGetInfo(&aslr_size, InfoType_AslrRegionSize, CUR_PROCESS_HANDLE, 0);
  line(state, "query ASLR size", rc);
  if (R_FAILED(rc) || aslr_base + aslr_size < aslr_base) return false;
  const bool have_heap = R_SUCCEEDED(svcGetInfo(&heap_base, InfoType_HeapRegionAddress,
                                                 CUR_PROCESS_HANDLE, 0)) &&
                         R_SUCCEEDED(svcGetInfo(&heap_size, InfoType_HeapRegionSize,
                                               CUR_PROCESS_HANDLE, 0)) &&
                         heap_base + heap_size >= heap_base;
  const u64 heap_end = have_heap ? heap_base + heap_size : 0;
  const u64 aslr_end = aslr_base + aslr_size;

  for (u64 cursor = aslr_base; cursor < aslr_end;) {
    MemoryInfo info{};
    u32 page_info = 0;
    rc = svcQueryMemory(&info, &page_info, cursor);
    if (R_FAILED(rc) || !info.size || info.addr + info.size <= cursor) {
      line(state, "scan ASLR for unmapped probe range", rc);
      return false;
    }
    const u64 region_end = info.addr + info.size;
    if (info.type == MemType_Unmapped) {
      const u64 begin = info.addr > aslr_base ? info.addr : aslr_base;
      const u64 end = region_end < aslr_end ? region_end : aslr_end;
      const bool overlaps_heap = have_heap && begin < heap_end && end > heap_base;
      if (!overlaps_heap && end >= begin && end - begin >= kReservationSize) {
        const u64 aligned = (begin + kProbeSize - 1) & ~(u64(kProbeSize) - 1);
        if (aligned >= begin && end - aligned >= kReservationSize) {
          *out_base = reinterpret_cast<void*>(aligned);
          if (state.log) {
            std::fprintf(state.log, "selected probe span: base=0x%016llX size=0x%zX\n",
                         static_cast<unsigned long long>(aligned), kReservationSize);
            std::fflush(state.log);
          }
          return true;
        }
      }
    }
    cursor = region_end;
  }
  return false;
}

void cleanup(ProbeState& state) {
  while (state.process_aliases_mapped) {
    void* alias = state.aliases[--state.process_aliases_mapped];
    line(state, "cleanup svcUnmapProcessMemory(alias)",
         svcUnmapProcessMemory(alias, envGetOwnProcessHandle(),
                               reinterpret_cast<u64>(state.source), kProbeSize));
  }
  if (state.shared_mapped) {
    line(state, "cleanup svcUnmapSharedMemory(source)",
         svcUnmapSharedMemory(state.shared, state.source, kProbeSize));
    state.shared_mapped = false;
  }
  if (state.shared != INVALID_HANDLE) {
    line(state, "cleanup svcCloseHandle(shared)", svcCloseHandle(state.shared));
    state.shared = INVALID_HANDLE;
  }
  if (state.reservation) {
    virtmemLock();
    virtmemRemoveReservation(state.reservation);
    virtmemUnlock();
    state.reservation = nullptr;
  }
}

bool run_probe(ProbeState& state) {
  Result rc = svcCreateSharedMemory(&state.shared, kProbeSize, Perm_Rw, Perm_DontCare);
  line(state, "svcCreateSharedMemory(16 MiB)", rc);
  if (R_FAILED(rc)) return false;

  if (!find_free_span(&state.source, state)) {
    line(state, "find free source and alias range", MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
    return false;
  }
  virtmemLock();
  state.reservation = virtmemAddReservation(state.source, kReservationSize);
  virtmemUnlock();
  if (!state.reservation) {
    line(state, "reserve source and alias addresses", MAKERESULT(Module_Libnx, LibnxError_OutOfMemory));
    return false;
  }

  rc = svcMapSharedMemory(state.shared, state.source, kProbeSize, Perm_Rw);
  line(state, "svcMapSharedMemory(source)", rc);
  if (R_FAILED(rc)) return false;
  state.shared_mapped = true;
  *static_cast<volatile u32*>(state.source) = 0x53484D31;

  for (size_t i = 0; i < kAliasCount; ++i) {
    state.aliases[i] = static_cast<u8*>(state.source) + kProbeSize * (i + 1);
    rc = svcMapProcessMemory(state.aliases[i], envGetOwnProcessHandle(),
                             reinterpret_cast<u64>(state.source), kProbeSize);
    char label[96];
    std::snprintf(label, sizeof(label), "svcMapProcessMemory(shared source alias %zu/9)", i + 1);
    line(state, label, rc);
    if (R_FAILED(rc)) return false;
    ++state.process_aliases_mapped;
  }

  const u32 from_source = *static_cast<volatile u32*>(state.source);
  const u32 from_alias = *static_cast<volatile u32*>(state.aliases[kAliasCount - 1]);
  if (from_source != 0x53484D31 || from_alias != from_source) {
    line(state, "shared source/nine-view coherence", MAKERESULT(Module_Libnx, LibnxError_BadInput));
    return false;
  }
  *static_cast<volatile u32*>(state.aliases[kAliasCount - 1]) = 0x56494557;
  if (*static_cast<volatile u32*>(state.source) != 0x56494557) {
    line(state, "guest alias write reaches shared backing", MAKERESULT(Module_Libnx, LibnxError_BadInput));
    return false;
  }

  auto* protected_page = static_cast<u8*>(state.aliases[0]) + kPageSize;
  const Result page_none = svcSetProcessMemoryPermission(envGetOwnProcessHandle(),
                                     reinterpret_cast<u64>(protected_page), kPageSize, Perm_None);
  line(state, "set one alias page to Perm_None", page_none);
  if (R_SUCCEEDED(page_none)) {
    rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(),
                                      reinterpret_cast<u64>(protected_page), kPageSize, Perm_Rw);
    line(state, "restore alias page to Perm_Rw", rc);
  }

  const Result region_none = svcSetProcessMemoryPermission(envGetOwnProcessHandle(),
                                     reinterpret_cast<u64>(state.aliases[0]), kProbeSize, Perm_None);
  line(state, "set whole alias to Perm_None", region_none);
  if (R_SUCCEEDED(region_none)) {
    rc = svcSetProcessMemoryPermission(envGetOwnProcessHandle(),
                                       reinterpret_cast<u64>(state.aliases[0]), kProbeSize, Perm_Rw);
    line(state, "restore whole alias to Perm_Rw", rc);
  }
  line(state, "shared backing <-> nine aliases coherence", 0);
  return true;
}
}  // namespace

int main(int, char**) {
  consoleInit(nullptr);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad{};
  padInitializeDefault(&pad);
  fsdevMountSdmc();

  ProbeState state{};
  state.log = std::fopen("sdmc:/switch/green_lantern/code_memory_shm_probe.log", "w");
  std::printf("Green Lantern - shared-memory process-alias probe\n\n");
  std::printf("Log: %s\n", state.log ? "sdmc:/switch/green_lantern/code_memory_shm_probe.log"
                                     : "SD log open failed");
  std::fflush(stdout);
  consoleUpdate(nullptr);

  if (envHasHeapOverride() && envGetHeapOverrideSize() > kHeapTarget) {
    void* new_heap = nullptr;
    const Result rc = svcSetHeapSize(&new_heap, kHeapTarget);
    line(state, "shrink loader heap to 1 GiB", rc);
    if (R_SUCCEEDED(rc)) {
      fake_heap_start = static_cast<char*>(new_heap);
      fake_heap_end = fake_heap_start + kHeapTarget;
    }
  }

  const bool pass = run_probe(state);
  cleanup(state);
  if (state.log) {
    std::fprintf(state.log, "\ndirect shared-memory guest alias: %s\n", pass ? "PASS" : "FAIL");
    std::fflush(state.log);
    std::fclose(state.log);
    state.log = nullptr;
  }
  std::printf("\nShared-memory guest process aliases: %s\n", pass ? "PASS" : "FAIL; CHECK LOG");
  std::printf("Press + to return to Sphaira.\n");
  consoleUpdate(nullptr);
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    consoleUpdate(nullptr);
  }
  consoleExit(nullptr);
  return 0;
}
