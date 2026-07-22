CREATE TYPE zdecimal;

CREATE FUNCTION zdecimal_in(cstring) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_in';

CREATE FUNCTION zdecimal_out(zdecimal) RETURNS cstring
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_out';

CREATE FUNCTION zdecimal_recv(internal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_recv';

CREATE FUNCTION zdecimal_send(zdecimal) RETURNS bytea
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_send';

CREATE TYPE zdecimal (
  INPUT = zdecimal_in,
  OUTPUT = zdecimal_out,
  RECEIVE = zdecimal_recv,
  SEND = zdecimal_send,
  INTERNALLENGTH = 16,
  ALIGNMENT = double
);

CREATE FUNCTION zdecimal_to_int4(zdecimal) RETURNS integer
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_to_int4';

CREATE FUNCTION zdecimal_from_int4(integer) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_from_int4';

CREATE FUNCTION zdecimal_to_int8(zdecimal) RETURNS int8
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_to_int8';

CREATE FUNCTION zdecimal_from_int8(int8) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_from_int8';

CREATE FUNCTION zdecimal_to_float4(zdecimal) RETURNS real
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_to_float4';

CREATE FUNCTION zdecimal_from_float4(real) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_from_float4';

CREATE FUNCTION zdecimal_to_float8(zdecimal) RETURNS double precision
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_to_float8';

CREATE FUNCTION zdecimal_from_float8(double precision) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_from_float8';

CREATE CAST (integer AS zdecimal)
  WITH FUNCTION zdecimal_from_int4 AS ASSIGNMENT;
CREATE CAST (int8 AS zdecimal)
  WITH FUNCTION zdecimal_from_int8 AS ASSIGNMENT;
CREATE CAST (real AS zdecimal)
  WITH FUNCTION zdecimal_from_float4 AS ASSIGNMENT;
CREATE CAST (double precision AS zdecimal)
  WITH FUNCTION zdecimal_from_float8 AS ASSIGNMENT;
CREATE CAST (numeric AS zdecimal) WITH INOUT AS ASSIGNMENT;

CREATE CAST (zdecimal AS integer)
  WITH FUNCTION zdecimal_to_int4 AS IMPLICIT;
CREATE CAST (zdecimal AS int8)
  WITH FUNCTION zdecimal_to_int8 AS IMPLICIT;
CREATE CAST (zdecimal AS real)
  WITH FUNCTION zdecimal_to_float4 AS IMPLICIT;
CREATE CAST (zdecimal AS double precision)
  WITH FUNCTION zdecimal_to_float8 AS IMPLICIT;
CREATE CAST (zdecimal AS numeric) WITH INOUT AS IMPLICIT;

CREATE FUNCTION zdecimal_neg(zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_neg';

CREATE OPERATOR - (
  PROCEDURE = zdecimal_neg,
  RIGHTARG = zdecimal
);

CREATE FUNCTION zdecimal_add(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_add';

CREATE OPERATOR + (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = +,
  PROCEDURE = zdecimal_add
);

CREATE FUNCTION zdecimal_sub(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_sub';

CREATE OPERATOR - (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  PROCEDURE = zdecimal_sub
);

CREATE FUNCTION zdecimal_mul(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_mul';

CREATE OPERATOR * (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = *,
  PROCEDURE = zdecimal_mul
);

CREATE FUNCTION zdecimal_div(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_div';

CREATE OPERATOR / (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  PROCEDURE = zdecimal_div
);

CREATE FUNCTION zdecimal_lt(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_lt';

CREATE OPERATOR < (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = zdecimal_lt
);

CREATE FUNCTION zdecimal_le(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_le';

CREATE OPERATOR <= (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = zdecimal_le
);

CREATE FUNCTION zdecimal_eq(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_eq';

CREATE OPERATOR = (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel,
  HASHES,
  MERGES,
  PROCEDURE = zdecimal_eq
);

CREATE FUNCTION zdecimal_ne(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_ne';

CREATE OPERATOR <> (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel,
  PROCEDURE = zdecimal_ne
);

CREATE FUNCTION zdecimal_ge(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_ge';

CREATE OPERATOR >= (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = zdecimal_ge
);

CREATE FUNCTION zdecimal_gt(zdecimal, zdecimal) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_gt';

CREATE OPERATOR > (
  LEFTARG = zdecimal,
  RIGHTARG = zdecimal,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = zdecimal_gt
);

CREATE FUNCTION zdecimal_cmp(zdecimal, zdecimal) RETURNS integer
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_cmp';

CREATE FUNCTION zdecimal_sort(internal) RETURNS void
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_sort';

CREATE OPERATOR CLASS zdecimal_ops
  DEFAULT FOR TYPE zdecimal USING btree AS
    OPERATOR	1	< ,
    OPERATOR	2	<= ,
    OPERATOR	3	= ,
    OPERATOR	4	>= ,
    OPERATOR	5	> ,
    FUNCTION	1	zdecimal_cmp(zdecimal, zdecimal),
    FUNCTION	2	zdecimal_sort(internal);

CREATE FUNCTION zdecimal_hash(zdecimal) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_hash';

CREATE OPERATOR CLASS zdecimal_ops
  DEFAULT FOR TYPE zdecimal USING hash AS
    OPERATOR	1	=,
    FUNCTION	1	zdecimal_hash(zdecimal);

CREATE FUNCTION zdecimal_smaller(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_smaller';

CREATE AGGREGATE min(zdecimal) (
  SFUNC = zdecimal_smaller,
  STYPE = zdecimal,
  SORTOP = <
);

CREATE FUNCTION zdecimal_larger(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_larger';

CREATE AGGREGATE max(zdecimal) (
  SFUNC = zdecimal_larger,
  STYPE = zdecimal,
  SORTOP = >
);

CREATE FUNCTION zdecimal_sum(zdecimal, zdecimal) RETURNS zdecimal
  IMMUTABLE LANGUAGE C
  AS '$libdir/libz', 'zdecimal_sum';

CREATE AGGREGATE sum(zdecimal) (SFUNC = zdecimal_sum, STYPE = zdecimal);

CREATE FUNCTION zdecimal_acc(zdecimal[], zdecimal) RETURNS zdecimal[]
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_acc';

CREATE FUNCTION zdecimal_avg(zdecimal[]) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zdecimal_avg';

CREATE AGGREGATE avg(zdecimal) (
  SFUNC = zdecimal_acc,
  STYPE = zdecimal[],
  FINALFUNC = zdecimal_avg,
  INITCOND = '{0,0}'
);
CREATE TYPE ztime;

CREATE FUNCTION ztime_in_csv(cstring) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_in_csv';

CREATE FUNCTION ztime_out_csv(ztime) RETURNS cstring
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_out_csv';

CREATE FUNCTION ztime_in_iso(cstring) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_in_iso';

CREATE FUNCTION ztime_out_iso(ztime) RETURNS cstring
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_out_iso';

CREATE FUNCTION ztime_in_fix(cstring) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_in_fix';

CREATE FUNCTION ztime_out_fix(ztime) RETURNS cstring
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_out_fix';

CREATE FUNCTION ztime_recv(internal) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_recv';

CREATE FUNCTION ztime_send(ztime) RETURNS bytea
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_send';

CREATE TYPE ztime (
  INPUT = ztime_in_csv,
  OUTPUT = ztime_out_csv,
  RECEIVE = ztime_recv,
  SEND = ztime_send,
  INTERNALLENGTH = 16,
  ALIGNMENT = double
);

CREATE FUNCTION ztime_to_int8(ztime) RETURNS int8
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_to_int8';

CREATE FUNCTION ztime_from_int8(int8) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_from_int8';

CREATE FUNCTION ztime_to_decimal(ztime) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_to_decimal';

CREATE FUNCTION ztime_from_decimal(zdecimal) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_from_decimal';

CREATE CAST (int8 AS ztime)
  WITH FUNCTION ztime_from_int8 AS ASSIGNMENT;
CREATE CAST (zdecimal AS ztime)
  WITH FUNCTION ztime_from_decimal AS ASSIGNMENT;

CREATE CAST (ztime AS int8)
  WITH FUNCTION ztime_to_int8 AS IMPLICIT;
CREATE CAST (ztime AS zdecimal)
  WITH FUNCTION ztime_to_decimal AS IMPLICIT;

CREATE FUNCTION ztime_add(ztime, zdecimal) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_add';

CREATE OPERATOR + (
  LEFTARG = ztime,
  RIGHTARG = zdecimal,
  COMMUTATOR = +,
  PROCEDURE = ztime_add
);

CREATE FUNCTION ztime_sub(ztime, zdecimal) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_sub';

CREATE OPERATOR - (
  LEFTARG = ztime,
  RIGHTARG = zdecimal,
  PROCEDURE = ztime_sub
);

CREATE FUNCTION ztime_delta(ztime, ztime) RETURNS zdecimal
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_delta';

CREATE OPERATOR - (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  PROCEDURE = ztime_delta
);

CREATE FUNCTION ztime_lt(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_lt';

CREATE OPERATOR < (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = ztime_lt
);

CREATE FUNCTION ztime_le(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_le';

CREATE OPERATOR <= (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = ztime_le
);

CREATE FUNCTION ztime_eq(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_eq';

CREATE OPERATOR = (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel,
  HASHES,
  MERGES,
  PROCEDURE = ztime_eq
);

CREATE FUNCTION ztime_ne(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_ne';

CREATE OPERATOR <> (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel,
  PROCEDURE = ztime_ne
);

CREATE FUNCTION ztime_ge(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_ge';

CREATE OPERATOR >= (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = ztime_ge
);

CREATE FUNCTION ztime_gt(ztime, ztime) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_gt';

CREATE OPERATOR > (
  LEFTARG = ztime,
  RIGHTARG = ztime,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = ztime_gt
);

CREATE FUNCTION ztime_cmp(ztime, ztime) RETURNS integer
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_cmp';

CREATE FUNCTION ztime_sort(internal) RETURNS void
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_sort';

CREATE OPERATOR CLASS ztime_ops
  DEFAULT FOR TYPE ztime USING btree AS
    OPERATOR	1	< ,
    OPERATOR	2	<= ,
    OPERATOR	3	= ,
    OPERATOR	4	>= ,
    OPERATOR	5	> ,
    FUNCTION	1	ztime_cmp(ztime, ztime),
    FUNCTION	2	ztime_sort(internal);

CREATE FUNCTION ztime_hash(ztime) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_hash';

CREATE OPERATOR CLASS ztime_ops
  DEFAULT FOR TYPE ztime USING hash AS
    OPERATOR	1	=,
    FUNCTION	1	ztime_hash(ztime);

CREATE FUNCTION ztime_smaller(ztime, ztime) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_smaller';

CREATE AGGREGATE min(ztime) (
  SFUNC = ztime_smaller,
  STYPE = ztime,
  SORTOP = <
);

CREATE FUNCTION ztime_larger(ztime, ztime) RETURNS ztime
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'ztime_larger';

CREATE AGGREGATE max(ztime) (
  SFUNC = ztime_larger,
  STYPE = ztime,
  SORTOP = >
);
CREATE TYPE zbitmap;

CREATE FUNCTION zbitmap_in(cstring) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_in';

CREATE FUNCTION zbitmap_out(zbitmap) RETURNS cstring
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_out';

CREATE FUNCTION zbitmap_recv(internal) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_recv';

CREATE FUNCTION zbitmap_send(zbitmap) RETURNS bytea
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_send';

CREATE TYPE zbitmap (
  INPUT = zbitmap_in,
  OUTPUT = zbitmap_out,
  RECEIVE = zbitmap_recv,
  SEND = zbitmap_send,
  INTERNALLENGTH = VARIABLE,
  ALIGNMENT = double,
  STORAGE = external
);

CREATE FUNCTION zbitmap_length(zbitmap) RETURNS uint4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_length';

CREATE FUNCTION zbitmap_get(zbitmap, uint4) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_get';

CREATE FUNCTION zbitmap_set(INOUT zbitmap, uint4) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_set';

CREATE FUNCTION zbitmap_clr(INOUT zbitmap, uint4) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_clr';

CREATE FUNCTION zbitmap_set_range(INOUT zbitmap, uint4, uint4) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_set_range';

CREATE FUNCTION zbitmap_clr_range(INOUT zbitmap, uint4, uint4) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_clr_range';

CREATE FUNCTION zbitmap_first(zbitmap) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_first';

CREATE FUNCTION zbitmap_last(zbitmap) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_last';

CREATE FUNCTION zbitmap_next(zbitmap, int4) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_next';

CREATE FUNCTION zbitmap_prev(zbitmap, int4) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_prev';

CREATE FUNCTION zbitmap_flip(INOUT zbitmap) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_flip';

CREATE FUNCTION zbitmap_or(INOUT zbitmap, zbitmap) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_or';

CREATE FUNCTION zbitmap_and(INOUT zbitmap, zbitmap) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_and';

CREATE FUNCTION zbitmap_xor(INOUT zbitmap, zbitmap) RETURNS zbitmap
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_xor';

CREATE FUNCTION zbitmap_lt(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_lt';

CREATE OPERATOR < (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = >,
  NEGATOR = >=,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = zbitmap_lt
);

CREATE FUNCTION zbitmap_le(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_le';

CREATE OPERATOR <= (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = >=,
  NEGATOR = >,
  RESTRICT = scalarltsel,
  JOIN = scalarltjoinsel,
  PROCEDURE = zbitmap_le
);

CREATE FUNCTION zbitmap_eq(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_eq';

CREATE OPERATOR = (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = =,
  NEGATOR = <>,
  RESTRICT = eqsel,
  JOIN = eqjoinsel,
  HASHES,
  MERGES,
  PROCEDURE = zbitmap_eq
);

CREATE FUNCTION zbitmap_ne(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_ne';

CREATE OPERATOR <> (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = <>,
  NEGATOR = =,
  RESTRICT = neqsel,
  JOIN = neqjoinsel,
  PROCEDURE = zbitmap_ne
);

CREATE FUNCTION zbitmap_ge(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_ge';

CREATE OPERATOR >= (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = <=,
  NEGATOR = <,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = zbitmap_ge
);

CREATE FUNCTION zbitmap_gt(zbitmap, zbitmap) RETURNS boolean
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_gt';

CREATE OPERATOR > (
  LEFTARG = zbitmap,
  RIGHTARG = zbitmap,
  COMMUTATOR = <,
  NEGATOR = <=,
  RESTRICT = scalargtsel,
  JOIN = scalargtjoinsel,
  PROCEDURE = zbitmap_gt
);

CREATE FUNCTION zbitmap_cmp(zbitmap, zbitmap) RETURNS integer
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_cmp';

CREATE FUNCTION zbitmap_sort(internal) RETURNS void
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_sort';

CREATE OPERATOR CLASS zbitmap_ops
  DEFAULT FOR TYPE zbitmap USING btree AS
    OPERATOR	1	< ,
    OPERATOR	2	<= ,
    OPERATOR	3	= ,
    OPERATOR	4	>= ,
    OPERATOR	5	> ,
    FUNCTION	1	zbitmap_cmp(zbitmap, zbitmap),
    FUNCTION	2	zbitmap_sort(internal);

CREATE FUNCTION zbitmap_hash(zbitmap) RETURNS int4
  IMMUTABLE STRICT LANGUAGE C
  AS '$libdir/libz', 'zbitmap_hash';

CREATE OPERATOR CLASS zbitmap_ops
  DEFAULT FOR TYPE zbitmap USING hash AS
    OPERATOR	1	=,
    FUNCTION	1	zbitmap_hash(zbitmap);
