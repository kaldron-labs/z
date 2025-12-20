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
