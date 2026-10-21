//===----------------------------------------------------------------------===//
//
// (c) 2025-2026 Qualcomm Technologies, Inc. All rights reserved.
//
// See https://spdx.org/licenses/BSD-3-Clause-Clear.html for license information.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
//===----------------------------------------------------------------------===//

#include <ripple-test-suite/ripple-test-suite.h>

#include <cassert>
#include <iostream>
#include <string>

#if defined(__aarch64__) && defined(__linux__)
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <linux/prctl.h>
#include <sys/prctl.h>

#ifndef RIPPLE_TEST_SUITE_SME_VECTOR_LENGTH_BYTES
#define RIPPLE_TEST_SUITE_SME_VECTOR_LENGTH_BYTES 64
#endif
#endif

#ifdef __hexagon__
#ifdef HEXAGON_STANDALONE
#include <hexagon_standalone.h>
#else
#include <HAP_compute_res.h>
#endif
#endif

namespace {

__attribute__((always_inline)) unsigned long long cycles() {
  return __builtin_readcyclecounter();
}

#if defined(__aarch64__) && defined(__linux__)
void setStreamingVectorLength() {
  constexpr unsigned long StreamingVectorLength =
      RIPPLE_TEST_SUITE_SME_VECTOR_LENGTH_BYTES;
  const long Result = prctl(PR_SME_SET_VL, StreamingVectorLength, 0, 0, 0);
  if (Result < 0 ||
      (Result & PR_SME_VL_LEN_MASK) != StreamingVectorLength) {
    std::fprintf(stderr,
                 "Unable to set the SME streaming vector length to %lu bytes: "
                 "result=%ld, errno=%d\n",
                 StreamingVectorLength, Result, errno);
    std::exit(EXIT_FAILURE);
  }
}
#endif

#ifdef __hexagon__
#ifdef HEXAGON_STANDALONE
void *allocateVtcmSim(size_t size) {
  enum {
    CFGBASE_VTCM_ADDRESS = 0x38,
    CFGBASE_VTCM_SIZE = 0x3c,
  };
  uintptr_t pa = __rdcfg(CFGBASE_VTCM_ADDRESS) << 16;
  unsigned cfg_vtcm_size = __rdcfg(CFGBASE_VTCM_SIZE);

  unsigned pageSize = size <= 4 * 1024            ? PAGE_4K
                      : size <= 16 * 1024         ? PAGE_16K
                      : size <= 64 * 1024         ? PAGE_64K
                      : size <= 256 * 1024        ? PAGE_256K
                      : size <= 1 * 1024 * 1024   ? PAGE_1M
                      : size <= 4 * 1024 * 1024   ? PAGE_4M
                      : size <= 16 * 1024 * 1024  ? PAGE_16M
                      : size <= 64 * 1024 * 1024  ? PAGE_64M
                      : size <= 256 * 1024 * 1024 ? PAGE_256M
                      : size <= size_t(1) * 1024 * 1024 * 1024
                          ? PAGE_1G
                          /* ? size < size_t(4) * 1024 * 1024 * 1024 */
                          : PAGE_4G;
  void *va = (void *)pa;
  unsigned int xwru = 15;
  unsigned int cccc = 7; // write back and cacheable
  unsigned int asid = 0;
  unsigned int aa = 0;
  unsigned int vg = 3; // Set valid and ignore asid
  add_translation_extended(1, va, pa, pageSize, xwru, cccc, asid, aa, vg);
  printf("VTCM at 0x%x, size %u K\n", pa, cfg_vtcm_size);

  return va;
}
#else
void *allocateVtcmHap(size_t size, unsigned int &ctx) {
  unsigned int context;
  int ret;

  size_t alignedSize = 1;
  while (alignedSize < size)
    alignedSize <<= 1;

  compute_res_attr_t r;
  if ((ret = HAP_compute_res_attr_init(&r)) != 0) {
    fprintf(stderr, "HAP_compute_res_attr_init: %d\n", ret);
    return nullptr;
  }

  ret = HAP_compute_res_attr_set_serialize(&r, 1);
  if (ret != 0) {
    fprintf(stderr, "HAP_compute_res_attr_set_serialize(1): %d\n", ret);
    return nullptr;
  }

  ret = HAP_compute_res_attr_set_vtcm_param(&r, alignedSize, 1);
  if (ret != 0) {
    fprintf(stderr, "HAP_compute_res_attr_set_vtcm_param: %d\n", ret);
    return nullptr;
  }

  context = HAP_compute_res_acquire(&r, 100000 /* timeout */);
  if (context == 0) {
    fprintf(stderr, "HAP_compute_res_acquire: %d\n", context);
    return nullptr;
  }

  void *p = HAP_compute_res_attr_get_vtcm_ptr(&r);
  return p;
}

void freeVtcmHap(unsigned int ctx) { HAP_compute_res_release(ctx); };
#endif
#endif

#if defined __hexagon__ && defined HEXAGON_STANDALONE
#define DECL_HEXAGON_REG(R)                                                    \
  inline unsigned int hexagon_read_##R() {                                         \
  unsigned int x;                                                                \
    __asm __volatile("%0=" #R : "=r"(x));                                      \
    return x;                                                                  \
  }                                                                            \
  inline void hexagon_write_##R(unsigned int x) {                                  \
    __asm __volatile(#R "=%0" : : "r"(x));                                     \
  }

DECL_HEXAGON_REG(SSR)
#endif

} // namespace

// A dirty workaround for Hexagon dinkumware libstdc++. I don't know which
// version would support std::stol but version 0x501 does not. For some reason,
// when compiling for Hexagon with libc++, _CPPLIB_VER is also defined as 0x501.
#if defined _LIBCPP_STD_VER && _LIBCPP_STD_VER < 11 ||                         \
    !defined _LIBCPP_STD_VER && defined _CPPLIB_VER && _CPPLIB_VER <= 0x501
namespace std {
long stol(const std::string &s) { return atol(s.c_str()); }
} // namespace std
#endif

namespace ripple_test_suite {

TestFramework::~TestFramework() {
#ifdef __hexagon__
#ifndef HEXAGON_STANDALONE
  if (m_vtcm)
    freeVtcmHap(m_vtcmCtx);
#endif
#endif
}

void TestFramework::init(int argc, char *argv[]) {
  for (int i = 1; i < argc; ++i) {
#ifdef __hexagon__
    if (std::string(argv[i]) == "--vtcm-size" && i + 1 < argc)
      m_vtcmSize = std::stol(argv[++i]);
#endif
  }

#ifdef __hexagon__
  if (m_vtcmSize) {
#ifdef HEXAGON_STANDALONE
    m_vtcm = allocateVtcmSim(m_vtcmSize);
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
#else
    m_vtcm = allocateVtcmHap(m_vtcmSize, m_vtcmCtx);
#endif
  }
#endif

#if defined __hexagon__ && defined HEXAGON_STANDALONE
  hexagon_write_SSR(hexagon_read_SSR() | 1 << 23);
#endif
}

TestSuite::TestSuite() : m_out(&std::cout) {}

TestSuite::~TestSuite() {
  for (const auto &i : m_Tests)
    delete i.second;
}

void TestSuite::openOut(const char *F) {
  m_outStream.open(F);
  if (m_outStream)
    m_out = &m_outStream;
}

void TestSuite::init(int argc, char *argv[]) {
  m_TestFramework.init(argc, argv);

  for (int i = 1; i < argc; ++i) {
    if (std::string(argv[i]) == "-o" && i + 1 < argc)
      openOut(argv[++i]);
    else if (std::string(argv[i]) == "--repeat" && i + 1 < argc)
      m_repeat = std::stol(argv[++i]);
  }
}

int TestSuite::run() {
  bool result = true;
  for (const auto &i : m_Tests) {
    auto shouldXfailResult = i.second->xfail();
    if (shouldXfailResult.first) {
      out() << "XFAIL"
            << ": " << i.first << ", REASON: " << shouldXfailResult.second
            << std::endl;
      continue;
    }

    i.second->prepare(m_repeat);
    unsigned long long dt = 0;
    for (int j = 0; j < m_repeat; ++j) {
      unsigned long long t0 = cycles();
      i.second->run(j);
      dt = cycles() - t0;
    }
    bool ret = i.second->verify();
    if (!ret)
      result = false;
    out() << (ret ? "PASS" : "FAIL") << ": " << i.first << ", " << dt
          << std::endl;
  }
  return result ? 0 : 1;
}

} // namespace ripple_test_suite

int main(int argc, char *argv[]) {
#if defined(__aarch64__) && defined(__linux__)
  setStreamingVectorLength();
#endif
  ripple_test_suite::TestSuite &Suite =
      ripple_test_suite::TestSuite::GetInstance();
  Suite.init(argc, argv);
  return Suite.run();
}
