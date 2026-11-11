#include <switch.h>

#include <atomic>
#include <cerrno>
#include <cstdio>

// When no loader-provided heap override exists, keep 1 GiB for ordinary
// allocations and leave room for guest RAM to be tested through shared memory.
size_t __nx_heap_size = 0x40000000;
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;

namespace {
constexpr unsigned kSvcCreateSharedMemory = 0x50;
constexpr u64 kGuestMappingWindow = 0x120000000;
constexpr size_t kPhysicalBackingSize = 0x20000000;
constexpr size_t kProbeChunkSizes[] = {
    0x01000000, // XPS view also aliases the first physical chunk.
    0x04000000, 0x04000000, 0x04000000, 0x04000000,
    0x04000000, 0x04000000, 0x04000000,
    0x01000000, 0x01000000, 0x01000000,
};
constexpr size_t kProbeChunkCount = sizeof(kProbeChunkSizes) / sizeof(kProbeChunkSizes[0]);
constexpr size_t probe_chunks_total() {
    size_t total = 0;
    for (size_t chunk_size : kProbeChunkSizes) total += chunk_size;
    return total;
}
static_assert(probe_chunks_total() == kPhysicalBackingSize);
constexpr size_t kProbeAliasCount = 5;
struct ProbeAlias {
    u64 guest_address;
    const char* name;
};
constexpr ProbeAlias kProbeAliases[] = {
    {0x7F000000, "guest 0x7F000000 (16 MiB XPS view)"},
    {0xA0000000, "guest 0xA0000000 (physical 64 KiB view)"},
    {0xC0000000, "guest 0xC0000000 (physical 16 MiB view)"},
    {0xE0000000, "guest 0xE0000000 (physical 4 KiB view; aligned for full-span test)"},
    {0x100000000, "guest 0x100000000 (physical raw view)"},
};

void log_line(std::FILE* file, const char* label, Result result) {
    if (file) {
        std::fprintf(file, "%s: 0x%08X\n", label, static_cast<unsigned>(result));
        std::fflush(file);
    }
}

void log_info(std::FILE* file, const char* label, InfoType type) {
    u64 value = 0;
    const Result result = svcGetInfo(&value, type, CUR_PROCESS_HANDLE, 0);
    if (file) {
        std::fprintf(file, "%s: result 0x%08X value 0x%016llX\n", label,
                     static_cast<unsigned>(result), static_cast<unsigned long long>(value));
        std::fflush(file);
    }
}

const char* applet_type_name(AppletType type) {
    switch (type) {
        case AppletType_Application: return "Application";
        case AppletType_SystemApplet: return "SystemApplet";
        case AppletType_LibraryApplet: return "LibraryApplet";
        case AppletType_OverlayApplet: return "OverlayApplet";
        case AppletType_SystemApplication: return "SystemApplication";
        default: return "Default/Unknown";
    }
}

void log_resource_limit(std::FILE* file) {
    u64 raw_handle = 0;
    const Result handle_result = svcGetInfo(&raw_handle, InfoType_ResourceLimit,
                                            0, 0);
    log_line(file, "svcGetInfo(ResourceLimit handle)", handle_result);
    if (R_FAILED(handle_result)) {
        return;
    }

    const Handle resource_handle = static_cast<Handle>(raw_handle);
    struct ResourceInfo { LimitableResource id; const char* name; };
    constexpr ResourceInfo resources[] = {
        {LimitableResource_Memory, "Memory"},
        {LimitableResource_Threads, "Threads"},
        {LimitableResource_TransferMemories, "TransferMemories"},
    };
    const bool limit_hint = envIsSyscallHinted(0x30);
    const bool current_hint = envIsSyscallHinted(0x31);
    for (const ResourceInfo& item : resources) {
        if (!limit_hint && !current_hint) {
            if (file) {
                std::fprintf(file, "resource limit %s: SVC 0x30/0x31 not hinted\n", item.name);
                std::fflush(file);
            }
            break;
        }
        if (limit_hint) {
            s64 value = 0;
            const Result rc = svcGetResourceLimitLimitValue(&value, resource_handle, item.id);
            if (file) {
                std::fprintf(file, "resource limit %s maximum: result 0x%08X value %lld (0x%llX)\n",
                             item.name, static_cast<unsigned>(rc), static_cast<long long>(value),
                             static_cast<unsigned long long>(value));
                std::fflush(file);
            }
        }
        if (current_hint) {
            s64 value = 0;
            const Result rc = svcGetResourceLimitCurrentValue(&value, resource_handle, item.id);
            if (file) {
                std::fprintf(file, "resource limit %s current: result 0x%08X value %lld (0x%llX)\n",
                             item.name, static_cast<unsigned>(rc), static_cast<long long>(value),
                             static_cast<unsigned long long>(value));
                std::fflush(file);
            }
        }
    }
}

void shrink_heap_for_shared_memory(std::FILE* file) {
    constexpr u64 kTargetHeapSize = 0x40000000;
    const bool has_override = envHasHeapOverride();
    const u64 current_size = has_override ? envGetHeapOverrideSize() : __nx_heap_size;
    if (file) {
        std::fprintf(file, "heap size before shrink: 0x%016llX\n",
                     static_cast<unsigned long long>(current_size));
        std::fflush(file);
    }
    if (current_size <= kTargetHeapSize) {
        if (file) {
            std::fprintf(file, "heap shrink skipped: current heap is already at or below 1 GiB\n");
            std::fflush(file);
        }
        return;
    }

    void* new_heap_address = nullptr;
    const Result result = svcSetHeapSize(&new_heap_address, kTargetHeapSize);
    log_line(file, "svcSetHeapSize(1 GiB)", result);
    if (R_SUCCEEDED(result)) {
        fake_heap_start = static_cast<char*>(new_heap_address);
        fake_heap_end = fake_heap_start + kTargetHeapSize;
        if (file) {
            std::fprintf(file, "heap after shrink address: %p size: 0x%016llX\n",
                         new_heap_address,
                         static_cast<unsigned long long>(kTargetHeapSize));
            std::fflush(file);
        }
    }
}

bool scan_aslr_free_space(u64* address_out, u64* base_out, u64* size_out,
                          u64* largest_free_out, u64* largest_free_base_out,
                          u64* alias_span_out, u64* alias_span_base_out,
                          std::FILE* log) {
    u64 base = 0;
    u64 size = 0;
    const Result base_rc = svcGetInfo(&base, InfoType_AslrRegionAddress,
                                     CUR_PROCESS_HANDLE, 0);
    const Result size_rc = svcGetInfo(&size, InfoType_AslrRegionSize,
                                     CUR_PROCESS_HANDLE, 0);
    u64 heap_region_base = 0;
    u64 heap_region_size = 0;
    const Result heap_base_rc = svcGetInfo(&heap_region_base, InfoType_HeapRegionAddress,
                                           CUR_PROCESS_HANDLE, 0);
    const Result heap_size_rc = svcGetInfo(&heap_region_size, InfoType_HeapRegionSize,
                                           CUR_PROCESS_HANDLE, 0);
    log_line(log, "svcGetInfo(ASLR base)", base_rc);
    log_line(log, "svcGetInfo(ASLR size)", size_rc);
    log_line(log, "svcGetInfo(heap region base)", heap_base_rc);
    log_line(log, "svcGetInfo(heap region size)", heap_size_rc);
    if (R_FAILED(base_rc) || R_FAILED(size_rc) || size < 0x2000 || base + size < base) {
        return false;
    }
    const bool heap_region_valid = R_SUCCEEDED(heap_base_rc) && R_SUCCEEDED(heap_size_rc) &&
                                   heap_region_base + heap_region_size >= heap_region_base;
    const u64 heap_region_end = heap_region_valid ? heap_region_base + heap_region_size : 0;
    if (log && heap_region_valid) {
        std::fprintf(log, "heap reserved region: base 0x%016llX size 0x%016llX\n",
                     static_cast<unsigned long long>(heap_region_base),
                     static_cast<unsigned long long>(heap_region_size));
        std::fflush(log);
    }

    *base_out = base;
    *size_out = size;
    const u64 end = base + size;
    u64 cursor = base;
    u64 candidate = 0;
    u64 largest_free = 0;
    u64 largest_free_base = 0;
    u64 alias_span = 0;
    u64 alias_span_base = 0;
    while (cursor <= end - 0x2000) {
        MemoryInfo info{};
        u32 page_info = 0;
        const Result query_rc = svcQueryMemory(&info, &page_info, cursor);
        if (R_FAILED(query_rc) || info.size == 0 || info.addr + info.size <= cursor) {
            log_line(log, "svcQueryMemory(ASLR scan)", query_rc);
            return false;
        }

        if (info.type == MemType_Unmapped) {
            const u64 info_end = info.addr + info.size;
            const u64 span_begin = info.addr > base ? info.addr : base;
            const u64 span_end = info_end < end ? info_end : end;
            const u64 first_page = (span_begin + 0xFFF) & ~u64(0xFFF);
            if (span_end >= span_begin && first_page >= span_begin &&
                first_page + 0x2000 >= first_page && first_page + 0x2000 <= span_end) {
                if (candidate == 0) {
                    candidate = first_page;
                }
                const u64 free_size = span_end - span_begin;
                if (free_size > largest_free) {
                    largest_free = free_size;
                    largest_free_base = span_begin;
                }
                // The heap region is reserved for heap growth and is not a
                // safe destination for shared-memory aliases, even where a
                // shrunken heap leaves pages currently reported as unmapped.
                const bool overlaps_heap_region = heap_region_valid &&
                    span_begin < heap_region_end && span_end > heap_region_base;
                if (!overlaps_heap_region && free_size >= kGuestMappingWindow &&
                    free_size > alias_span) {
                    alias_span = free_size;
                    alias_span_base = first_page;
                }
            }
        }
        cursor = info.addr + info.size;
    }
    *address_out = candidate;
    *largest_free_out = largest_free;
    *largest_free_base_out = largest_free_base;
    *alias_span_out = alias_span;
    *alias_span_base_out = alias_span_base;
    if (log) {
        std::fprintf(log, "free ASLR pair candidate: 0x%016llX\n",
                     static_cast<unsigned long long>(candidate));
        std::fprintf(log, "largest unmapped ASLR span: base 0x%016llX size 0x%016llX\n",
                     static_cast<unsigned long long>(largest_free_base),
                     static_cast<unsigned long long>(largest_free));
        std::fprintf(log, "largest free span outside heap region fitting guest window: base 0x%016llX size 0x%016llX\n",
                     static_cast<unsigned long long>(alias_span_base),
                     static_cast<unsigned long long>(alias_span));
        std::fprintf(log, "guest 4.5 GiB window fits in one unmapped span: %s\n",
                     largest_free >= kGuestMappingWindow ? "yes" : "no");
        std::fflush(log);
    }
    return candidate != 0;
}
}  // namespace

int main(int, char**) {
    consoleInit(nullptr);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad{};
    padInitializeDefault(&pad);

    std::FILE* log = nullptr;
    const Result mount_result = fsdevMountSdmc();
    errno = 0;
    log = std::fopen("sdmc:/green_lantern_memory_probe.log", "w");
    const int log_open_errno = errno;

    std::printf("Green Lantern - memory capability probe\n\n");
    std::printf("SD mount: 0x%08X\n", static_cast<unsigned>(mount_result));
    std::printf("Log open: %s (errno %d)\n", log ? "YES" : "NO", log_open_errno);
    log_line(log, "fsdevMountSdmc", mount_result);

    const AppletType applet_type = appletGetAppletType();
    std::printf("Applet type: %d (%s)\n", static_cast<int>(applet_type),
                applet_type_name(applet_type));
    if (log) {
        std::fprintf(log, "applet type: %d (%s)\n", static_cast<int>(applet_type),
                     applet_type_name(applet_type));
        std::fflush(log);
    }
    const bool heap_override = envHasHeapOverride();
    std::printf("Heap override: %s\n", heap_override ? "YES" : "NO");
    if (log) {
        std::fprintf(log, "envHasHeapOverride: %s\n", heap_override ? "yes" : "no");
        if (heap_override) {
            std::fprintf(log, "loader heap override address: %p size: 0x%016llX\n",
                         envGetHeapOverrideAddr(),
                         static_cast<unsigned long long>(envGetHeapOverrideSize()));
        } else {
            std::fprintf(log, "configured __nx_heap_size: 0x%016llX\n",
                         static_cast<unsigned long long>(__nx_heap_size));
        }
        std::fflush(log);
    }
    log_info(log, "InfoType_IsApplication", InfoType_IsApplication);
    log_info(log, "InfoType_TotalMemorySize", InfoType_TotalMemorySize);
    log_info(log, "InfoType_UsedMemorySize", InfoType_UsedMemorySize);
    log_info(log, "InfoType_SystemResourceSizeTotal", InfoType_SystemResourceSizeTotal);
    log_info(log, "InfoType_SystemResourceSizeUsed", InfoType_SystemResourceSizeUsed);
    u64 physical_total = 0;
    u64 physical_used = 0;
    const Result physical_total_rc = svcGetSystemInfo(
        &physical_total, SystemInfoType_TotalPhysicalMemorySize, 0, 0);
    const Result physical_used_rc = svcGetSystemInfo(
        &physical_used, SystemInfoType_UsedPhysicalMemorySize, 0, 0);
    if (log) {
        std::fprintf(log, "physical DRAM total: result 0x%08X value 0x%016llX\n",
                     static_cast<unsigned>(physical_total_rc),
                     static_cast<unsigned long long>(physical_total));
        std::fprintf(log, "physical DRAM used: result 0x%08X value 0x%016llX\n",
                     static_cast<unsigned>(physical_used_rc),
                     static_cast<unsigned long long>(physical_used));
        std::fflush(log);
    }
    log_resource_limit(log);
    shrink_heap_for_shared_memory(log);
    log_info(log, "InfoType_UsedMemorySize(after heap resize)", InfoType_UsedMemorySize);
    log_resource_limit(log);

    const bool hinted = envIsSyscallHinted(kSvcCreateSharedMemory);
    std::printf("SVC 0x50 hinted: %s\n", hinted ? "YES" : "NO");
    if (log) {
        std::fprintf(log, "svcCreateSharedMemory hinted: %s\n", hinted ? "yes" : "no");
        std::fflush(log);
    }

    if (hinted) {
        log_info(log, "InfoType_TotalMemorySize(before)", InfoType_TotalMemorySize);
        log_info(log, "InfoType_UsedMemorySize(before)", InfoType_UsedMemorySize);
        u64 candidate = 0;
        u64 aslr_base = 0;
        u64 aslr_size = 0;
        u64 largest_free = 0;
        u64 largest_free_base = 0;
        u64 alias_span = 0;
        u64 alias_span_base = 0;
        const bool found = scan_aslr_free_space(
            &candidate, &aslr_base, &aslr_size, &largest_free, &largest_free_base,
            &alias_span, &alias_span_base, log);
        std::printf("ASLR region: base 0x%016llX size 0x%016llX\n",
                    static_cast<unsigned long long>(aslr_base),
                    static_cast<unsigned long long>(aslr_size));
        std::printf("Largest free ASLR span: base 0x%016llX size 0x%016llX\n",
                    static_cast<unsigned long long>(largest_free_base),
                    static_cast<unsigned long long>(largest_free));
        const u64 alias_base = alias_span_base;
        const bool aliases_fit = found && alias_span >= kGuestMappingWindow &&
                                 alias_base + kGuestMappingWindow >= alias_base &&
                                 alias_base + kGuestMappingWindow <= alias_span_base + alias_span;
        std::printf("Exact ReXGlue guest window fits in one free span: %s\n",
                    aliases_fit ? "YES" : "NO");

        size_t completed_chunks = 0;
        size_t mapped_views = 0;
        size_t backing_offset = 0;
        bool aliases_match = aliases_fit;
        if (aliases_fit) {
            constexpr size_t physical_alias_indices[] = {2, 3, 4};
            for (size_t chunk = 0; chunk < kProbeChunkCount; ++chunk) {
                const size_t chunk_size = kProbeChunkSizes[chunk];
                const size_t offsets[] = {0x1000, 0x2000, chunk_size / 2,
                                          chunk_size - sizeof(u32)};
                Handle handle = INVALID_HANDLE;
                const Result create_result = svcCreateSharedMemory(
                    &handle, chunk_size, Perm_Rw, Perm_DontCare);
                char label[80]{};
                std::snprintf(label, sizeof(label), "create chunk %u size 0x%zX",
                              static_cast<unsigned>(chunk), chunk_size);
                log_line(log, label, create_result);
                if (R_FAILED(create_result)) {
                    std::printf("Create chunk %u failed: 0x%08X\n",
                                static_cast<unsigned>(chunk),
                                static_cast<unsigned>(create_result));
                    aliases_match = false;
                    break;
                }

                void* aliases[kProbeAliasCount]{};
                size_t chunk_views = 0;
                for (size_t i = (chunk == 0 ? 0 : 1); i < kProbeAliasCount; ++i) {
                    aliases[i] = reinterpret_cast<void*>(
                        alias_base + kProbeAliases[i].guest_address + backing_offset);
                    const Result map_result = svcMapSharedMemory(
                        handle, aliases[i], chunk_size, Perm_Rw);
                    std::snprintf(label, sizeof(label), "map chunk %u view %u",
                                  static_cast<unsigned>(chunk), static_cast<unsigned>(i));
                    log_line(log, label, map_result);
                    if (R_FAILED(map_result)) {
                        std::printf("Map chunk %u at view %u failed: 0x%08X\n",
                                    static_cast<unsigned>(chunk), static_cast<unsigned>(i),
                                    static_cast<unsigned>(map_result));
                        aliases_match = false;
                        break;
                    }
                    ++chunk_views;
                    ++mapped_views;
                }

                const size_t expected_views = chunk == 0 ? kProbeAliasCount
                                                          : kProbeAliasCount - 1;
                if (chunk_views == expected_views) {
                    for (size_t n = 0; n < 4; ++n) {
                        *reinterpret_cast<volatile u32*>(
                            static_cast<u8*>(aliases[1]) + offsets[n]) =
                            static_cast<u32>(0x4C410000u + chunk * 4 + n);
                    }
                    std::atomic_thread_fence(std::memory_order_seq_cst);
                    for (size_t n = 0; n < 4; ++n) {
                        const u32 marker = static_cast<u32>(0x4C410000u + chunk * 4 + n);
                        for (size_t i : physical_alias_indices) {
                            const u32 observed = *reinterpret_cast<volatile u32*>(
                                static_cast<u8*>(aliases[i]) + offsets[n]);
                            aliases_match &= observed == marker;
                        }
                        if (chunk == 0) {
                            const u32 xps = *reinterpret_cast<volatile u32*>(
                                static_cast<u8*>(aliases[0]) + offsets[n]);
                            aliases_match &= xps == marker;
                        }
                    }
                    ++completed_chunks;
                } else {
                    aliases_match = false;
                }

                const Result close_result = svcCloseHandle(handle);
                std::snprintf(label, sizeof(label), "close chunk %u handle",
                              static_cast<unsigned>(chunk));
                log_line(log, label, close_result);
                if (R_FAILED(close_result)) {
                    aliases_match = false;
                    break;
                }
                if (chunk_views != expected_views) break;
                backing_offset += chunk_size;
            }
        }
        std::printf("Physical 512 MiB backing chunks mapped: %u / %u\n",
                    static_cast<unsigned>(completed_chunks),
                    static_cast<unsigned>(kProbeChunkCount));
        std::printf("Mapped alias views: %u (expected 45)\n",
                    static_cast<unsigned>(mapped_views));
        std::printf("Writes match across aliases: %s\n",
                    aliases_match && completed_chunks == kProbeChunkCount ? "YES" : "NO");
        if (log) {
            std::fprintf(log,
                         "completed shared-memory chunks for 512 MiB backing: %u/%u\n"
                         "mapped shared-memory alias views: %u\n"
                         "writes match across aliases: %s\n",
                         static_cast<unsigned>(completed_chunks),
                         static_cast<unsigned>(kProbeChunkCount),
                         static_cast<unsigned>(mapped_views),
                         aliases_match && completed_chunks == kProbeChunkCount ? "yes" : "no");
            std::fflush(log);
        }
        // Leave mappings for process teardown; explicit unmap has hung on
        // previous hardware runs before returning from the SVC.
        log_info(log, "InfoType_UsedMemorySize(after close)", InfoType_UsedMemorySize);

    } else {
        std::printf("Call skipped: loader did not advertise this SVC.\n");
    }

    std::printf("\nLog: sdmc:/green_lantern_memory_probe.log\n");
    std::printf("Press + to return to Sphaira.\n");
    if (log) {
        std::fclose(log);
    }

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
