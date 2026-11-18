# Zxdp DPDK implementation plan

Status: finalized against the repaired Zutcp working tree. DPDK remains
unimplemented. This is the execution plan; concepts_brief.md retains the adopted
design and explicit EAL configuration declaration. dpdk.md and dpdk_loopback.md
are historical conversation/reference material, not competing specifications.

## Baseline and fixed boundaries

Use the current source, including the completed zutcp_repair3 repair and later
header/implementation consolidation and explicit init(Params) setup API.
Zutcp.hh/Zutcp.cc declare and implement Instance, which owns both connection
directions and listeners and implements ZiMxPlugin's Ztc::Mx interface. There is
no separate Hub, Client, Server or nativeInstance layer. Static bootstrap
constructs Instance with the mx, calls init(Params), checks its structured result,
then installs it on the stopped mx before allocator registration. Transfer code
uses Ztcp/Ztls through ZiMultiplex. Do not introduce module loading or redo
this repair. Preserve all existing working-tree changes and their test artifacts.

Zxdp already has the cold instance(Type::Xdp) factory, virtual bufPool/init/send/
rx/wake/pollTx/stopTx/close/allQueues, the Params tagged union and private Xdp
pools/UMEM. Extend those interfaces. No InstanceData, commitFn, external-buffer
attachment adapter, shared UMEM/mbuf allocator, Rx-only instance or intermediate
DPDK Tx submission queue is needed. Zi::IOBufPool stays geometry plus alloc/free;
no pool occupancy/writable/wait/cancel API or backing-based TCP receive accounting.

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

EAL is discovered through
ZmSingleton<Dpdk::EAL, ZmSingletonCleanup<ZmCleanup::Platform>>. Its constructor
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
    unsigned rxInterval = 50;  // maximum idle park in microseconds; zero polls
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
zeroCopy(); do not restore public ifindex(). zeroCopy reports actual operation:
classify the configured PMD's data path, not just the Dpdk selector. Software
pcap copies; direct ring forwarding does not. A fixture's simulated wire copy is
outside the production instance's data path. Do not claim zero copy for an
unverified copy-capable PMD.

## 3. Private pools, device ownership and packet service

Create private ZxdpDpdkInstance.hh/.cc and ZxdpDpdkPool.hh/.cc. BufPool derives
from Zi::IOBufPool and provides native mbuf backing; its effective count/size/
headroom/tailroom/alignment are the supplied geometry. Defer physical pool setup
until instance init so stopped configuration, including control count changes,
is frozen before materialization. Native pools must remain alive through final
caller-held backing return, independently of the factory/instance handle.
Disable per-lcore mbuf caching initially so mbufs returned on other threads are
available to the bounded pool rather than stranded in another thread's cache.

Resolve name once with rte_eth_dev_get_port_by_name and retain its numeric port
privately. EAL owns port registration/lifetime; Instance acquires exclusive use.
Reject concurrent instance binding with ZeException. Configure one Rx and one Tx
queue for the existing owners. Use the supplied Rx mempool for Rx replenishment;
require at least one nonempty Tx pool. Derive queue capacities from supplied
counts subject to PMD descriptor limits/adjustment. No public descriptor-count
knob, extra reserve pool or half-pool partition is introduced.

Query device MTU/MAC and configure for the existing contiguous-frame contract.
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
metadata path. Zutcp attaches an IOBuf stub to those bytes; final return reaches
the original pool. Do not add a payload copy to production Rx. Register existing
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

stopTx() gates submission and honors txStopped completion without a sender
thread. Keep completion progress alive while draining. Follow docs/IO.md: gate
new Rx/setup work, complete the Rx continuation, then drain Tx/accepted device
work on Tx and retire queues. close() returns true/pending/ZeException under the
existing retry contract. On success stop the port and release exclusive use;
ordinary close does not destructively close/remove the EAL-owned device.
Failed-init rollback unwinds only acquired instance resources, retains the handle
while pending, and keeps externally configured devices available for reuse.
A subsequent instance can reconfigure the stopped port after prior retirement.

allQueues() traverses actual configured queues/rings and normalizes available PMD
stats/reads into Ztc telemetry. Unsupported individual stats remain unavailable,
not fabricated. Reuse existing traversal/lifetime conventions; no extra queue
registry, index or cache exists solely for observation. Diagnostic reads do not
require accurate snapshots or new hot-path synchronization.

## 4. Repaired Zutcp integration: exact configuration

Preserve the unified InstanceParams and all repaired TCP behavior; do not restore
InstanceConfig or a second configuration object. In Zutcp.hh add:

```cpp
struct DpdkParams {
  ZtString<> name;
  unsigned rxInterval = 50; // microseconds; zero polls
};

struct InstanceParams {
  unsigned	interface = 0; // Linux control-plane interface in both modes
  Zxdp::Type::T type = Zxdp::Type::Xdp;
  DpdkParams dpdk;
  ZuCSpan	netmask;
  PacketFn	packet;
  unsigned	spin = 1000;
  unsigned	packetCount = ZiIOBuf_DefltCount;
  ZmSchedTParams	txThread = ZmSchedTParams().isolated(true);
  unsigned	txSid = 0;

  unsigned	controlBufCount = DefaultControlBufCount;
  unsigned	txControlCredits = DefaultTxControlCredits;
  unsigned	rxRetainFramesPerCxn = DefaultRxRetainPerCxn;
  uint64_t	maxData = DefaultMaxData;
  unsigned	maxCxns = DefaultMaxCxns, maxPendingCxns = DefaultMaxPendingCxns;
  unsigned	closedTupleRingSize = DefaultClosedTupleRingSize;
  unsigned	tfoCookieCacheSize = DefaultTFOCookieCacheSize;

  ZuTime	connectTimeout{DefaultConnectTimeout};
  ZuTime	userTimeout{DefaultUserTimeout};
  ZuTime	closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime	finWaitTimeout{DefaultFinWaitTimeout};
  ZuTime	ackHoldTimeout{0, 25000000};
  bool		congestionControl = true;
  uint64_t	maxIdleTimeout = 0;
  ZuTime	heartBeat;
  unsigned	maxPayload = DefaultMaxPayload;
  RtoParams	rtoParams;
  ZuTime	tlpPeerAckDelay{0, 200000000};

  bool		sackPermitted = true, windowScale = true, timestamps = true, ecn = false;
};
```

Include the direct ZtString dependency for the owned DPDK name. Preserve the
current aggregate's remaining member ordering/defaults and Instance::Params alias.
Keep the current public setup interface:

```cpp
explicit Instance(ZiMultiplex *);
ZuUnion<void, ZeException> init(Params params);
```

There is no configure(), config(), params() or public configuration getter. Do
not restore one, a constructor taking Params, InstanceConfig, or separate backend
setting members. The existing m_params retains the accepted aggregate internally.
The cold constructor remains backend-neutral and creates no native pools or
DPDK/EAL resources. An uninitialized object must remain safely destructible.
Bootstrap calls init while the mx is stopped, checks the result, then installs
the plugin before any application allocator registration.

Extend the current init(Params config) validation/resource-creation branch:

* Before resource creation, validate the incoming backend-specific settings.
  Dpdk requires prior explicit EAL initialization and a nonempty config.dpdk.name;
  it has no txSid/txThread requirement. A nonempty Xdp netmask is an unsupported
  Dpdk filter configuration error. These are returned structured init failures,
  not constructor errors or silent fallback.
* When m_ingress is absent, select instance(config.type), using the incoming
  config rather than the not-yet-committed m_params. Xdp keeps current interface/
  queue discovery. Dpdk ingress count is mx->rxBufSize() when nonzero, otherwise
  ZiIOBuf_DefltCount; do not call Xdp::queryLink() for DPDK provisioning.
* Preserve the current Linux interface MTU/secret setup and create ingress,
  packet and control providers locally using the selected bufPool(). Physical
  ingress is MTU/frame-sized; internal packet and control counts come from the
  incoming packetCount/controlBufCount. Catch ZeException and return it through
  init's existing error branch. Keep the current local-resource/adoption ordering.
* Construct the existing Allocators aggregate in m_allocators only after the
  local providers exist. Preserve ZuUnion<void, Allocators>, its single packet,
  data, control and Rx adapters, addProvider accounting and ingress-return wake
  callback. Do not restore eager m_rxAllocator/m_packetAllocator/m_dataAllocator/
  m_controlAllocator members or construct native resources in the public ctor.
* Apply existing initControl, history/cookie sizing and owned-netmask setup, move
  the accepted config into m_params and rebind its netmask span as today. Type
  and the owned dpdk fields travel with that move; no second retained copy is
  needed. Keep all current policy validation and control-credit ordering.

Repeated init while stopped follows the current retained-pool contract. Keep its
fixed interface/packetCount checks and extend that identity guard to type and,
for Dpdk, dpdk.name. Reject attempted backend/device changes before changing
resources or m_params; changing them requires a fresh Instance. Other settings,
including dpdk.rxInterval, are accepted through the same init entry and take
effect on the next start. Do not silently retain an old idle interval or preserve
old parameters using the removed configure() next-copy scheme.

Preserve the private injected-pool test constructor and its existing no-kernel
semantics. Extend ZutcpTest::Instance::apply to carry the selected type and dpdk
bootstrap settings, as it already carries interface/pool/sender settings, so
policy fixtures do not accidentally request a backend switch. Test explicit idle
updates through init with the retained private parameter snapshot. These remain
private fixture facilities, not production accessors or a second owner API.

For static DPDK bootstrap, after successful explicit EAL initialization:

```cpp
ZmRef<Zutcp::Instance> plugin = new Zutcp::Instance{&mx};
auto initialized = plugin->init({
  .interface = routeIfindex,
  .type = Zxdp::Type::Dpdk,
  .dpdk = {.name = "net_ring0", .rxInterval = 50}
});
// Check initialized; only success permits mx.plugin(plugin) and allocator setup.
```

Existing AF_XDP bootstrap continues to construct with &mx and call init with
its current designated settings and default Xdp selection. No loader or separate
application-facing connection API is introduced.

Keep Instance::allocator's current registration-time bound:
min(requested payload size + protocol headroom, MTU + Ethernet overhead).
Smaller requests remain smaller. Zi copies the bounded effective pool geometry,
so default allocBuf() calls need no new per-call MTU limit. Do not add generic
pool queries or an allocation-time size API.

In start_(), select the matching Zxdp parameter union alternative. Populate
common rxPool/txPools/rx callback/spin/scheduler/txStopped identically; add Xdp's
ifindex/netmask/txSid or Dpdk's name/rxInterval in its branch. Retain independent
pool references and let temporary configuration-list membership expire as today.
Publish readiness only after successful init and matching established MTU; for
DPDK also verify the represented Linux link MAC matches the packet port. Return
structured setup failure and run normal rollback if geometry differs.

On restart recreate instance(m_params.type), not instance(Type::Xdp), and rebuild
the same branch from retained settings. Reuse pools only when compatible with prior
retirement and outstanding caller-held backing. Preserve existing error/close
retries, provider lifetimes, watch callbacks, shared policy, secrets and tuple
history. Keep the synchronous Rx-owned resolve(remote, local) result; it needs
no worker dispatch or asynchronous compatibility adapter. No new transport
owner or lifecycle controller is introduced.

Keep the repaired Instance::resolve(remote, local) -> ZuUnion<IPPath, ZeException>
on mx Rx. Reuse Linux routes and permanent neighbor mappings, including explicit
next-hop MAC for routed destinations. A Linux control-plane interface can
represent a userspace-owned DPDK link; configure matching addresses/MAC/MTU and
associate it with the named DPDK port at bootstrap. It need not forward packets.
No ARP/ND feature, exception path, custom route resolver or non-I/O route worker
is required for this existing static-neighbor contract.

## 5. Zdb network allocation and failure

In zdb/src/ZdbBuf.hh change the mx overload of tableBufAllocFn<T>() from App to
TxTCP, preserving table-requested size/alignment/heap/count. Guard allocBuf()'s
null result before assigning skip with `if (!buf) return nullptr;`. Preserve
localBufAllocFn and the standalone local allocator. Existing storage/header/table
null handling remains authoritative; do not add waiting or heap fallback.

In zdb/test/ZdbFBTest.cc add a finite plugin-backed pool test using a local test
ZiMxPlugin/IOBufPool implementation. It can implement unused operations as test
stubs; it must genuinely exercise the mx allocator dispatch and callback. Keep
this helper in that test source, without linking Zdb tests against Zutcp/DPDK.
Assert TxTCP dispatch, requested geometry handed to the plugin, effective bounded
geometry, null on exhaustion before skip access, success after return, and
unchanged standalone allocation. Inspect zdbsagareptest's App/RxTCP receive and
reassembly assertions; change only expectations actually affected by outgoing
role selection. Do not mechanically replace valid App receive/reassembly uses.

## 6. Software fixtures and coverage

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
zxdp/itest/zxdpdpdkfixture.hh/.cc. Set fixed software device names/MACs and build
pcap input/output for byte verification. Run EAL initialization variants in fresh
subprocesses; do not rely on reinitializing process-global EAL between test cases.
No physical NIC/VFIO binding is required for software acceptance.

| Existing scope | New DPDK driver | Required adaptation |
| --- | --- | --- |
| Pool allocation/lifetime | ZxdpDpdkPoolTest | mbuf identity, geometry, exhaustion/return, retained backing |
| Instance init/close | ZxdpDpdkInstanceTest | explicit EAL, mandatory Tx, structured failures, close/recreate |
| Native packet I/O | zxdpdpdkpackettest | direct submission, Rx pool provenance, exact bytes, independent retirement |
| Wake/service | zxdpdpdkwaketest | counted wakes, sustained Rx and configurable bounded idle; no Xdp sender assertion |
| Loopback transfer | zxdpdpdklooptest | independent software peer, normal send/receive and teardown |
| BPF classifier/filter | no DPDK copy | remains Xdp-specific |

In Zutcp add zutcpdpdktest.cc with its private fixture helper
zutcpdpdkfixture.hh/.cc. Use paired named ring ports and fixed Linux dummy
control-plane interfaces/routes/permanent neighbors in isolated network namespace
setup. Configure their MAC/MTU to match their DPDK ports. Mirror the repaired
zutcpmxtest's applicable connection, bidirectional bytes, shared ownership,
start/stop/restart and pool retirement scope through ZiMultiplex. Verify failed
DPDK setup is a structured init result and a cold Instance is safely destroyed;
first init creates the selected native pools; repeated stopped init rejects
backend/device changes, permits an idle interval update and retains pool identity;
restart uses the retained type/name/latest interval. Inspect internal parameters
only through existing private test access, not a restored public getter. Verify
init controlBufCount/txControlCredits reach the providers/Allocators aggregate
before backend start freezes pool geometry. Include Ztcp application transfer
using zutcpapptest's established pattern. Do not copy its
Xdp-specific sender/filter assertions or reintroduce the removed public API.
Keep AF_XDP drivers unchanged except existing regression expectations legitimately
shared by status/selection. Software acceptance does not prove NIC DMA or wire
performance; no new benchmark or physical-NIC acceptance project belongs here.

## Exact file boundary

Create:

* zxdp/src/ZxdpDpdk.hh, zxdp/src/ZxdpDpdk.cc
* zxdp/src/ZxdpDpdkInstance.hh, zxdp/src/ZxdpDpdkInstance.cc
* zxdp/src/ZxdpDpdkPool.hh, zxdp/src/ZxdpDpdkPool.cc
* zxdp/test/ZxdpDpdkPoolTest.cc, zxdp/test/ZxdpDpdkInstanceTest.cc
* zxdp/itest/zxdpdpdkfixture.hh, zxdp/itest/zxdpdpdkfixture.cc
* zxdp/itest/zxdpdpdkpackettest.cc, zxdp/itest/zxdpdpdkwaketest.cc,
  zxdp/itest/zxdpdpdklooptest.cc
* zutcp/itest/zutcpdpdktest.cc, zutcp/itest/zutcpdpdkfixture.hh,
  zutcp/itest/zutcpdpdkfixture.cc

Modify:

* configure.ac, z.config: optional dependency, flags and feature conditional.
* zxdp/src/Zxdp.hh, zxdp/src/Zxdp.cc: selector/union/factory/status, Xdp override.
* zxdp/src/Makefile.am: conditionally install EAL header, compile private DPDK
  sources, apply DPDK flags/linking only when enabled; keep Zi dependency.
* zxdp/test/Makefile.am, zxdp/itest/Makefile.am: conditional programs/fixture builds.
* zutcp/src/Zutcp.hh, zutcp/src/Zutcp.cc: settings and init/start/restart branches.
* zutcp/test/ZutcpTestInstance.hh: carry native bootstrap identity through its
  existing private policy fixture helper; no public getter or alternate owner.
* zutcp/itest/Makefile.am: conditional DPDK driver/fixture; Ztcp is a test consumer
  dependency, not a new libZutcp dependency.
* zdb/src/ZdbBuf.hh, zdb/test/ZdbFBTest.cc: outgoing role/null guard/finite tests.
* zdb/itest/zdbsagareptest.cc only if affected outgoing-type assertions need repair.
* zxdp/README.md, zxdp/GUIDELINES.md, zxdp/INTERFACE.md,
  zxdp/test/README.md, zxdp/itest/README.md: actual API/setup/test commands.
* zutcp/README.md, zutcp/GUIDELINES.md, zutcp/itest/README.md,
  zutcp/util/README.md: typed static DPDK bootstrap and existing routing model.
* zdb/test/README.md: plugin-backed table allocation coverage.

Regenerate configure, config.h.in and affected Makefile.in files using repository
practice; track generated outputs only as this repository normally does. Do not
modify Zi/Ztcp/Ztls, generic App dispatch, existing application examples or the
repaired protocol algorithms for this backend addition. Existing static AF_XDP
bootstrap remains valid; DPDK-specific bootstrap belongs in its new fixture and
documented example configuration. No additional public pool APIs are authorized.

## Buildable phases and verification

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
4. Add typed Zutcp selection/start/restart and DPDK integration tests, then Zdb's
   role/null guard and finite-pool tests. Run affected suites and update docs.

Use z.config for configuration; do not mix build modes. At each checkpoint use
top-level make -j8 to refresh dependency closure. Validate --with-dpdk=no,
--with-dpdk=yes with supported libdpdk, and explicit yes with libdpdk absent.
In enabled builds run make -C zxdp/test test, make -C zutcp/test test and
make -C zdb/test test after their targets are built. Run the new DPDK itest
programs through their documented namespace fixture; also rerun applicable
existing AF_XDP native fixtures from zutcp/itest/README.md. Run
prove -j1 zdb/itest/zdbsagareptest after its dependencies are built. Keep optional
Ztcp/Ztls consumer verification scoped to the affected tests.

Required evidence: structured missing-EAL/unsupported-build/port-busy/init failure,
correct default Xdp selection, direct mbuf submission and caller/device lifetime,
false-send ownership, receiver pool provenance, counted wakes and idle interval,
normal close and failed-init rollback, same-port recreation and Zutcp restart,
retained backend identity and accepted idle updates through stopped init,
registration-time MTU bound including smaller Tx requests, and finite Zdb pool
exhaustion/recovery. Mirror existing behavioral scope rather than adding a new
protocol validation matrix. Record actual TAP/build results separately from
these planned checks. Finish with git diff --check and warning-free builds.
