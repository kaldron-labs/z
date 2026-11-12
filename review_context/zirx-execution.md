# ZiRx repair execution evidence

The ZiRx connection receive paths now obtain each empty receive slot from
`io.cxn->info().rxAllocator->allocBuf(impl, 0)`. The CRTP allocator hook remains
App framing storage. One callback-local reference holds unfinished input; an
invocation moves that state and received input into local references before any
Hdr/Body callback. A saved callback identity keeps the invocation alive and
prevents a retired callback from overwriting completion, disconnect or rearm.
Arm/rearm uses `ZiConnection::recv`, preserving transport plugin notification.
No plugin or backing-type branch was added to ZiRx. Memory-only entry points
retain their previous framing logic.

Repository search finds connection-mode ZiRx users only in Zdb and the TCP
client/server and UDP client examples; their existing App/network allocator
split was already correct. Ztcp and Ztls do not inherit ZiRx. Zquic's receive
path is unchanged. ZiRx includes the existing ZiMultiplex definition to access
connection information; no generic connection API was added.

## Verification

Clang debug, existing build, shared eight-job jobserver through
`/home/ubuntu/zquic-zirx-execution/make.py`; Zi src rebuilt before owner tests,
then both repairs were implemented before dependent verification began.

- `make.py -C zi`: pass, no warnings.
- `make.py -C zi/test test`: pass, 22 files / 144 top-level tests.
- `make.py -C zi/itest test`: pass, ring matrix, residue, heap/hash tuning;
  residue Python 9 tests, shell and Go tests also pass.
- `ZiRxTest` keeps 3 memory-only tests and adds 8 connection combinations of
  builtin/registered-stub, async/sync and TCP/UDP. Coverage includes empty
  slots/stubs, original exact-message backing with no App allocation, every
  split of a seven-byte frame, coalesced messages and partial tails, a message
  larger than one packet, cumulative unknown headers bounded by MaxBufSize,
  Sync partial consumption, malformed headers, drop/error/completion/rearm,
  network allocation rejection at initial arm and refill, partial-accumulator
  cleanup and final registered packet release off the Rx owner. Fake stubs
  have three bytes of headroom and four of tailroom. Every installed receive
  slot is checked against its network allocator and original owner.
- `libtool exec valgrind --leak-check=full --error-exitcode=99
  ./zi/test/ZiRxTest`: exit 0, ERROR SUMMARY 0; definitely, indirectly and
  possibly lost all 0. Framework singleton caches remain reachable (31,904
  bytes in 61 blocks).
- TCP builtin controlled peers: Zi client `-f` receives exactly 6000 body
  bytes after a split HTTP header; Zi server `-f` completes a split request
  and sends exactly its declared response body (12378 bytes in recorded run).
  A further controlled client run completes 4 x 32000-byte bodies with peer
  SHUT_WR, and server run returns 4 exact declared response bodies.
- Native TCP pair after the example correction below: 32/32 complete
  responses, client 809730 body bytes, server 814785 wire bytes. Network trace
  identifies all 32 Content-Length headers, six above 32768, maximum 47871.
  This explicitly covers output spanning the connection MPS. Final four-pair
  smoke and four-message UDP echo both pass. UDP client output preserves the
  server source address and all four original messages.

Detailed logs, commands and caller-owned orchestration scripts are retained in
`/home/ubuntu/zquic-zirx-execution/` as `zirx-*`, `zi-example-*`,
`zi-controlled-*`, `zi-client-large-peer.log`, `zi-server-large-peer.log`,
`zi-pair-*-client.strace.*` and `zi-*-trace-pair.json`. No listeners remain and
owned `/tmp` scratch was removed. Residue helper tests clean their deliberate
failed-directory fixtures themselves.

## Demonstrated corollary example defect

Initial native TCP pairs exited successfully while only 2 or 3 of 4 response
bodies completed in several runs. Network tracing proved one response declared
45183 body bytes but delivered exactly 32768 total wire bytes before EOF.
`zimxserver::Output` splits at connection MPS; its old `sent` disconnected on
every buffer. The first synchronous completion could close Tx before the
producer submitted the suffix. The narrow example correction sets the full
response byte count before emission, subtracts each completed buffer and
records the response/disconnects only after the remainder reaches zero. It
preserves immediate send-failure teardown and does not modify ZiTx,
ZiConnection or ZiMultiplex. The 32-response trace above verifies large
responses after the correction.

## Audit

Checked the diff against docs/AUDIT.md: one network receive source, one
callback-local framing reference, no new operation object, queue, lock, timer,
allocator hierarchy or public lifecycle interface. App storage is never a
receive target; original packet backing is retained on complete-message
paths, and only framing-required coalescing/trailing copies are introduced.

## Persisted-store and lower-transport dependent gates

After corresponding module pass manifests were present, existing persisted-store
integration targets passed: SQLite seven files / 59 tests and PostgreSQL three
files / 23 tests. SQLite covers its two-file leader/follower replication and
promotion fixture, all-types restart, persistence/recovery and saga crash paths.
A direct eight-check replication run was then allowed to finish all DB/mx
stop/final operations; separate read-only SQLite connections opened both files,
verified the replicated `sqlite` row in `a_order` and `PRAGMA integrity_check`
returned `ok` for both. The fixture files were removed after successful readback.
The existing SQLite replication fixture was subsequently strengthened to enable
mx fragmentation in debug builds, restart the promoted follower, explicitly
evict its cached row and verify replicated data through the reopened store.
The final targeted run passes 11 checks, and independent read-only connections
again verify both persisted files and integrity. This directly covers persisted
fragmented ZiRx replication; the base Zdb three-host fixture adds coalesced input
and onward forwarding coverage.

PostgreSQL tests ran on a newly initialized caller-owned local cluster with only
a private Unix socket (no TCP listener or external database). Existing installed
uint/libz extensions were enabled in a disposable `repair` database, loading the
current source-tree libZu. Existing saga fixtures verify durable replay across
reconnect, fresh-store recovery, abrupt process loss, cross-table/shard updates
and persistence of independent namespace sequence histories. The cluster stopped
successfully and data/socket directories were removed. No installed extension,
server configuration or production source was changed.

Logs: `zdb-sqlite-itest-{build,test}.log`, `zdb-pq-itest-{build,test}.log`,
`zdb-sqlite-repl-direct.log`, `zdb-sqlite-repl-readback.json`, `pq-*.log` and
`pq-isolated-result.json` under the caller-owned execution directory. The result
manifest records success with both cluster paths cleaned.

The lower TLS integration target also passes: three files / 13 tests covering
Vault DBus, Tx packet behavior and incremental Rx packet working pairs. Rx
coverage includes shared/separate owner arrangements, asynchronous signer
suspend/resume/cancel/restart, KeyUpdate, retained plaintext and exact input
reference balance. Ztcp has no `itest` directory in the current source tree;
its raw Rx regression is covered by the coordinator's unit-suite gate. Logs are
`ztls-itest-lower-{build,test}.log`. Owned fixture residue was removed and the
pre-existing rotated TLS diagnostic log family was restored unchanged.

Final SQLite focused artifacts are `zdb-sqlite-repl-final-build.log`,
`zdb-sqlite-repl-final-direct.log`, `zdb-sqlite-repl-final-readback.json` and
`zdb-sqlite-repl-final-readback.log`. The stronger fixture only extends the
existing integration source; no new backend, protocol or test infrastructure
was added. The applicable full integration rerun also passes: seven files / 62 tests,
recorded in `zdb-sqlite-itest-final-test.log`. Owned fixture files and empty
residue roots were removed; `persisted-itest-result.json` records the final
PostgreSQL and SQLite outcomes.
