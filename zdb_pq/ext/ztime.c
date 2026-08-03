#include <postgres.h>
#include <fmgr.h>
#include <libpq/pqformat.h>
#include <utils/array.h>
#include <utils/sortsupport.h>
#include <port/pg_bswap.h>

#include <zlib/zu_time.h>

inline static bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

inline static bool isdigit__(char c) {
  return c >= '0' && c <= '9';
}

#define ztime_in_fn(fmt) \
  PG_FUNCTION_INFO_V1(ztime_in_##fmt); \
  Datum ztime_in_##fmt(PG_FUNCTION_ARGS) { \
	zu_time *v = (zu_time *)palloc(sizeof(zu_time)); \
	const char *s = PG_GETARG_CSTRING(0); \
	unsigned int n = zu_time_in_##fmt(v, s); \
 \
	if (likely(n)) while (unlikely(isspace__(s[n]))) ++n; \
	if (!n || s[n]) { \
	  ereport( \
		ERROR, \
		(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), \
		 errmsg("invalid input syntax for ztime: \"%s\"", s)) \
	  ); \
	} \
 \
	PG_RETURN_POINTER(v); \
  }

ztime_in_fn(csv)
ztime_in_fn(iso)
ztime_in_fn(fix)

#define ztime_out_fn(fmt) \
PG_FUNCTION_INFO_V1(ztime_out_##fmt); \
Datum ztime_out_##fmt(PG_FUNCTION_ARGS) { \
  const zu_time *v = (const zu_time *)PG_GETARG_POINTER(0); \
  unsigned int n = zu_time_out_##fmt##_len(v); \
  char *s = palloc(n); \
  zu_time_out_##fmt(s, n, v); \
  PG_RETURN_CSTRING(s); \
}

ztime_out_fn(csv)
ztime_out_fn(iso)
ztime_out_fn(fix)

PG_FUNCTION_INFO_V1(ztime_recv);
Datum ztime_recv(PG_FUNCTION_ARGS) {
  StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);
  zu_time *v = (zu_time *)palloc(sizeof(zu_time));
  pq_copymsgbytes(buf, (char *)v, 12);
  v->tv_sec = pg_bswap64(v->tv_sec);
  v->tv_nsec = pg_bswap32(v->tv_nsec);
  v->_ = 0;
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(ztime_send);
Datum ztime_send(PG_FUNCTION_ARGS) {
  const zu_time *v_ = (const zu_time *)PG_GETARG_POINTER(0);
  zu_time v = *v_;
  StringInfoData buf;
  pq_begintypsend(&buf);
  enlargeStringInfo(&buf, 12);
  Assert(buf.len + 12 <= buf.maxlen);
  v.tv_sec = pg_bswap64(v.tv_sec);
  v.tv_nsec = pg_bswap32(v.tv_nsec);
  memcpy((char *)(buf.data + buf.len), &v, 12);
  buf.len += 12;
  PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

PG_FUNCTION_INFO_V1(ztime_to_int8);
Datum ztime_to_int8(PG_FUNCTION_ARGS) {
  const zu_time *p = (const zu_time *)PG_GETARG_POINTER(0);
  zu_decimal d;
  int64_t i;
  if (zu_time_null(p)) PG_RETURN_NULL();
  zu_time_to_decimal(&d, p);
  i = zu_decimal_to_int(&d);
  PG_RETURN_INT64(i);
}

PG_FUNCTION_INFO_V1(ztime_from_int8);
Datum ztime_from_int8(PG_FUNCTION_ARGS) {
  int64_t i = PG_GETARG_INT64(0);
  zu_time *v = (zu_time *)palloc(sizeof(zu_time));
  zu_decimal d;
  zu_decimal_from_int(&d, i);
  PG_RETURN_POINTER(zu_time_from_decimal(v, &d));
}

PG_FUNCTION_INFO_V1(ztime_to_decimal);
Datum ztime_to_decimal(PG_FUNCTION_ARGS) {
  const zu_time *p = (const zu_time *)PG_GETARG_POINTER(0);
  zu_decimal *d = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_time_to_decimal(d, p));
}

PG_FUNCTION_INFO_V1(ztime_from_decimal);
Datum ztime_from_decimal(PG_FUNCTION_ARGS) {
  const zu_decimal *d = (const zu_decimal *)PG_GETARG_POINTER(0);
  zu_time *v = (zu_time *)palloc(sizeof(zu_time));
  PG_RETURN_POINTER(zu_time_from_decimal(v, d));
}

PG_FUNCTION_INFO_V1(ztime_add);
Datum ztime_add(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_time *v = (zu_time *)palloc(sizeof(zu_time));
  zu_time_add(v, l, r);
  if (!zu_time_null(l) && r->value != zu_decimal_null() && zu_time_null(v))
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(ztime_sub);
Datum ztime_sub(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_time *v = (zu_time *)palloc(sizeof(zu_time));
  zu_time_sub(v, l, r);
  if (!zu_time_null(l) && r->value != zu_decimal_null() && zu_time_null(v))
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(ztime_delta);
Datum ztime_delta(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  zu_time_delta(v, l, r);
  if (!zu_time_null(l) && !zu_time_null(r) && v->value == zu_decimal_null())
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(ztime_lt);
Datum ztime_lt(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_time_cmp(l, r) < 0);
}

PG_FUNCTION_INFO_V1(ztime_le);
Datum ztime_le(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_time_cmp(l, r) <= 0);
}

PG_FUNCTION_INFO_V1(ztime_eq);
Datum ztime_eq(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(!zu_time_cmp(l, r));
}

PG_FUNCTION_INFO_V1(ztime_ne);
Datum ztime_ne(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(!!zu_time_cmp(l, r));
}

PG_FUNCTION_INFO_V1(ztime_ge);
Datum ztime_ge(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_time_cmp(l, r) >= 0);
}

PG_FUNCTION_INFO_V1(ztime_gt);
Datum ztime_gt(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_time_cmp(l, r) > 0);
}

PG_FUNCTION_INFO_V1(ztime_cmp);
Datum ztime_cmp(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  PG_RETURN_INT32(zu_time_cmp(l, r));
}

static int ztime_sort_cmp(Datum l_, Datum r_, SortSupport _) {
  const zu_time *l = (const zu_time *)DatumGetPointer(l_);
  const zu_time *r = (const zu_time *)DatumGetPointer(r_);
  return zu_time_cmp(l, r);
}
PG_FUNCTION_INFO_V1(ztime_sort);
Datum ztime_sort(PG_FUNCTION_ARGS) {
  SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);
  ssup->comparator = ztime_sort_cmp; // not exposed in SQL
  PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(ztime_hash);
Datum ztime_hash(PG_FUNCTION_ARGS) {
  const zu_time *v = (const zu_time *)PG_GETARG_POINTER(0);
  PG_RETURN_INT32(zu_time_hash(v));
}

PG_FUNCTION_INFO_V1(ztime_smaller);
Datum ztime_smaller(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  int i = zu_time_cmp(l, r);
  PG_RETURN_POINTER(i < 0 ? l : r);
}

PG_FUNCTION_INFO_V1(ztime_larger);
Datum ztime_larger(PG_FUNCTION_ARGS) {
  const zu_time *l = (const zu_time *)PG_GETARG_POINTER(0);
  const zu_time *r = (const zu_time *)PG_GETARG_POINTER(1);
  int i = zu_time_cmp(l, r);
  PG_RETURN_POINTER(i > 0 ? l : r);
}
