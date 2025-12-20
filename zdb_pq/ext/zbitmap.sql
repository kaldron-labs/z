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
