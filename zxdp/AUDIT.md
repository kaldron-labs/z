# Zxdp audit work record

Status: in progress. This is not a completed audit or acceptance report.
Scope: all repository-owned source in `src`, `test`, `util`, and `itest`,
against every flag in `../docs/AUDIT.md`. Repository source changes stay in `zxdp`.

## Receive/wake API correction — 2026-11-12

The public receive entry is now `rx()` with a configurable `Params::spin`
(default 1000). Empty receive probe rounds precede internal blocking epoll.
`wake()` posts to an internal eventfd; preallocated atomic pulse accounting
preserves wakes before entry and concurrent scheduler notifications. The receiver
observes wakes while processing traffic and between internal batches. Packet
readiness continues processing inside the same call. Public nonblocking receive
polling and readiness descriptors were removed; internal descriptors close after
caller-quiesced service and kernel queue retirement. No runtime worker, buffer
adapter, per-packet search or hot-path heap allocation was introduced.

Current Clang/debug/ASan evidence is recorded in
[receive/wake results](../review_context/zutcp-execution/rx-wake-current/results.json):
full-tree build without warnings; four unit programs/nine subtests; four scoped
kernel programs/six subtests; a separate single-queue wake run/two subtests; and
two dependent Zutcp integration programs/five subtests. The wake test interposes
only the real syscall boundary to verify zero-spin blocking and race ordering;
it does not simulate readiness or add production hooks. The utility now supplies
separate Rx/Tx owners and delegates receive spinning/blocking to the library.

The earlier work record below describes superseded layers and is historical.
This receive/wake correction does not close the unfinished whole-module audit.

## Repairs

* Removed the Nif-state and Tx-queue copies of `PoolArray`. Instance retains
  the immutable pool batch until final completion; layout replacement is
  prohibited while its Nif exists. Tx already requires providers to outlive
  service and derives its direct slot metadata at setup. This removes
  two setup allocations and two sets of pool-reference increments/decrements.
  Original IOBuf descriptor holds and mapping retention remain necessary.
* Replaced the BPF builder's separate 512-element fixup array and count with
  chains in pending instructions' offset fields. Marking a forward label
  patches the final offsets in place. Labels use one heap-identified cold-path
  array, replacing the independent 128-label capacity/count. Linux's
  `BPF_MAXINSNS` bounds instruction indices and signed offsets. The output
  instruction array still moves directly to its consumer.
* `BufPool::slot` now validates canonical identity once and returns `NoSlot`
  for invalid backing; `owns` uses that result. Rx no longer validates and
  computes the same pool membership twice. Pool tests cover foreign, advanced,
  and null origins in addition to valid slots.
* Pool consumers use narrow accessors for activity, region offset and wake
  retention; the kernel-retirement observer uses Xsk accessors for registration
  identity rather than accessing another object's private fields.
* Removed unjustified trailing underscores from Rx/Tx queue operations and
  retirement-observer operations, updating module callers.
* Removed the impossible pending-reservation branch after Ring's always-on
  assertion. Removed the separate Nif retirement-proof boolean; the observer's
  completed state is the authority through the Rx/Tx handoff.
* Separated Nif Rx/Tx mutable groups at cache-line boundaries.
* Bounded FILL publication by `rxQuantum`. Startup primes available frames
  through asynchronous Rx turns before readiness; retained application frames
  do not prevent restart. The synthetic three-frame/two-quantum fixture now
  checks publication over separate owner turns. Integration caught a missing
  wake when this continuation initially used `push`; it now uses `rxRun`.
* Replaced the Nif implementation's friend-accessed private state with explicit
  `NifData` shared by its opaque implementation and facade. Exposed link fields
  live in `NifLink`; Instance configuration remains private behind narrow
  accessors. Only same-containing-type accesses to `m_` fields remain.
  Removed unsupported public API underscore suffixes and private helper suffixes;
  real facade/internal wrapper pairs retain theirs.
* Removed separate Rx/Tx initialized booleans; each queue's existing mx sentinel
  now determines initialization. Its retired state still distinguishes restart.
* Reduced Tx slots from 16 to 8 bytes: immutable extent/control metadata replaces
  repeated pool pointers. Canonical offsets are checked directly against arena
  geometry. Rx/padding/nonmember slots retain the zero-extent sentinel. The
  31-bit extent follows the existing validated page-size/MTU bounds.
* Tx admission and CQ service use a named 64-packet quantum. Completion indexes
  the UMEM chunk and pops a descriptor reference directly: no packet/buffer
  search, association traversal or suspended lookup. A live chunk keeps one
  original object and packet wire origin; unchanged repeated sends are supported.
  CQ consumption publishes the completed vector together.
* Descriptor references occupy a dense array prefix. Constant-time swap removal
  relocates at most one metadata record and repairs its links by index. Original
  buffer references move without reference-count churn; packet bytes never move.
  This removes the descriptor free chain, its initialization/restart walks and
  full-slot/full-capacity retirement scans. Final retirement visits only live
  descriptors, whose references must be released after kernel retirement.
  A 96-packet regression covers interleaved CQ order, malformed offsets,
  independent application lifetime, ring wrap and repeated-buffer stacks.
* Instance provisioning reuses its already-validated layout/page size when
  creating UMEM; it no longer repeats the query and complete layout traversal.
* Failed endpoint claims no longer allocate/free an EndpointNode. The kernel
  map and binding checks share a named capacity with its port-space basis.
* Interface queue membership uses a direct array indexed by the kernel-reported
  queue ID. Join/unlink no longer search a binding list, and BPFBinding no longer
  carries membership-list links. The array is allocated once at interface setup.
* Consolidated diagnostic-prefix reads in the integration tests using ZiFile;
  removed FILE APIs, redundant buffer zeroing and varargs index formatting.
  The allocated integration PacketPlugin now has its concrete, identified NHeap.
* Cached invariant loop bounds in production and classifier tests, and removed
  the unused address reset on retired Tx associations.

## Container and lifetime inventory

* Instance pool array: retained public pool identities; caller output arrays
  share the pools rather than copying payload. Provisioning uses a temporary
  batch for all-or-nothing publication, moved on success.
* Tx slots: direct chunk-indexed extent/role and descriptor-list heads. Tx
  holds: one original buffer reference/address/link per possible outstanding
  descriptor, with an embedded free chain. Repeated submissions require
  separate descriptor references. The sparse Rx/padding entries remain a
  deliberate direct-index cost: a second range/hash structure would add storage
  or packet-time searches. Completion never traverses these association links.
* Rx slots: original FILL holds plus recycle-observation state, indexed by the
  physical pool's slot ID. Setup allocation; packet payload is never staged.
* BPF code/labels/verifier and rtnetlink receive buffer: control-path storage.
  Verifier and netlink scratch sizing/initialization remain under review.
* Queue memberships use direct indexing. Interface registry and endpoint keys
  still use intrusive linked storage with immediate explicit removal. Kernel
  maps provide packet-time lookup.
  The endpoint key list is retained for selective retirement. Scaling,
  endpoint-map capacity and bounded configuration/teardown remain under review.
* Ingress metadata retains its allocator because the original buffer can
  outlive Instance/Nif; allocator/pool retain the mapping. These references
  implement backing lifetime, rather than retaining a connection owner.

## Coverage still required

Every flag remains subject to final whole-module verification. In particular:

* Structure: remaining underscore names, exposed data placement, template
  invariants, loop-condition calls, repeated indexing, compile-time spans,
  member alignment and hybrid class/struct shape.
* Storage/allocation: all concrete heap identities and inherited allocators,
  capacities, padding, intrusive ownership, fixed arrays/tables, scratch choices,
  deterministic removal and retained-owner references.
  GDB layout evidence in `/tmp/zxdp-audit-layout-types.log` shows
  KernelRetirement occupies 512 bytes, including a 48-byte hole before its
  cache-line-aligned observer. Moving the observer after the scalar state is
  a remaining layout repair to verify.
* Containers/movement: every container declaration and use, allocation/copy
  purpose, byte/string temporaries, initialization before overwrite, and logger
  capture lifetimes across library, tests and utilities.
* Algorithms: duplication, scheduler continuation prerequisites, polling and
  blocking contracts, deterministic test synchronization, iterator scope,
  counts/aliases/null checks, dispatch/nesting/hashing, latency and bounded turns.
  Known remaining linear configuration lookups are the interface registry's
  namespace/interface lookup/removal and BPFBinding endpoint-key removal; these
  require indexed framework containers. Packet/buffer admission and completion
  no longer search. Remaining setup/teardown walks also require workload review.
* Framework: existing parsing/formatting/I/O facilities, sentinel states,
  dynamic-module applicability, casts, private-field access, enum mappings,
  dead code, scope, statistics atomics and owner teardown.

## Verification

Current direct-completion and queue-index revision:

* Warning-free top-level Clang ASan/ZDEBUG build:
  `/tmp/zxdp-audit-direct-queue-build.log`.
* Eight unit programs / 34 TAP tests and five privileged integration programs /
  20 TAP tests pass with ASan/LSan:
  `/tmp/zxdp-audit-direct-queue-unit.log` and
  `/tmp/zxdp-audit-direct-queue-itest.log`.
* The release tree was configured through `z.config` and cleanly rebuilt before
  this audit. Current Zxdp sources were overlaid and the top-level incremental
  build passed without warnings (`/tmp/zxdp-audit-direct-release-build.log`).
  All module C++ sources and Makefile.am files matched the working tree byte
  for byte before verification.
* Memcheck passes all eight unit programs and four native privileged integration
  programs (`zxdptxtest`, `zxdprxtest`, `zxdpniftest`, `zxdplifecycletest`), with
  zero errors and zero definitely/indirectly/possibly lost bytes in all twelve
  main processes: `/tmp/zxdp-audit-direct-mc-*.log`.
* The registry's intentional fork/`_exit` probe retains the inherited live
  parent registry snapshot. Memcheck classifies its inherited 256-byte Bindings
  allocation as possibly lost in that child only. The allocation predates fork;
  the child must not retire the parent's kernel attachment. The normal parent
  releases it and reports zero lost bytes. Full child diagnostics remain in
  `zxdp-audit-direct-mc-zxdplifecycletest.log`; this is not an ongoing parent leak.
* Separate packet processes pass Memcheck in wait, spin and low-latency modes,
  with 32 request/replies and 129-byte payloads per mode. All six helper processes
  report zero errors and zero definitely/indirectly/possibly lost bytes:
  `/tmp/zxdp-audit-direct-packet-*.memcheck`. Fixture directories are
  `/tmp/zxdp-audit-direct-pair-wait-a9apvacp`,
  `/tmp/zxdp-audit-direct-pair-spin-qq3z33l8`, and
  `/tmp/zxdp-audit-direct-pair-ll-b5l3pvnq`.

An intermediate CQ implementation had scheduler-mode request/reply timeouts in
one run concurrent with release compilation
(`/tmp/zxdp-audit-structure-final-itest.log`). The current direct-indexed CQ
implementation passes the complete integration suite; the intermediate failure
log remains available for load/reliability follow-up.

Existing evidence does not yet prove the complete audit objective. The coverage
and configuration-lookup work listed above remain open.
