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

#include "ripple_matmul_f32_f32p_f32p_sme_mopa.h"
#include <algorithm>
#include <ripple.h>
#include <ripple_sme_veclib.h>

#define PE_TYPE 0
#define SVE_BYTES (512 / 8)
#define VEC_ELEMS (SVE_BYTES / sizeof(float))
#define BLOCK_LEN (VEC_ELEMS * 2)

/// Ripple implementationx. Assuming VL == 512bits.
/// We use Ripple vector library implemented in ripple_sme_veclib.h, these
/// expand to vectorized implementations according to the block size specified
/// by ripple_set_block_shape(). Functions include: zeroAccumulator_f32,
/// outerProductAccumulate_f32, and getAccumHorizSlice_f32. See the header,
/// https://qualcomm.github.io/learn-ripple/, and inline comments below for more
/// details.
__arm_new("za") __arm_locally_streaming void ripple_matmul_f32_f32p_f32p_sme_mopa(
    size_t M, size_t N, size_t K, const void *lhs_packed,
    const void *rhs_packed, void *dst, size_t dst_stride_row_,
    size_t dst_stride_col, float CLAMP_MIN, float CLAMP_MAX) {
  const size_t dst_stride_row =
      dst_stride_row_ / sizeof(float); // byte to index
  const float *LHS = (const float *)lhs_packed;
  const float *RHS = (const float *)rhs_packed;
  float *DST = (float *)dst;

  ripple_block_t TileShape = ripple_set_block_shape(PE_TYPE,
                                                    /*size dim 0*/ BLOCK_LEN,
                                                    /*size dim 1*/ BLOCK_LEN);
  // Our block size (BLOCK_LEN) is 2 vector lengths.
  ripple_block_t VecShape = ripple_set_block_shape(PE_TYPE,
                                                   /*size dim 0*/ BLOCK_LEN);

  // ripple_id gives a vector index for the shape.
  size_t InVecIdx = ripple_id(VecShape, /*dimension*/ 0);

  for (size_t m = 0; m < M; m += BLOCK_LEN) {
    for (size_t n = 0; n < N; n += BLOCK_LEN) {
      // Index calculation. For RHS we add 1 for the packed bias.
      size_t LHS_BeginIdx = (m * K);
      size_t RHS_BeginIdx = (n * (K + 1));

      // Zero the tiles before the multiply-accumulate loop.
      zeroAccumulator_f32(TileShape);
      // Prefill the tiles with bias.
      // outerproduct(1.0f, Bias) fills the full tile with bias.
      outerProductAccumulate_f32(1.0f, RHS[RHS_BeginIdx + InVecIdx]);
      // Rest of the k values in the packed array are RHS data values.
      RHS_BeginIdx += BLOCK_LEN;

      for (size_t k = 0; k < K; ++k) {
        // Flattened LHS transposed blocks of BLOCK_LEN x K.
        // Flattened RHS.
        float LHS_Vec = LHS[LHS_BeginIdx + (k * BLOCK_LEN) + InVecIdx];
        float RHS_Vec = RHS[RHS_BeginIdx + (k * BLOCK_LEN) + InVecIdx];
        outerProductAccumulate_f32(LHS_Vec, RHS_Vec);
      }

      // Loop along M dimension.
      for (size_t m_slice = 0; m_slice < std::min(M - m, BLOCK_LEN);
           ++m_slice) {
        size_t DST_RowIdx = (m + m_slice) * dst_stride_row;
        // Masking along N dimension.
        if (n + InVecIdx < N) {
          // This gives us a block of rows at a specific index in the tile.
          float T = getAccumHorizSlice_f32(TileShape, m_slice);
          T = __builtin_elementwise_maximum(
              CLAMP_MIN, __builtin_elementwise_minimum(CLAMP_MAX, T));
          DST[DST_RowIdx + n + InVecIdx] = T;
        }
      }
    }
  }
}
