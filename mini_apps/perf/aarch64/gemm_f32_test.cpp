//===----------------------------------------------------------------------===//
//
// (c) 2025-2026 Qualcomm Technologies, Inc. All rights reserved.
//
// See https://spdx.org/licenses/BSD-3-Clause-Clear.html for license
// information.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <ripple-test-suite/ripple-test-suite.h>
#include <ripple.h>
#include <ripple_sme_veclib.h>

// KleidiAI headers
#include "gemm_f32_ripple_sme_mopa.h"
#include "kai/ukernels/matmul/matmul_clamp_f32_f32p_f32p/kai_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa.h"
#include "kai/ukernels/matmul/pack/kai_lhs_pack_f32p2vlx1_f32_sme.h"
#include "kai/ukernels/matmul/pack/kai_rhs_pack_kxn_f32p2vlx1biasf32_f32_f32_sme.h"

/// Wrappers for the actual functions.
namespace {
/// Reference implementation using KleidiAI SME kernel.
void gemm_f32_ref(size_t M, size_t N, size_t K, const float *lhs_packed,
                  const float *rhs_packed, float *result, float clamp_min,
                  float clamp_max) {
  kai_run_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa(
      M, N, K, lhs_packed, rhs_packed, result,
      N * sizeof(float), // dst_stride_row
      sizeof(float),     // dst_stride_col
      clamp_min, clamp_max);
}

void gemm_f32_ripple(size_t M, size_t N, size_t K, const float *lhs_packed,
                     const float *rhs_packed, float *result, float clamp_min,
                     float clamp_max) {
  gemm_f32_ripple_sme_mopa(M, N, K, lhs_packed, rhs_packed, result,
                           N * sizeof(float), // dst_stride_row
                           sizeof(float),     // dst_stride_col
                           clamp_min, clamp_max);
}

} // namespace

namespace ripple_test_suite {

enum KernelT { Reference, RippleOpt };

template <size_t M, size_t N, size_t K, bool verifyResults, KernelT KT>
class GemmF32Test : public Test {
  std::vector<float> lhs_packed, rhs_packed, result, result_scratch, result_ref;
  float clamp_min, clamp_max;

public:
  GemmF32Test(TestFramework &TestFramework) : Test(TestFramework) {
    srand(time(0));
    assert(verify_ripple_sme_api_fixed_vscale() &&
           "Runtime VL different than Ripple's fixed assumption.");
    // Kernel parameters
    const size_t mr =
        kai_get_mr_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa();
    const size_t nr =
        kai_get_nr_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa();
    const size_t kr =
        kai_get_kr_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa();
    const size_t sr =
        kai_get_sr_matmul_clamp_f32_f32p2vlx1_f32p2vlx1b_2vlx2vl_sme_mopa();

    const size_t lhs_packed_size =
        kai_get_lhs_packed_size_lhs_pack_f32p2vlx1_f32_sme(M, K, mr, kr, sr) /
        sizeof(float);
    const size_t rhs_packed_size =
        kai_get_rhs_packed_size_rhs_pack_kxn_f32p2vlx1biasf32_f32_f32_sme(N,
                                                                          K) /
        sizeof(float);

    result.resize(M * N, 0.0f);
    result_scratch.resize(M * N, 0.0f);
    lhs_packed.resize(lhs_packed_size, 0.0f);
    rhs_packed.resize(rhs_packed_size, 0.0f);

    // Create unpacked source matrices. We won't use them again.
    std::vector<float> lhs_unpacked(M * K);
    std::vector<float> rhs_unpacked(K * N);
    std::vector<float> bias(N);

    // Initialize unpacked matrices with random data.
    for (size_t i = 0; i < M * K; ++i)
      lhs_unpacked[i] = randnFullRangef();
    for (size_t i = 0; i < K * N; ++i)
      rhs_unpacked[i] = randnFullRangef();
    for (size_t i = 0; i < N; ++i)
      bias[i] = randnFullRangef();

    // Pack using KleidiAI functions.
    kai_run_lhs_pack_f32p2vlx1_f32_sme(M, K, mr, kr, sr,
                                       /*m_idx_start=*/0, lhs_unpacked.data(),
                                       /*lhs_stride_row=*/K * sizeof(float),
                                       lhs_packed.data());
    kai_run_rhs_pack_kxn_f32p2vlx1biasf32_f32_f32_sme(
        /*num_groups=*/1, N, K, nr, kr, sr,
        /*rhs_stride_row=*/N * sizeof(float), rhs_unpacked.data(), bias.data(),
        /*scale=*/nullptr, rhs_packed.data(),
        /*extra_bytes=*/0,
        /*params=*/nullptr);

    float val1 = randnFullRangef();
    float val2 = randnFullRangef();
    clamp_min = std::min(val1, val2);
    clamp_max = std::max(val1, val2);

    // Compute the reference result.
    if (verifyResults) {
      result_ref.resize(M * N, 0.0f);
      gemm_f32_ref(M, N, K, lhs_packed.data(), rhs_packed.data(),
                   result_ref.data(), clamp_min, clamp_max);
    }
  }

  void run(unsigned iter) override {
    // run() can be called multiple times, don't overwrite the result.
    float *resultPtr = (iter == 0) ? result.data() : result_scratch.data();
    if (KT == KernelT::Reference)
      gemm_f32_ref(M, N, K, lhs_packed.data(), rhs_packed.data(), resultPtr,
                   clamp_min, clamp_max);
    if (KT == KernelT::RippleOpt)
      gemm_f32_ripple(M, N, K, lhs_packed.data(), rhs_packed.data(), resultPtr,
                      clamp_min, clamp_max);
  }

  bool verify() const override {
    if (!verifyResults)
      return true;
    return isclose(M * N, result_ref.data(), result.data());
  }
};

// Verification tests.
#define X(M, N, K, NAME, DESC)                                                 \
  DefineTest<GemmF32Test<M, N, K, true, RippleOpt>> GemmF32Test_##NAME(DESC);

// Single element.
X(1, 1, 1, 1x1x1, "gemm.f32.ripple.verify.1x1x1")
// Single row/column.
X(1, 32, 32, 1x32x32, "gemm.f32.ripple.verify.1x32x32")
X(32, 1, 32, 32x1x32, "gemm.f32.ripple.verify.32x1x32")
X(32, 32, 1, 32x32x1, "gemm.f32.ripple.verify.32x32x1")
// Slightly off from block boundaries.
X(17, 17, 17, 17, "gemm.f32.ripple.verify.17")
X(15, 15, 15, 16, "gemm.f32.ripple.verify.16")
X(33, 33, 33, 33, "gemm.f32.ripple.verify.33")
X(31, 31, 31, 31, "gemm.f32.ripple.verify.31")
X(63, 63, 63, 63, "gemm.f32.ripple.verify.63")
X(65, 65, 65, 65, "gemm.f32.ripple.verify.65")
// Single Tile.
X(16, 16, 64, 16x16x64, "gemm.f32.ripple.verify.oneTile")
// Tall matrices (M > N).
X(64, 16, 64, 128x32x64, "gemm.f32.ripple.verify.tall")
X(64, 32, 64, 256x64x128, "gemm.f32.ripple.verify.tall2")
// Wide matrices (N > M).
X(16, 64, 64, 32x128x64, "gemm.f32.ripple.verify.wide")
X(32, 64, 64, 64x256x128, "gemm.f32.ripple.verify.wide2")
// Large K.
X(32, 32, 1024, 32x32x256, "gemm.f32.ripple.verify.deep")

#undef X

// Perf tests, skip verification.
#define X(SIZE)                                                                \
  DefineTest<GemmF32Test<SIZE, SIZE, SIZE, false, Reference>>                  \
      GemmF32Perf_##SIZE##_ref("gemm.f32.ref.perf." #SIZE);                    \
  DefineTest<GemmF32Test<SIZE, SIZE, SIZE, false, RippleOpt>>                  \
      GemmF32Perf_##SIZE##_ripple("gemm.f32.ripple.perf." #SIZE);

X(128)
X(256)
X(512)

#undef X

} // namespace ripple_test_suite
