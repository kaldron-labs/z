# Pending Diff Audit

The pending diff does not yet pass the `GUIDELINES.md` audit. The findings below
are divided into blockers, amber flags, and categories checked without a
finding.

## Blocking findings

1. **H1 Tx error callback has incorrect lifetime and attribution.**

   Each pipelined operation installs `[this]` into the shared physical TCP link
   at `zhttp/src/ZhttpClient.hh:3533`. Later operations overwrite earlier
   callbacks, and the link can retain a dangling `Operation *` after retirement.
   The callback should be link/pool-owned or otherwise tied to the individual
   transmission safely.

2. **H1 directly accesses an Rx-owned container from Tx.**

   `m_operations` is mutated on Rx, but `Link::close()` iterates it after
   dispatching to Tx at `zhttp/src/ZhttpClient.hh:3702`. This violates shard
   ownership and can race with completion/reassignment. The linear find/splice
   completion path also becomes quadratic at high pipelining.

3. **Synchronous resolver completion can corrupt cancellation state.**

   Assignments at `zhttp/src/ZhttpClient.hh:5385` and
   `zhttp/src/ZhttpClient.hh:5455` start resolution before publishing the
   returned handle. A synchronous callback can clear or replace
   `m_routeDiscovery`, after which the outer assignment installs the obsolete
   handle and can lose a nested fallback request. The test resolver is
   explicitly synchronous at `zhttp/test/ZhttpClientCancelTest.cc:25`.

4. **The implemented `Link` is not the persistent connection owner described
   by the design.**

   `Pool::LinkSlot` at `zhttp/src/ZhttpClient.hh:4405` contains only counters and
   flags. TLS/H2/H3 allocate protocol-private links and remove/destroy them when
   connections go down, for example at `zhttp/src/ZhttpClient.hh:1647` and
   `zhttp/src/ZhttpClient.hh:2359`. Thus the stable pool link identity does not
   own the one current live connection. Several `LinkSlot` fields are
   consequently write-only or dead.

5. **Shared/Rx/Tx member organization is not consistently applied.**

   Mixed-shard structures lack the required shared-first, cache-line-aligned Rx,
   cache-line-aligned Tx layout:

   - `ClientMessage`: `zhttp/src/ZhttpClient.hh:786`
   - `Pool`: `zhttp/src/ZhttpClient.hh:5805`
   - `Client`: `zhttp/src/ZhttpClient.hh:6066`

   `Pool::limited()` also reads the Rx-owned `m_links` container before
   dispatching; validation should use stable configuration data.

6. **TLS application headroom remains an unauthorized cross-shard read.**

   `ztls/src/Ztls.hh:787` reads Rx-owned `m_headroom` from application threads,
   explicitly described as an unclean read at `ztls/src/Ztls.hh:1352`.
   Headroom affects correctness and is not telemetry. It needs a stable
   conservative value or an explicit publication mechanism. The telemetry
   counters nearby remain atomic even though unclean primitive snapshots are
   permitted.

7. **QUIC still has ownership and encapsulation violations.**

   - Endpoint address state is made cross-thread accessible with `m_addrLock` at
     `zquic/src/Zquic_.hh:644`, contrary to the instruction not to add locks to
     legitimize non-owner access.
   - `m_appClose` is classified Rx-exclusive while being read by Tx at
     `zquic/src/ZquicLink.hh:9376`.
   - `Zquic::Stream` exposes `m_` members under `public` at
     `zquic/src/ZquicStream.hh:822`.
   - `receiveStop()` posts a short-lived intrusive reference to its owner at
     `zquic/src/ZquicStream.hh:564`.

8. **Request timers do not follow the required three-phase teardown pattern.**

   `RequestTimer_` is separately allocated/ref-counted and scheduler callbacks
   capture the timer while it retains a raw `agent` pointer at
   `zhttp/src/ZhttpClient.hh:4450` and `zhttp/src/ZhttpClient.hh:5581`.
   Cancellation is not followed by an explicit same-thread continuation and
   late-callback drain before finalization.

9. **New socket tests can hang indefinitely on failure paths.**

   The helpers use blocking `accept()`/`recv()` beginning at
   `zhttp/test/ZhttpClientCancelTest.cc:340`. If the client fails before
   connecting or completing, the main thread reaches `join()` while the server
   remains blocked. Test deadlines do not close or wake those sockets.

## Amber audit flags

- Numerous nondependent declarations are nested in the large `Pool` template
  beginning at `zhttp/src/ZhttpClient.hh:4323`.
- New hot-path allocations lack explicit heap identification, including H1
  links/operations and coordinator pools.
- Route endpoint arrays are repeatedly copied per attempt around
  `zhttp/src/ZhttpClient.hh:5354`.
- Several request, stop, and failure loops can process unbounded configured
  concurrency in one I/O turn.
- `stopTx_()` keeps a queue iterator alive while posting cross-shard work at
  `zhttp/src/ZhttpClient.hh:5787`.
- New `struct` types contain private-style `m_` data; these should be classes, or
  public records without `m_`.
- CRTP downcasts, type-erased `void *` operation state, an unnecessary byte-span
  `reinterpret_cast`, repeated invariant `length()` calls, fixed test arrays,
  and unexplained numeric capacities all trigger corresponding audit flags.
- Pool-level H2/H3 tests are missing for stream negotiation,
  saturation/unsaturation, application limiting, and reconnecting persistent
  links.

## Flags checked with no finding

No new concepts/`requires`, STL substitution, `enum class`, broad lambda
captures, hand-rolled hashing, direct `FILE`/`printf` logging, intrusive-ref
values, qlog capture violations, sleep polling, or formatting errors were
found. Discrete protocol decisions generally use `switch`, and no reliance on
scheduler dispatch being synchronous was found.

## Validation

- `git diff --check`: passed.
- Full incremental clang debug `make -j4`: passed.
- `timeout 120 ./zhttp/test/ZhttpClientPoolTest`: all 8 subtests passed.
