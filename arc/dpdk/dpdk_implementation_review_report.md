# DPDK Implementation Review — Resumed Resolution

**Target:** [concepts_brief.md](concepts_brief.md) and the finalized execution plan
[dpdk_plan.md](dpdk_plan.md).
**Status:** Finalized against the repaired Zutcp source. All recorded planning
findings are resolved in dpdk_plan.md. DPDK implementation and its runtime
verification remain future work; no further Goldfish audit is claimed.

The original three fresh-context reviews completed with 13 Major findings,
consolidated into eight actions. Their original line references apply to
[the reviewed snapshot](review_context/zxdp-dpdk-readiness/reviewed_concepts_brief.md).
The generic report filenames now document a separate I/O repair; this scoped
report preserves the DPDK review without replacing those artifacts.

## Binding corrections

* Include Zutcp DPDK selection, startup and restart integration.
* Zdb table replication should request TxTCP through ZiMultiplex; this change
  remains pending in the current Zdb caller. Keep table-requested sizing and
  transport MTU constraints.
* The source has already removed obsolete pool queries and waiter APIs. Public
  Zi::IOBufPool is geometry plus alloc/free only. Do not reintroduce writable,
  slots, occupancy/status queries, wait/cancel or IOBufWait.
* Receive accounting is a counted per-connection intrusive Zi::IOQueue of
  Zi::IOBuf nodes, with head insertion and tail eviction at the configured cap.
  No backing-slot accounting or substitute permission/wait protocol.

## Action status

| Action | Status | Remaining work |
| --- | --- | --- |
| A1: exact file inventory | specified | Backend/build/test paths and affected consumer callers are enumerated in the brief. |
| A2: buildable phases | specified | Four buildable checkpoints are in the brief. |
| A3: queue provisioning | closed | Routine pool-derived queue setup subject to driver requirements; the entire Rx pool is available to the PMD. No reserved subset or new application sizing policy. |
| A4: port lifecycle | resolved | EAL owns devices; instance exclusively uses a port, asynchronously gates/drains Rx then Tx per docs/IO.md, stops/releases it for reuse; destructive port close belongs to final EAL teardown. |
| A5: receive segments | closed as separate design issue | Existing contiguous-frame contract applies. Size usable mbuf capacity for MTU/frame overhead and leave scatter/coalescing disabled; ordinary setup, not a new receive feature or rejection subsystem. |
| A6: fixture mapping | specified; ownership resolved | Named backend coverage is in the brief, using the adopted destination Rx-pool fixture ownership. |
| A7: consumer scope | resolved | Include Zutcp backend startup/restart and Zdb TxTCP. Preserve Linux routing/static neighbors with an explicit routing-interface/DPDK-port association; no new resolver or ARP/ND feature. |
| A8: dependency policy | resolved | Optional like PostgreSQL: configure --with-dpdk, wrapper default yes and -K opt-out; enabled requires libdpdk >=25.11 and fails clearly if missing. Conditional backend/test builds. |

## Original finding traceability

### A1

* `readiness-1-1` — Major: Exact change boundary is missing.
* `codebase-1-1` — Major: Exact change boundary is unspecified.
### A2

* `readiness-1-2` — Major: Numbered steps do not establish buildable checkpoints.
* `codebase-1-6` — Major: Numbered tasks lack stable phase checkpoints.
### A3

* `readiness-1-3` — Major: Pool-derived provisioning lacks an executable capacity rule.
### A4

* `readiness-1-4` — Major: Port retirement and instance recreation lack a lifecycle decision.
* `codebase-1-3` — Major: Port retirement and instance recreation need an explicit lifecycle decision.
### A5

* `readiness-1-5` — Major: Receive representation needs an explicit segment policy.
### A6

* `readiness-1-6` — Major: Coverage inheritance needs an explicit DPDK test mapping.
* `codebase-1-4` — Major: Mirror coverage does not specify necessary fixture adaptations.
* `style-1-1` — Major: Inherited testing scope is ambiguous.
### A7

* `codebase-1-2` — Major: Caller changes omit the existing AF_XDP-only selection boundary.
### A8

* `codebase-1-5` — Major: Dependency integration still requires build-policy choices.

Raw normalized per-lens reports, findings.json and approved external snapshots
remain in review_context/zxdp-dpdk-readiness. Their assumptions must be interpreted
under the binding corrections above. The selected architecture is unchanged.

## Work performed on resume

Read the current Zi pool interface, Xdp pool internals, Zutcp retained-buffer
queue and allocation/provider paths, Zdb table allocation, source history and
prior review artifacts. Corrected the brief's obsolete writable/interface
assumptions. No C++ source edits, builds or tests were performed.

## Adopted dependency resolution

The author selected optional DPDK using the PostgreSQL precedent. The brief now
specifies configure control, pkg-config, wrapper control/defaults, conditional
source/test compilation and enabled/disabled validation. Existing AF_XDP-only
builds remain possible. This resolves A8; source changes remain unimplemented.

## Receive-segment clarification

The earlier A5 presentation overstated an implementation setup detail as a new
design decision. With usable mbuf capacity fitting the configured MTU/frame
overhead and scatter/coalescing disabled, normal Rx does not require chaining.
No additional oversized/chained receive policy or copying path is requested.
Do not expand testing merely to satisfy this withdrawn design question.

## Author clarification: pool sizes

Rx pool buffers are always MTU-sized. Configured Tx sizes may be below or
above MTU. Clamp during buffer allocation so the application never sees a
buffer exceeding MTU; do not clamp the configuration itself. Queue provisioning
must not assume uniform sizes, infer MTU from a Tx configuration or force all
Tx requests to MTU. The receive-segment question stays closed.

## A4 adopted resolution: reusable ports

* `Dpdk::EAL` owns the configured device registrations for its lifetime.
* An instance acquires exclusive use of one named port; concurrent binding of
  another instance to that port fails with a structured error.
* Instance teardown follows docs/IO.md: gate new ingress/timer/I/O work and
  post an Rx drain continuation; from Rx post a Tx drain continuation; drain
  queued Tx and accepted device work on Tx, then complete queue retirement,
  stop the port and release exclusive use. Keep completion progress alive while
  closing. Return pending/error until safe retirement; do not block I/O owners
  or discard accepted work in place of draining. Caller-held backing remains
  valid independently; raw owner back-pointers are protected by drain ordering.
* Ordinary instance close does not call destructive `rte_eth_dev_close()`,
  remove the named device, or finalize EAL. A later instance can configure/start
  the same port with its supplied pools. Retain backing needed by any retained
  device queue state until that state is reconfigured or finalized.
* Failed initialization unwinds only the instance's acquired resources; it does
  not remove the configured device. It preserves its handle for pending rollback.
* Final EAL teardown closes its stopped ports before `rte_eal_cleanup()`.
* Software fixtures stop producers/peers and drain queued mbufs before releasing
  their rings; ring stop alone does not perform that drain.

Status: adopted with author-required asynchronous draining. This records port ownership/reuse,
not a change to the public pool interface or backing-based receive accounting.

## A3 correction: mirror AF_XDP receive flow

The half-pool descriptor proposal and Rx-only/zero-Tx-queue provision are
withdrawn. Both backends require at least one nonempty Tx pool. The current
filter/wake tests omitted Tx setup; they did not establish a separate public
Rx-only mode. Preserve their independent assertions with valid Tx provisioning.

DPDK pools provide native mbuf backing with configured count/size and allocation-
time MTU constraints. Rx packets arrive into pool backing, RecvFn supplies its
pointer/length, and Zutcp constructs IOBuf metadata referring to it. Final return
uses the original allocator/pool. Mirror the existing AF_XDP ownership flow.
A driver descriptor is a queue entry referencing backing, not a second pool.
No arbitrary half-pool partition or backing-based transport accounting is adopted.

Verified source: ZiMultiplex::allocator, Stack::allocator and ingress setup,
Xdp::Instance::replenish/receiveQueue, Stack::received, IOBufPoolAllocator and
RxAllocator::free. DPDK is not yet implemented; its documented Rx API supplies
mbufs from the queue's configured mempool and replenishes from that same pool.
The current Stack::allocator still clamps bytes before bufPool; the intended
allocation-time-only clamping must be carried through DPDK/Zutcp integration
rather than claimed as already verified in that implementation.

## Mandatory Tx provisioning and receive-flow verification

Implemented the requested nonempty Tx requirement for AF_XDP instance and arena
initialization. The DPDK plan carries the same requirement. Existing filter/wake
fixtures now supply a Tx pool; there was no dedicated successful Rx-only feature
test to retain or delete. Missing-Tx provisioning is now rejected in the pool
unit suite. Independent filter and wake behavior coverage remains intact.

The packet integration run exposed a repeatable asynchronous rebind race before
its final lease subtest. Its main-thread setup now follows the wake fixture's
existing bounded init/close/retry pattern; production teardown and packet service
were not changed for that race. The initial temporary namespace harness also
used host sysfs for interface indices; the corrected harness uses namespace
netlink via ip. Those failed attempts are not counted as passing verification.

Final checks:

* Top-level `make -j8`: passed.
* `make -C zxdp/test test`: passed, 4 programs / 14 top-level TAP tests.
* Native packet/filter/wake tests under sudo in an anonymous two-queue-veth
  namespace: passed, 3 programs / 7 top-level TAP tests.
* `git diff --check`: passed.

Flow verified by source inspection: backend Rx pool backing is posted to the
kernel/driver, completed reception calls RecvFn with the backing pointer/length,
Zutcp constructs IOBuf metadata referencing those bytes, and final release
returns through the original Rx allocator/ingress pool. DPDK will mirror this
using native mbufs; no DPDK implementation or runtime validation exists yet.
The driver descriptor references the backing; it is not another application
packet buffer. The half-pool split remains withdrawn.


## Continued review after Zutcp::Instance rename

At that review the multiplexer plugin had been renamed `Zutcp::Instance`, with
`InstanceParams`, `InstanceConfig` and a `nativeInstance()` bridge. The later
Zutcp ownership repair removes that bridge. References to Stack
in earlier review evidence describe the old name. The DPDK backend remains
unimplemented. This continuation inspects the remaining findings; it is not a
fresh Goldfish audit or a convergence claim. No source changes or new runtime
checks were made during this continuation.

### A3: closed as routine queue setup

The entire supplied Rx pool is available to the PMD. Descriptor count controls
queue depth, not permission to use a subset of mbufs. Derive queue setup from
pool geometry subject to the selected driver's requirements. No reserved subset,
half-pool partition, additional application knob or backing-based TCP receive
accounting is needed. Later exhaustion from application retention and resulting
packet loss are acceptable.

The earlier replacement-first PMD example assumed zxdp would configure every
mbuf as an initial descriptor. That choice was never proposed and does not
establish an outstanding design question. A3 is closed.

### A6: ring fixtures must supply the receiving pool's backing

DPDK 25.11 `net_ring` Rx dequeues existing mbuf pointers; its Rx queue setup
ignores the supplied mempool. A direct Tx-to-Rx ring connection therefore hands
the receiver the sender's Tx backing. That differs from the existing Zutcp
receive path, whose Rx metadata returns backing through its ingress allocator.

Evidence: [ring PMD implementation](https://github.com/DPDK/dpdk/blob/v25.11/drivers/net/ring/rte_eth_ring.c),
`eth_ring_rx`, `eth_ring_tx`, and `eth_rx_queue_setup`.

Adopted fixture adaptation: inject incoming frames using mbufs allocated from
the destination's Rx pool. For each packet, dequeue the outgoing mbuf from the
sender's Tx ring, allocate a destination Rx-pool mbuf, copy the packet bytes,
enqueue the destination mbuf on its Rx ring, and release the outgoing submission
reference. On destination allocation/enqueue failure, release applicable
references and drop the packet. This models transfer between independent hosts
while preserving actual backend calls and receiver allocation provenance. The
copy belongs exclusively to the software peer fixture; production DPDK stays
direct. Stop fixture producers and drain their queues during teardown.
An individual mbuf is dequeued or released; draining describes queue consumption.

Use pcap input/output for independent byte/checksum comparison. Its ordinary
receive path allocates from the queue's supplied pool; do not substitute ring
pointer forwarding as proof of that ownership behavior.

Suggested coverage mapping, within the existing AF_XDP scope:

| Existing test | DPDK counterpart | Adaptation |
| --- | --- | --- |
| ZxdpPoolTest | ZxdpDpdkPoolTest | Native mbuf allocation/return, configured geometry, exhaustion and retained backing. |
| ZxdpInstanceTest | ZxdpDpdkInstanceTest | Explicit EAL prerequisite, init failures, mandatory Tx provisioning and close/recreate. |
| zxdppackettest | zxdpdpdkpackettest | Direct submission, received bytes, backing lifetime, exhaustion and draining through ring/pcap peers. |
| zxdpwaketest | zxdpdpdkwaketest | Active receive, counted wake and bounded idle wait; omit AF_XDP sender-thread assertions. |
| zxdplooptest | zxdpdpdklooptest | Software peer progress through the normal backend path. |
| ZxdpClassifierTest / zxdpfiltertest | no DPDK copy | The current BPF classifier and Xdp netmask filter are backend-specific. |

The relevant Zutcp native fixture must additionally select DPDK and exercise
start/stop/restart with the same pool and endpoint ownership contract. Do not
claim ring or pcap results validate physical NIC DMA or driver refill behavior.

### A1/A2: concrete proposed file boundary and build checkpoints

Modify `configure.ac` and `z.config` for the adopted optional dependency. Modify
`zxdp/src/Makefile.am`, `zxdp/test/Makefile.am` and
`zxdp/itest/Makefile.am` for conditional sources, flags, libraries and tests.
Keep generated Autotools outputs consistent with repository practice.

Create public `zxdp/src/ZxdpDpdk.hh` and its implementation
`zxdp/src/ZxdpDpdk.cc` for EAL configuration/lifetime. Create private
`zxdp/src/ZxdpDpdkInstance.hh/.cc` and `zxdp/src/ZxdpDpdkPool.hh/.cc` for the
instance and native mbuf pools. Modify `zxdp/src/Zxdp.hh` and `Zxdp.cc` for
selection, parameter union, factory dispatch and status(). Keep AF_XDP internals
in place. Concrete DPDK declarations are built only when enabled.

Create the five DPDK test sources named in the coverage table, plus private
`zxdp/itest/zxdpdpdkfixture.hh/.cc` shared by the three integration drivers.
Update `zxdp/test/README.md`, `zxdp/itest/README.md`, `zxdp/README.md`,
`zxdp/INTERFACE.md` and `zxdp/GUIDELINES.md` with actual setup and guarantees.

Modify `zutcp/src/ZutcpMx.hh/.cc` for backend selection and initial/restart pool
provisioning; `zutcp/src/ZutcpIP.hh/.cc` where routing/device metadata currently
requires Linux interface identity. Extend `zutcp/itest/zutcpmxtest.cc` and its
`Makefile.am`/README for the backend-specific fixture. Modify only the
network-multiplexer overload of `tableBufAllocFn` in `zdb/src/ZdbBuf.hh` to
request TxTCP, preserving its requested geometry and the standalone allocator.
Consumer routing is resolved by the later adopted Linux routes/static-neighbor
section. The brief enumerates affected selection/configuration callers.

Proposed buildable checkpoints:

1. Optional dependency wiring and the explicit EAL wrapper. Build both enabled
   and disabled configurations before exposing an instantiable DPDK backend.
2. Complete native pool/instance implementation and factory selection together,
   including all virtual overrides, status for both backends, draining and
   structured failure paths. Run pool/instance tests and existing AF_XDP tests.
3. Add software ring/pcap fixtures and mapped backend integration tests. Verify
   Rx-pool provenance, direct Tx and accepted-work draining in those fixtures.
4. Integrate Zutcp selection, device/path metadata and restart, then change the
   Zdb network allocator role. Run the affected unit/native consumer tests.

The file inventory, build checkpoints and named coverage mapping are now in the
brief. A3 is closed and the later author resolution settles A7 routing/neighbor
configuration. Earlier resolver and dynamic-discovery proposals are superseded.


## Consumer integration review continuation

Incorporated the concrete file boundary, four buildable checkpoints and named
backend test mapping into concepts_brief.md. Updated its public Zutcp parameter
reference to InstanceParams. These documentation edits do not implement DPDK.

A7 now has a concrete proposal in the brief under “Remaining consumer decision:
DPDK path configuration.” Source inspection establishes that changing factory
selection alone leaves constructor geometry, cold netlink resolution, restart,
worker fallback and ISN scope dependent on AF_XDP/Linux identity. The proposal
uses application-supplied DPDK path resolution, explicitly configured geometry,
tagged backend settings and the existing worker-to-Rx completion/drain pattern.
It does not silently adopt a new routing API or require a Linux companion device.

The final readiness re-audit has not run: the path-source contract still needs
an author decision, and its chosen API must be reflected in the caller manifest.
Original reviewer findings are retained. No production source changes, builds
or runtime tests were performed in this documentation continuation.


## Author correction: Linux owns routing

Linux kernel routing is the selected control plane. The application-supplied
resolver proposal is withdrawn and removed from the brief. So is the proposed
ISN scope change based solely on avoiding a Linux control-plane interface.

IPv4 next-hop MAC resolution is ARP; IPv6 uses ND. Current resolveIPPath accepts
only static neighbors and does not trigger discovery. The remaining A7 question
is the kernel-interface/DPDK-port mapping and, for an exclusively userspace-owned
NIC, the path carrying kernel ARP/ND packets to and from the link. Kernel-visible
and bifurcated devices need not use the same arrangement as VFIO-owned devices.
No ARP/ND implementation or exception-path design has yet been adopted.


## Adopted A7 resolution: preserve the existing fixture control plane

The author resolved the route/neighbor question by retaining the current Zutcp
arrangement: fixed endpoint IPs, Linux routes and permanent IP-to-MAC neighbor
entries. Substituting DPDK does not require adding dynamic ARP/ND or a new route
resolver. Zutcp retains Linux control-plane identity; the application/fixture
associates it with the named DPDK port and configures matching addresses/MAC/MTU.
The Linux control-plane interface need not be the physical PMD-owned device.
Zxdp handles packet I/O and has no routing or neighbor-resolution responsibility.

Recorded this as adopted in concepts_brief.md, replacing the open dynamic
neighbor/exception-path discussion. The earlier application resolver, ISN scope
change and requirement to choose a kernel ARP/ND exchange path are withdrawn.
Existing static-neighbor lookup and worker-to-Rx completion/draining remain.
The adopted loopback peer continues to transfer packet bytes into receiver-owned
Rx mbufs; this is independent of Linux route/neighbor configuration.

A7 is resolved. File inventory includes the affected consumer callers; final
readiness verification has not yet run. These were documentation-only changes;
no production source, build or test run was required for recording this decision.


## Final readiness re-audit

Three fresh-context, read-only reviewers completed: implementation-readiness,
codebase-details and style. Each read its required persona/standard and target
context; none failed or reported unavailable required material. Their normalized
reports and the stable reviewed snapshot are in
`review_context/zxdp-dpdk-readiness/final-*.md` and
`final_review_snapshot.md`; machine-readable traceability is in
`final_findings.json`. This is the one final re-audit following the original
resolution cycle, not a claim of convergence.

**Result at that reviewed snapshot: not implementation-ready.** Three Major
findings were reported. Subsequent author resolutions and dpdk_plan.md now close
all three; the preserved reports describe the earlier snapshot, not a new audit.
The settled Rx provisioning, software-loopback ownership, Linux static routing
and neighbor setup, optional dependency and direct-Tx architecture are unchanged.

### final-M1 — closed: registration-time MTU clamping adopted

Source: final readiness finding 1. Snapshot lines 299–308, 514–519 and 610–615.
The reviewer identified that the snapshot's allocation-time requirement did not
match current AF_XDP registration-time clamping. The author selected the existing
behavior as preferable: cap effective pool geometry when the allocator is
registered. DPDK must follow the same rule. Zi then inherits bounded geometry
and default allocBuf() remains safe without a per-call MTU parameter.

Updated the brief accordingly. No allocation-layer redesign or removal of the
current Zutcp preclamp is required. Earlier allocation-time-only requirements
and proposed fixes in this report are superseded. Preserve smaller Tx requests;
do not expand every Tx pool to MTU. This finding is closed.

### final-M2 — closed: concrete repaired-Instance settings

The original readiness finding concerned unspecified backend settings and a
route-worker fallback. The current Instance resolves directly on mx Rx and uses
a cold Instance(ZiMultiplex*) constructor followed by init(Params). There is no
configure() or public parameter getter. dpdk_plan.md section 4 defines exact
DpdkParams/InstanceParams additions, first-resource selection using config.type,
accepted settings in m_params, repeated-init pool identity checks and start/
restart selection. It preserves the lazy Allocators aggregate and defines errors
through init's structured result. No constructor setup, old next-copy policy
configuration, new loader or copied application owner is restored.

### final-M3 — closed: explicit Zdb guard and finite plugin test

The original readiness/codebase findings identified a real null-dereference path
in the proposed App-to-TxTCP migration and missing test scope. dpdk_plan.md
section 5 specifies TxTCP for only the mx-backed callback and the exact guard
`if (!buf) return nullptr;` immediately after allocBuf(), before assigning skip.
Standalone allocation and downstream failure handling remain unchanged.

The plan names zdb/src/ZdbBuf.hh and zdb/test/ZdbFBTest.cc and specifies a local,
finite ZiMxPlugin/IOBufPool test without a Zutcp/DPDK dependency. It covers TxTCP
dispatch, requested/effective geometry, exhaustion and allocation after return.
Only actually affected outgoing-role assertions in zdbsagareptest may change;
valid App receive/reassembly backing is preserved. This closes the planning
finding; the source guard/tests have not been implemented by this review.

### final-N1 — historical reference in the brief

Style reported one Nit: A7 is undefined within the standalone target. Record it
as nonblocking; the final sentence naming A7 can be removed during the above
edits. No other style finding was reported.

The review execution at that snapshot is complete. Its remaining corrections
are now specified in dpdk_plan.md; see the finalization record below. No
production source was edited or runtime test run by this document review.


## Author resolution: prefer registration-time pool clamping

The author explicitly selected clamping the pool size when the allocator is
registered, matching the current AF_XDP implementation. Updated every active
allocation-time requirement in concepts_brief.md. This supersedes the earlier
historical instruction not to clamp pool configuration. final-M1 is closed;
final-M2 (concrete Zutcp parameters) and final-M3 (Zdb exhaustion handling/tests)
remain open. Documentation only; no production change or runtime test required.


## Author resolution: route queries run on multiplexer Rx

Zutcp may use the multiplexer's Rx thread for route resolution. Updated the
brief to run the existing destination-IP-based Linux route/static-neighbor query
there for both backends, remove non-I/O-worker dispatch and the Xdp sender-slot
fallback, and preserve setup generation checks and lifecycle ordering. The
current helper uses blocking netlink calls; this adopts Rx execution of those
cold queries without inventing a new asynchronous resolver or hidden thread.

The missing-route-worker subpoint of final-M2 is closed. Its concrete public
backend-parameter declaration remains open, as does final-M3. No C++ source was
changed by recording this decision.


## Zutcp ownership baseline for subsequent DPDK work

The final author direction is
`Ztcp → ZiMultiplex → ZiMxPlugin → Zutcp::Instance`. The app statically constructs
and configures Instance and installs it with the existing ZiMultiplex::plugin().
Instance combines connecting and listening/accepted capabilities and implements
ZiMxPlugin's inherited Ztc::Mx interface. It does not implement Ztc::Hub and must
not add a Hub adapter or another lifecycle controller. Earlier optional-Hub
wording in this review is superseded.

The source repair in zutcp_repair3.md removes the separate Zutcp Hub/Client/Server
application hierarchy and nativeInstance() bridge. Shared policy, admission,
secrets, history, connection ownership and pools live in Instance; internal
listeners retain local bind/options, pending/live membership and drain state.
Application transfers use Ztcp/ZiMultiplex; private protocol probes retain the
actual protocol and independent wire assertions through native test facilities.

allQueues() traverses existing internal objects to expose normalized telemetry
for the actual Zutcp and Zxdp queues, delegating Zxdp enumeration through its
existing interface. It implies no additional queue container, index, registry,
cache or tracking mechanism. Preserve existing traversal lifetime rules.

The author reports Zutcp repaired. Source inspection confirms the single-instance
API, private protocol headers and direct Rx resolver; use this working tree as
the baseline for dpdk_plan.md. This plan update does not rerun or certify the
repair tests and does not claim a DPDK implementation.


## Finalization against the completed Zutcp repair

Re-read the repaired Zutcp Instance and unified InstanceParams,
construction/allocator/start/restart/resolve paths, installed/private headers,
current application documentation and static bootstrap. Preserve these working-
tree changes. The author reports the repair complete; no further repair gate is
imposed by this plan. Static plugin bootstrap and inherited Ztc::Mx telemetry are
the current contract, superseding older loader/Hub proposals.

Created dpdk_plan.md as the authoritative execution artifact, retaining the full
EALParams declaration in the linked design brief. It names exact source/test/
documentation boundaries and four buildable phases. Added concrete DPDK consumer
settings and initial/restart branching, retained registration-time MTU clamping
and direct Rx resolution, and specified the author-approved Zdb null guard with
finite plugin-backed tests. All original A1–A8 and final-M1–M3 planning items are
resolved. The style Nit is removed from the active brief.

Validation of this documentation update: checked current API/source references,
existing modification paths, new-file intent, plan/brief parameter consistency,
relative Markdown links and whitespace. No production sources changed. DPDK
builds/tests are specified future acceptance, not reported as executed. Historical
fresh-review snapshots/reports remain intact; no extra re-audit or convergence
claim is made beyond the recorded review cycle.


### Live source-layout reconciliation

During final path checks the shared working tree consolidated ZutcpMx.hh/.cc into
Zutcp.hh/.cc and folded InstanceConfig into the existing InstanceParams/m_params.
Re-read those changes and updated the active brief and execution plan to the new
paths and full current parameter declaration. The DPDK additions extend that
aggregate; configure() preserves its new fixed bootstrap fields with the existing
ones. No separate InstanceConfig or restored Mx header is planned. Historical
review references still describe their preserved snapshots. Source remains
untouched by this documentation work.


## Alignment after subsequent Zutcp changes

Re-read current Zutcp.hh/.cc, ZutcpAlloc, installed/private build inputs,
InstanceParams/params()/configure(), constructor validation/provider publication,
initial/restart backend creation, the direct resolver and private test fixture.
The active plan now reproduces the current aggregate layout/defaults and adds
only DPDK settings. It preserves params(), constructor-bound native setup and
stopped policy configuration. configure() retains next.type/next.dpdk with the
existing fixed fields; initial and restart factories use m_params.type, not a
nonexistent m_type. Constructor control-pool/credit initialization and the private
fixture constructor remain part of the baseline.

Updated the DPDK integration assertions to cover retained settings through policy
updates/restart and constructor control geometry before backend init. No source
repair, generic plugin redesign, worker or new configuration object is implied.
Prior snapshot reports remain historical; this was plan/source reconciliation,
not a new Goldfish review, build or runtime test.


Alignment checks passed: removing only the proposed type/dpdk fields from either
plan declaration reproduces the current source aggregate's fields/defaults;
plan/brief declarations agree, params()/configure()/restart references match,
local links and code fences are valid, and git diff --check passes. Relevant
source hashes are recorded in aligned_zutcp_source.json and were unchanged during
these checks. No implementation source or test run was part of this update.


## Alignment to explicit init(Params) and lazy allocators

The latest Zutcp constructor now takes only ZiMultiplex*. init(Params params)
validates settings, builds native resources, creates the lazy Allocators aggregate
and commits the accepted m_params. configure(), configureControl() and the public
params() accessor have been removed; initControl is internal. Source startup,
allocator, direct resolver and ordered teardown remain the reference behavior.

Rewrote the active plan/brief integration instructions accordingly: select the
first backend using incoming config.type, retain it via m_params after success,
and use that retained type on restart. Extend repeated-init native-pool identity
checks to backend/name without restoring next-copy configuration or getters;
rxInterval may be updated while stopped through init. Preserve local provider
adoption, the ZuUnion<void, Allocators> storage and the injected-pool test ctor.
The bootstrap example now constructs the cold object, checks init's structured
result, then registers the plugin/allocators. Existing private fixture apply
carries new backend settings; no production introspection API is introduced.

Updated the integration tests/file boundary for cold/init failures, retained-pool
identity, accepted idle changes and start/restart. Historical earlier alignment
records describe superseded APIs. This is documentation-only reconciliation;
no source edit, build or runtime test is claimed.
