# ZdbSL performance microbenchmark

Build and run from the configured tree:

```sh
make -C zdb_sqlite/bench bench
```

The benchmark creates a temporary SQLite database with 20,000 rows and reports
warm p50/p99 latency for one-, three-, and eight-column continuation seeks, a
mixed-direction continuation, fixed-scalar plus 16KiB primitive-vector and
4KiB text/byte payload insert/update/find/select operations, prepared
`synchronous=OFF` transactions, and close/reopen.  It also prints SQLite's
query-plan detail, outstanding SQLite allocation count, and bytes currently
managed by SQLite.  Query latency is in nanoseconds; reopen latency is in
microseconds.  Benchmark-owned dynamic storage uses named `ZdbSL.Bench*` heap
IDs so Z heap telemetry can attribute fallback allocation outside timed paths.

The benchmark is intentionally not part of `make test`: its results depend on
the host kernel, filesystem, SQLite build, and CPU.  Compare before/after runs
on the same idle host and retain the complete output with performance-review
evidence.
