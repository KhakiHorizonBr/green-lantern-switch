from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
SRC=ROOT/'outputs/partial-submit/source-snapshot'
NATIVE=HERE/'native_experiment/native'
NATIVE.mkdir(parents=True,exist_ok=True)
shutil.copytree(SRC/'native',NATIVE,dirs_exist_ok=True)
shutil.copy2(ROOT/'work/green-lantern-360/project/switch/native_experiment/guest_pacing_stats.h',NATIVE.parent)
(HERE/'source').mkdir(exist_ok=True)
shutil.copy2(SRC/'green_lantern_switch_main.cpp',HERE/'source/green_lantern_switch_main.cpp')

p=NATIVE/'native_renderer.cpp';s=p.read_text(encoding='utf-8')
anchor='REXCVAR_DEFINE_BOOL(nx_gpu_profile, true, "Switch",'
s=s.replace(anchor,'REXCVAR_DEFINE_UINT32(nx_ablation_mode, 0, "Switch",\n    "Diagnostic only: 0 baseline, 1 omit draw commands after preparation, 2 half scissor width, 3 omit resolves including clears");\n'+anchor)
s=s.replace('if (d.flags & kDrawScissor) f.vkCmdSetScissor(c, 0, 1, &d.scissor);','''if (d.flags & kDrawScissor) {
      VkRect2D scissor = d.scissor;
      if (REXCVAR_GET(nx_ablation_mode) == 2) scissor.extent.width /= 2;
      f.vkCmdSetScissor(c, 0, 1, &scissor);
    }''')
s=s.replace('    if (d.flags & kDrawIndexed) {\n      f.vkCmdDrawIndexed', '    if (REXCVAR_GET(nx_ablation_mode) == 1) return; // Keep preparation and state recording.\n    if (d.flags & kDrawIndexed) {\n      f.vkCmdDrawIndexed')
s=s.replace('void Renderer::Impl::Resolve() {','void Renderer::Impl::Resolve() {\n  if (REXCVAR_GET(nx_ablation_mode) == 3) return; // Invalid images expected; no resolve or clear side effects.')
assert s.count('if (REXCVAR_GET(nx_ablation_mode) == 1)')==1
assert s.count('if (REXCVAR_GET(nx_ablation_mode) == 3)')==1
p.write_text(s,encoding='utf-8')

p=HERE/'source/green_lantern_switch_main.cpp';s=p.read_text(encoding='utf-8')
s=s.replace('unsigned partial_submit_draws = 0;','unsigned partial_submit_draws = 0;\n      unsigned ablation_mode = 0;')
s=s.replace('          if (std::sscanf(line, " partial_submit_draws', '''          if (std::sscanf(line, " ablation_mode = %u %c", &value, &trailing) == 1 && value <= 3)
            ablation_mode = value;
          if (std::sscanf(line, " partial_submit_draws''')
s=s.replace('      const std::string partial_submit_text', '''      const bool ablation_applied = rex::cvar::SetFlagByName("nx_ablation_mode", std::to_string(ablation_mode).c_str());
      REXLOG_INFO("[gl-ablation-v1] mode={} applied={}; diagnostic modes may produce invalid images; baseline=0; no clock changes", ablation_mode, ablation_applied);
      const std::string partial_submit_text''')
p.write_text(s,encoding='utf-8')

p=NATIVE/'native_system.cpp';s=p.read_text(encoding='utf-8')
s=s.replace('#include <switch/kernel/svc.h>','#include <switch.h>')
anchor='  void Report() {\n'
s=s.replace(anchor,'''  void ReportClocks() {
#if defined(__SWITCH__)
    const Result init = clkrstInitialize();
    uint32_t hz[3] = {}; Result rc[3] = {init, init, init};
    const PcvModuleId ids[] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
    if (R_SUCCEEDED(init)) {
      for (unsigned i = 0; i < 3; ++i) {
        ClkrstSession session{};
        rc[i] = clkrstOpenSession(&session, ids[i], 3);
        if (R_SUCCEEDED(rc[i])) {
          rc[i] = clkrstGetClockRate(&session, &hz[i]);
          clkrstCloseSession(&session);
        }
      }
      clkrstExit();
    }
    REXLOG_INFO("[gl-clocks-v1] operation_mode={} performance_mode={} CPU={}Hz rc={:08X} GPU={}Hz rc={:08X} RAM={}Hz rc={:08X}; read-only requested rates, not utilization",
        unsigned(appletGetOperationMode()), unsigned(appletGetPerformanceMode()),
        hz[0], rc[0], hz[1], rc[1], hz[2], rc[2]);
#endif
  }
  void Report() {
    ReportClocks();
    const auto report_now = Clock::now();
    if (report_time_.time_since_epoch().count()) {
      const double seconds = std::chrono::duration<double>(report_now - report_time_).count();
      REXLOG_INFO("[gl-fps-window-v1] seconds={:.6f} swaps={} fps={:.3f}; swap count / measured wall time, not screen scanout",
          seconds, swaps_ - reported_swaps_, double(swaps_ - reported_swaps_) / seconds);
    }
    report_time_ = report_now;
''')
s=s.replace('    frame_intervals_.Add(interval_ns);','''    frame_intervals_.Add(interval_ns);
    if (short_frames_ == 0) short_start_ns_ = end_for_short_;
    // Sum consecutive completion intervals: no fixed-ten-second assumption.
    short_elapsed_ns_ += interval_ns;
    ++short_frames_;
    const unsigned bucket = interval_ns <= 35000000 ? 0 : interval_ns <= 50000000 ? 1 : interval_ns <= 100000000 ? 2 : 3;
    ++short_buckets_[bucket];
    short_frame_ms_.Add(interval_ns);
    if (short_elapsed_ns_ >= 1000000000ULL) {
      REXLOG_INFO("[gl-fps-second-v1] frame={} seconds={:.6f} frames={} fps={:.3f}; intervals <=35ms={} 35-50ms={} 50-100ms={} >100ms={}; completion intervals, not scanout",
          swaps_ - 1, double(short_elapsed_ns_) / 1e9, short_frames_,
          double(short_frames_) * 1e9 / double(short_elapsed_ns_),
          short_buckets_[0], short_buckets_[1], short_buckets_[2], short_buckets_[3]);
      short_frame_ms_.Report("one-second completion intervals");
      short_elapsed_ns_ = 0; short_frames_ = 0;
      for (auto& bucket_count : short_buckets_) bucket_count = 0;
    }'''.replace('    if (short_frames_ == 0) short_start_ns_ = end_for_short_;\n',''))
s=s.replace('  diagnostics::Distribution frame_intervals_,','''  Clock::time_point report_time_{};
  uint64_t short_elapsed_ns_ = 0, short_frames_ = 0, short_buckets_[4] = {};
  diagnostics::Distribution short_frame_ms_;
  diagnostics::Distribution frame_intervals_,''')
# Boot-equivalent first rendering interval, then each profile report.
s=s.replace('if (previous.time_since_epoch().count() == 0) { frame_diagnostics_before_', 'if (previous.time_since_epoch().count() == 0) { ReportClocks(); frame_diagnostics_before_')
p.write_text(s,encoding='utf-8')
print('Prepared isolated source; shared Vulkan/deko sources untouched')
