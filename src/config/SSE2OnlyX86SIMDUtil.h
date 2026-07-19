/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef KRKRSDL2_SSE2_ONLY_X86_SIMD_UTIL_H
#define KRKRSDL2_SSE2_ONLY_X86_SIMD_UTIL_H

/*
 * krkrz's shared x86simdutil.h mixes SSE and AVX helpers. The macOS arm64
 * target compiles only its SSE2 implementations through SIMDe, so expose the
 * small SSE subset those translation units use and suppress the mixed header.
 */
#ifndef __X86_SIMD_UTIL_H__
#define __X86_SIMD_UTIL_H__

#include "SIMDeRenames.h"

extern __m128 log_ps(__m128 x);
extern __m128 exp_ps(__m128 x);
extern __m128 sin_ps(__m128 x);
extern __m128 cos_ps(__m128 x);

inline __m128 pow_ps(__m128 x, __m128 y)
{
	return exp_ps(_mm_mul_ps(y, log_ps(x)));
}

inline __m128 m128_rcp_22bit_ps(const __m128 &a)
{
	__m128 estimate = _mm_rcp_ps(a);
	return _mm_sub_ps(
		_mm_add_ps(estimate, estimate),
		_mm_mul_ps(_mm_mul_ps(a, estimate), estimate));
}

inline __m128 m128_rcp_22bit_ss(const __m128 &a)
{
	__m128 estimate = _mm_rcp_ss(a);
	return _mm_sub_ss(
		_mm_add_ss(estimate, estimate),
		_mm_mul_ss(_mm_mul_ss(a, estimate), estimate));
}

inline __m128 m128_hsum_sse1_ps(__m128 sum)
{
	__m128 tmp = sum;
	sum = _mm_shuffle_ps(sum, tmp, _MM_SHUFFLE(1, 0, 3, 2));
	sum = _mm_add_ps(sum, tmp);
	tmp = sum;
	sum = _mm_shuffle_ps(sum, tmp, _MM_SHUFFLE(2, 3, 0, 1));
	return _mm_add_ps(sum, tmp);
}

#endif
#endif
