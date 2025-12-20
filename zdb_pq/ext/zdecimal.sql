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
