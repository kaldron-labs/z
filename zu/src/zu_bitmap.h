//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZtBitmap - C API

#ifndef zu_bitmap_H
#define zu_bitmap_H

#ifndef zu_lib_H
#include <zlib/zu_lib.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* pragma pack() prevents compiler from relying on 16-byte alignment */
#pragma pack(push, 8)
typedef struct {
  uint64_t	length; // in words
  uint64_t	data[1];
} zu_bitmap;
#pragma pack(pop)

/* allocate/free zu_bitmap */
typedef void *(*zu_bitmap_alloc_fn)(unsigned size); // size in bytes
typedef void (*zu_bitmap_free_fn)(void *);

/* allocator */
typedef struct {
  zu_bitmap_alloc_fn	alloc;
  zu_bitmap_free_fn	free;
} zu_bitmap_allocator;

/* new, delete */
ZuExtern zu_bitmap *zu_bitmap_new_(const zu_bitmap_allocator *, unsigned n);
ZuExtern zu_bitmap *zu_bitmap_new(const zu_bitmap_allocator *, unsigned n);
ZuExtern zu_bitmap *zu_bitmap_new_fill(const zu_bitmap_allocator *, unsigned n);
ZuExtern void zu_bitmap_delete(const zu_bitmap_allocator *, zu_bitmap *v);

/* copy */
ZuExtern zu_bitmap *zu_bitmap_copy(
  const zu_bitmap_allocator *, const zu_bitmap *p);

/* resize */
ZuExtern zu_bitmap *zu_bitmap_resize(
  const zu_bitmap_allocator *, zu_bitmap *v, unsigned n);

/* get length (in bits) */
ZuExtern unsigned zu_bitmap_length(const zu_bitmap *v);

/* parse string, returns #bytes scanned, 0 on invalid input */
ZuExtern unsigned zu_bitmap_in(
  const zu_bitmap_allocator *, zu_bitmap **v, const char *s);
/* returns output length including null terminator */
ZuExtern unsigned zu_bitmap_out_len(const zu_bitmap *v);
/* output to string, returns end pointer to null terminator */
ZuExtern char *zu_bitmap_out(char *s, unsigned n, const zu_bitmap *v);

/* get underlying array */
ZuExtern unsigned zu_bitmap_get_wlength(const zu_bitmap *v);
ZuExtern uint64_t zu_bitmap_get_word(const zu_bitmap *v, unsigned i);

/* set underlying array */
ZuExtern void zu_bitmap_set_word(zu_bitmap *v, unsigned i, uint64_t w);

/* 3-way comparison */
ZuExtern int zu_bitmap_cmp(const zu_bitmap *l, const zu_bitmap *r);

/* hash */
ZuExtern uint32_t zu_bitmap_hash(const zu_bitmap *v);

/* basic single-bit functions (get, set, clr) */
ZuExtern bool zu_bitmap_get(const zu_bitmap *v, unsigned i);
ZuExtern zu_bitmap *zu_bitmap_set(
  const zu_bitmap_allocator *, zu_bitmap *v, unsigned i);
ZuExtern zu_bitmap *zu_bitmap_clr(zu_bitmap *v, unsigned i);

/* bit-range set/clear */
ZuExtern zu_bitmap *zu_bitmap_set_range(
  const zu_bitmap_allocator *, zu_bitmap *v, unsigned begin, unsigned end);
ZuExtern zu_bitmap *zu_bitmap_clr_range(
  zu_bitmap *v, unsigned begin, unsigned end);

/* first, last, next, prev */
ZuExtern int zu_bitmap_first(const zu_bitmap *v);
ZuExtern int zu_bitmap_last(const zu_bitmap *v);
ZuExtern int zu_bitmap_next(const zu_bitmap *v, int i);
ZuExtern int zu_bitmap_prev(const zu_bitmap *v, int i);

/* zero, fill, flip */
ZuExtern zu_bitmap *zu_bitmap_zero(zu_bitmap *v);
ZuExtern zu_bitmap *zu_bitmap_fill(zu_bitmap *v);
ZuExtern zu_bitmap *zu_bitmap_flip(zu_bitmap *v);

/* or, and, xor */
ZuExtern zu_bitmap *zu_bitmap_or(
  const zu_bitmap_allocator *, zu_bitmap *v, const zu_bitmap *p);
ZuExtern zu_bitmap *zu_bitmap_and(
  const zu_bitmap_allocator *, zu_bitmap *v, const zu_bitmap *p);
ZuExtern zu_bitmap *zu_bitmap_xor(
  const zu_bitmap_allocator *, zu_bitmap *v, const zu_bitmap *p);

#ifdef __cplusplus
}
#endif

#endif /* zu_bitmap_H */
