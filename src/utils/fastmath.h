/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018-2019 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace lczero {
// These stunts are performed by trained professionals, do not try this at home.

// Fast approximate log2(x). Does no range checking.
// The approximation used here is log2(2^N*(1+f)) ~ N+f*(1+k-k*f) where N is the
// exponent and f the fraction (mantissa), f>=0. The constant k is used to tune
// the approximation accuracy. In the final version some constants were slightly
// modified for better accuracy with 32 bit floating point math.
inline float FastLog2(const float a) {
  uint32_t tmp;
  std::memcpy(&tmp, &a, sizeof(float));
  uint32_t expb = tmp >> 23;
  tmp = (tmp & 0x7fffff) | (0x7f << 23);
  float out;
  std::memcpy(&out, &tmp, sizeof(float));
  out -= 1.0f;
  // Minimize max absolute error.
  return out * (1.3465552f - 0.34655523f * out) - 127 + expb;
}

// Fast approximate 2^x. Does only limited range checking.
// The approximation used here is 2^(N+f) ~ 2^N*(1+f*(1-k+k*f)) where N is the
// integer and f the fractional part, f>=0. The constant k is used to tune the
// approximation accuracy. In the final version some constants were slightly
// modified for better accuracy with 32 bit floating point math.
inline float FastExp2(const float a) {
  int32_t exp;
  if (a < 0) {
    if (a < -126) return 0.0;
    // Not all compilers optimize floor, so we use (a-1) here to round down.
    // This is obviously off-by-one for integer a, but fortunately the error
    // correction term gives the exact value for 1 (by design, for continuity).
    exp = static_cast<int32_t>(a - 1);
  } else {
    exp = static_cast<int32_t>(a);
  }
  float out = a - exp;
  // Minimize max relative error.
  out = 1.0f + out * (0.6602339f + 0.33976606f * out);
  int32_t tmp;
  std::memcpy(&tmp, &out, sizeof(float));
  tmp += static_cast<int32_t>(static_cast<uint32_t>(exp) << 23);
  std::memcpy(&out, &tmp, sizeof(float));
  return out;
}

// Fast approximate ln(x). Does no range checking.
inline float FastLog(const float a) {
  return 0.6931471805599453f * FastLog2(a);
}

// Fast approximate exp(x). Does only limited range checking.
inline float FastExp(const float a) { return FastExp2(1.442695040f * a); }

// Safeguarded fast logistic function, based on FastExp().
inline float FastLogistic(const float a) {
  if (a > 20.0f) {return 1.0f;}
  if (a < -20.0f) {return 0.0f;}
  return 1.0f / (1.0f + FastExp(-a));
}

inline float FastSign(const float a) {
  // Microsoft compiler does not have a builtin for copysign and emits a
  // library call which is too expensive for hot paths.
#if defined(_MSC_VER)
  // This doesn't treat signed 0's the same way that copysign does, but it
  // should be good enough, for our use case.
  return a < 0 ? -1.0f : 1.0f;
#else
  return std::copysign(1.0f, a);
#endif
}

// Fast approximate 1/sqrt(x) using bit manipulation.
// Based on the classic Quake III algorithm.
// Expects positive input values. Does no range checking; produces undefined
// behavior for zero, negative, or special values (NaN, infinity).
inline float FastInvSqrt(const float a) {
  float halfx = 0.5f * a;
  uint32_t i;
  std::memcpy(&i, &a, sizeof(float));
  i = 0x5f3759df - (i >> 1);  // Magic constant
  float y;
  std::memcpy(&y, &i, sizeof(float));
  y = y * (1.5f - halfx * y * y);  // Newton iteration
  return y;
}

// Fast approximate pow(a, b) using bit manipulation.
// Based on Martin Ankerl's implementation (https://martin.ankerl.com/2012/01/25/optimized-approximative-pow-in-c-and-cpp/)
// Approximately 4x faster than std::pow for fractional exponents.
// Accuracy typically within 5%, with rare cases up to 12% error.
// For better accuracy with integer or near-integer exponents, use FastPrecisePow.
// Expects positive base. Does no range checking.
inline float FastPow(float a, float b) {
  union {
    float f;
    int32_t i;
  } u = {a};
  u.i = static_cast<int32_t>(b * (u.i - 1064866805) + 1064866805);
  return u.f;
}

// More accurate version of FastPow that handles integer exponent part separately.
// Approximately 3x faster than std::pow.
// Significantly more accurate than FastPow when exponent > 1.
// Expects positive base. Does no range checking.
inline float FastPrecisePow(float a, float b) {
  // Separate integer and fractional parts
  int e = static_cast<int>(b);
  union {
    float f;
    int32_t i;
  } u = {a};
  u.i = static_cast<int32_t>((b - e) * (u.i - 1064866805) + 1064866805);

  // Handle integer part using exponentiation by squaring
  float r = 1.0f;
  float base = a;
  int exp = e;
  if (exp < 0) {
    base = 1.0f / base;
    exp = -exp;
  }
  while (exp) {
    if (exp & 1) {
      r *= base;
    }
    base *= base;
    exp >>= 1;
  }

  return r * u.f;
}

// Apply positive policy decay transformation with fixed sqrt decay (exponent=0.5).
// Returns raw (unnormalized) P_eff = 1 / (1 + odds * power_term) where:
//   odds = 1/P - 1
//   effective_scale = scale_per_move * num_legal_moves
//   power_term = 1/sqrt(1 + N/effective_scale)
// When P=0, scale=0, or num_legal_moves<=0, returns P unchanged.
// NOTE: Caller must normalize by sum of all raw_P_eff values to ensure sum(P_eff) = 1.
// Uses FastInvSqrt for optimal performance.
inline float ApplyPolicyDecay(float p, float n_child, float scale_per_move,
                               int num_legal_moves) {
  if (p == 0.0f || scale_per_move == 0.0f || num_legal_moves <= 0) return p;

  float effective_scale = scale_per_move * num_legal_moves;
  float base = 1.0f + n_child / effective_scale;

  // Sqrt decay: (1 + N/scale)^(-0.5) = 1/sqrt(1 + N/scale)
  float power_term = FastInvSqrt(base);

  float odds = 1.0f / p - 1.0f;
  return 1.0f / (1.0f + odds * power_term);
}

}  // namespace lczero
