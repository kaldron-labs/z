# Zxdp DPDK implementation plan

This plan assumes the initialization and ownership changes in [Zutcp repair 3](../zutcp/zutcp_repair3.md) have landed. DPDK extends that interface. The repair owns the coordinated AF_XDP, TCP, multiplexer and example migrations.

The rebuild and verification sequence below incorporates the [Zutcp rebuild plan](../zutcp/REBUILD_zutcp.md).

## Baseline and fixed boundaries

Follow the [Zutcp allocation skeleton](../zutcp/zutcp_skeleton.md) on the frozen
Zi interface. The application opens the selected device on a stopped mx, moves
the initialized, unallocated, never-started device into Zutcp, then moves the
initialized plugin into that same mx. Device and plugin borrow the mx.

Device init discovers the link. Allocator requests collect typed pool objects.
Explicit mx.alloc(), or implicit allocation on first start, provisions the
complete set. Start activates queues and installs callbacks. Stop drains the
run. The device owns its allocated native backing until finalization. Direct
device allocation is reserved for standalone Zxdp tests.

Direct Tx is mx/Zutcp Tx -> rte_eth_tx_burst -> PMD/NIC. Rx polling and completion
service use the existing single Rx/Tx owners. No DPDK sender thread or EAL packet
worker is required. Queue setup is backend implementation work under driver
constraints; all supplied Rx mbufs remain available to the PMD. Slow application
retention may exhaust the pool and cause packet loss. Do not create extra backing
or reserve an arbitrary fraction of the pool to avoid that outcome.

## 1. Optional dependency and explicit EAL

In configure.ac, add --with-dpdk=yes|no following --with-pq. Only explicit yes
enables PKG_CHECK_MODULES([DPDK], [libdpdk >= 25.11]); enabled-but-missing fails
configuration. Reject invalid option values and enabled non-Linux targets.
Define HAVE_DPDK for C++ conditionals and Automake DPDK for sources/tests; export
DPDK_CFLAGS/DPDK_LIBS. Keep them out of disabled builds. In z.config add -K to
usage/getopts, default DPDK=yes on Linux and pass --with-dpdk through both compiler
branches. MinGW disables DPDK. Preserve all existing wrapper options.

Create installed ZxdpDpdk.hh and ZxdpDpdk.cc for Zxdp::Dpdk::EAL/EALParams.
Use the exact owned aggregate EALParams and enum declarations in the brief's
“Adopted explicit EAL parameters” section; that declaration is normative input
to this plan, not an invitation to redesign it. It covers lcores/main lcore,
PCI/vdev discovery, memory/NUMA/hugepages, IOVA, process isolation, diagnostics,
VFIO and extra native argv tokens. Keep all strings/lists owned. No environment
variable configuration is required.

EAL is discovered through Dpdk::EAL::instance(). Its constructor
does not initialize DPDK. Expose init(const EALParams &) returning
ZuUnion<void, ZeException> and ready() const. The application must explicitly
initialize it before requesting DPDK instances/pools. A second init attempt is
an error, not silent configuration replacement or hidden on-demand initialization.
Its destructor closes its stopped registered ports then calls rte_eal_cleanup()
after successful initialization. Instances, pools and retained mbufs must retire
first; do not rely on ordering within one singleton cleanup level.

Build private writable argv, beginning with a program name. Translate named
options to native EAL argv and append raw args as individual tokens without shell
parsing. Empty strings/void numeric values/Default enums omit that option. Map
allow/block/driverPath to -a/-b/-d, memoryMB/channels/ranks to -m/-n/-r; other named
fields use their documented DPDK 25.11 long options. Reject named/raw duplicates
of the same option (including aliases) and retain DPDK's native validation.
Empty lcores selects one suitable allowed CPU for the main lcore; preserve the
initializing thread's intended affinity and never assume CPU zero is allowed.
Explicit mappings remain under application control. No packet work is launched
merely because EAL workers were configured.

## 2. Zxdp selection and API

In Zxdp.hh extend Type to Xdp, Dpdk and Params to
ZuUnion<Xdp::Params, Dpdk::Params>. Preserve current macro and in-place union
construction conventions. Add:

```cpp
namespace Dpdk {
  struct Params : public Params_ {
    ZtString<> name;           // owned DPDK Ethernet device name
    unsigned ifindex = 0;      // Linux interface representing this port for TCP routing
    unsigned rxInterval = 50;  // maximum idle park in microseconds; 0 keeps polling
  };
}
```

These parameter types and Type::Dpdk need only framework headers and remain
expressible in disabled builds. EAL/concrete DPDK definitions and linking are
conditional. Zxdp::instance(Type::Dpdk) throws a structured ZeException when
support was not built or EAL is not ready, consistent with its ZmRef return
signature. It never substitutes Xdp. Construct the private Dpdk::Instance when
available; keep the Xdp factory behavior unchanged.

Add pure virtual void status() and implement it in both backends using the
normal ZiLOG(Info, logging-lambda) convention. Log backend/device, state, MTU/MAC,
operating copy mode and useful queue information. Retain mtu(), mac() and
zeroCopy(). Expose the repaired device metadata, including Linux interface
identity and Rx queue count, before TCP init. DPDK keeps its numeric port ID
private. Classify zeroCopy using the active PMD data path. Software
pcap copies; direct ring forwarding does not. A fixture's simulated wire copy is
outside the production instance's data path. Do not claim zero copy for an
unverified copy-capable PMD.

## 3. Private pools, device ownership and packet service

Create private ZxdpDpdkInstance.hh/.cc and ZxdpDpdkPool.hh/.cc. Implement
bufPool(type, count, size, headRoom, tailRoom, align) by forwarding the immutable
role to Zi::IOBufPool_ and retaining the requested geometry. At this point
native packet slots are unavailable. The device's alloc(rxPool, txPools) receives
the actual common ingress pool and every distinct native Tx pool. Use the same
mbuf allocation logic for TxTCP and TxUDP, preserving each pool's geometry and
the repaired transport's internal reserves.

Prepare the native mbuf mempools for the whole set before publishing any backing.
A partial failure releases provisional resources, returns the originating error
and leaves the device open for retry with the frozen set. After success, an
identical direct allocation is idempotent. A different set returns an error and
preserves the original pools. The device retains the successful allocation across
activation failure, stop and restart. AF_XDP's UMEM layout, registration socket
and FILL rings are private to that implementation.

Disable per-lcore mbuf caching initially so mbufs returned on other threads are
available to the bounded pool rather than stranded in another thread's cache.

Device init takes the mx directly in init(ZiMultiplex *, const Params &).
The Dpdk parameters carry spin and the device-specific settings. Resolve name with rte_eth_dev_get_port_by_name, acquire exclusive
use of the EAL-owned port and query its MTU, MAC and queue capabilities. For TCP
bootstrap, require the ifindex of the Linux control-plane link and validate its
MTU/MAC against the port. Standalone ring/pcap callers can omit that link.
Expose the represented interface identity, port MTU/MAC and single selected Rx
queue on successful init. Init needs no pools or receive callback.
Return an open, lookup or geometry error synchronously and release partial
acquisitions. A competing instance receives a structured busy error.

Validate the ingress pool and at least one nonempty Tx pool at allocation.
Start configures one Rx and one Tx queue using the allocated backing. Use the
common Rx mempool for replenishment. Derive queue capacities from supplied counts subject to PMD descriptor
limits and adjustment. Configure the existing contiguous-frame contract.

Rx backing fits MTU plus frame/native headroom; do not enable scatter, coalescing
or other offloads requiring chained/oversized representation. Tx pools may be
smaller than MTU. Preserve required alignment without enlarging logical capacity.
Materialize setup storage and packet bookkeeping before publishing readiness.

Use direct backend-private packet/mbuf identity, including packet pointers after
headroom offsets; do not search pools or attach external backing on each send.
Private setup-time address indexing is allowed. Each accepted packet has one
outstanding device submission. Native references protect driver access separately
from the caller/recovery allocation lease. False send acquires no submission
lease and leaves caller bytes/ownership intact; true transfers a driver reference
and leaves any independent caller lease valid. Roll back temporary references on
refusal. Do not enable PMD fast-free assumptions that contradict these references.
No submitted Zi metadata is retained solely for device completion.

pollTx() drives available PMD completion cleanup and reports each retired
submission exactly once, preserving Zutcp's pending-credit accounting. Keep
bookkeeping allocation-free and Tx-owned; pool return may occur on other threads.
Handle PMD unsupported/error results explicitly without treating them as proof
of retirement. Accepted work remains owned until actual retirement or safe device
teardown; close errors never authorize freeing backing still in use.

rx() receives mbufs from the configured Rx pool, calls RecvFn with the packet
pointer/length and transfers that received backing to the caller's existing
metadata path. Zutcp attaches that backing to the typed stub prepared through
the endpoint's Rx allocator. Final return reaches the common ingress pool.
Preserve coalesced off-owner return wakes during active I/O. Do not add a payload copy to production Rx. Register existing
scheduler threads with DPDK where required by the chosen APIs; do not launch
EAL packet workers to replace them.

Use the adopted idle order: bounded receive batches with wake checks, immediate
next-burst service under sustained input, then Params_::spin empty probes. Use
native arrival waiting when supported; bounded producer-head memory monitoring
may help short idle but does not imply kernel descheduling. For extended idle,
park on the internal counted-wake eventfd with short timed waits backing off to
rxInterval microseconds; zero stays active. Probe after timeout and restore
active service immediately on a packet. Preserve preposted/concurrent wake pulses
and check during sustained input and wait entry. No per-packet syscall is required
for fixture arrival notification; do not modify the stock ring PMD.

On stop or activation failure, gate new service, wake and quiesce Rx, and
drain accepted device work on the Tx owner. Keep completion service active
while draining. Account for dequeued mbufs and reclaim posted buffers when the
PMD relinquishes them. Idle Rx descriptors must not require arriving traffic to
finish stop. Deactivate queues, release per-run callbacks, and complete stop only
when queue I/O and callbacks have quiesced. Preserve the opened port, exclusive
binding, device-owned mempools and application-held buffers.
A later start on this instance rebinds the same backing. A fresh instance may
acquire the port after the prior instance finalizes. EAL retains responsibility
for removal of its registered ports and process cleanup.

allQueues() traverses actual configured queues/rings and normalizes available PMD
stats/reads into Ztc telemetry. Unsupported individual stats remain unavailable,
not fabricated. Reuse existing traversal/lifetime conventions; no extra queue
registry, index or cache exists solely for observation. Diagnostic reads do not
require accurate snapshots or new hot-path synchronization.

## 4. Zutcp integration through the repaired device interface

The application selects DPDK at the Zxdp factory. Put the port name, Linux
routing ifindex and idle interval on the Dpdk device parameters. TCP retains
its transport settings, including packet/control counts, credits, limits and
the application packet callback. Use the initialized device's interface
identity for TCP secrets and Linux route/static-neighbor lookup, and its MTU
and Rx queue count for the repaired ingress-sizing policy. Preserve the
registration-time Tx allocation bound for both large and small requests.

The following bootstrap assumes explicit EAL init succeeded and the routing
link has matching MAC and MTU. It uses the repair's proposed ownership API.

```cpp
ZiMultiplex mx{mxParams};
auto xdp = Zxdp::instance(Zxdp::Type::Dpdk);
Zxdp::Params params(
  Zxdp::Params::New<Zxdp::Params::Index<Zxdp::Dpdk::Params>{}>{});
auto &device = params.p<Zxdp::Dpdk::Params>();
device.name = "net_ring0";
device.ifindex = routeIfindex;
auto opened = xdp->init(&mx, params);
if (opened.is<ZeException>()) {
  ZiLogEvent(ZuMv(opened.p<ZeException>()));
  return 1;
}
ZmRef<Zutcp::Instance> tcp = new Zutcp::Instance{};
auto initialized = tcp->init(&mx, ZuMv(xdp), tcpParams);
if (initialized.is<ZeException>()) {
  ZiLogEvent(ZuMv(initialized.p<ZeException>()));
  return 1;
}
mx.plugin(ZuMv(tcp));

// During protocol initialization, the protocol retains its own receive handle:
// m_socketRxAllocator = mx->allocator<Zi::IOBufType::Rx, RxBuf, RxSize>(rxCount);
auto txUDP = mx.allocator<Zi::IOBufType::TxUDP>(udpTxCount);
auto tx0 = mx.allocator<Zi::IOBufType::TxTCP>(txCount0);
auto tx1 = mx.allocator<Zi::IOBufType::TxTCP>(txCount1);
if (needTxBuffersBeforeStart) {
  auto allocated = mx.alloc();
  if (allocated.is<ZeException>()) {
    ZiLogEvent(ZuMv(allocated.p<ZeException>()));
    return 1;
  }
  // Native Tx buffers can now be allocated from the retained handles.
}
if (!mx.start()) return 1;
```

Follow repair 3's stopped-init rules. Identical effective device settings are
idempotent. A changed device setting, including rxInterval, is a conflict and
preserves the original configuration. At TCP init, check the mx identity before
moving the initialized device into m_xdp. For every incoming nonnull device,
prior successful allocation is fatal, even on repeat init. A moved-from repeat
argument denotes the already-owned device. A different, otherwise eligible
device on the same mx is a recoverable conflict and stays with its caller.
A mismatched mx at either handoff is fatal.

Register the plugin before entry to the first allocator(), alloc() or start()
call, regardless of its result. Same-object registration is idempotent only
while this window is open. Each protocol retains its selected typed Rx handle
at init and passes it to listen, connect or udp, including on reconnect and
accepted connections. The first network Rx request records the common ingress
pool. Subsequent Rx specializations prepare their own backingless typed stubs
over that pool. Kernel-mode Rx uses built-in storage. Private parser copies and
reassembly use App storage. Preserve the originating allocator on retained
delivered buffers. Tx lookup validates the mx and role and reads allocator->pool.

Obtain every allocator handle, including App, before explicit or implicit
mx.alloc(). Entry closes the registry permanently. All later allocator requests
return nullptr, including identical requests after a failed allocation. Existing
App handles allocate independently of native backing. Zutcp forwards the
collected pool objects unchanged through plugin->alloc(rxPool, txPools) to
m_xdp->alloc(rxPool, txPools). Allocation failure leaves mx stopped and allows
retry with the same set. After success, repeated stopped mx.alloc() and restart
reuse the allocation without another hook call.

Start binds the run's receive and completion callbacks to the allocated device.
TCP and UDP share this packet service. On failed activation, drain any activated
queues and retry start on the same device. Return dependent application buffers
before teardown. While stopped, mx calls plugin final with allocator storage
and the pool collection still alive, including when no start occurred, with or
without explicit allocation. Device final frees native backing and releases the
port claim once. Both instances clear their borrowed mx pointers before mx
releases its plugin reference and then its allocator storage.

For routed peers, use the repaired synchronous Rx-owned resolve(remote, local)
and the permanent neighbor entry for the next hop. The representing Linux link
supplies the control-plane identity for a userspace-owned DPDK port.

## 5. Software fixtures and coverage

Use net_ring and net_pcap following dpdk_loopback.md through normal DPDK calls.
The ring PMD forwards existing mbuf pointers and ignores the Rx mempool supplied
to queue setup. Therefore the fixture must not directly forward one endpoint's
Tx mbuf into another's Rx path. Dequeue an outgoing mbuf, allocate a destination
Rx-pool mbuf, copy packet bytes, enqueue that destination mbuf on its Rx ring,
then release the outgoing submission reference. On allocation/enqueue failure,
release applicable references and drop the packet. This copy is the fixture's
simulated wire, not a production backend copy. Stop peer producers before
draining queues and releasing remaining mbufs/rings.

Provide private shared ring/pcap setup, independent peer progress and cleanup in
zxdp/itest/zxdpdkfixture.hh/.cc. Set fixed software device names/MACs and build
pcap input/output for byte verification. Run EAL initialization variants in fresh
subprocesses; do not rely on reinitializing process-global EAL between test cases.
No physical NIC/VFIO binding is required for software acceptance.

| Existing scope | New DPDK driver | Required adaptation |
| --- | --- | --- |
| Pool allocation/lifetime | ZxdpDpdkPoolTest | mbuf identity, geometry, exhaustion/return, retained backing |
| Device open/alloc/start/stop/final | ZxdpDpdkInstanceTest | explicit EAL, open without pools, typed pool set at alloc, structured failures, reuse after final |
| Native packet I/O | zxdpdkpackettest | direct submission, Rx pool provenance, exact bytes, independent retirement |
| Wake/service | zxdpdkwaketest | counted wakes, sustained Rx and configurable bounded idle; no Xdp sender assertion |
| Loopback transfer | zxdpdklooptest | independent software peer, normal send/receive and teardown |
| BPF classifier/filter | no DPDK copy | remains Xdp-specific |

In Zutcp add zutcpdpdktest.cc with its private fixture helper
zutcpdpdkfixture.hh/.cc. Use paired named ring ports and fixed Linux dummy
control-plane interfaces/routes/permanent neighbors in isolated network namespace
setup. Configure their MAC/MTU to match their DPDK ports. Mirror the repaired
zutcpmxtest's applicable connection, bidirectional bytes, shared ownership,
start/stop/restart and pool retirement scope through ZiMultiplex. Test the
following boundaries using the DPDK instance and normal multiplexer entry points.

* Inject synchronous device-open failure and verify any acquired port claim is
  released. Check successful discovery before either ownership transfer. Exercise
  idempotent init, recoverable conflict and fatal handoff of a preallocated device
  in a separate process.
* Request multiple typed Rx handles and multiple Tx pools, including two of the
  same role with different geometry. Check the single ingress pool, distinct Tx
  identities and immutable roles. Before alloc, typed stubs have no backing and
  native Tx slots are unavailable. Exercise explicit allocation and allocation
  from start. Inject partial allocation failure, verify rollback and rejection
  of late allocator requests (including App), then retry the original set.
* After explicit alloc, write a Tx buffer before start. Hold it through failed
  activation, successful start, stop and restart. Check its address, contents,
  original allocator and occupied slot, then return it. Check finalization without
  start both before and after allocation, exclusive ownership through stop, and
  same-port acquisition after final.
* Run TCP-only, UDP-only and mixed traffic on the repaired plugin. Verify Ztcp
  byte transfer, UDP datagram boundaries and peer addresses. Verify the selected
  typed Rx allocator on each endpoint, including accept and reconnect, and common
  ingress backing return. Stop an idle receiver without injecting a packet.

Keep AF_XDP drivers unchanged except existing regression expectations legitimately
shared by status/selection. DPDK acceptance uses software fixtures. Phase 5
benchmarks cover the kernel TCP and Zutcp/AF_XDP paths.

## DPDK implementation file boundary

Create:

* zxdp/src/ZxdpDpdk.hh, zxdp/src/ZxdpDpdk.cc
* zxdp/src/ZxdpDpdkInstance.hh, zxdp/src/ZxdpDpdkInstance.cc
* zxdp/src/ZxdpDpdkPool.hh, zxdp/src/ZxdpDpdkPool.cc
* zxdp/test/ZxdpDpdkPoolTest.cc, zxdp/test/ZxdpDpdkInstanceTest.cc
* zxdp/itest/zxdpdkfixture.hh, zxdp/itest/zxdpdkfixture.cc
* zxdp/itest/zxdpdkpackettest.cc, zxdp/itest/zxdpdkwaketest.cc,
  zxdp/itest/zxdpdklooptest.cc
* zutcp/itest/zutcpdpdktest.cc, zutcp/itest/zutcpdpdkfixture.hh,
  zutcp/itest/zutcpdpdkfixture.cc

Modify:

* configure.ac, z.config: optional dependency, flags and feature conditional.
* zxdp/src/Zxdp.hh, zxdp/src/Zxdp.cc: selector/union/factory/status, Xdp override.
* zxdp/src/Makefile.am: conditionally install EAL header, compile private DPDK
  sources, apply DPDK flags/linking only when enabled; keep Zi dependency.
* zxdp/test/Makefile.am, zxdp/itest/Makefile.am: conditional programs/fixture builds.
* zutcp/itest/Makefile.am: conditional DPDK driver/fixture; Ztcp is a test consumer
  dependency, not a new libZutcp dependency.
* zxdp/README.md, zxdp/GUIDELINES.md, zxdp/INTERFACE.md,
  zxdp/test/README.md, zxdp/itest/README.md: actual API/setup/test commands.
* zutcp/README.md, zutcp/GUIDELINES.md, zutcp/itest/README.md,
  zutcp/util/README.md: typed static DPDK bootstrap and existing routing model.

Regenerate configure, config.h.in and affected Makefile.in files using repository
practice; track generated outputs only as this repository normally does. Do not
modify Zi/Ztcp/Ztls, generic App dispatch, existing application examples or the
repaired protocol algorithms for this backend addition. Existing static AF_XDP
bootstrap remains valid; DPDK-specific bootstrap belongs in its new fixture and
documented example configuration. No additional public pool APIs are authorized.

## Buildable DPDK checkpoints

1. Add optional dependency wiring, EAL declarations/implementation and explicit
   EAL tests in the new instance driver, without exposing an instantiable DPDK
   Type yet. Enabled and disabled configurations must build with make -j8.
2. Add Type/Params/status and the complete private pool/instance overrides and
   factory branch together. Add pool/instance tests and their conditional wiring.
   Keep each virtual interface/factory addition buildable; no placeholder backend
   that reports success. Run existing Xdp tests too.
3. Add shared software fixtures and packet/wake/loop tests. Verify accepted-work
   retirement and receiver pool identity as well as bytes. Supply runnable exact
   commands and visible setup failures in README; skips are not acceptance.
4. Exercise the repaired TCP/UDP allocation, activation and restart paths with the
   DPDK device in the Zutcp integration fixture.
   Run affected suites and update docs.

## Rebuild and verification sequence

Use Clang release builds configured through `z.config`. Reuse compatible build
products and batch rebuilds. Follow the [build guidance](../../docs/BUILD.md)
for a clean rebuild when establishing the release configuration or changing
build mode. At each DPDK checkpoint refresh the dependency closure with a
top-level build. Build changed libraries before their consumers and build each
test directory's default target before `make -C <module>/test test`.

Delegate independent work to subagents under one shared GNU make jobserver with
eight slots total. Start the jobserver with `make -j8` and have recursive make
calls inherit its slots. ASan, LSan, GCC,
MinGW and ARM are outside this rebuild matrix. Run listeners, native fixtures
and debugging tools outside the sandbox with the required access. Use Valgrind
through `libtool exec` for memory corruption diagnosis. Work autonomously with
the available authorization. At each phase exit remove temporary test and crash
residue from `/tmp` and the worktree.

Diagnose each failure to its root cause, repair it in the owning phase and rerun
the affected checks before closing that phase. The DPDK implementation file
boundary applies to the backend addition. Baseline repairs belong to their
rebuild phase, and completed Phases 1–3 freeze their modules for later work.

### Phase 1 — Foundation

Build `zu`, `zm`, `zt`, `ze`, `zf`, `zi` and `zv`, then run each module's
`make -C <module>/test test`. Freeze these modules when the phase passes.

### Phase 2 — Ztcp

After Phase 1 passes, build `ztcp` and run `make -C ztcp/test test`. Freeze
`ztcp` when the phase passes.

### Phase 3 — Zdbus and Ztls

After Phase 1 passes, build `zdbus` and `ztls`. Run `make -C zdbus/test test`
and `make -C ztls/test test`. Phase 3 can run alongside Phase 2. Freeze both
modules when Phase 3 passes.

### Phase 4 — Zxdp and Zutcp

After Phases 2 and 3 pass, build `zxdp` and `zutcp` and run their unit suites.
Apply the DPDK checkpoints in order. Validate `--with-dpdk=no`,
`--with-dpdk=yes` with supported `libdpdk`, and configuration failure for
explicit `yes` when `libdpdk` is absent. In the final enabled build, run
`make -C zxdp/test test` and `make -C zutcp/test test` and run the new DPDK
integration programs through their documented software and namespace fixtures.
Rerun the applicable AF_XDP drivers from the
[Zutcp integration README](../../zutcp/itest/README.md).

Required evidence: structured missing-EAL/unsupported-build/port-busy/init failure,
correct default Xdp selection, direct mbuf submission and caller/device lifetime,
false-send ownership, receiver pool provenance, counted wakes and idle interval,
open rollback, frozen pool-set retry after allocation failure, activation retry,
stable native backing across restart, per-operation typed Rx delivery,
exclusive binding through stop and same-port reuse after finalization, and the
registration-time MTU bound including smaller Tx requests. Mirror the existing
behavioral scope.

### Phase 5 — Zfix transport comparison

Once Phases 2 and 3 pass, build `zfix` and run its kernel-TCP suite with
`make -C zfix/test test`. This kernel-only work may overlap Phase 4 when the
build outputs it uses are stable. Serialize reconfiguration and rebuilds that
write to the same build tree. Start the Zutcp/AF_XDP runs and transport
comparisons after Phase 4 passes.

1. Build and run the equivalent Zfix suite using `zutcp` through the `zxdp`
   AF_XDP backend.
2. Build and run the Zfix interoperability and benchmark fixtures for kernel
   TCP and Zutcp/AF_XDP. Report latency and throughput for one connection and
   for multiple connections under comparable workloads. See the
   [Zfix interop README](../../zfix/interop/README.md) and the
   [Zfix benchmark README](../../zfix/bench/README.md).
3. Report latency and throughput for all four client/server pairings:
   Zfix/Zfix, Zfix/QuickFIX, QuickFIX/Zfix and QuickFIX/QuickFIX. Record the
   transport, workload and connection count for each result.

Record actual TAP, integration, build and benchmark results as the phases
finish. Require warning-free builds and finish with `git diff --check`.
