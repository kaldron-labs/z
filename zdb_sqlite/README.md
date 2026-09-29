# Zdb SQLite store

`libZdbSL` is the persistent SQLite backend for Zdb. It uses one SQLite file,
one connection, and one configured isolated `ZmScheduler` thread per store.
The store reports `replicated=false`; when replication is configured, Zdb
performs it between hosts and each host uses its own SQLite file.

SQLite 3.37 or newer is required by default and must be available through
pkg-config. Pass `-S` to `z.config` or `--without-sqlite` to `configure` to
disable the module. PostgreSQL is independent and may be disabled with `-P`.

## Configuration

```yaml
store: {
  module: "/path/to/libZdbSL.so",
  thread: zdb_sl,
  connect: "/var/lib/app/app.db",
  synchronous: NORMAL
}
```

`thread` must name an isolated scheduler thread other than the multiplex Rx or
Tx thread. `connect` is a local filesystem path. `synchronous` defaults to
`NORMAL`; `FULL` and `OFF` are also accepted. Every connection uses WAL mode
and SQLite's default automatic checkpoint policy. `FULL` asks SQLite for the
strongest configured commit durability, `NORMAL` avoids an extra WAL sync in
the common path, and `OFF` omits SQLite sync operations. The guarantees still
depend on the filesystem and storage device. WAL requires local shared-memory
access; network filesystems and concurrent writer processes are unsupported.
External read-only connections are supported, although long readers can delay
checkpoints.

Startup creates missing store metadata, application tables, internal saga
tables, and indices transactionally. A compatible table may acquire a missing
index. Existing logical fields, storage classes, encoding versions, key
definitions, or index definitions that disagree with the application schema
cause open to fail without rewriting the table. The file records the shard
count and storage-format version and rejects incompatible values.

Writes and their delete/high-water metadata use one transaction. Completion
callbacks run asynchronously on the configured store thread after commit or
rollback. Busy/locked, read-only, I/O, corruption, and constraint errors are
returned through the normal Zdb result callbacks; the backend does not retry or
wait. Shutdown fences new operations, drains queued scheduler work, finalizes
statements, and closes the connection before its completion callback.

## Storage format

Tables are `STRICT` and use one SQLite column per reflected field. Signed
integers through 64 bits use `INTEGER`, strings use `TEXT COLLATE BINARY`, and
the remaining types use canonical `BLOB` encodings. UInt64 is 8-byte big
endian. Int128, UInt128, Fixed, and Decimal use 16-byte sortable big-endian
encodings; signed encodings flip the high bit. Float uses an 8-byte sortable
IEEE encoding with signed zero and NaN canonicalization. Time and DateTime are
single 12-byte columns containing sign-flipped big-endian components. Vectors
start with a big-endian 32-bit count. Format version 1 is defined in
`plan.md` and enforced by the stored logical schema metadata.

Key 0 has a unique index and identifies a row. Secondary-key indices are
non-unique. `find` returns one matching row for a non-unique secondary key;
Zdb subsequently addresses any update or delete by that row's key 0. SQLite's
native INTEGER, BINARY TEXT, and bytewise BLOB ordering controls `select`
results and continuations.

## CLI inspection and backup

Ordinary `.dump` output is lossless and represents encoded values as SQLite
BLOB literals. Load the optional inspection extension to format scalar values:

```sql
.load /path/to/libZdbSLInspect.so sqlite3_zdbslinspect_init
SELECT zdbsl_datetime(created), zdbsl_decimal(price) FROM a_order
  ORDER BY created;
```

The available functions are `zdbsl_datetime`, `zdbsl_time`,
`zdbsl_decimal`, and `zdbsl_float`. Order by the stored column rather than the
formatted text. Loading the extension does not change `.dump` output.

For a live database, use SQLite's backup API or a tool command that includes
the active WAL state, such as `.backup`; copying only the main file while the
store is running is unsafe. Restore into a local directory with permissions for
the database, WAL, and shared-memory files, then start Zdb normally so schema
and topology validation run.

## Tests

```sh
make -C zdb_sqlite test
./zdb_sqlite/itest/zdbsltest \
  -m "$PWD/zdb_sqlite/src/.libs/libZdbSL.so" \
  -c /tmp/zdbsltest.db
```

The suite covers codecs, schema validation and repair, durability configuration,
CLI inspection and dump/restore, persistence and recovery, saga failure/crash
paths, and Zdb replication/promotion between two distinct SQLite files. The
integration databases are disposable and are modified by the tests.
