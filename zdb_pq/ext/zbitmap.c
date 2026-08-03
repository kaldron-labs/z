#include <stdint.h>

#include <postgres.h>
#include <fmgr.h>
#include <varatt.h>
#include <libpq/pqformat.h>
#include <utils/sortsupport.h>

#include <zlib/zu_bitmap.h>

#define ZBITMAP_TOASTABLE	// zbitmap is TOAST-enabled
#define ZBITMAP_MAX_LEN	8192	// maximum #words in a bitmap

inline static bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

/* reverse of VARDATA */
#define DATAVAR(ptr) (((uint8_t *)ptr) - VARHDRSZ)

#ifdef ZBITMAP_TOASTABLE
#define PG_GETARG_CZBITMAP_P(n) \
  (const zu_bitmap *)VARDATA(PG_DETOAST_DATUM(PG_GETARG_DATUM(n)))
#define PG_GETARG_ZBITMAP_P(n) \
  (zu_bitmap *)VARDATA(PG_DETOAST_DATUM(PG_GETARG_DATUM(n)))
#else
#define PG_GETARG_CZBITMAP_P(n) \
  (const zu_bitmap *)VARDATA(PG_GETARG_POINTER(n))
#define PG_GETARG_ZBITMAP_P(n) \
  (zu_bitmap *)VARDATA(PG_GETARG_POINTER(n))
#endif

static void *zbitmap_alloc(unsigned size) {
  void *ptr;
  size += VARHDRSZ;
  ptr = palloc(size);
  if (!ptr) return 0;
  SET_VARSIZE(ptr, size);
  return VARDATA(ptr);
}

static void zbitmap_free(void *ptr) {
  pfree(DATAVAR(ptr));
}

static zu_bitmap_allocator allocator = {
  .alloc = zbitmap_alloc,
  .free = zbitmap_free
};

PG_FUNCTION_INFO_V1(zbitmap_in);
Datum zbitmap_in(PG_FUNCTION_ARGS) {
  zu_bitmap *v;
  const char *s = PG_GETARG_CSTRING(0);
  unsigned int n;

  n = zu_bitmap_in(&allocator, &v, s);

  /* SQL requires trailing spaces to be ignored while erroring out on other
   * "trailing junk"; together with postgres reliance on C string
   * null-termination, this prevents incrementally parsing values within
   * a containing string without copying the string or mutating it with
   * null terminators, but we'll play along, sigh */
  if (likely(n)) while (unlikely(isspace__(s[n]))) ++n;
  if (!v || s[n])
    ereport(
      ERROR,
      (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
       errmsg("invalid input syntax for zbitmap: \"%s\"", s))
    );

  PG_RETURN_POINTER(DATAVAR(v));
}

PG_FUNCTION_INFO_V1(zbitmap_out);
Datum zbitmap_out(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  unsigned int n = zu_bitmap_out_len(v);
  char *s = palloc(n);
  zu_bitmap_out(s, n, v);
  PG_RETURN_CSTRING(s);
}

PG_FUNCTION_INFO_V1(zbitmap_recv);
Datum zbitmap_recv(PG_FUNCTION_ARGS) {
  StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);
  unsigned i, n = (unsigned)pq_getmsgint64(buf);
  zu_bitmap *v;
  if (n >= ZBITMAP_MAX_LEN)
    ereport(ERROR,
      (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
       errmsg("bitmap length is too large"),
       errdetail("A bitmap cannot be longer than %d 64bit words.",
	 ZBITMAP_MAX_LEN)));
  v = zu_bitmap_new_(&allocator, n);
  for (i = 0; i < n; i++)
    zu_bitmap_set_word(v, i, pq_getmsgint64(buf));
  PG_RETURN_POINTER(DATAVAR(v));
}

PG_FUNCTION_INFO_V1(zbitmap_send);
Datum zbitmap_send(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  unsigned i, n = zu_bitmap_get_wlength(v);
  unsigned size = (n + 1) * sizeof(uint64_t);
  StringInfoData buf;
  pq_begintypsend(&buf);
  enlargeStringInfo(&buf, size);
  Assert(buf.len + size <= buf.maxlen);
  pq_sendint64(&buf, n);
  for (i = 0; i < n; i++)
    pq_sendint64(&buf, zu_bitmap_get_word(v, i));
  PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

PG_FUNCTION_INFO_V1(zbitmap_length);
Datum zbitmap_length(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  PG_RETURN_INT32(zu_bitmap_length(v));
}

PG_FUNCTION_INFO_V1(zbitmap_get);
Datum zbitmap_get(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  unsigned i = PG_GETARG_INT32(1);
  PG_RETURN_BOOL(zu_bitmap_get(v, i));
}

PG_FUNCTION_INFO_V1(zbitmap_set);
Datum zbitmap_set(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  unsigned i = PG_GETARG_INT32(1);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_set(&allocator, v, i)));
}

PG_FUNCTION_INFO_V1(zbitmap_clr);
Datum zbitmap_clr(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  unsigned i = PG_GETARG_INT32(1);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_clr(v, i)));
}

PG_FUNCTION_INFO_V1(zbitmap_set_range);
Datum zbitmap_set_range(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  unsigned begin = PG_GETARG_INT32(1);
  unsigned end = PG_GETARG_INT32(2);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_set_range(&allocator, v, begin, end)));
}

PG_FUNCTION_INFO_V1(zbitmap_clr_range);
Datum zbitmap_clr_range(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  unsigned begin = PG_GETARG_INT32(1);
  unsigned end = PG_GETARG_INT32(2);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_clr_range(v, begin, end)));
}

PG_FUNCTION_INFO_V1(zbitmap_first);
Datum zbitmap_first(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  PG_RETURN_INT32(zu_bitmap_first(v));
}

PG_FUNCTION_INFO_V1(zbitmap_last);
Datum zbitmap_last(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  PG_RETURN_INT32(zu_bitmap_last(v));
}

PG_FUNCTION_INFO_V1(zbitmap_next);
Datum zbitmap_next(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  int i = PG_GETARG_INT32(1);
  PG_RETURN_INT32(zu_bitmap_next(v, i));
}

PG_FUNCTION_INFO_V1(zbitmap_prev);
Datum zbitmap_prev(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  int i = PG_GETARG_INT32(1);
  PG_RETURN_INT32(zu_bitmap_prev(v, i));
}

PG_FUNCTION_INFO_V1(zbitmap_flip);
Datum zbitmap_flip(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_flip(v)));
}

PG_FUNCTION_INFO_V1(zbitmap_or);
Datum zbitmap_or(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  const zu_bitmap *p = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_or(&allocator, v, p)));
}

PG_FUNCTION_INFO_V1(zbitmap_and);
Datum zbitmap_and(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  const zu_bitmap *p = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_and(&allocator, v, p)));
}

PG_FUNCTION_INFO_V1(zbitmap_xor);
Datum zbitmap_xor(PG_FUNCTION_ARGS) {
  zu_bitmap *v = PG_GETARG_ZBITMAP_P(0);
  const zu_bitmap *p = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_POINTER(DATAVAR(zu_bitmap_xor(&allocator, v, p)));
}

PG_FUNCTION_INFO_V1(zbitmap_lt);
Datum zbitmap_lt(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(zu_bitmap_cmp(l, r) < 0);
}

PG_FUNCTION_INFO_V1(zbitmap_le);
Datum zbitmap_le(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(zu_bitmap_cmp(l, r) <= 0);
}

PG_FUNCTION_INFO_V1(zbitmap_eq);
Datum zbitmap_eq(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(!zu_bitmap_cmp(l, r));
}

PG_FUNCTION_INFO_V1(zbitmap_ne);
Datum zbitmap_ne(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(!!zu_bitmap_cmp(l, r));
}

PG_FUNCTION_INFO_V1(zbitmap_ge);
Datum zbitmap_ge(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(zu_bitmap_cmp(l, r) >= 0);
}

PG_FUNCTION_INFO_V1(zbitmap_gt);
Datum zbitmap_gt(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_BOOL(zu_bitmap_cmp(l, r) > 0);
}

PG_FUNCTION_INFO_V1(zbitmap_cmp);
Datum zbitmap_cmp(PG_FUNCTION_ARGS) {
  const zu_bitmap *l = PG_GETARG_CZBITMAP_P(0);
  const zu_bitmap *r = PG_GETARG_CZBITMAP_P(1);
  PG_RETURN_INT32(zu_bitmap_cmp(l, r));
}

static int zbitmap_sort_cmp(Datum l_, Datum r_, SortSupport _) {
#ifdef ZBITMAP_TOASTABLE
  const zu_bitmap *l = (const zu_bitmap *)VARDATA(PG_DETOAST_DATUM(l_));
  const zu_bitmap *r = (const zu_bitmap *)VARDATA(PG_DETOAST_DATUM(r_));
#else
  const zu_bitmap *l = (const zu_bitmap *)VARDATA(DatumGetPointer(l_));
  const zu_bitmap *r = (const zu_bitmap *)VARDATA(DatumGetPointer(r_));
#endif
  return zu_bitmap_cmp(l, r);
}
PG_FUNCTION_INFO_V1(zbitmap_sort);
Datum zbitmap_sort(PG_FUNCTION_ARGS) {
  SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);
  ssup->comparator = zbitmap_sort_cmp; // not exposed in SQL
  PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(zbitmap_hash);
Datum zbitmap_hash(PG_FUNCTION_ARGS) {
  const zu_bitmap *v = PG_GETARG_CZBITMAP_P(0);
  PG_RETURN_INT32(zu_bitmap_hash(v));
}
