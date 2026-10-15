// Startup regression probe for the Horizon exception boundary. It uses a
// deliberate undefined instruction, so no game memory or content is needed.
#pragma once

extern "C" void RexSwitchExceptionProbe(const void* expected, void* actual);
extern "C" const char RexSwitchExceptionProbeTrap[];

__asm__(
    ".text\n"
    ".balign 4\n"
    ".global RexSwitchExceptionProbe\n"
    ".type RexSwitchExceptionProbe, %function\n"
    "RexSwitchExceptionProbe:\n"
    "sub sp, sp, #144\n"
    "stp q8, q9, [sp, #0]\n"
    "stp q10, q11, [sp, #32]\n"
    "stp q12, q13, [sp, #64]\n"
    "stp q14, q15, [sp, #96]\n"
    "mrs x2, fpcr\n"
    "mrs x3, fpsr\n"
    "stp x2, x3, [sp, #128]\n"
    "add x4, x0, #512\nldp x2, x3, [x4]\n"
    "msr fpcr, x2\n"
    "msr fpsr, x3\n"
    "ldp q0, q1, [x0, #0]\n"
    "ldp q2, q3, [x0, #32]\n"
    "ldp q4, q5, [x0, #64]\n"
    "ldp q6, q7, [x0, #96]\n"
    "ldp q8, q9, [x0, #128]\n"
    "ldp q10, q11, [x0, #160]\n"
    "ldp q12, q13, [x0, #192]\n"
    "ldp q14, q15, [x0, #224]\n"
    "ldp q16, q17, [x0, #256]\n"
    "ldp q18, q19, [x0, #288]\n"
    "ldp q20, q21, [x0, #320]\n"
    "ldp q22, q23, [x0, #352]\n"
    "ldp q24, q25, [x0, #384]\n"
    "ldp q26, q27, [x0, #416]\n"
    "ldp q28, q29, [x0, #448]\n"
    "ldp q30, q31, [x0, #480]\n"
    ".global RexSwitchExceptionProbeTrap\n"
    "RexSwitchExceptionProbeTrap:\n"
    "udf #0x5052\n"
    "stp q0, q1, [x1, #0]\n"
    "stp q2, q3, [x1, #32]\n"
    "stp q4, q5, [x1, #64]\n"
    "stp q6, q7, [x1, #96]\n"
    "stp q8, q9, [x1, #128]\n"
    "stp q10, q11, [x1, #160]\n"
    "stp q12, q13, [x1, #192]\n"
    "stp q14, q15, [x1, #224]\n"
    "stp q16, q17, [x1, #256]\n"
    "stp q18, q19, [x1, #288]\n"
    "stp q20, q21, [x1, #320]\n"
    "stp q22, q23, [x1, #352]\n"
    "stp q24, q25, [x1, #384]\n"
    "stp q26, q27, [x1, #416]\n"
    "stp q28, q29, [x1, #448]\n"
    "stp q30, q31, [x1, #480]\n"
    "mrs x2, fpcr\n"
    "mrs x3, fpsr\n"
    "add x4, x1, #512\nstp x2, x3, [x4]\n"
    "ldp x2, x3, [sp, #128]\n"
    "msr fpcr, x2\n"
    "msr fpsr, x3\n"
    "ldp q8, q9, [sp, #0]\n"
    "ldp q10, q11, [sp, #32]\n"
    "ldp q12, q13, [sp, #64]\n"
    "ldp q14, q15, [sp, #96]\n"
    "add sp, sp, #144\n"
    "ret\n"
    ".size RexSwitchExceptionProbe, .-RexSwitchExceptionProbe\n"
 );

static bool RexSwitchCheckExceptionRegisters() {
  // Assembly writes raw bits; the NaN payloads and upper halves matter too.
  alignas(16) uint64_t expected[66];
  alignas(16) uint64_t actual[66]{};
  for (unsigned i = 0; i < 64; ++i) expected[i] = 0x7FF1234589ABC000ull + i;
  expected[64] = 0x00400000; // round toward positive infinity
  expected[65] = 1;          // accrued invalid-operation flag
  for (unsigned trial = 0; trial < 16; ++trial) {
    RexSwitchExceptionProbe(expected, actual);
    for (unsigned i = 0; i < 66; ++i) {
      if (actual[i] != expected[i]) {
        std::fprintf(stderr, "[exception-probe] FAIL trial=%u word=%u expected=%016" PRIx64
                             " actual=%016" PRIx64 "\n", trial, i, expected[i], actual[i]);
        std::fflush(stderr);
        return false;
      }
    }
  }
  std::fprintf(stderr, "[exception-probe] PASS: 32 vectors, FPCR and FPSR across 16 scheduled exceptions\n");
  std::fflush(stderr);
  return true;
}
