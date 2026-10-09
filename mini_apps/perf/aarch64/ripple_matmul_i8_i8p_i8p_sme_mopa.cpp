//===----------------------------------------------------------------------===//
//
// SPDX-FileCopyrightText: Copyright Arm Limited and/or its affiliates
// SPDX-FileCopyrightText: (c) 2025-2026 Qualcomm Technologies, Inc.
//
// SPDX-License-Identifier: Apache-2.0
//
// Derived from a KleidiAI SME MOPA matmul micro-kernel
// (https://gitlab.arm.com/kleidi/kleidiai), licensed under the Apache License,
// Version 2.0. See LICENSE-KLEIDIAI.TXT or
// http://www.apache.org/licenses/LICENSE-2.0.
//
// Modified by Qualcomm Technologies, Inc.: re-implemented in C++ using the
// Ripple programming model.
//
//===----------------------------------------------------------------------===//

#include "ripple_matmul_i8_i8p_i8p_sme_mopa.h"
#include <algorithm>
#include <ripple.h>
#include <ripple_sme_veclib.h>

#define PE_TYPE 0

#define SVE_BYTES (512 / 8)
#define VEC_ELEMS_I8 (SVE_BYTES / sizeof(int8_t))
#define VEC_ELEMS_I32 (SVE_BYTES / sizeof(int32_t))
#define BLOCK_LEN_I8 (VEC_ELEMS_I8 * 2)
#define BLOCK_LEN_I32 (VEC_ELEMS_I32 * 2)

/// Ripple based implementation of int8 signed matmul using SME.
/// We use Ripple vector library implemented in ripple_sme_veclib.h, functions
/// include: zeroAccumulator_i32, outerProductAccumulate_i32,
/// addHorizToAccumulator_i32, and getAccumHorizSlice_i32. These functions
/// expand to vectorized implementations that work on the SME storage. The block
/// sizes in our implementation are fixed (as opposed to scalable), and are set
/// using calls to ripple_set_block_shape(). We assume that VL == 512bits.
/// See the header <ripple_sme_veclib.h> for implementation details and the
/// interface, and https://qualcomm.github.io/learn-ripple/ and inline comments
/// below for more details.
///
/// Reference KleidiAI kernel:
///   kai_run_matmul_clamp_qai8_qai8p2vlx4_qsi8cxp2vlx4sb_2vlx2vl_sme_mopa
/// Packing kernels:
///   kai_run_lhs_pack_x8p2vlx4_x8_sme
///   kai_run_rhs_pack_kxn_qsi8cxp2vlx4sb_qs8cx_f32_i32_sme
__arm_new("za") __arm_locally_streaming void ripple_matmul_i8_i8p_i8p_sme_mopa(
    size_t M, size_t N, size_t K, const void *lhs_packed,
    const void *rhs_packed, void *dst, size_t dst_stride_row,
    size_t dst_stride_col, int32_t CLAMP_MIN, int32_t CLAMP_MAX,
    int32_t ZeroPoint) {
  const int8_t *LHS = (const int8_t *)lhs_packed;
  const int8_t *RHS = (const int8_t *)rhs_packed;
  int8_t *DST = (int8_t *)dst;

  ripple_block_t TileShape =
      ripple_set_block_shape(PE_TYPE,
                             /*size dim 0*/ BLOCK_LEN_I32,
                             /*size dim 1*/ BLOCK_LEN_I32);
  // We use both I32 and I8 type shaped and indices to access the same vector
  // because values of different bit-width are packed in the same array.
  ripple_block_t VecShapeI32 =
      ripple_set_block_shape(PE_TYPE,
                             /*size dim 0*/ BLOCK_LEN_I32);
  size_t VecIdxI32 = ripple_id(VecShapeI32, /*dimension*/ 0);
  ripple_block_t InVecShapeI8 =
      ripple_set_block_shape(PE_TYPE,
                             /*size dim 0*/ BLOCK_LEN_I8);
  size_t VecIdxI8 = ripple_id(InVecShapeI8, /*dimension*/ 0);

  // Four k values are packed togather.
  size_t K_RoundUp = (K + 3) & ~3;

  for (size_t m = 0; m < M; m += BLOCK_LEN_I32) {
    size_t RHS_ScalarIdx = 0;
    for (size_t n = 0; n < N; n += BLOCK_LEN_I32) {
      // Zero the tiles before the multiply-accumulate loop.
      zeroAccumulator_i32(TileShape);

      size_t LHS_ScalarIdx = m * K_RoundUp;
      int32_t Bias =
          reinterpret_cast<const int32_t *>(&RHS[RHS_ScalarIdx])[VecIdxI32];
      // Our bias is int32, we can pre-fill the zero'ed storage with the bias by
      // adding it to all tiles.
      addHorizToAccumulator_i32(Bias);
      RHS_ScalarIdx += BLOCK_LEN_I8;

      for (size_t k = 0; k < K; k += 4) {
        // Flattened LHS and RHS.
        int8_t LHS_Vec = LHS[LHS_ScalarIdx + VecIdxI8];
        int8_t RHS_Vec = RHS[RHS_ScalarIdx + VecIdxI8];
        LHS_ScalarIdx += BLOCK_LEN_I8;
        RHS_ScalarIdx += (BLOCK_LEN_I8);
        outerProductAccumulate_i8(LHS_Vec, RHS_Vec);
      }

      // Requantize the matmul result and store it.
      float Scale =
          reinterpret_cast<const float *>(&RHS[RHS_ScalarIdx])[VecIdxI32];
      RHS_ScalarIdx += BLOCK_LEN_I8;

      // Loop along M dimension.
      for (size_t m_slice = 0; m_slice < std::min(M - m, BLOCK_LEN_I32);
           ++m_slice) {
        size_t DST_RowIdx = (m + m_slice) * dst_stride_row;
        // Masking along N dimension.
        if (n + VecIdxI32 < N) {
          int32_t IntRes = getAccumHorizSlice_i32(TileShape, m_slice);
          IntRes = __builtin_elementwise_roundeven(float(IntRes) * Scale +
                                                   float(ZeroPoint));
          IntRes = __builtin_elementwise_min(
              __builtin_elementwise_max(IntRes, CLAMP_MIN), CLAMP_MAX);
          DST[DST_RowIdx + (n + VecIdxI32)] = IntRes;
        }
      }
    }
  }
}