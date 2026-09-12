For Postgres testing:
1. Postgres uses path to control which installation is used
2. Ensure that the correct `pg_config` is found first in the path
  - e.g. `~/postgres/bin` if debugging locally
3. Ensure `pg_config --bindir` is correct
4. Ensure postgres is running: `pg_ctl -D [datadir] start`
5. Check databases: `psql 'host=/tmp dbname=postgres'`: `\l`
  - `CREATE DATABASE test;` to create a new test database
6. Ensure `pguint`, `libz` extensions and their dependencies are installed:
  - `uint.so`, `libz.so`, `libZu.so` should all exist in `pg_config --libdir`
  - if not, install them using `make && make install && make installcheck`
7. Ensure extensions are enabled in the test database:
  - `psql 'host=/tmp dbname=test'`:
    ```
    CREATE EXTENSION uint;
    CREATE EXTENSION libz;
    ```
    - Check enabled extensions with `\dx`
8. Run `zdbpqtest` with `-m zdb_pq/src/.libs/libZdbPQ.so` `-c 'host=/tmp dbname=test'`

## Continuation benchmark

`zdb_pq/bench/zdbpqbench` prints the generated mixed-direction continuation
SQL and its size for two, four, and eight fields. With a libpq connection
string it also creates and analyzes a disposable temporary 100,000-row table,
then prints `EXPLAIN (ANALYZE, BUFFERS)` for the eight-field continuation:

```sh
make -C zdb_pq/bench -j8
./zdb_pq/bench/zdbpqbench 'host=/tmp dbname=test'
```

The PostgreSQL fixture is session-local and disappears when the benchmark
exits. Run it against a disposable development server; it is intentionally
not part of `make test` and has no timing threshold.

## Integration tests

Use a disposable database: these tests create tables and modify fixture rows.
From the repository root, after building prerequisites through `zdb_pq`:

```sh
export PATH=/home/count0/postgres/bin:$PATH
export PGDATA=/home/count0/postgres/data
export ZDB_MODULE=$PWD/zdb_pq/src/.libs/libZdbPQ.so
export ZDB_CONNECT='host=/tmp dbname=test'
make -C zdb_pq -j8
make -C zdb_pq test
```

`itest/zdbsagatest` uses four DB shards on distinct scheduler threads. The
main saga row uses shard 2; mutations target shards 0 and 1 across two tables.
Shard 3 is unused; the DB topology requires a power-of-two shard count.
`itest/zdbpqtest` uses the same four-shard count so both programs can share
`ZDB_CONNECT` without conflicting with the persisted DB metadata.
Use a fresh test database when changing the persisted shard topology. The
`pqSaga1` definition inserts and updates `pq_saga_order`, then deletes from
`pq_saga_item`. It tests live completion and recovery of an intentionally
interrupted saga after closing and reopening the PostgreSQL connection. Replay
must skip the already-journaled insert/update, preserve the incremented quantity
without applying it twice, finish the delete, and clean the saga rows.
The reconnect case flushes queued writes before reconnecting. A separate
child-process case confirms the update through a backend SELECT, then calls
`_Exit` without DB stop/final or object destruction. The parent waits for that
specific exit and verifies recovery using fresh DB state. This covers abrupt
process loss at a known durable cut, not termination between backend writes.
Two further children throw or return without committing inside an update
callback after its step intent. The parent requires process termination via
`abort`, then starts a healthy saga instance to recover any surviving journal
rows and verify normal execution. The aborting POSIX children disable core
dumps before starting worker threads.

The batch fixture also submits sagas with a persisted zero `ZuTime` deadline
and interrupts them before and after each step intent. Initial execution is
unaffected; after each drained reconnect, recovery compensates the applied
prefix instead of replaying forward, then removes the saga journal rows.

The same binary opens `saga_ns` in both `public` and `zdb`, checking independent
CRUD and sequence histories, then closes and reopens both handles on the same
connection. After deleting the rows it recreates the store and checks that
independent MRD entries restore both sequence histories without cached state.
These are disposable fixture tables as well.
It repeats these checks with `saga_ns` padded with `n` to the maximum logical
table-ID length, exercising the namespace marker and SQL suffix budget.

The integration target runs these programs serially because they share the
configured database and its schema bootstrap.

Both programs emit TAP and run through the module's `make test` target, which
skips them unless `ZDB_MODULE` and `ZDB_CONNECT` are set. For memory checks:

```sh
./libtool --mode=execute valgrind --error-exitcode=99 --leak-check=full \
  ./zdb_pq/itest/zdbsagatest
```
