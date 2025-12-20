#include <postgres.h>
#include <fmgr.h>
#include <libpq/pqformat.h>
#include <utils/array.h>
#include <utils/sortsupport.h>
#include <port/pg_bswap.h>

#include <zlib/zu_decimal.h>

#ifndef pg_bswap128
#ifdef WORDS_BIGENDIAN
#define pg_bswap128(x) (x)
#else
#if defined(__GNUC__) && !defined(__llvm__)
#define pg_bswap128(x) __builtin_bswap128(x)
#else
inline static uint128_t pg_bswap128(uint128_t i) {
  return
    ((uint128_t)(pg_bswap64(i))<<64) |
    (uint128_t)pg_bswap64(i>>64);
}
#endif /* __GNUC__ && !__llvm__ */
#endif /* WORDS_BIGENDIAN */
#endif /* pg_bswap128 */

inline static bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

PG_FUNCTION_INFO_V1(zdecimal_in);
Datum zdecimal_in(PG_FUNCTION_ARGS) {
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  const char *s = PG_GETARG_CSTRING(0);
  unsigned int n;

  /* postgres uses NaN; meanwhile ZuDecimal intentionally omits
   * positive/negative infinity */
  if (unlikely(s[0] == 'N' && s[1] == 'a' && s[2] == 'N' && s[3] == '\0')) {
    v->value = zu_decimal_null();
    PG_RETURN_POINTER(v);
  }

  /* postgres numeric supports inputs like 0x, 0o, 0b for hex/octal/binary,
   * this would be a mis-use of the type, intentionally omitted here */
  n = zu_decimal_in(v, s);

  /* SQL requires trailing spaces to be ignored while erroring out on other
   * "trailing junk"; together with postgres reliance on C string
   * null-termination, this prevents incrementally parsing values within
   * a containing string without copying the string or mutating it with
   * null terminators, but we'll play along, sigh */
  if (likely(n)) while (unlikely(isspace__(s[n]))) ++n;
  if (!n || s[n])
    ereport(
      ERROR,
      (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
       errmsg("invalid input syntax for zdecimal: \"%s\"", s))
    );

  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_out);
Datum zdecimal_out(PG_FUNCTION_ARGS) {
  const zu_decimal *v = (const zu_decimal *)PG_GETARG_POINTER(0);
  unsigned int n = zu_decimal_out_len(v);
  char *s = palloc(n);
  zu_decimal_out(s, n, v);
  /* postgres uses NaN - change nan to NaN */
  if (s[0] == 'n' && s[1] == 'a' && s[2] == 'n' && s[3] == '\0')
    s[0] = 'N', s[2] = 'N';
  PG_RETURN_CSTRING(s);
}

PG_FUNCTION_INFO_V1(zdecimal_recv);
Datum zdecimal_recv(PG_FUNCTION_ARGS) {
  StringInfo buf = (StringInfo)PG_GETARG_POINTER(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  pq_copymsgbytes(buf, (char *)v, 16);
  v->value = pg_bswap128(v->value);
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_send);
Datum zdecimal_send(PG_FUNCTION_ARGS) {
  const zu_decimal *v = (const zu_decimal *)PG_GETARG_POINTER(0);
  uint128_t value = v->value;
  StringInfoData buf;
  pq_begintypsend(&buf);
  enlargeStringInfo(&buf, 16);
  Assert(buf.len + 16 <= buf.maxlen);
  value = pg_bswap128(value);
  memcpy((char *)(buf.data + buf.len), &value, 16);
  buf.len += 16;
  PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

PG_FUNCTION_INFO_V1(zdecimal_to_int4);
Datum zdecimal_to_int4(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  if (p->value == zu_decimal_null()) PG_RETURN_NULL();
  PG_RETURN_INT32((int32)zu_decimal_to_int(p));
}

PG_FUNCTION_INFO_V1(zdecimal_from_int4);
Datum zdecimal_from_int4(PG_FUNCTION_ARGS) {
  int32 i = PG_GETARG_INT32(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_from_int(v, i));
}

PG_FUNCTION_INFO_V1(zdecimal_to_int8);
Datum zdecimal_to_int8(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  if (p->value == zu_decimal_null()) PG_RETURN_NULL();
  PG_RETURN_INT64(zu_decimal_to_int(p));
}

PG_FUNCTION_INFO_V1(zdecimal_from_int8);
Datum zdecimal_from_int8(PG_FUNCTION_ARGS) {
  int64 i = PG_GETARG_INT64(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_from_int(v, i));
}

PG_FUNCTION_INFO_V1(zdecimal_to_float4);
Datum zdecimal_to_float4(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  PG_RETURN_FLOAT4((float4)zu_decimal_to_double(p));
}

PG_FUNCTION_INFO_V1(zdecimal_from_float4);
Datum zdecimal_from_float4(PG_FUNCTION_ARGS) {
  float4 f = PG_GETARG_FLOAT4(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_from_double(v, (double)f));
}

PG_FUNCTION_INFO_V1(zdecimal_to_float8);
Datum zdecimal_to_float8(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  PG_RETURN_FLOAT8(zu_decimal_to_double(p));
}

PG_FUNCTION_INFO_V1(zdecimal_from_float8);
Datum zdecimal_from_float8(PG_FUNCTION_ARGS) {
  float8 d = PG_GETARG_FLOAT8(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_from_double(v, d));
}

PG_FUNCTION_INFO_V1(zdecimal_round);
Datum zdecimal_round(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  PG_RETURN_INT64(zu_decimal_round(p));
}

PG_FUNCTION_INFO_V1(zdecimal_neg);
Datum zdecimal_neg(PG_FUNCTION_ARGS) {
  const zu_decimal *p = (const zu_decimal *)PG_GETARG_POINTER(0);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_neg(v, p));
}

PG_FUNCTION_INFO_V1(zdecimal_add);
Datum zdecimal_add(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  zu_decimal_add(v, l, r);
  if (l->value != zu_decimal_null() &&
      r->value != zu_decimal_null() &&
      v->value == zu_decimal_null())
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_sub);
Datum zdecimal_sub(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  zu_decimal_sub(v, l, r);
  if (l->value != zu_decimal_null() &&
      r->value != zu_decimal_null() &&
      v->value == zu_decimal_null())
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_mul);
Datum zdecimal_mul(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  zu_decimal_mul(v, l, r);
  if (l->value != zu_decimal_null() &&
      r->value != zu_decimal_null() &&
      v->value == zu_decimal_null())
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_div);
Datum zdecimal_div(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
  zu_decimal_div(v, l, r);
  if (l->value != zu_decimal_null() &&
      r->value != zu_decimal_null() &&
      v->value == zu_decimal_null())
    ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
	  errmsg("value out of range: overflow")));
  PG_RETURN_POINTER(v);
}

PG_FUNCTION_INFO_V1(zdecimal_lt);
Datum zdecimal_lt(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_decimal_cmp(l, r) < 0);
}

PG_FUNCTION_INFO_V1(zdecimal_le);
Datum zdecimal_le(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_decimal_cmp(l, r) <= 0);
}

PG_FUNCTION_INFO_V1(zdecimal_eq);
Datum zdecimal_eq(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(!zu_decimal_cmp(l, r));
}

PG_FUNCTION_INFO_V1(zdecimal_ne);
Datum zdecimal_ne(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(!!zu_decimal_cmp(l, r));
}

PG_FUNCTION_INFO_V1(zdecimal_ge);
Datum zdecimal_ge(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_decimal_cmp(l, r) >= 0);
}

PG_FUNCTION_INFO_V1(zdecimal_gt);
Datum zdecimal_gt(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_BOOL(zu_decimal_cmp(l, r) > 0);
}

PG_FUNCTION_INFO_V1(zdecimal_cmp);
Datum zdecimal_cmp(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  PG_RETURN_INT32(zu_decimal_cmp(l, r));
}

static int zdecimal_sort_cmp(Datum l_, Datum r_, SortSupport _) {
  const zu_decimal *l = (const zu_decimal *)DatumGetPointer(l_);
  const zu_decimal *r = (const zu_decimal *)DatumGetPointer(r_);
  return zu_decimal_cmp(l, r);
}
PG_FUNCTION_INFO_V1(zdecimal_sort);
Datum zdecimal_sort(PG_FUNCTION_ARGS) {
  SortSupport ssup = (SortSupport)PG_GETARG_POINTER(0);
  ssup->comparator = zdecimal_sort_cmp; // not exposed in SQL
  PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(zdecimal_hash);
Datum zdecimal_hash(PG_FUNCTION_ARGS) {
  const zu_decimal *v = (const zu_decimal *)PG_GETARG_POINTER(0);
  PG_RETURN_INT32(zu_decimal_hash(v));
}

PG_FUNCTION_INFO_V1(zdecimal_smaller);
Datum zdecimal_smaller(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  int i = zu_decimal_cmp(l, r);
  PG_RETURN_POINTER(i < 0 ? l : r);
}

PG_FUNCTION_INFO_V1(zdecimal_larger);
Datum zdecimal_larger(PG_FUNCTION_ARGS) {
  const zu_decimal *l = (const zu_decimal *)PG_GETARG_POINTER(0);
  const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
  int i = zu_decimal_cmp(l, r);
  PG_RETURN_POINTER(i > 0 ? l : r);
}

PG_FUNCTION_INFO_V1(zdecimal_sum);
Datum zdecimal_sum(PG_FUNCTION_ARGS) {
  if (unlikely(PG_ARGISNULL(0))) {
    if (unlikely(PG_ARGISNULL(1))) PG_RETURN_NULL();
    PG_RETURN_POINTER(PG_GETARG_POINTER(1));
  }
  if (unlikely(PG_ARGISNULL(1))) PG_RETURN_POINTER(PG_GETARG_POINTER(0));
  {
    zu_decimal *l = (zu_decimal *)PG_GETARG_POINTER(0);
    const zu_decimal *r = (const zu_decimal *)PG_GETARG_POINTER(1);
	if (AggCheckCallContext(fcinfo, NULL)) {
	  PG_RETURN_POINTER(zu_decimal_add(l, l, r));
	} else {
	  zu_decimal *v = (zu_decimal *)palloc(sizeof(zu_decimal));
	  PG_RETURN_POINTER(zu_decimal_add(v, l, r));
	}
  }
}

PG_FUNCTION_INFO_V1(zdecimal_acc);
Datum zdecimal_acc(PG_FUNCTION_ARGS) {
  ArrayType *array = AggCheckCallContext(fcinfo, NULL) ?
    PG_GETARG_ARRAYTYPE_P(0) :
    PG_GETARG_ARRAYTYPE_P_COPY(0);
  zu_decimal *state;
  const zu_decimal *v;

  if (ARR_NDIM(array) != 1 ||
      ARR_DIMS(array)[0] != 2 ||
      ARR_HASNULL(array) ||
      ARR_SIZE(array) != ARR_OVERHEAD_NONULLS(1) + sizeof(zu_decimal) * 2) {
    elog(ERROR, "zdecimal_acc expected 2-element zdecimal array");
    PG_RETURN_ARRAYTYPE_P(array);
  }

  if (PG_ARGISNULL(1)) PG_RETURN_ARRAYTYPE_P(array);

  state = (zu_decimal *)ARR_DATA_PTR(array);
  v = (const zu_decimal *)PG_GETARG_POINTER(1);

  zu_decimal_add(&state[0], &state[0], v);
  state[1].value += zu_decimal_scale();

  PG_RETURN_ARRAYTYPE_P(array);
}

PG_FUNCTION_INFO_V1(zdecimal_avg);
Datum zdecimal_avg(PG_FUNCTION_ARGS) {
  ArrayType *array = AggCheckCallContext(fcinfo, NULL) ?
    PG_GETARG_ARRAYTYPE_P(0) :
    PG_GETARG_ARRAYTYPE_P_COPY(0);
  const zu_decimal *state;
  zu_decimal *v;

  if (ARR_NDIM(array) != 1 ||
      ARR_DIMS(array)[0] != 2 ||
      ARR_HASNULL(array) ||
      ARR_SIZE(array) != ARR_OVERHEAD_NONULLS(1) + sizeof(zu_decimal) * 2) {
    elog(ERROR, "zdecimal_avg expected 2-element zdecimal array");
    PG_RETURN_NULL();
  }

  state = (const zu_decimal *)ARR_DATA_PTR(array);

  if (unlikely(!state[1].value)) PG_RETURN_NULL();

  v = (zu_decimal *)palloc(sizeof(zu_decimal));
  PG_RETURN_POINTER(zu_decimal_div(v, &state[0], &state[1]));
}
