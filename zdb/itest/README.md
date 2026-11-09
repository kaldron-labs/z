# Zdb integration tests

Build with `make -j8`, then run `make -C zdb/itest test`. The suite runs serially
because replication tests bind loopback ports.

`zdbsagareptest` exercises saga recovery on later leader activation, using two
DBs and in-memory stores. It pauses the leader after an insert and update have
replicated, fails and stops that leader, then checks that the promoted follower
replays the saga without repeating either mutation. Completion must precede
the follower's `up` callback, and the row must retain exactly one increment.
Coordination uses scheduler callbacks and semaphores, without sleeps.

The test uses loopback ports 9945 and 9946. Run it directly with
`prove -j1 zdb/itest/zdbsagareptest`; use `./libtool exec` for debugging or
Valgrind. It does not need PostgreSQL and does not change an external database.

`zdbsagareptest` also runs `policyReplication` through three hosts. It replicates two row types with distinct ADL sizes/heaps and two
instances of the smaller type, checking typed producer/store allocations,
receiver frame backing, store-completion identity and retained buffers through
multiplexer stop. The intermediate host forwards the original receive backing
through an independent transmit descriptor; all three stores must observe each
row. The fixture uses ports 9955 through 9957. Startup and delivery waits are bounded on the controlling test thread.
