//===----------------------------------------------------------------------===//
//
// (c) 2025-2026 Qualcomm Technologies, Inc. All rights reserved.
//
// See https://spdx.org/licenses/BSD-3-Clause-Clear.html for license
// information.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <ripple-test-suite/ripple-test-suite.h>
#include <ripple.h>
#include <ripple_sme_veclib.h>

// KleidiAI headers
#include "kai/kai_common.h"
#include "kai/ukernels/matmul/matmul_clamp_qai8_qai8p_qsi8cxp/kai_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa.h"
#include "kai/ukernels/matmul/pack/kai_lhs_pack_x8p2vlx4_x8_sme.h"
#include "kai/ukernels/matmul/pack/kai_rhs_pack_kxn_qsi8cxp2vlx4sb_qs8cx_f32_i32_sme.h"
#include "ripple_matmul_i8_i8p_i8p_sme_mopa.h"

/// Wrappers for the actual functions.
namespace {
/// Reference implementation using KleidiAI SME kernel.
void gemm_i8_ref(size_t M, size_t N, size_t K, const int8_t *lhs_packed,
                 const int8_t *rhs_packed, int8_t *result, int32_t clamp_min,
                 int32_t clamp_max, int32_t zero_point) {
  const struct kai_matmul_requantize32_params params = {
      /*min_value=*/clamp_min,
      /*max_value=*/clamp_max,
      /*output_zero_point=*/zero_point,
  };
  kai_run_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa(
      M, N, K, lhs_packed, rhs_packed, result,
      N * sizeof(int8_t), // dst_stride_row
      sizeof(int8_t),     // dst_stride_col
      &params);
}

void gemm_i8_ripple(size_t M, size_t N, size_t K, const int8_t *lhs_packed,
                    const int8_t *rhs_packed, int8_t *result, int32_t clamp_min,
                    int32_t clamp_max, int32_t zero_point) {
  ripple_matmul_i8_i8p_i8p_sme_mopa(M, N, K, lhs_packed, rhs_packed, result,
                                    N * sizeof(int8_t), // dst_stride_row
                                    sizeof(int8_t),     // dst_stride_col
                                    clamp_min, clamp_max, zero_point);
}

} // namespace

namespace ripple_test_suite {

enum KernelT { Reference, RippleOpt };

template <size_t M, size_t N, size_t K, bool verifyResults, KernelT KT>
class GemmI8Test : public Test {
  std::vector<int8_t> lhs_packed, rhs_packed, result, result_scratch,
      result_ref;
  int32_t clamp_min, clamp_max, zero_point;

public:
  std::pair<bool, std::string> xfail() const override {
    return {!verify_ripple_sme_api_fixed_vscale(),
            "Runtime VL different than Ripple's fixed assumption."};
  }

  GemmI8Test(TestFramework &TestFramework) : Test(TestFramework) {
    srand(time(0));
    // Kernel parameters
    const size_t mr =
        kai_get_mr_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa();
    const size_t nr =
        kai_get_nr_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa();
    const size_t kr =
        kai_get_kr_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa();
    const size_t sr =
        kai_get_sr_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa();

    const size_t lhs_packed_size =
        kai_get_lhs_packed_size_lhs_pack_x8p2vlx4_x8_sme(M, K, mr, kr, sr);
    const size_t rhs_packed_size =
        kai_get_rhs_packed_size_rhs_pack_kxn_qsi8cxp2vlx4sb_qs8cx_f32_i32_sme(
            N, K);

    result.resize(M * N, 0);
    result_scratch.resize(M * N, 0);
    lhs_packed.resize(lhs_packed_size, 0);
    rhs_packed.resize(rhs_packed_size, 0);

    // Create unpacked source matrices. We won't use them again.
    std::vector<int8_t> lhs_unpacked(M * K);
    std::vector<int8_t> rhs_unpacked(K * N);
    std::vector<int32_t> bias(N);
    std::vector<float> scale(N);

    auto randInt8 = [&]() -> int8_t {
      const uint8_t u = static_cast<uint8_t>(
          F().random() * std::numeric_limits<uint8_t>::max());
      return static_cast<int8_t>(u);
    };
    auto randInt32 = [&]() -> int32_t {
      const uint32_t u = static_cast<uint32_t>(
          F().random() * std::numeric_limits<uint32_t>::max());
      return static_cast<int32_t>(u);
    };
    auto randScale = [&]() -> float {
      return static_cast<float>(F().random() * 0.09 + 0.01);
    };

    // Initialize unpacked matrices with random data.
    for (size_t i = 0; i < M * K; ++i)
      lhs_unpacked[i] = randInt8();
    for (size_t i = 0; i < K * N; ++i)
      rhs_unpacked[i] = randInt8();
    for (size_t i = 0; i < N; ++i) {
      bias[i] = randInt32();
      scale[i] = randScale();
    }

    const struct kai_rhs_pack_qsi8cx_params rhs_pack_params = {
        /*lhs_zero_point=*/randInt8(),
        /*scale_multiplier=*/randScale(),
    };

    // Pack using KleidiAI functions.
    kai_run_lhs_pack_x8p2vlx4_x8_sme(M, K, mr, kr, sr,
                                     /*m_idx_start=*/0, lhs_unpacked.data(),
                                     /*lhs_stride_row=*/K * sizeof(int8_t),
                                     lhs_packed.data());
    kai_run_rhs_pack_kxn_qsi8cxp2vlx4sb_qs8cx_f32_i32_sme(
        /*num_groups=*/1, N, K, nr, kr, sr,
        /*rhs_stride_row=*/N * sizeof(int8_t), rhs_unpacked.data(), bias.data(),
        scale.data(), rhs_packed.data(),
        /*extra_bytes=*/0, &rhs_pack_params);

    // Use two random int8 values and sort them so clamp_min <= clamp_max.
    const int32_t val1 = randInt8();
    const int32_t val2 = randInt8();
    clamp_min = std::min(val1, val2);
    clamp_max = std::max(val1, val2);
    zero_point = randInt8();

    // Compute the reference result.
    if (verifyResults) {
      result_ref.resize(M * N, 0);
      gemm_i8_ref(M, N, K, lhs_packed.data(), rhs_packed.data(),
                  result_ref.data(), clamp_min, clamp_max, zero_point);
    }
  }

  void run(unsigned iter) override {
    // run() can be called multiple times, don't overwrite the result.
    int8_t *resultPtr = (iter == 0) ? result.data() : result_scratch.data();
    if (KT == KernelT::Reference)
      gemm_i8_ref(M, N, K, lhs_packed.data(), rhs_packed.data(), resultPtr,
                  clamp_min, clamp_max, zero_point);
    if (KT == KernelT::RippleOpt)
      gemm_i8_ripple(M, N, K, lhs_packed.data(), rhs_packed.data(), resultPtr,
                     clamp_min, clamp_max, zero_point);
  }

  bool verify() const override {
    if (!verifyResults)
      return true;
    return isclose(M * N, result_ref.data(), result.data());
  }
};

// Verification tests.
#define X(M, N, K, NAME, DESC)                                                 \
  DefineTest<GemmI8Test<M, N, K, true, RippleOpt>> GemmI8Test_##NAME(DESC);

// Single element.
X(1, 1, 1, 1x1x1, "gemm.i8.ripple.verify.1x1x1")
// Single row/column.
X(1, 32, 32, 1x32x32, "gemm.i8.ripple.verify.1x32x32")
X(32, 1, 32, 32x1x32, "gemm.i8.ripple.verify.32x1x32")
X(32, 32, 1, 32x32x1, "gemm.i8.ripple.verify.32x32x1")
// Slightly off from block boundaries.
X(17, 17, 17, 17, "gemm.i8.ripple.verify.17")
X(15, 15, 15, 16, "gemm.i8.ripple.verify.16")
X(33, 33, 33, 33, "gemm.i8.ripple.verify.33")
X(31, 31, 31, 31, "gemm.i8.ripple.verify.31")
X(63, 63, 63, 63, "gemm.i8.ripple.verify.63")
X(65, 65, 65, 65, "gemm.i8.ripple.verify.65")
// K at/around the kr=4 packing boundary (int8 LHS/RHS are packed in groups
// of 4 along K.
X(8, 8, 3, 8x8x3, "gemm.i8.ripple.verify.k3")
X(8, 8, 4, 8x8x4, "gemm.i8.ripple.verify.k4")
X(8, 8, 5, 8x8x5, "gemm.i8.ripple.verify.k5")
X(8, 8, 7, 8x8x7, "gemm.i8.ripple.verify.k7")
X(8, 8, 8, 8x8x8, "gemm.i8.ripple.verify.k8")
X(8, 8, 9, 8x8x9, "gemm.i8.ripple.verify.k9")
// Single Tile.
X(16, 16, 64, 16x16x64, "gemm.i8.ripple.verify.oneTile")
// Tall matrices (M > N).
X(64, 16, 64, 128x32x64, "gemm.i8.ripple.verify.tall")
X(64, 32, 64, 256x64x128, "gemm.i8.ripple.verify.tall2")
// Wide matrices (N > M).
X(16, 64, 64, 32x128x64, "gemm.i8.ripple.verify.wide")
X(32, 64, 64, 64x256x128, "gemm.i8.ripple.verify.wide2")
// Large K.
X(32, 32, 1024, 32x32x256, "gemm.i8.ripple.verify.deep")

#undef X

// Perf tests, skip verification.
#define X(SIZE)                                                                \
  DefineTest<GemmI8Test<SIZE, SIZE, SIZE, false, Reference>>                   \
      GemmI8Perf_##SIZE##_ref("gemm.i8.ref.perf." #SIZE);                      \
  DefineTest<GemmI8Test<SIZE, SIZE, SIZE, false, RippleOpt>>                   \
      GemmI8Perf_##SIZE##_ripple("gemm.i8.ripple.perf." #SIZE);

X(128)
X(256)
X(512)
X(1024)
X(2048)

#undef X

} // namespace ripple_test_suite
