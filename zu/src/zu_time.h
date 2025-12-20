//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZuTime - C API

#ifndef zu_time_H
#define zu_time_H

#ifndef zu_lib_H
#include <zlib/zu_lib.h>
#endif

#include <zlib/zu_decimal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* carbon copy of ZuTime */
typedef struct {
  int64_t	tv_sec;
  uint32_t	tv_nsec;
  uint32_t	_;	/* pad to 16 bytes */
} zu_time;

/* initialize new zu_time */
ZuExtern zu_time *zu_time_init(zu_time *v);

/* check if null */
ZuExtern bool zu_time_null(const zu_time *v);

/* parse string, returns #bytes scanned, 0 on invalid input */
ZuExtern unsigned zu_time_in_csv(zu_time *v, const char *s);
ZuExtern unsigned zu_time_in_iso(zu_time *v, const char *s);
ZuExtern unsigned zu_time_in_fix(zu_time *v, const char *s);
/* returns output length including null terminator */
ZuExtern unsigned zu_time_out_csv_len(const zu_time *v);
ZuExtern unsigned zu_time_out_iso_len(const zu_time *v);
ZuExtern unsigned zu_time_out_fix_len(const zu_time *v);
/* output to string, returns end pointer to null terminator */
ZuExtern char *zu_time_out_csv(char *s, unsigned n, const zu_time *v);
ZuExtern char *zu_time_out_iso(char *s, unsigned n, const zu_time *v);
ZuExtern char *zu_time_out_fix(char *s, unsigned n, const zu_time *v);

/* convert to/from zu_decimal */
ZuExtern zu_decimal *zu_time_to_decimal(zu_decimal *d, const zu_time *v);
ZuExtern zu_time *zu_time_from_decimal(zu_time *v, const zu_decimal *d);

/* 3-way comparison */
ZuExtern int zu_time_cmp(const zu_time *l, const zu_time *r);

/* hash */
ZuExtern uint32_t zu_time_hash(const zu_time *v);

/* add, subtract, delta */
ZuExtern zu_time *zu_time_add(
    zu_time *v, const zu_time *l, const zu_decimal *r);
ZuExtern zu_time *zu_time_sub(
    zu_time *v, const zu_time *l, const zu_decimal *r);
ZuExtern zu_decimal *zu_time_delta(
    zu_decimal *v, const zu_time *l, const zu_time *r);

#ifdef __cplusplus
}
#endif

#endif /* zu_time_H */
