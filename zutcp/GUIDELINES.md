# Instance ownership boundary

Applications statically construct one Instance, configure it through
init(Params params), and install it through ZiMultiplex::plugin() before allocator
registration. Init is idempotent while stopped and fails in every other engine
state. Start requires initialization. Start/stop use ZmEngine, including queued
opposite requests, and report completion through ZiMxPlugin's existing callbacks.
Stop retains configuration and history; final is idempotent, runs only after
stop and releases configuration, pools, secrets and retained history. Destruction
also calls final. Keep one Zxdp instance across stop/start; its own engine drains
sender and kernel access before reporting stopped. There is no initialized flag
or restart-time replacement.
There is no configure() method or public params() accessor. Transfer code uses Ztcp/Ztls
or the generic Zi callback interface. There is no Zutcp Hub/Client/Server
application transport hierarchy or nativeInstance() bridge. Instance directly
owns outgoing/accepted protocol records, listener endpoints, admission, secrets,
history and native pools. Internal listeners own only local bind/options,
pending/live membership and listener drain state.

Instance implements ZiMxPlugin and its inherited Ztc::Mx interface, not Ztc::Hub.
One Cxn base supplies connection identity, application association and intrusive
ownership membership; CliCxn and SrvCxn share the Cxn_ protocol implementation.
Instance owns one m_cxns list, m_cxnCount and m_cxnsLock. allCxns() snapshots
published application connections from that list, filtering pending and retiring
entries, then invokes callbacks without holding the membership lock. There is no
separate observation list or connection-manager object.

Connection admission counters and limit checks belong directly to Instance.
Cxn carries its admitted/pending/closing flags; there are no separate admission
token or ledger classes. Reserve before constructing a connection, cancel failed
setup reservations, and attach successful reservations before first delivery.
Keep passive tuple deduplication through acceptance and first-packet reentrancy.
Release admitted capacity only after the connection's Rx/Tx drain. Admission
totals are distinct from ownership-list counts because reservation precedes
construction.

Client and listener endpoints embed their binding hash nodes. Instance contains
the non-owning EndpointIndex directly; no separately allocated binding records,
opaque owner pointers or duplicate bound-local-address fields are maintained.
Dispatch preserves exact-local then wildcard lookup, and unbind removes only
the requesting endpoint's own node before its owner drains. Accepted connections
share their listener's binding and retain the existing TCP tuple routing.

allQueues() delegates directly to Zxdp's existing ring enumeration. It does not
traverse connections or listeners, or expose protocol bookkeeping as transport
queues. Do not add queue containers, indexes, registries or tracking caches.
Queue callbacks borrow the XDP rings; finish enumeration before closing their
owning XDP instance.

Destination route/static-neighbor lookup runs synchronously on mx Rx during
outbound and passive setup. It is intentional bounded blocking netlink work,
with no route-worker selection or round trip. This supersedes historical notes
below that place lookup outside Rx/Tx. Preserve real Rx/Tx ownership and ordered
teardown; test contexts cannot reintroduce a second production owner or mode.

# Current Phase 8 execution boundary

The live protocol uses standard
TCP headers/options and raw application bytes over Instance/Zxdp. Endpoint ownership
uses TCP tuples; accepted byte history, canonical backing and owner handoffs
remain authoritative. Intermediate packet/frame codecs and peer packet-number
acknowledgments are deleted. Providers reserve the maximum standard Ethernet,
IPv6 and TCP headers (114 bytes); the intermediate extra64-byte reserve is gone.

Keep the accepted whole-original send/recovery policy, including positive peer
windows smaller than an original. RST receive policy is window-qualified, with
silent out-of-window rejection and no reflection of accepted peer resets.

Raw send returns accepted/refused without a commit callback. Tx publishes the
packet's ACK monotonically through the atomic `sentAck` only after successful
return, then finishes recovery bookkeeping before queued peer-ACK work runs.
Rx admits peer ACKs without consulting a transmitted byte frontier. Keep
generation checks and the independent receive-window and RST qualification.

Native providers implement `Zi::IOBufPool` over storage injected from a selected
`Zxdp::Instance` backend's `bufPool()` factory. Keep concrete pool classes internal
to Zxdp. Configuration owns its pool list; initialization retains independent
backend references so configuration-list membership can expire after `init()`.
Tx buffer allocation failure drops the connection; producers do not wait for
pool grants. Alignment changes physical stride without inflating logical buffer capacity.

Build/test Zutcp and its dependency closure in explicit directory/target order.
Use the five native drivers and separate independent-kernel interop runner while
finishing TCP. Selected Ztcp/Ztls application checks follow TCP completion; they
are optional consumers and are not dependencies of Zutcp. Preserve the existing mx/XDP ownership model throughout.

The phase-specific notes below record earlier implementation boundaries. Current
Phase 8 wire changes supersede their references to intermediate framing.

# Historical Phase 1 execution boundary

The live transport now uses Instance/Zxdp with TCP-aligned IP carriage. The native
endpoint tests exercise the client/server cutover and working pairs. Do not restore a kernel UDP mode,
fallback or separate backend. Cold interface/route/static-neighbor queries remain
control work outside Rx/Tx.

Allocator dependency is strictly one way: Zutcp supplies lower-level pool
providers through the ZiMxPlugin allocator hook. Do not call
`ZiMultiplex::allocator` from Zutcp, directly or through a helper. Instance owns its
internal native packet pool and metadata adapters independently of that factory.
Queued Tx ranges now retain their existing node through `TxBacking`. Keep stable
base/extent and raw lease ownership there, separately from the queue's mutable
offset/length. Retained frames must survive queue removal and submitted Zi
metadata destruction without owning a connection. Before raw handoff the node's
pending descriptor owns source storage; clear that descriptor after accepted
detachment. Native writers now allocate through Instance's local producer adapter
and first submission reuses the same registered slot. The inline TxData type and
queue are removed. Preserve whole-buffer credit admission, bounded piggyback
prefix space and unchanged application payload addresses. Restore producer view
metadata on refusal. Preserve the copied recovery/packetization algorithms in
Phase 1. Native first submission reuses its original registered allocation;
recovery builds the inherited selected ranges using the existing source storage.
Do not restore covering-packet retransmission, header movement into ACKed payload,
queued path-cap migration or the CQ waiter extension as Phase 1 requirements.
Backing-lifetime fixes remain: final ACK of all prepared native source bytes
returns the caller lease before producer callbacks, while kernel retirement
independently gates physical reuse. Historical frame metadata cannot resurrect
ACKed native byte work or assemble retired payload storage.

Phase 1 is stripping plus IP/Zxdp wiring and regression testing. No new recovery,
congestion, flow-control or packetization behavior belongs in that phase. Broader
pressure/fairness/lifecycle/observation matrices are later work, not extra exit
gates. See the current boundary above.

Framework/application callers request their own higher-level allocators from mx;
factory exercises belong in tests or clients. The locally owned builtin byte
adapter remains for receive heap replacement; it is not a native producer
fallback. Shared source nodes own native data leases through their frame ranges;
existing sent-record nodes own copied/control packet leases. Both are independent
of kernel CQ. Keep temporary Zi handoff scoped to synchronous submission and
clear it before refusal can discard its record. Provider owners drain all these
references before provider destruction.

Zi Tx submission uses the same retained source node as native writers. Its
PacketBuf base is a borrowed wire view; stable payload coordinates and the caller
lease remain separate. Reuse that view for registered first submission, preserving
the original payload address. Detach both the wire view and submitted application
metadata only after raw acceptance. PacketLease releases through the original Zi
allocator, so independent application pools need no lookup or extra provider index.
Application Tx factory geometry includes the bounded intermediate protocol reserve.

ZiTxContext completion reports local admission in length. Its callback owns offset
advancement; do not advance it before invoking the callback. Queued output retains
metadata until raw submission, while accepted output retains backing independently
of metadata. A full connection queue leaves its current context pending; the
existing Tx dequeue notification resumes it. Reentrant send notification defers
through the existing Tx owner queue. No CQ callback or protocol recovery extension
belongs in this context mapping.

Control output now uses a dedicated protocol-sized pool, including Initial,
ACK, window, probe, close and version-negotiation output. Positive DATA remains
on data backing; FIN-only/control packets may use the control pool. HubParams
owns controlBufCount (256) and txControlCredits (64), applied to Instance under the
serialized stopped lifecycle. The old InstanceParams::controlCount is removed.
Callers sharing Instance use matching instance settings. Do not mutate control count
while old control backing remains held or resize a live arena.

Data admission uses total accepted outstanding < capacity - reserved; control
uses total outstanding < capacity. Provisioned physical Tx counts supply the
conservative capacity. Charge only accepted sends and release only on CQ or
successful kernel retirement, independently of transport caller leases. This
requires no class ledger, packet search or completion callback. Keep the policy
in Zutcp; do not add reserved credits or priority parameters to Zxdp.

Large ACKs that cannot piggyback are sent from control backing before native
data. Refusal leaves ACK state pending; preserve successful control work in the
flush result if later data is blocked. Use txAdmission only on Tx or under the
serialized stopped control path. Receive-frame admission is now wired through
connection DATA; full native pressure/fairness/lifecycle/observation gates remain open.

Receive slices embed an allocator adapter that owns their existing original
packet reference. Generic heap replacement releases that reference immediately;
aliases retain the same original packet independently. Release slice storage
before its embedded allocator is destroyed, and leave the base destructor no
storage to free. Do not restore a borrowed builtin view that pins the original
packet after heap replacement or add another backing reference count.

Each connection owns a `Zi::IOQueue` of retained receive buffers on
its Rx owner. Add novel DATA's original buffer at the head. While the list count
exceeds `rxRetainFramesPerCxn` (default 256), pop the tail and release that list
reference. Drain the list during Rx teardown. Connections have independent
counts and limits; application aliases retain their normal buffer references.
Default ingress provisioning leaves each hardware queue more buffers than one
connection's default retained-list bound. Idle queues keep their FILL buffers;
provisioning only a smaller per-queue share can starve an active flow before it
can evict its oldest retained buffer. Explicit mx receive-buffer counts remain
caller-specified.

There is no physical-slot charge ledger, generation account, shared retention
cap or final-release accounting service. Final ingress return still wakes receive
service so an empty FILL ring can be replenished.

Pool clients use only allocation/return and geometry. Keep slot/address
calculations and allocation/completion bookkeeping internal to Zxdp. Retained
transmit backing needs no writable query; preserve kernel/caller leases through
completion and teardown.

The re-audit found `migCloseOnFail` configuration residue and unused
`asyncThread` settings, which are now removed. Server establishment now waits for retained response ACK evidence, completed Tx
ACK work and generation-checked Rx publication; the response-ACK checkpoint closes
that finding.
Earlier deletion claims below do not close those findings. Retain the agreed
Initial/response/ACK and bootstrap IDs while correcting the live path.

The mechanically renamed baseline is recorded in
`../review_context/zutcp-execution/zutcp-mechanical-rename.json`. Phase 1 is now
being adapted under `../zutcp_plan.md`; the QUIC-specific material below describes
the copied baseline until the corresponding facilities are removed. It does not
require retaining TLS/protection, migration or multiple streams in Zutcp.

The common transport owner is `Connection`; Link and Connection are the same
concept and must remain consolidated. The role names are `CliCxn` and `SrvCxn`,
with corresponding headers and no compatibility aliases. During
ZiMxPlugin connection integration, connect uses CliCxn and accept uses SrvCxn.
Connection has `ZiConnection *mxCxn`; install both `cxn->mxCxn = mxCxn` and
`mxCxn->plugin(cxn)` using the common Connection pointer before publication.
Connection-directed MxPlugin callbacks navigate through `mxCxn->plugin()`;
callbacks into Zi navigate through `cxn->mxCxn`. Neither direction uses a
hash/tree lookup or separate association registry. Wire packet demultiplexing
uses the existing endpoint index independently of this association.
Fence owner-directed work and clear the association before either object dies.

Phase 1 consolidates `Stream` into `Connection`. There is no separate stream
concept in the target implementation: no Stream/CliStream/SrvStream type, object,
identifier, map, factory or callback. Move surviving byte reassembly/delivery,
receive-credit retirement, Tx retention and producer behavior into Connection,
with the same Rx/Tx owners and original buffer references. Do not preserve a
single-stream wrapper or alias. Setup now negotiates only connection byte credit
through `maxData`; do not
restore stream byte/count transport parameters or Hub settings. The copied
stream guidance below describes only the baseline until consolidation removes it.

The current merged implementation exposes connection `reader()` and
`writer()`/`writer_()` facilities, with `Reader` and `TxBufAlloc` byte storage.
Use `ZutcpConnectionUtil.hh` for connection credit and receive final-size state.
Data/reset/stop wire records have no stream ID; connection controls are
`MaxData` and `DataBlocked`. Do not restore removed per-stream/count controls,
ID/limit utilities, child owner pointers, stream lifecycle logging or a
self-selection/refcount step in packetization. Bootstrap CIDs remain fixed for the connection; CID rotation and sequence metadata
are removed.

Use `dataRxAllocator()`/`dataTxAllocator()` for connection byte storage. Parsed
`Frame` has no sequence/ID field or CID rotation controls.
Data recovery identity consists of wire offset, length and FIN, independently of
buffer coordinates or CID/control metadata. Remove obsolete stream diagnostics
and accessors outright; do not preserve their layout or add compatibility aliases.

Reset merged byte state on its Rx/Tx owners, including queue sequence state and
terminal flags. Publish one byte-work generation from Rx and install that value
on Tx; reject stale posted admissions, writers and completion/control handoffs.
Retire only installed queue credit during reset: pending admissions release their
own reserved credit once when dispatched. Generation checks protect reuse, while
owner lifetime still requires the independent teardown/drain contract.

Keep all Zi provider/object composition in Zutcp. `ZutcpAlloc` supplies the first
Tx/control provider over a common pool injected from the backend factory. `ZutcpMx::Instance`
composes those providers with one physical Rx reserve, accepted-output metadata
detachment and mx owner lifecycle; see the current checkpoint in
`../zutcp_plan.md`. The stripped transport remains subsequent work.
`ZutcpIP` owns direct Ethernet/IP framing. Its writer receives payload already
at the family-specific header offset and writes headers/padding only. It adds no
UDP header or transport shim. `writeTCP` builds the TCP base header over payload
already at `tcpHead(type)`, with protocol 6, source/destination ports and the TCP
pseudo-header/header/payload checksum. `readTCP` validates that checksum before
publishing borrowed option/payload spans. Sequence, ACK, window and state semantics
belong to the connection; this codec alone does not establish TCP interoperability.
The live endpoint and final-packet recovery lease integration are implemented;
the retained protocol regression suite is the Phase 1 acceptance gate. Keep payload ownership and
endpoint routing in the transport; there is no packet codec or route lookup in
Zxdp. `resolveIPPath` queries kernel route/static-neighbor state during cold
control setup only. Never run its blocking request/reply on packet Rx/Tx owners.
It constrains lookup to the configured interface, preserves an explicit local
source and publishes the resulting path only on complete success. It does not
run ARP/ND or keep a routing-table replica.
Server establishment requires ACK evidence for a retained ACK-eliciting Initial
response. Accumulate evidence through the existing bounded ACK work, then publish
the installed Tx generation to Rx. Check the current generation and peer parameter
readiness there. Do not establish on parameter installation, submission, an ACK
sample or a Tx-side mutation of Rx state. Valid data alongside the ACK can queue
its response while handshaking; resume it after state/budget publication and before
application notification, which may initiate teardown.

Peer transport parameters belong to Rx and are installed only from plaintext
Initial input. Tx receives a bounded snapshot for ACK timing and connection byte
credit; installation/reset run on Tx. Do not write Tx connection credit from Rx.
TLS/protection/early-data implementation and certificate/ALPN configuration are
removed, along with the separate protected Handshake packet space. Do not restore
those APIs or a Crypto-owned parameter carrier when porting callers.

`InitialPkt` in `ZutcpTransport` supplies the plaintext codec: parameter-length
prefix, encoded values and directly constructed frames. Rx installs peer limits;
the owning connection establishes from that state. Initial output constructs its
own local parameter record on Tx and gathers retained frame spans into the final
packet. This copy constructs the contiguous datagram without an intermediate
payload buffer. Short packets also carry ordinary frames. Neither output path
reserves protection tags or samples. Pure ACK Initials omit parameters to avoid
a parameter-response ACK loop.

Unknown/removed frame types fail admission. Close output may piggyback a pending
ACK without waiting for its delay timer.

Initial token framing, Retry bootstrap/parameter state and the `NEW_TOKEN` and
`HANDSHAKE_DONE` facilities are removed. Payload length follows the CIDs directly
in the Initial header; do not restore a token-taking writer overload or a
confirmation-control slot. Bootstrap still creates its routing CIDs, without
generating reset tokens. Reset-token transport parameters, their codec and
parameter-event logging are removed. The separate reset-token and path-challenge types, CID rotation controls/tables,
migration APIs/settings/diagnostics, candidate-path state/timers and endpoint
rebinding are removed. Bootstrap routing uses the fixed local CID plus the
original destination CID, with bounded router tombstones. The server records its
peer tuple on Rx at successful bootstrap and drops packets from other tuples
before packet admission. Tx keeps its own peer destination. Preserve ordinary
MTU, ECN and initial send-budget accounting. Opening that initial budget on
establishment does not create a candidate path or validation exchange.
Native endpoint wiring is implemented; complete the remaining ownership,
admission/control and owner/race gates. The copied migration guidance below is
historical.
Keep Zi types, adapter state and mx scheduler integration in Zutcp. Zxdp owns
its required configurable spin-before-epoll `rx()` and interrupting `wake()`.
Delegate the plugin hooks to that pair; use the raw named-parameter pool/callback
interface in the updated plan.
Instance owns native local address/port membership on Rx. Bind/unbind there; dispatch
validates TCP integrity and advances the original registered Rx view without
copying payload or taking an endpoint reference. Remove membership before draining
the endpoint's raw callbacks. `Instance::resolvePath` uses an existing mx worker
outside Rx/Tx and posts completion to Rx. Native setup must provide that worker,
drain every accepted setup callback and fence its generation before owner release.
Instance stop waits for pending setup before the Tx/backend drain. No blocking route
lookup or independent protocol owner thread belongs on the packet path.

Reuse Clang/debug/ASan for development and acceptance with leak detection enabled.
Apply `../REBUILD.md` dependency order, freezes, batching and build reuse with the
author's ASan override; avoid unnecessary clean builds. Separate optional release
profiling and any non-ASan Memcheck build from the active development tree.

# zutcp supplementary guidelines

These guidelines are the audit checklist for code changes in `zutcp`.  They
supplement the repo-wide rules in `../GUIDELINES.md`; follow both when editing
or reviewing `zutcp` code.  Implementation discovery and source navigation live
in `README.md`.

# General zutcp

- Keep QUIC runtime work on the owning Rx or Tx thread.  Do not add locks to
  make non-owner access convenient.
- Keep protocol state changes close to the RFC mechanism they implement.
  Cross-thread entry points should snapshot only destination-owned metadata and
  immediately post to the owning thread.
- Use existing `Zutcp` protocol enums, fixed-size protocol values, Z socket
  address types, and module-local helpers before adding ad hoc string or STL
  representations.
- Use `ZiLOG(Debug, ...)` for debug text logging.

# Buffers

`Zutcp` exposes UDP packet allocator classes and ordinary plaintext allocation aliases through
`Zutcp.hh`. Keep those roles distinct:

- `PktRxBufAllocator` defaults to the `"Zutcp.Pkt.Rx"` heap for received UDP
  datagrams and in-place QUIC packet protection removal. Configure it through
  `ZiMultiplex::allocator` registration and the UDP call's receive allocator;
  the endpoint receives the original `io.buf`.
- `PktTxBufAllocator` defaults to the `"Zutcp.Pkt.Tx"` heap for transmit
  packetization and packet protection output. Each endpoint retains its own
  allocator; active output belongs to `ZiTxContext::buf` until kernel retirement.
- `StreamTxBufAlloc` defaults to the `"Zutcp.Stream.TxBuf"` heap for
  application stream bytes that must be retained for ACK, loss,
  retransmission, reset, cancellation, or teardown handling.
- `CryptoRxBufAlloc` defaults to the `"Zutcp.Crypto.RxBuf"` heap for retained
  CRYPTO receive fragments.
- `CryptoTxBufAlloc` defaults to the `"Zutcp.Crypto.TxBuf"` heap for TLS
  output staged for QUIC CRYPTO frame packetization.

Completed transmit datagrams use `Zutcp_::TxPkt`, an intrinsic `ZiTxBuf`
node with destination address and ECN metadata. Rx and retained plaintext buffers
remain separate metadata types. The mx factory selects TxUDP backing; no builtin
backing assumption is permitted. Packet protection writes directly into the
provider's writable span with its original headroom and tailroom.

Each physical `EndpointCxn` owns its common `ZiTx` queue on mx Tx. Its 4096-packet
capacity includes the installed active head. Reject full-queue submissions before
counting submitted bytes, recording both backpressure and a local drop. Posted
submissions may acknowledge posting before rejection; QUIC recovery still owns
retained plaintext and packet records above this boundary. Priority admission
inserts immediately after the active head, newest priority first among pending
packets. Ordinary submissions remain FIFO. Address and ECN stay immutable while
queued and are installed afresh for every selected head.

Posted output snapshots only raw physical identity and generation. Compare that
identity on Tx without dereferencing or retaining the old connection; obtain the
current connection locally at admission. Existing endpoint owner-drain rules
protect the raw endpoint callback. Do not add per-packet owner references.

When a protocol ACK opens congestion credit, service the existing bounded retained
retransmission batch before ordinary producers. Fresh bulk output must not spend
all reopened credit before deferred recovery runs. Socket completion reports one
local datagram and byte count, without ACK,
recovery retirement or producer notification. EAGAIN retains the same protected
packet. Bind posted output to its original physical connection; reject stale
submissions after rebind rather than routing them through a replacement. Closing
prevents admission; physical disconnect and Tx owner drain precede context and
queue retirement. Backend-held backing remains the backend's responsibility until
its retirement. ZiTx advances queued output in the common completion turn rather
than posting the former per-packet scheduler continuation; pacing remains above
this queue. Measure bounded-drain latency when changing the capacity.

`BufSize` is `1472`.  It is packet-buffer capacity, not the active UDP payload
limit.  The active path payload starts at the QUIC minimum and is raised only by
PMTUD/DPLPMTUD success.

Transmit packet protection is source-to-destination.  Retained plaintext
stream/control byte ranges are gathered as read-only inputs and encrypted
directly into the final Tx packet buffer.  Do not add an intermediate
ciphertext staging buffer.

Receive packet protection decrypts in place in the Rx packet buffer.  STREAM
receive data is represented by retained slices of that decrypted packet buffer:
`StreamRxData` carries both a `ZmRef<ZiIOBuf>` to the packet and the payload
span.  Frame-processing APIs that accept STREAM payload must require the packet
reference; do not add a frame-only STREAM copy fallback.

CRYPTO receive is different from STREAM receive.  Fragmented or out-of-order
CRYPTO data may be copied into retained `CryptoRxBufAlloc` buffers for
reassembly.  In-order CRYPTO delivery and TLS output should use direct
spans/origin-backed buffers when possible.

Cross-shard STREAM sends transfer an owned range or construct directly in the
stream writer's retained buffers. Do not stage bytes in an asynchronous byte
array before copying them into stream storage. TLS output already has a
`CryptoTxBufAlloc` owner: transfer it through the CRYPTO send callback and retain
its ranges for retransmission until the existing epoch/reset teardown retires
them. Packet protection reads those ranges directly into final UDP output.

Hidden fallback copies below the public API boundary are forbidden.  Any new
copy path must be documented in this section and covered by a test that explains
why the copy is required.

# Recovery and handshake confirmation

PTO reclamation selects the oldest eligible retained packets. Newly emitted
retransmissions must not indefinitely displace older STREAM or control work.
Use the existing recovery references and ACK retirement for HANDSHAKE_DONE;
do not add a separate completion timer or retry queue.

A PTO services its selected packet-number space. When another space has data
in flight and transmit keys, leave room for an acknowledgment-eliciting probe
there within the two-datagram budget (RFC 9002 section 6.2.4). A successful
Handshake retransmission must not suppress all outstanding 1-RTT probes. A
failed primary send does not authorize an unrelated substitute. Application
Data does not independently select PTO before handshake confirmation.
Candidate path proof retains priority over ordinary recovery for both active
client migration and passive server rebinding.

Server confirmation occurs at TLS completion; client confirmation occurs on
HANDSHAKE_DONE. Retire the actual Handshake secrets on Rx and the independent
Tx protector/recovery state on Tx. Defer server Rx retirement until its current
CRYPTO delivery callback unwinds. Late crypto and ACK snapshots must not
repopulate a retired packet-number space. Initial-key storage has a separate
existing cross-owner read and is not changed by the Handshake retirement repair.

Short-header key trials preserve ciphertext in scratch until authentication
succeeds, then copy authenticated plaintext back into the original packet. This
copy permits old/next-key retry after a failed authentication; STREAM slices
still retain the original packet backing.
