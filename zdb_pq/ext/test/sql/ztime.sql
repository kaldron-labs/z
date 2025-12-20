SELECT CAST(0::zdecimal AS ztime);
SELECT CAST(-1::zdecimal AS ztime);
SELECT CAST(0::zdecimal AS ztime) - 1::zdecimal;
SELECT CAST(0::int8 AS ztime);
SELECT CAST(-1::int8 AS ztime);
SELECT CAST(0::int8 AS ztime) - 1::zdecimal;
SELECT CAST(- CAST('0048/08/09 10:30:42'::ztime AS zdecimal) AS ztime);
SELECT ''::ztime;
SELECT 'x'::ztime;
SELECT '1969/11/01 21:19:54.01 x'::ztime;

CREATE TABLE test_ztime (x ztime);
CREATE UNIQUE INDEX test_ztime1_x_key ON test_ztime USING btree (x);
INSERT INTO test_ztime VALUES (CAST(-1::zdecimal as ztime)), (CAST(0::zdecimal as ztime)), (CAST(1::zdecimal as ztime)), (CAST(2::zdecimal as ztime)), (CAST(3::zdecimal as ztime)), (CAST(4::zdecimal as ztime)), (CAST(5::zdecimal as ztime)), (CAST(null::zdecimal as ztime));

SET enable_seqscan = off;
SET enable_bitmapscan = off;

EXPLAIN (COSTS OFF) SELECT * FROM test_ztime WHERE x = CAST(3::zdecimal as ztime);
SELECT * FROM test_ztime WHERE x = CAST(3::zdecimal as ztime);

DROP TABLE test_ztime;

RESET enable_seqscan;
RESET enable_bitmapscan;

SELECT CAST(1::zdecimal as ztime) < CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) < CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) < CAST(4::zdecimal as ztime);
SELECT CAST(1::zdecimal as ztime) <= CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) <= CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) <= CAST(4::zdecimal as ztime);
SELECT CAST(1::zdecimal as ztime) = CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) = CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) = CAST(4::zdecimal as ztime);
SELECT CAST(1::zdecimal as ztime) <> CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) <> CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) <> CAST(4::zdecimal as ztime);
SELECT CAST(1::zdecimal as ztime) >= CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) >= CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) >= CAST(4::zdecimal as ztime);
SELECT CAST(1::zdecimal as ztime) > CAST(1::zdecimal as ztime);
SELECT CAST(5::zdecimal as ztime) > CAST(2::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) > CAST(4::zdecimal as ztime);

SELECT ztime_cmp(CAST(1::zdecimal as ztime), CAST(1::zdecimal as ztime));
SELECT ztime_cmp(CAST(5::zdecimal as ztime), CAST(2::zdecimal as ztime));
SELECT ztime_cmp(CAST(3::zdecimal as ztime), CAST(4::zdecimal as ztime));
SELECT pg_typeof(CAST(1::zdecimal as ztime) + 1::zdecimal);
SELECT CAST(1::zdecimal as ztime) + 1::zdecimal;
SELECT CAST(3::zdecimal as ztime) + 4::zdecimal;
SELECT CAST(5::zdecimal as ztime) + 2::zdecimal;
SELECT CAST(3::zdecimal as ztime) + '-4'::zdecimal;
SELECT CAST(5::zdecimal as ztime) + '-2'::zdecimal;
SELECT pg_typeof(CAST(1::zdecimal as ztime) - CAST(1::zdecimal as ztime));
SELECT pg_typeof(CAST(1::zdecimal as ztime) - 1::zdecimal);
SELECT CAST(1::zdecimal as ztime) - CAST(1::zdecimal as ztime);
SELECT CAST(3::zdecimal as ztime) - 4::zdecimal;
SELECT CAST(5::zdecimal as ztime) - 2::zdecimal;
SELECT CAST(3::zdecimal as ztime) - '-4'::zdecimal;
SELECT CAST(5::zdecimal as ztime) - '-2'::zdecimal;

SELECT CAST(CAST('42.01'::zdecimal AS ztime) as int8);
SELECT CAST(CAST('-42.01'::zdecimal AS ztime) as int8);

SELECT ztime_out_csv(CAST(0::int8 AS ztime));
SELECT ztime_out_iso(CAST(0::int8 AS ztime));
SELECT ztime_out_fix(CAST(0::int8 AS ztime));

SELECT ztime_in_csv(ztime_out_csv(CAST('1.01'::zdecimal AS ztime)));
SELECT ztime_in_iso(ztime_out_iso(CAST('-1.01'::zdecimal AS ztime)));
SELECT ztime_in_fix(ztime_out_fix(CAST('42.42'::zdecimal AS ztime)));

SELECT ztime_hash(CAST(1::zdecimal as ztime));
