//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// unused reference code - MSVC integer overflow intrinsics

#include <intrin.h>
#include <stdint.h>

#include <iostream>

#define CHECK(x) std::cout << #x << " = " << int(x) << '\n';

int main()
{
  int32_t i32;
  uint32_t u32;
  int64_t i64;
  uint64_t u64;

  CHECK(_add_overflow_i32(0, 2, 3, &i32));
  CHECK(_addcarry_u32(0, 2, 3, &u32));
  CHECK(_add_overflow_i32(0, ~0 & ~(1<<31), ~0 & ~(1<<31), &i32));
  CHECK(_addcarry_u32(0, ~0, ~0, &u32));

  CHECK(_add_overflow_i64(0, 2, 3, &i64));
  CHECK(_addcarry_u64(0, 2, 3, &u64));
  CHECK(_add_overflow_i64(
      0, ~0 & ~(uint64_t(1)<<63), ~0 & ~(uint64_t(1)<<63), &i64));
  CHECK(_addcarry_u64(0, ~0, ~0, &u64));

  CHECK(_sub_overflow_i32(0, -3, -2, &i32));
  CHECK(_subborrow_u32(0, 3, 2, &u32));
  CHECK(_sub_overflow_i32(0, (1<<31), 3, &i32));
  CHECK(_subborrow_u32(0, 2, 3, &u32));

  CHECK(_sub_overflow_i64(0, -3, -2, &i64));
  CHECK(_subborrow_u64(0, 3, 2, &u64));
  CHECK(_sub_overflow_i64(0, (uint64_t(1)<<63), 3, &i64));
  CHECK(_subborrow_u64(0, 2, 3, &u64));

  int32_t j32;
  uint32_t v32;
  int64_t j64;
  uint64_t v64;

  CHECK(_mul_full_overflow_i32(2, 3, &i32, &j32));
  CHECK(_mul_full_overflow_u32(2, 3, &u32, &v32));
  CHECK(_mul_full_overflow_i32(~0 & ~(1 << 31), ~0 & ~(1 << 31), &i32, &j32));
  CHECK(_mul_full_overflow_u32(~0, ~0, &u32, &v32));

  CHECK(_mul_full_overflow_i64(2, 3, &i64, &j64));
  CHECK(_mul_full_overflow_u64(2, 3, &u64, &v64));
  CHECK(_mul_full_overflow_i64(
      ~0 & ~(uint64_t(1) << 63), ~0 & ~(uint64_t(1) << 63), &i64, &j64));
  CHECK(_mul_full_overflow_u64(~0, ~0, &u64, &v64));
}
