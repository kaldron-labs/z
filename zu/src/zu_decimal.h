//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZuDecimal - C API

#ifndef zu_decimal_H
#define zu_decimal_H

#ifndef zu_lib_H
#include <zlib/zu_lib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef __int128_t int128_t;
typedef __uint128_t uint128_t;

#define zu_decimal_scale() ((int128_t)1000000000000000000ULL)
#define zu_decimal_null() (((int128_t)1)<<127)

/* prevent compiler from relying on 16-byte alignment */
#pragma pack(push, 8)
typedef struct {
  int128_t	value;
} zu_decimal;
#pragma pack(pop)

/* parse string, returns #bytes scanned, -1 on invalid input */
ZuExtern int zu_decimal_in(zu_decimal *v, const char *s);
/* returns output length including null terminator */
ZuExtern unsigned zu_decimal_out_len(const zu_decimal *v);
/* output to string, returns end pointer to null terminator */
ZuExtern char *zu_decimal_out(char *s, unsigned n, const zu_decimal *v);

/* convert to/from integer */
ZuExtern int64_t zu_decimal_to_int(const zu_decimal *v); /* truncates */
ZuExtern zu_decimal *zu_decimal_from_int(zu_decimal *v, int64_t i);
ZuExtern int64_t zu_decimal_round(const zu_decimal *v); /* rounds */

/* convert to/from double */
ZuExtern double zu_decimal_to_double(const zu_decimal *v);
ZuExtern zu_decimal *zu_decimal_from_double(zu_decimal *v, double d);

/* 3-way comparison */
ZuExtern int zu_decimal_cmp(const zu_decimal *l, const zu_decimal *r);

/* hash */
ZuExtern uint32_t zu_decimal_hash(const zu_decimal *v);

/* negate */
ZuExtern zu_decimal *zu_decimal_neg(zu_decimal *v, const zu_decimal *p);

/* add, subtract, multiply, divide */
ZuExtern zu_decimal *zu_decimal_add(
    zu_decimal *v, const zu_decimal *l, const zu_decimal *r);
ZuExtern zu_decimal *zu_decimal_sub(
    zu_decimal *v, const zu_decimal *l, const zu_decimal *r);
ZuExtern zu_decimal *zu_decimal_mul(
    zu_decimal *v, const zu_decimal *l, const zu_decimal *r);
ZuExtern zu_decimal *zu_decimal_div(
    zu_decimal *v, const zu_decimal *l, const zu_decimal *r);

#ifdef __cplusplus
}
#endif

#endif /* zu_decimal_H */
