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
// Modified by Qualcomm Technologies, Inc.
//
//===----------------------------------------------------------------------===//

#pragma once
#include <cstddef>
#include <cstdint>

__arm_new("za") __arm_locally_streaming void ripple_matmul_i8_i8p_i8p_sme_mopa(
    size_t M, size_t N, size_t K, const void *lhs_packed,
    const void *rhs_packed, void *dst, size_t dst_stride_row,
    size_t dst_stride_col, int32_t CLAMP_MIN, int32_t CLAMP_MAX,
    int32_t ZeroPoint);