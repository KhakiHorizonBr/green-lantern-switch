/**
 * @file        rex/core/threading_switch.cpp
 * @brief       Thread shim for Horizon: affinity, priority and suspension
 *
 * See threading_switch.h for why this layer exists and for the priority
 * scheme, which is what decides whether the game stutters or not.
 */

#include <rex/platform.h>
#include <rex/logging.h>

#if REX_PLATFORM_SWITCH

#include "threading_switch.h"

/*
 * switch.h is included here, privately. This file uses none of the rex::thread
 * types, so libnx polluting the global namespace (Thread, Timer, Event,
 * Handle...) does no harm. The header above does not include it, precisely so
 * that threading_posix.cpp does not suffer from it.
 */
#include <switch.h>

#include <errno.h>
#include <sched.h>
#include <malloc.h>
#include <sys/reent.h>

#include <mutex>
#include <unordered_map>

extern "C" void _start(void);
extern "C" char __tls_start[], __tls_end[];

namespace {

constexpr size_t kNativeWorkerStackSize = 1024 * 1024;
constexpr size_t kNativeStackOverhead = 64 * 1024;
struct NativeStackSlot {
  void* memory = nullptr;
  void* owner = nullptr;
};
NativeStackSlot native_stack_slots[2];
std::mutex native_stack_mutex;
thread_local bool native_thread_creation = false;

/*
 * Registry pthread_t -> Horizon Handle.
 *
 * libnx exposes no way to get the handle of another thread, and the SDK
 * changes the priority and affinity of other threads, so it has to be tracked
 * by hand.
 *
 * Not a hot path: it is touched when threads are created and destroyed, never
 * per frame. A plain mutex is more than enough.
 */
struct Registro {
  std::mutex m;
  std::unordered_map<uintptr_t, RexSwitchHandle> mapa;
  // Threads paused by RexSwitchPauseThread (a guest suspend). The stall
  // sampler must not resume them after reading their context.
  std::unordered_map<RexSwitchHandle, int> pausados;
};

Registro& registro() {
  static Registro r;
  return r;
}

}  // namespace

extern "C" {

bool RexSwitchInitNativeThreadStackReserve(void) {
  std::lock_guard<std::mutex> lock(native_stack_mutex);
  const size_t tls_size = static_cast<size_t>(__tls_end - __tls_start);
  if (tls_size + sizeof(struct _reent) + 4096 > kNativeStackOverhead) {
    REXLOG_ERROR("Switch native stack reserve: TLS/reent exceeds reserved overhead: tls={}",
                 tls_size);
    return false;
  }
  for (auto& slot : native_stack_slots) {
    if (!slot.memory) {
      slot.memory = memalign(4096, kNativeWorkerStackSize + kNativeStackOverhead);
      if (!slot.memory) {
        // No slot has been handed to a thread during startup.
        for (auto& allocated : native_stack_slots) {
          free(allocated.memory);
          allocated = {};
        }
        REXLOG_ERROR("Switch native stack reserve: early allocation failed");
        return false;
      }
    }
  }
  const struct mallinfo stats = mallinfo();
  REXLOG_INFO("Switch native stack reserve: 2 x {} KiB backing, {} KiB host stack, "
              "TLS={} bytes; malloc used={} MiB free={} MiB",
              (kNativeWorkerStackSize + kNativeStackOverhead) >> 10,
              kNativeWorkerStackSize >> 10, tls_size,
              stats.uordblks >> 20, stats.fordblks >> 20);
  return true;
}

void RexSwitchSetNativeThreadCreation(bool enabled) { native_thread_creation = enabled; }
bool RexSwitchNativeThreadCreationIsActive(void) { return native_thread_creation; }

bool RexSwitchAcquireNativeThreadStack(void* owner, size_t stack_size,
                                      void** backing, size_t* backing_size) {
  std::lock_guard<std::mutex> lock(native_stack_mutex);
  if (!owner || stack_size > kNativeWorkerStackSize) return false;
  for (auto& slot : native_stack_slots) {
    if (slot.memory && !slot.owner) {
      slot.owner = owner;
      *backing = slot.memory;
      *backing_size = kNativeWorkerStackSize + kNativeStackOverhead;
      return true;
    }
  }
  return false;
}

void RexSwitchReleaseNativeThreadStack(void* owner) {
  std::lock_guard<std::mutex> lock(native_stack_mutex);
  for (auto& slot : native_stack_slots) {
    if (slot.owner == owner) slot.owner = nullptr;
  }
}

void RexSwitchRegisterCurrentThread(void) {
  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  r.mapa[reinterpret_cast<uintptr_t>(pthread_self())] = threadGetCurHandle();
}

void RexSwitchUnregisterCurrentThread(void) {
  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  r.mapa.erase(reinterpret_cast<uintptr_t>(pthread_self()));
}

RexSwitchHandle RexSwitchHandleFor(pthread_t thread) {
  if (pthread_equal(thread, pthread_self()))
    return threadGetCurHandle();

  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  auto it = r.mapa.find(reinterpret_cast<uintptr_t>(thread));
  return it == r.mapa.end() ? REX_SWITCH_INVALID_HANDLE : it->second;
}

/* --- afinidad ---------------------------------------------------------- */

int pthread_getaffinity_np(pthread_t thread, size_t size, cpu_set_t* set) {
  if (set == nullptr || size < sizeof(cpu_set_t))
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  s32 preferido = 0;
  u64 mascara = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferido, &mascara, h)))
    return EINVAL;

  set->bits = mascara;
  return 0;
}

int pthread_setaffinity_np(pthread_t thread, size_t size, const cpu_set_t* set) {
  if (set == nullptr || size < sizeof(cpu_set_t))
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  /*
   * Horizon also wants a preferred core. The lowest one in the mask is
   * taken; -1 would mean "whatever it already had", which is not what the
   * POSIX API being imitated asks for.
   */
  s32 preferido = -1;
  for (int i = 0; i < 4; i++) {
    if (set->bits & (1ull << i)) { preferido = i; break; }
  }
  if (preferido < 0)
    return EINVAL;  /* mascara vacia: POSIX tampoco lo permite */

  /*
   * Only 4 cores exist. Asking for more is not a caller error (the SDK
   * passes 64-bit masks), so it is trimmed silently.
   */
  const u32 mascara = static_cast<u32>(set->bits & 0xF);

  if (R_FAILED(svcSetThreadCoreMask(h, preferido, mascara)))
    return EPERM;
  return 0;
}

/*
 * --- priority ------------------------------------------------------------
 *
 * sched_param::sched_priority is passed straight through as the Horizon
 * priority, without inverting the scale. This is deliberate: callers using the
 * REX_SWITCH_PRIO_* constants expect Horizon numbers, where lower means higher
 * priority. Inverting it here to look like POSIX would only turn the priority
 * scheme upside down, which is exactly the bug that causes stutter and audio
 * dropouts.
 */

int pthread_getschedparam(pthread_t thread, int* policy, struct sched_param* param) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  s32 prio = 0;
  if (R_FAILED(svcGetThreadPriority(&prio, h)))
    return EINVAL;

  if (policy) *policy = SCHED_FIFO;
  if (param) param->sched_priority = prio;
  return 0;
}

int pthread_setschedparam(pthread_t thread, int policy, const struct sched_param* param) {
  (void)policy;
  if (param == nullptr)
    return EINVAL;

  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;

  if (R_FAILED(svcSetThreadPriority(h, static_cast<u32>(param->sched_priority))))
    return EPERM;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy) {
  (void)attr;
  (void)policy;
  /* Horizon has a single policy. Accepting and doing nothing is correct. */
  return 0;
}

/* --- mutex robustos: no existen ---------------------------------------- */

int pthread_mutexattr_setrobust(pthread_mutexattr_t* attr, int robustness) {
  (void)attr;
  /*
   * PTHREAD_MUTEX_STALLED is accepted (the real behavior), and ROBUST is
   * accepted without providing it: the SDK uses it as a safeguard against a
   * thread dying while holding the mutex, which should not happen in the guest.
   */
  if (robustness != PTHREAD_MUTEX_STALLED && robustness != PTHREAD_MUTEX_ROBUST)
    return EINVAL;
  return 0;
}

int pthread_mutex_consistent(pthread_mutex_t* mutex) {
  (void)mutex;
  return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t* attr, int* robustness) {
  (void)attr;
  if (robustness) *robustness = PTHREAD_MUTEX_STALLED;
  return 0;
}

/*
 * --- suspension ----------------------------------------------------------
 *
 * The SDK suspends threads by sending them a signal. Horizon does not deliver
 * signals, so that cannot work however much shimming is added: the system
 * mechanism has to be used.
 *
 * A difference in semantics worth knowing: svcSetThreadActivity stops the
 * thread wherever it is, not at a safe point chosen by the thread itself. For
 * what the SDK does with this (suspending guest threads, which is what the
 * original console did) that is correct. For any use that needs the thread to
 * stop at a specific place, it is not.
 */

int RexSwitchPauseThread(pthread_t thread) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;
  if (pthread_equal(thread, pthread_self()))
    return EDEADLK;  /* pausing oneself cannot be undone */
  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  if (R_FAILED(svcSetThreadActivity(h, ThreadActivity_Paused)))
    return EPERM;
  ++r.pausados[h];
  return 0;
}

int RexSwitchResumeThread(pthread_t thread) {
  RexSwitchHandle h = RexSwitchHandleFor(thread);
  if (h == REX_SWITCH_INVALID_HANDLE)
    return ESRCH;
  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  r.pausados.erase(h);
  return R_SUCCEEDED(svcSetThreadActivity(h, ThreadActivity_Runnable)) ? 0 : EPERM;
}

size_t RexSwitchSampleThreads(RexSwitchThreadSample* out, size_t max) {
  // The registry lock is held throughout, so a guest suspend cannot land
  // between reading a thread and resuming it. That is safe because no thread
  // can be paused while holding this lock: it is only taken in this file, and
  // never while a pause is in effect. Nothing else may be locked while a
  // thread is paused, because the paused thread may be the one holding it.
  auto& r = registro();
  std::lock_guard<std::mutex> lock(r.m);
  RexSwitchHandle handles[64];
  size_t count = 0;
  const RexSwitchHandle self = threadGetCurHandle();
  for (const auto& [thread, h] : r.mapa) {
    (void)thread;
    if (h != self && !r.pausados.count(h) && count < max && count < 64) {
      handles[count++] = h;
    }
  }

  const uint64_t base = reinterpret_cast<uint64_t>(&_start);
  uint64_t text_hi = base;
  MemoryInfo mi{};
  u32 page_info = 0;
  if (R_SUCCEEDED(svcQueryMemory(&mi, &page_info, base))) {
    text_hi = mi.addr + mi.size;
  }
  auto in_image = [&](uint64_t a) { return a >= base && a < text_hi; };
  auto readable = [&](uint64_t a) {
    MemoryInfo m{};
    u32 pi = 0;
    return R_SUCCEEDED(svcQueryMemory(&m, &pi, a)) && m.type != MemType_Unmapped &&
           (m.perm & Perm_R) && a + 16 <= m.addr + m.size;
  };

  for (size_t i = 0; i < count; ++i) {
    RexSwitchThreadSample& s = out[i];
    s = RexSwitchThreadSample{};
    u64 tid = 0;
    svcGetThreadId(&tid, handles[i]);
    s.thread_id = tid;
    if (R_FAILED(svcSetThreadActivity(handles[i], ThreadActivity_Paused))) {
      continue;
    }
    // The pause takes effect when the thread next leaves the kernel, so the
    // context read can fail briefly.
    ThreadContext ctx{};
    bool got = false;
    for (int attempt = 0; attempt < 50 && !got; ++attempt) {
      got = R_SUCCEEDED(svcGetThreadContext3(&ctx, handles[i]));
      if (!got) {
        svcSleepThread(100'000);
      }
    }
    if (got) {
      s.ok = 1;
      s.pc_in_image = in_image(ctx.pc.x);
      s.lr_in_image = in_image(ctx.lr);
      s.pc = s.pc_in_image ? ctx.pc.x - base : ctx.pc.x;
      s.lr = s.lr_in_image ? ctx.lr - base : ctx.lr;
      uint64_t frame = ctx.fp;
      for (int f = 0; f < 32 && s.frame_count < 8 && (frame & 7) == 0 && readable(frame); ++f) {
        const uint64_t next = reinterpret_cast<const uint64_t*>(frame)[0];
        const uint64_t ret = reinterpret_cast<const uint64_t*>(frame)[1];
        if (in_image(ret)) {
          s.frames[s.frame_count++] = ret - base;
        }
        if (next <= frame) {
          break;
        }
        frame = next;
      }
    }
    svcSetThreadActivity(handles[i], ThreadActivity_Runnable);
  }
  return count;
}

/* --- priority of the current thread ------------------------------------ */

uint64_t RexSwitchCurrentKernelThreadId(void) {
  u64 tid = 0;
  svcGetThreadId(&tid, CUR_THREAD_HANDLE);
  return tid;
}

uint32_t RexSwitchCurrentThreadId(void) {
  /* The current thread's handle serves as a unique id, stable while it lives. */
  return static_cast<uint32_t>(threadGetCurHandle());
}

/*
 * threadGetSelf() only returns something for threads that libnx created with threadCreate. Ours
 * come from pthread_create (rex::thread::Thread::Create -> PosixThread), so if the TLS does not carry
 * libnx's ThreadVars, this used to be a silent no-op: the priority was not applied and the log said
 * it was, because it only printed the cvar value.
 *
 * CUR_THREAD_HANDLE (0xFFFF8000) is the pseudo-handle that the Horizon kernel always understands as
 * "the calling thread", wherever it came from. The result is returned so that it can be checked in
 * the log instead of assumed.
 */
bool RexSwitchSetCurrentThreadPriorityOk(int priority) {
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  if (R_SUCCEEDED(svcSetThreadPriority(h, static_cast<u32>(priority)))) {
    return true;
  }
  if (h != CUR_THREAD_HANDLE) {
    return R_SUCCEEDED(svcSetThreadPriority(CUR_THREAD_HANDLE, static_cast<u32>(priority)));
  }
  return false;
}

void RexSwitchSetCurrentThreadPriority(int priority) {
  (void)RexSwitchSetCurrentThreadPriorityOk(priority);
}

void RexSwitchLogThreadResourceStats(void) {
  const struct mallinfo stats = mallinfo();
  REXLOG_ERROR("Switch thread malloc stats: used={} KiB free={} KiB top={} KiB",
               stats.uordblks >> 10, stats.fordblks >> 10, stats.keepcost >> 10);
  u64 raw_handle = 0;
  const Result info_result = svcGetInfo(&raw_handle, InfoType_ResourceLimit, 0, 0);
  if (R_FAILED(info_result)) {
    REXLOG_ERROR("Switch resource stats: InfoType_ResourceLimit failed: 0x{:08X}",
                 info_result);
    return;
  }
  const Handle resource_limit = static_cast<Handle>(raw_handle);
  s64 threads_current = -1, threads_limit = -1;
  s64 memory_current = -1, memory_limit = -1;
  const Result threads_current_result = svcGetResourceLimitCurrentValue(
      &threads_current, resource_limit, LimitableResource_Threads);
  const Result threads_limit_result = svcGetResourceLimitLimitValue(
      &threads_limit, resource_limit, LimitableResource_Threads);
  const Result memory_current_result = svcGetResourceLimitCurrentValue(
      &memory_current, resource_limit, LimitableResource_Memory);
  const Result memory_limit_result = svcGetResourceLimitLimitValue(
      &memory_limit, resource_limit, LimitableResource_Memory);
  REXLOG_ERROR("Switch resource stats: threads={}/{} (current rc=0x{:08X}, limit rc=0x{:08X}); "
               "memory={}/{} MiB (current rc=0x{:08X}, limit rc=0x{:08X})",
               static_cast<long long>(threads_current), static_cast<long long>(threads_limit),
               threads_current_result, threads_limit_result,
               static_cast<long long>(memory_current >> 20),
               static_cast<long long>(memory_limit >> 20), memory_current_result,
               memory_limit_result);
  svcCloseHandle(resource_limit);
}

/*
 * Preferred core of the current thread.
 *
 * All our threads run with "preferred core -1", which on Horizon means the process default core:
 * the same one for all of them. With the ring at 94.7 % of a core and the game's main thread at
 * 88.2 %, both prefer the same place and the kernel has to keep moving them. Each migration throws
 * away the thread's L1 and part of its L2, and that does not show in the mean: it shows in the
 * variance, which is exactly what the player notices.
 *
 * The mask is not touched. The thread's existing mask is passed, so this only says "start here",
 * not "you may only run here": if the core is busy, the kernel can still move the thread. That is
 * why it cannot cause starvation, which is what makes real affinity pinning dangerous.
 *
 * Returns false if the kernel does not accept it (for example if the core is not in the process
 * mask: that is the "Too few processor cores" error seen when trying to use the fourth core).
 */
bool RexSwitchSetCurrentThreadCore(int core) {
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  s32 preferido = 0;
  u64 mascara = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferido, &mascara, h))) {
    if (h == CUR_THREAD_HANDLE ||
        R_FAILED(svcGetThreadCoreMask(&preferido, &mascara, CUR_THREAD_HANDLE))) {
      return false;
    }
    h = CUR_THREAD_HANDLE;
  }
  if (core >= 0 && (mascara & (u64(1) << core)) == 0) {
    return false;  // that core is not this process's: do not force it
  }
  return R_SUCCEEDED(svcSetThreadCoreMask(h, core, mascara));
}

/*
 * The real pin, with the mask reduced to a single core.
 *
 * Measured: a preferred core is not enough. Horizon accepts it and moves the thread anyway (0.24
 * migrations per loop, wherever it is set). This takes the other cores away from the thread.
 *
 * Different from RexSwitchSetCurrentThreadCore and more dangerous: if that core saturates, the
 * thread does not get to run. That is why it sits behind a cvar that is off by default and is
 * checked in the log. Horizon only time-slices priority 0x3B.
 */
bool RexSwitchPinCurrentThreadToCore(int core) {
  if (core < 0) {
    return false;
  }
  Thread* yo = threadGetSelf();
  Handle h = (yo != nullptr) ? yo->handle : CUR_THREAD_HANDLE;
  s32 preferido = 0;
  u64 mascara = 0;
  if (R_FAILED(svcGetThreadCoreMask(&preferido, &mascara, h))) {
    if (h == CUR_THREAD_HANDLE ||
        R_FAILED(svcGetThreadCoreMask(&preferido, &mascara, CUR_THREAD_HANDLE))) {
      return false;
    }
    h = CUR_THREAD_HANDLE;
  }
  const u64 solo_ese = u64(1) << core;
  if ((mascara & solo_ese) == 0) {
    return false;  // that core is not this process's
  }
  return R_SUCCEEDED(svcSetThreadCoreMask(h, core, solo_ese));
}

/*
 * Which core the calling thread is running on right now. This is what allows counting migrations:
 * it is read on every loop iteration and counted each time it changes. There is no syscall to ask
 * it about another thread, so each thread has to look at itself.
 */
int RexSwitchCurrentCore(void) { return int(svcGetCurrentProcessorNumber()); }

}  // extern "C"

#endif  /* REX_PLATFORM_SWITCH */
