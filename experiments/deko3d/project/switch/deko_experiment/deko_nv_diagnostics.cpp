// SPDX-License-Identifier: GPL-3.0-or-later
#include <switch.h>
#include <rex/logging.h>
#include <string>
static thread_local std::string operation_context="boot";
extern "C" void RexDekoOperationContext(const char* text) {operation_context=text?text:"?";}

extern "C" Result __real_nvGpuChannelAppendEntry(NvGpuChannel*,iova_t,size_t,u32,u32);
extern "C" Result __wrap_nvGpuChannelAppendEntry(NvGpuChannel* channel,iova_t start,
    size_t words,u32 flags,u32 threshold) {
  // Use libnx's own kickoff bookkeeping, including fence increments. Splitting
  // the raw ioctl instead would incorrectly duplicate its fence payload.
  // Hardware 080 timed out submitting 886 entries; keep each ioctl bounded.
  constexpr u32 kBatchEntries=64;
  if(channel && channel->num_entries>=kBatchEntries) {
    const Result rc=nvGpuChannelKickoff(channel);
    if(R_FAILED(rc))return rc;
  }
  return __real_nvGpuChannelAppendEntry(channel,start,words,flags,threshold);
}

// gpu_channel.c calls Kickoff internally when AppendEntry fills its queue;
// --wrap=nvGpuChannelKickoff cannot intercept calls inside that same object.
// Intercept the ioctl boundary instead, preserving arguments and Result.
extern "C" Result __real_nvioctlChannel_KickoffPb(u32,nvioctl_gpfifo_entry*,u32,u32,nvioctl_fence*);
extern "C" Result __wrap_nvioctlChannel_KickoffPb(u32 fd,nvioctl_gpfifo_entry* entries,
    u32 count,u32 flags,nvioctl_fence* fence) {
  const Result rc=__real_nvioctlChannel_KickoffPb(fd,entries,count,flags,fence);
  if(R_FAILED(rc)) {
    REXLOG_ERROR("[deko-pair-v10] {}",operation_context);
    NvError error{};const Result er=nvioctlChannel_GetErrorInfo(fd,&error);
    REXLOG_ERROR("[deko-ioctl-v4] kickoff result=0x{:08X} module={} description={} fd={} entries={} flags=0x{:X} "
        "error_result=0x{:08X} type={} info={:08X},{:08X},{:08X},{:08X},{:08X}",
        rc,R_MODULE(rc),R_DESCRIPTION(rc),fd,count,flags,er,error.type,
        error.info[0],error.info[1],error.info[2],error.info[3],error.info[4]);
    if(entries && count<=GPFIFO_QUEUE_SIZE) {
      for(u32 i=0;i<count;++i) {
        // GPFIFO high word: address high 8 bits, then length (21 bits).
        const u32 high=entries[i].desc32[1],words=(high>>10)&0x1FFFFF;
        if(i<4 || i+4>=count || !words)
          REXLOG_ERROR("[deko-ioctl-v4] entry={} raw={:016X} words={}",i,entries[i].desc,words);
      }
    }
    rex::FlushLogging();
  }
  return rc;
}
