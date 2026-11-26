# Packet device lifecycle and ownership

## Construction and Env ownership

The application constructs and retains `ZmRef<Zpkt::Env>` before creating any Nif.
`env->init(cf)` loads the configured `module` and invokes its C-linkage
`ZpktPlugin(cf)` entry point, which returns a fully initialized Plugin. The plugin
owns typed, name-keyed interface configuration; DPDK Plugin directly owns EAL.
Initialization creates no named Nif. Source Cf storage may be released afterward.
The module remains resident after any entry-point invocation, including failure.
Startup-only configuration/loading throws `ZeException`; no partially initialized
plugin is published.

`Env::nif(ZiMultiplex *, ZuCSpan name)` returns
`ZuUnion<Nif *, ZeException>`. It requires initialized Env and caller-serialized
setup. On a name miss, the multiplexer must be stopped. Core `nifQuery` resolves
the Linux name and obtains `NifInfo` containing ifindex, MTU, MAC and Rx queue
count. Env passes this metadata unchanged to `Plugin::nif(mx, name, info)`. The
backend consumes its owned configuration and opens resources during construction.
There is no separate Nif initialization method. Creation failure publishes no
child or cache entry and preserves structured errors.

The recipient immediately acquires a `ZmRef<Nif>` from a successful result.
A new Nif is initialized, stopped and unallocated. Env owns a
unique-name `ZmRBTree` of child references; each child retains a raw Env pointer.
A same-name/multiplexer hit returns the existing pointer without query, reopening
or backend calls, even while running. A different multiplexer returns an error
without changing the child. Successful finalization removes only that child's
identity, clears its parent pointer and permits a replacement. Repeated final is
harmless; an old finalized child cannot retire a replacement.

Env has no engine or start/stop API. The application serializes init, lookup/tree
mutation and finalization. It stops/drains children using their existing engines
and returns dependent buffers before calling `Env::final()`. Env asserts stopped
children, holds a reference while finalizing each, and calls Plugin final only
after children have retired. A child error retains its reference and propagates
without finalizing Plugin underneath it. Env/Plugin final return result unions;
destructors log errors and never throw. External Nif references must retire
before releasing the final Env reference. Env outlives every child; the last Env
reference release destroys Plugin and its DPDK EAL member.

`Env::nifQueryFn(NifQueryFn)` accepts a nonnull plain function pointer with the
same result contract as `nifQuery`. It defaults to the real query, survives init,
and is configured during serialized setup. A query error never invokes Plugin.
This seam does not replace backend BPF attachment transactions.

## AF_XDP

Configure the multiplexer and its sender slot before construction. Select
`libZpkt_xdp.so` in Cf and provide `interfaces { name { txSid N } }`.
The key is the Linux interface name; `txSid` is required, nonzero and validated
against the supplied multiplexer. `rxSpin` controls the receive loop and `txSpin`
controls the sender loop. Rx defaults to `(Zi_DefltSpin<<2)` (16,384), and Tx
defaults to `Zi_DefltSpin` (4,096). AF_XDP redirects all
interface traffic, including non-IP frames. Its sender slot is distinct from Rx
and Tx owners. `env->nif(&mx, name)` opens synchronously using core-supplied
metadata and retains the borrowed multiplexer and link resources until final.

## Shared allocation

In applications using Zutcp, inject a `Zutcp::MxPlugin` into the multiplexer
constructor. Move the initialized, stopped, unallocated Nif into TCP with
`tcp->init(mx, env, ZuMv(nif), params)` before requesting allocator handles.
Initialization throws `ZeException` on failure. TCP borrows Env and consumes the
Nif reference only after validation succeeds. Failure preserves the caller's
reference and finalization obligation. An identical stopped repeat may
pass an empty reference, but Env, multiplexer and TCP configuration must match.
After success, TCP owns Nif lifecycle calls; TCP final clears its borrowed Env
pointer and never finalizes Env. Request allocator handles from that same
multiplexer. `mx.alloc()`
allocates before start when the application needs native Tx buffers early.
Otherwise `mx.start()` performs that allocation.

Configure TCP retries in the `Zutcp::MxParams` passed to `tcp->init()`.
For both backends, `txRetries` sets the number of immediate retries on the
shared TX thread after an idle device refuses original TCP data (default 1).
After these attempts, retry once per `txBackoff` interval while idle refusals
continue. The interval is a positive `ZuTime`, default 1 ms. Zero `txRetries`
skips the spin phase. Timed retries have no count limit and end on acceptance
or connection teardown.

Standalone XDP tests may call `xdp->alloc(rxPool, txPools)` directly. Create
the pools with
`bufPool(type, count, size, headRoom, tailRoom, align)` over `Rx`, `TxTCP`,
and `TxUDP`. One common ingress pool supplies receive frames. The intrusive
Tx list may contain multiple pools per role, each with independent geometry.
A transport can also supply internal Tx reserves with `reserveTxPools()`
before backing allocation. XDP retains pool references without changing the
caller list's membership.

XDP maps one contiguous arena and registers it with one AF_XDP socket.
Linux requires a bound UMEM owner and separate FILL/CQ pairs for distinct
device/queue pairs. Queue zero owns the registration. Other queues retain
their own bound owners sharing that registration. Per-run service sockets
share with the owner for their queue. XDP publishes pool backing only after
registration and ring setup succeed. Failure releases provisional resources.
After success, allocation with the same pool set returns success without a
new mapping or registration. A different set returns an error.

## Packet service

Activate with `start(receive)` or `start(receive, txStopped, completed)`
after the multiplexer scheduler starts. The second form reports activation
through a callback. Supply each run's receive callback and optional sender
drain continuation at this point. An activation failure releases any per-run
sockets and mappings and retains the allocation for retry. Inspect `error()`
after start or stop completes.

The receive callback takes the Ethernet frame pointer, its length, and a
borrowed `ZiRxBurst *`. A singleton has a null burst. AF_XDP and DPDK
supply a scope only when multiple descriptors were already available in one
dequeue. Contiguous deliveries may hand off original buffer nodes together.
The scope drains on return from that dequeue, including a wake or discard.

Call `rx()` from the receive owner. It processes frames until `wake()`
interrupts the loop. The receiver spins up to the configured idle budget and
then blocks on packet readiness and eventfd. Receiving packets or reposting
returned frames restarts that budget. If the common ingress pool is empty
and any queue has at most half its receive supply, the first returned frame
wakes the parked owner to replenish FILL. Empty queues have first claim on
available frames, in round-robin order.

Allocate on a Tx pool and write the complete Ethernet frame directly into
that backing. `send(packet, length)` accepts one outstanding kernel
submission per slot. The Tx owner can submit up to `Zpkt::MaxTxBatch` frames
with `send(ZuSpan<const Zpkt::TxPacket>)`. The returned count accepts exactly
that many leading entries. The caller retains the remaining entries in
resubmittable form. DPDK makes one PMD call for the prepared prefix. AF_XDP
publishes descriptors in order and stops at the first refusal.
The AF_XDP sender uses nonblocking `sendto()` on the assigned
multiplexer slot. The producer increments a descriptor count before publish
and schedules the sender on the zero-to-one transition. Descriptor consumption
tracks pending work. `pollTx()` separately retires kernel leases on the Tx
owner. The caller returns its own allocation lease with the original pool.
Recycling waits for both leases.

`bool Nif::tcpChksumOffload()` reports whether the started device is
actually configured for TCP checksum offload. Query it after each start, including
restart. Device capability discovery/enablement belongs to Zpkt; software TCP
checksum calculation belongs to its caller. AF_XDP queries the netdev generic-
netlink XSK checksum capability, provisions a private Tx metadata prefix and
registers its length with UMEM. It reports true only with successful metadata
registration, native zero-copy service and current driver capability. Copy mode,
unsupported registration and pools requiring a full-page frame report false.
DPDK reports its enabled setting after configuration fallback. All frames use
plain `send()`.

When offload is false, supply a completed TCP checksum. When true, TCP frames
must contain the pseudo-header seed; DPDK derives native header lengths/flags
from the Ethernet/IP/TCP headers and leaves caller bytes untouched. It excludes
software Ethernet padding from the mbuf extent; the NIC supplies wire padding.
The native TCP caller emits contiguous IPv4/IPv6 TCP frames. UDP and other raw
protocols retain their existing checksum contract. AF_XDP writes checksum start/
offset metadata before accepted TCP frames and excludes software padding from
the descriptor extent. The private prefix preserves caller capacity, alignment
and configured head/tail room. Every accepted descriptor selects fresh metadata
options; refusal leaves caller bytes and the prefix unchanged. DPDK clears
stale mbuf flags on refusal/reuse. No software fallback exists inside Zpkt.

Call `txReady(packet)` on the Tx owner before rewriting headers in retained
backing. It checks the existing submission lease and reserves no capacity.
Both scalar operations return `Zu::IO::Result`. Compare their results explicitly.

| Result | `txReady(packet)` | `send(packet, length)` |
| --- | --- | --- |
| `Zi::OK` | The caller may write the frame. | The provider acquired its submission lease. |
| `Zi::NotReady` | A prior submission still holds the slot. | A prior submission or device capacity prevents acceptance. |
| `Zi::IOError` | The provider is down. | Terminal error, including a down provider or invalid frame extent. |

Supply an address within caller-held Tx backing from this provider. A bad backing
address violates an internal invariant and triggers a fatal assertion. Submit on
the same owner, and keep accepted bytes immutable until retirement. Refusal
acquires no submission lease. The vector overload returns the accepted prefix count.

## Stop and finalization

The caller quiesces its Rx and Tx service before stop and keeps the multiplexer
scheduler running until completion. Stop blocks new submissions and delivery,
drains queued sender work and Tx completion, and retires the run's callback
references and service sockets. XDP preserves its registered arena, FILL/CQ
mappings, posted frame accounting, and any application-held slots. Restart
opens fresh service sockets and uses the same pools and packet addresses.
If device failure leaves a transmit completion unresolved, its backing stays
accounted for and later activation first checks the retained CQ. If ingress
quiescence cannot establish a kernel grace period, finalization is required
before the backing can be replaced.

Return dependent buffers before finalization. While stopped, `final()`
releases the persistent ring mappings, registration socket, arena and
initialized link resources. This applies after any number of runs and before
the first start. It clears the borrowed multiplexer pointer. Standalone tests
call it while their multiplexer still lives. In an application, multiplexer
finalization drives TCP finalization, and TCP drives XDP finalization.

## DPDK lifecycle

Select `libZpkt_dpdk.so` through Env configuration. The concrete Plugin owns
EAL directly and loads its optional `eal` child using the unchanged EALParams CLI
facet metadata. Named fields and raw argv tokens may configure different options;
repeated list values preserve order. EAL retains one process-wide attempt and
its existing cleanup behavior, including native success before a later startup
failure. Independent EAL cases use fresh processes.

Each interface entry has required nonempty native `port`, optional `spin` (`Zi_DefltSpin`)
and `rxInterval` (50 microseconds). The map key must resolve to a Linux Nif.
Env supplies that Linux identity and geometry; the backend resolves only `port`
through native DPDK APIs and discovers backend capabilities. It trusts the
configured pairing and performs no Linux query or MTU/MAC agreement check.
The opened port remains exclusive through device finalization. Successful named
lookup exposes the supplied Linux MTU/MAC/routing identity and DPDK's selected
single Rx queue before pools exist. Repeat lookup is owned by Env's tree.

Provision the common Rx pool and each distinct Tx pool as one unit.
If native allocation fails, retry with the same pool collection. Direct
standalone allocation is idempotent for that collection. Backing has native
mbuf headroom and exact logical capacity for the requested role. Tx pools
may be smaller than an MTU. The PMD can use every Rx mbuf.

Start configures one contiguous-frame Rx queue and one Tx queue. Run `rx()`
and `pollTx()` on their single owners. Call `send()` on the Tx owner with a
pointer within its native backing. Rejected sends preserve caller bytes and
ownership. Accepted sends hold one driver reference until retirement. Keep
bytes immutable until then. The same `txReady(packet)` Tx-owner contract applies
before re-encoding retained backing. `pollTx()` counts each retired submission once,
including when the application retains a separate recovery allocation lease.

The Rx owner checks counted wakes between bursts. It spins through the
configured number of empty probes, then uses a receive interrupt fd when
available and short timed eventfd waits capped by the configured `rxInterval`.
A zero interval keeps polling. Receive callbacks own the received mbuf through
the common Rx pool. Return off the owner coalesces a wake while Rx is parked.

Stop gates new work, drains the Rx callback and services accepted Tx on the
Tx owner while the scheduler is running. A stopped or failed run retains the
opened port and native backing for restart. Return dependent application
buffers before finalization. Finalization releases backing and lets another
instance open the same port.

`allQueues()` enumerates the configured Rx and Tx queues. Consume each view
inside its callback. DPDK counters are available only as supplied by the PMD.
`status()` reports the backend, opened device, link geometry, copy mode and
the current queue setup. Ring uses direct mbuf forwarding. Pcap uses copies
and supports finite capture input in this lifecycle.
