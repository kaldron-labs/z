SELECT - '42.01'::zdecimal;
SELECT - '0'::zdecimal;
SELECT - 'nan'::zdecimal;
SELECT '42.01'::zdecimal;
SELECT '-42.01'::zdecimal;
SELECT ''::zdecimal;
SELECT 'x'::zdecimal;
SELECT '42 x'::zdecimal;
SELECT - '999999999999999999.999999999999999999'::zdecimal;
SELECT  '-999999999999999999.999999999999999999'::zdecimal;
SELECT   '999999999999999999.999999999999999999'::zdecimal;
SELECT - '100000000000000000.000000000000000001'::zdecimal;
SELECT  '-100000000000000000.000000000000000001'::zdecimal;
SELECT   '100000000000000000.000000000000000001'::zdecimal;
SELECT - '1000000000000000000.000000000000000001'::zdecimal;
SELECT  '-1000000000000000000.000000000000000001'::zdecimal;
SELECT   '1000000000000000000.000000000000000001'::zdecimal;

SELECT zdecimal_sum(NULL::zdecimal, NULL::zdecimal);
SELECT zdecimal_sum(NULL::zdecimal, 1::zdecimal);
SELECT zdecimal_sum(2::zdecimal, NULL::zdecimal);
SELECT zdecimal_sum(2::zdecimal, 1::zdecimal);

SELECT sum(val::zdecimal) FROM (SELECT NULL::zdecimal WHERE false) _ (val);
SELECT sum(val::zdecimal) FROM (VALUES (1), (null), (2), (5)) _ (val);

SELECT avg(val::zdecimal) FROM (SELECT NULL::zdecimal WHERE false) _ (val);
SELECT avg(val::zdecimal) FROM (VALUES (1), (null), (2), (5), (6)) _ (val);
SELECT avg(val::zdecimal) FROM (VALUES (999999999999999999.999999999999999999), (-999999999999999999.999999999999999999)) _ (val);

CREATE TABLE test_zdecimal (x zdecimal);
CREATE UNIQUE INDEX test_zdecimal1_x_key ON test_zdecimal USING btree (x);
INSERT INTO test_zdecimal VALUES (-1), (0), (1), (2), (3), (4), (5), (null);

SET enable_seqscan = off;
SET enable_bitmapscan = off;

EXPLAIN (COSTS OFF) SELECT * FROM test_zdecimal WHERE x = 3::zdecimal;
SELECT * FROM test_zdecimal WHERE x = 3::zdecimal;

EXPLAIN (COSTS OFF) SELECT * FROM test_zdecimal WHERE x = 3::zdecimal;
SELECT * FROM test_zdecimal WHERE x = 3::zdecimal;

DROP TABLE test_zdecimal;

RESET enable_seqscan;
RESET enable_bitmapscan;

SELECT '1'::zdecimal < '1'::zdecimal;
SELECT '5'::zdecimal < '2'::zdecimal;
SELECT '3'::zdecimal < '4'::zdecimal;
SELECT '1'::zdecimal <= '1'::zdecimal;
SELECT '5'::zdecimal <= '2'::zdecimal;
SELECT '3'::zdecimal <= '4'::zdecimal;
SELECT '1'::zdecimal = '1'::zdecimal;
SELECT '5'::zdecimal = '2'::zdecimal;
SELECT '3'::zdecimal = '4'::zdecimal;
SELECT '1'::zdecimal <> '1'::zdecimal;
SELECT '5'::zdecimal <> '2'::zdecimal;
SELECT '3'::zdecimal <> '4'::zdecimal;
SELECT '1'::zdecimal >= '1'::zdecimal;
SELECT '5'::zdecimal >= '2'::zdecimal;
SELECT '3'::zdecimal >= '4'::zdecimal;
SELECT '1'::zdecimal > '1'::zdecimal;
SELECT '5'::zdecimal > '2'::zdecimal;
SELECT '3'::zdecimal > '4'::zdecimal;

SELECT zdecimal_cmp('1'::zdecimal, '1'::zdecimal);
SELECT zdecimal_cmp('5'::zdecimal, '2'::zdecimal);
SELECT zdecimal_cmp('3'::zdecimal, '4'::zdecimal);
SELECT pg_typeof('1'::zdecimal + '1'::zdecimal);
SELECT '1'::zdecimal + '1'::zdecimal;
SELECT '3'::zdecimal + '4'::zdecimal;
SELECT '5'::zdecimal + '2'::zdecimal;
SELECT '-3'::zdecimal + '4'::zdecimal;
SELECT '-5'::zdecimal + '2'::zdecimal;
SELECT '3'::zdecimal + '-4'::zdecimal;
SELECT '5'::zdecimal + '-2'::zdecimal;
SELECT '127'::zdecimal + '127'::zdecimal;
SELECT pg_typeof('1'::zdecimal - '1'::zdecimal);
SELECT '1'::zdecimal - '1'::zdecimal;
SELECT '3'::zdecimal - '4'::zdecimal;
SELECT '5'::zdecimal - '2'::zdecimal;
SELECT '-3'::zdecimal - '4'::zdecimal;
SELECT '-5'::zdecimal - '2'::zdecimal;
SELECT '3'::zdecimal - '-4'::zdecimal;
SELECT '5'::zdecimal - '-2'::zdecimal;
SELECT pg_typeof('1'::zdecimal * '1'::zdecimal);
SELECT '1'::zdecimal * '1'::zdecimal;
SELECT '3'::zdecimal * '4'::zdecimal;
SELECT '5'::zdecimal * '2'::zdecimal;
SELECT '-3'::zdecimal * '4'::zdecimal;
SELECT '-5'::zdecimal * '2'::zdecimal;
SELECT '3'::zdecimal * '-4'::zdecimal;
SELECT '5'::zdecimal * '-2'::zdecimal;
SELECT '127'::zdecimal * '127'::zdecimal;
SELECT pg_typeof('1'::zdecimal / '1'::zdecimal);
SELECT '1'::zdecimal / '1'::zdecimal;
SELECT '3'::zdecimal / '4'::zdecimal;
SELECT '5'::zdecimal / '2'::zdecimal;
SELECT '-3'::zdecimal / '4'::zdecimal;
SELECT '-5'::zdecimal / '2'::zdecimal;
SELECT '3'::zdecimal / '-4'::zdecimal;
SELECT '5'::zdecimal / '-2'::zdecimal;
SELECT '5'::zdecimal / '0'::zdecimal;

SELECT CAST('42.01'::zdecimal AS int8);
SELECT CAST('-42.01'::zdecimal AS int8);

SELECT CAST('42.01'::zdecimal AS double precision);
SELECT CAST('-42.01'::zdecimal AS double precision);

SELECT CAST('42.01'::zdecimal AS numeric);
SELECT CAST('-42.01'::zdecimal AS numeric);

SELECT zdecimal_hash(42.01);
SELECT zdecimal_hash(42.01::zdecimal);
