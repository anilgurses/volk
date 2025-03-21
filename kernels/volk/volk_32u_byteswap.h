/* -*- c++ -*- */
/*
 * Copyright 2012, 2014 Free Software Foundation, Inc.
 *
 * This file is part of VOLK
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

/*!
 * \page volk_32u_byteswap
 *
 * \b Overview
 *
 * Byteswaps (in-place) an aligned vector of int32_t's.
 *
 * <b>Dispatcher Prototype</b>
 * \code
 * void volk_32u_byteswap(uint32_t* intsToSwap, unsigned int num_points)
 * \endcode
 *
 * \b Inputs
 * \li intsToSwap: The vector of data to byte swap.
 * \li num_points: The number of data points.
 *
 * \b Outputs
 * \li intsToSwap: returns as an in-place calculation.
 *
 * \b Example
 * \code
 *   int N = 10;
 *   unsigned int alignment = volk_get_alignment();
 *
 *   uint32_t bitstring[] = {0x0, 0x1, 0xf, 0xffffffff,
 *       0x5a5a5a5a, 0xa5a5a5a5, 0x2a2a2a2a,
 *       0xffffffff, 0x32, 0x64};
 *   uint32_t hamming_distance = 0;
 *
 *   printf("byteswap vector =\n");
 *   for(unsigned int ii=0; ii<N; ++ii){
 *       printf("    %.8x\n", bitstring[ii]);
 *   }
 *
 *   volk_32u_byteswap(bitstring, N);
 *
 *   printf("byteswapped vector =\n");
 *   for(unsigned int ii=0; ii<N; ++ii){
 *       printf("    %.8x\n", bitstring[ii]);
 *   }
 * \endcode
 */

#ifndef INCLUDED_volk_32u_byteswap_u_H
#define INCLUDED_volk_32u_byteswap_u_H

#include <inttypes.h>
#include <stdio.h>

#if LV_HAVE_AVX2
#include <immintrin.h>
static inline void volk_32u_byteswap_u_avx2(uint32_t* intsToSwap, unsigned int num_points)
{

    unsigned int number;

    const unsigned int nPerSet = 8;
    const uint64_t nSets = num_points / nPerSet;

    uint32_t* inputPtr = intsToSwap;

    const uint8_t shuffleVector[32] = { 3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,
                                        8,  15, 14, 13, 12, 19, 18, 17, 16, 23, 22,
                                        21, 20, 27, 26, 25, 24, 31, 30, 29, 28 };

    const __m256i myShuffle = _mm256_loadu_si256((__m256i*)&shuffleVector);

    for (number = 0; number < nSets; number++) {

        // Load the 32t values, increment inputPtr later since we're doing it in-place.
        const __m256i input = _mm256_loadu_si256((__m256i*)inputPtr);
        const __m256i output = _mm256_shuffle_epi8(input, myShuffle);

        // Store the results
        _mm256_storeu_si256((__m256i*)inputPtr, output);
        inputPtr += nPerSet;
    }

    // Byteswap any remaining points:
    for (number = nSets * nPerSet; number < num_points; number++) {
        uint32_t outputVal = *inputPtr;
        outputVal = (((outputVal >> 24) & 0xff) | ((outputVal >> 8) & 0x0000ff00) |
                     ((outputVal << 8) & 0x00ff0000) | ((outputVal << 24) & 0xff000000));
        *inputPtr = outputVal;
        inputPtr++;
    }
}
#endif /* LV_HAVE_AVX2 */


#ifdef LV_HAVE_SSE2
#include <emmintrin.h>

static inline void volk_32u_byteswap_u_sse2(uint32_t* intsToSwap, unsigned int num_points)
{
    unsigned int number = 0;

    uint32_t* inputPtr = intsToSwap;
    __m128i input, byte1, byte2, byte3, byte4, output;
    __m128i byte2mask = _mm_set1_epi32(0x00FF0000);
    __m128i byte3mask = _mm_set1_epi32(0x0000FF00);

    const uint64_t quarterPoints = num_points / 4;
    for (; number < quarterPoints; number++) {
        // Load the 32t values, increment inputPtr later since we're doing it in-place.
        input = _mm_loadu_si128((__m128i*)inputPtr);
        // Do the four shifts
        byte1 = _mm_slli_epi32(input, 24);
        byte2 = _mm_slli_epi32(input, 8);
        byte3 = _mm_srli_epi32(input, 8);
        byte4 = _mm_srli_epi32(input, 24);
        // Or bytes together
        output = _mm_or_si128(byte1, byte4);
        byte2 = _mm_and_si128(byte2, byte2mask);
        output = _mm_or_si128(output, byte2);
        byte3 = _mm_and_si128(byte3, byte3mask);
        output = _mm_or_si128(output, byte3);
        // Store the results
        _mm_storeu_si128((__m128i*)inputPtr, output);
        inputPtr += 4;
    }

    // Byteswap any remaining points:
    number = quarterPoints * 4;
    for (; number < num_points; number++) {
        uint32_t outputVal = *inputPtr;
        outputVal = (((outputVal >> 24) & 0xff) | ((outputVal >> 8) & 0x0000ff00) |
                     ((outputVal << 8) & 0x00ff0000) | ((outputVal << 24) & 0xff000000));
        *inputPtr = outputVal;
        inputPtr++;
    }
}
#endif /* LV_HAVE_SSE2 */


#ifdef LV_HAVE_NEON
#include <arm_neon.h>

static inline void volk_32u_byteswap_neon(uint32_t* intsToSwap, unsigned int num_points)
{
    uint32_t* inputPtr = intsToSwap;
    unsigned int number = 0;
    unsigned int n8points = num_points / 8;

    uint8x8x4_t input_table;
    uint8x8_t int_lookup01, int_lookup23, int_lookup45, int_lookup67;
    uint8x8_t swapped_int01, swapped_int23, swapped_int45, swapped_int67;

    /* these magic numbers are used as byte-indices in the LUT.
       they are pre-computed to save time. A simple C program
       can calculate them; for example for lookup01:
      uint8_t chars[8] = {24, 16, 8, 0, 25, 17, 9, 1};
      for(ii=0; ii < 8; ++ii) {
          index += ((uint64_t)(*(chars+ii))) << (ii*8);
      }
    */
    int_lookup01 = vcreate_u8(74609667900706840);
    int_lookup23 = vcreate_u8(219290013576860186);
    int_lookup45 = vcreate_u8(363970359253013532);
    int_lookup67 = vcreate_u8(508650704929166878);

    for (number = 0; number < n8points; ++number) {
        input_table = vld4_u8((uint8_t*)inputPtr);
        swapped_int01 = vtbl4_u8(input_table, int_lookup01);
        swapped_int23 = vtbl4_u8(input_table, int_lookup23);
        swapped_int45 = vtbl4_u8(input_table, int_lookup45);
        swapped_int67 = vtbl4_u8(input_table, int_lookup67);
        vst1_u8((uint8_t*)inputPtr, swapped_int01);
        vst1_u8((uint8_t*)(inputPtr + 2), swapped_int23);
        vst1_u8((uint8_t*)(inputPtr + 4), swapped_int45);
        vst1_u8((uint8_t*)(inputPtr + 6), swapped_int67);

        inputPtr += 8;
    }

    for (number = n8points * 8; number < num_points; ++number) {
        uint32_t output = *inputPtr;
        output = (((output >> 24) & 0xff) | ((output >> 8) & 0x0000ff00) |
                  ((output << 8) & 0x00ff0000) | ((output << 24) & 0xff000000));

        *inputPtr = output;
        inputPtr++;
    }
}
#endif /* LV_HAVE_NEON */

#ifdef LV_HAVE_NEONV8
#include <arm_neon.h>

static inline void volk_32u_byteswap_neonv8(uint32_t* intsToSwap, unsigned int num_points)
{
    uint32_t* inputPtr = (uint32_t*)intsToSwap;
    const unsigned int n8points = num_points / 8;
    uint8x16_t input;
    uint8x16_t idx = { 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12 };

    unsigned int number = 0;
    for (number = 0; number < n8points; ++number) {
        __VOLK_PREFETCH(inputPtr + 8);
        input = vld1q_u8((uint8_t*)inputPtr);
        input = vqtbl1q_u8(input, idx);
        vst1q_u8((uint8_t*)inputPtr, input);
        inputPtr += 4;

        input = vld1q_u8((uint8_t*)inputPtr);
        input = vqtbl1q_u8(input, idx);
        vst1q_u8((uint8_t*)inputPtr, input);
        inputPtr += 4;
    }

    for (number = n8points * 8; number < num_points; ++number) {
        uint32_t output = *inputPtr;

        output = (((output >> 24) & 0xff) | ((output >> 8) & 0x0000ff00) |
                  ((output << 8) & 0x00ff0000) | ((output << 24) & 0xff000000));

        *inputPtr++ = output;
    }
}
#endif /* LV_HAVE_NEONV8 */


#ifdef LV_HAVE_GENERIC

static inline void volk_32u_byteswap_generic(uint32_t* intsToSwap,
                                             unsigned int num_points)
{
    uint32_t* inputPtr = intsToSwap;

    unsigned int point;
    for (point = 0; point < num_points; point++) {
        uint32_t output = *inputPtr;
        output = (((output >> 24) & 0xff) | ((output >> 8) & 0x0000ff00) |
                  ((output << 8) & 0x00ff0000) | ((output << 24) & 0xff000000));

        *inputPtr = output;
        inputPtr++;
    }
}
#endif /* LV_HAVE_GENERIC */


#endif /* INCLUDED_volk_32u_byteswap_u_H */
#ifndef INCLUDED_volk_32u_byteswap_a_H
#define INCLUDED_volk_32u_byteswap_a_H

#include <inttypes.h>
#include <stdio.h>


#if LV_HAVE_AVX2
#include <immintrin.h>
static inline void volk_32u_byteswap_a_avx2(uint32_t* intsToSwap, unsigned int num_points)
{

    unsigned int number;

    const unsigned int nPerSet = 8;
    const uint64_t nSets = num_points / nPerSet;

    uint32_t* inputPtr = intsToSwap;

    const uint8_t shuffleVector[32] = { 3,  2,  1,  0,  7,  6,  5,  4,  11, 10, 9,
                                        8,  15, 14, 13, 12, 19, 18, 17, 16, 23, 22,
                                        21, 20, 27, 26, 25, 24, 31, 30, 29, 28 };

    const __m256i myShuffle = _mm256_loadu_si256((__m256i*)&shuffleVector);

    for (number = 0; number < nSets; number++) {

        // Load the 32t values, increment inputPtr later since we're doing it in-place.
        const __m256i input = _mm256_load_si256((__m256i*)inputPtr);
        const __m256i output = _mm256_shuffle_epi8(input, myShuffle);

        // Store the results
        _mm256_store_si256((__m256i*)inputPtr, output);
        inputPtr += nPerSet;
    }

    // Byteswap any remaining points:
    for (number = nSets * nPerSet; number < num_points; number++) {
        uint32_t outputVal = *inputPtr;
        outputVal = (((outputVal >> 24) & 0xff) | ((outputVal >> 8) & 0x0000ff00) |
                     ((outputVal << 8) & 0x00ff0000) | ((outputVal << 24) & 0xff000000));
        *inputPtr = outputVal;
        inputPtr++;
    }
}
#endif /* LV_HAVE_AVX2 */


#ifdef LV_HAVE_SSE2
#include <emmintrin.h>


static inline void volk_32u_byteswap_a_sse2(uint32_t* intsToSwap, unsigned int num_points)
{
    unsigned int number = 0;

    uint32_t* inputPtr = intsToSwap;
    __m128i input, byte1, byte2, byte3, byte4, output;
    __m128i byte2mask = _mm_set1_epi32(0x00FF0000);
    __m128i byte3mask = _mm_set1_epi32(0x0000FF00);

    const uint64_t quarterPoints = num_points / 4;
    for (; number < quarterPoints; number++) {
        // Load the 32t values, increment inputPtr later since we're doing it in-place.
        input = _mm_load_si128((__m128i*)inputPtr);
        // Do the four shifts
        byte1 = _mm_slli_epi32(input, 24);
        byte2 = _mm_slli_epi32(input, 8);
        byte3 = _mm_srli_epi32(input, 8);
        byte4 = _mm_srli_epi32(input, 24);
        // Or bytes together
        output = _mm_or_si128(byte1, byte4);
        byte2 = _mm_and_si128(byte2, byte2mask);
        output = _mm_or_si128(output, byte2);
        byte3 = _mm_and_si128(byte3, byte3mask);
        output = _mm_or_si128(output, byte3);
        // Store the results
        _mm_store_si128((__m128i*)inputPtr, output);
        inputPtr += 4;
    }

    // Byteswap any remaining points:
    number = quarterPoints * 4;
    for (; number < num_points; number++) {
        uint32_t outputVal = *inputPtr;
        outputVal = (((outputVal >> 24) & 0xff) | ((outputVal >> 8) & 0x0000ff00) |
                     ((outputVal << 8) & 0x00ff0000) | ((outputVal << 24) & 0xff000000));
        *inputPtr = outputVal;
        inputPtr++;
    }
}
#endif /* LV_HAVE_SSE2 */

#ifdef LV_HAVE_RVV
#include <riscv_vector.h>

static inline void volk_32u_byteswap_rvv(uint32_t* intsToSwap, unsigned int num_points)
{
    size_t n = num_points;
    size_t vlmax = __riscv_vsetvlmax_e8m1();
    if (vlmax <= 256) {
        vuint8m1_t vidx = __riscv_vreinterpret_u8m1(
            __riscv_vsub(__riscv_vreinterpret_u32m1(__riscv_vid_v_u8m1(vlmax)),
                         0x3020100 - 0x10203,
                         vlmax / 4));
        for (size_t vl; n > 0; n -= vl, intsToSwap += vl) {
            vl = __riscv_vsetvl_e32m8(n);
            vuint8m8_t v =
                __riscv_vreinterpret_u8m8(__riscv_vle32_v_u32m8(intsToSwap, vl));
            v = RISCV_PERM8(__riscv_vrgather, v, vidx);
            __riscv_vse32(intsToSwap, __riscv_vreinterpret_u32m8(v), vl);
        }
    } else {
        vuint16m2_t vidx = __riscv_vreinterpret_u16m2(
            __riscv_vsub(__riscv_vreinterpret_u64m2(__riscv_vid_v_u16m2(vlmax)),
                         0x3000200010000 - 0x100020003,
                         vlmax / 4));
        for (size_t vl; n > 0; n -= vl, intsToSwap += vl) {
            vl = __riscv_vsetvl_e32m8(n);
            vuint8m8_t v =
                __riscv_vreinterpret_u8m8(__riscv_vle32_v_u32m8(intsToSwap, vl));
            v = RISCV_PERM8(__riscv_vrgatherei16, v, vidx);
            __riscv_vse32(intsToSwap, __riscv_vreinterpret_u32m8(v), vl);
        }
    }
}
#endif /* LV_HAVE_RVV */

#ifdef LV_HAVE_RVA23
#include <riscv_vector.h>

static inline void volk_32u_byteswap_rva23(uint32_t* intsToSwap, unsigned int num_points)
{
    size_t n = num_points;
    for (size_t vl; n > 0; n -= vl, intsToSwap += vl) {
        vl = __riscv_vsetvl_e32m8(n);
        vuint32m8_t v = __riscv_vle32_v_u32m8(intsToSwap, vl);
        __riscv_vse32(intsToSwap, __riscv_vrev8(v, vl), vl);
    }
}
#endif /* LV_HAVE_RVA23 */

#endif /* INCLUDED_volk_32u_byteswap_a_H */
