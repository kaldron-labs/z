# Native TCP and UDP integration tests

Native fixtures retain an intrusive Env before their multiplexer, configure the
selected module through ZfCf, and acquire initialized Nifs by Linux name. DPDK
EAL lives in the plugin; dummy Linux interfaces provide routing identity while
independently named ring/pcap ports provide the simulated wire. Build-tree module
paths are supplied per target. Stop/drain and buffer-return ordering is unchanged.

These Linux tests exercise actual MxPlugin/Zpkt packet I/O, standard TCP headers
and raw application bytes. They require sudo/root, network namespaces, iproute2,
ethtool, tcpdump, Python3 and Perl/prove. AF_XDP cases require usable AF_XDP/BPF.
AFP cases require nftables. Missing privileges or fixture variables produce
visible skips.
See the [kernel/native quickstart](../util/README.md) for application integration,
provider lifetime, interface ownership, routes, static neighbors and sudo setup.
Physical Ethernet operation remains unverified in the current environment.

## Build and run

Follow [the repository rebuild](../../REBUILD.md#phase-6) through Phase 7 in
the current configuration. Rebuild the allowed
configured dependencies before `zutcp/src` and its consumers, sharing one make
jobserver with total concurrency 8. The
commands below illustrate directory targets; when run concurrently they must
inherit that jobserver rather than create independent -j8 pools. Build the five
native drivers explicitly to avoid requiring optional application consumers:

```sh
make -C zi/test -j8 libZiTest.la
make -C zutcp/itest -j8 zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  bash zutcp/itest/zutcpmxfixture.sh \
  bash zutcp/itest/zutcpipfixture.sh \
  bash -c 'cd zutcp/itest && prove \
    zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest'
```

Compilation runs unprivileged. The launcher creates anonymous private veth/dummy
links and exports the existing `ZPKT_*` fixture variables. The mapping fixture
adds private routes and static neighbors and disables checksum offload. Each
subprocess fixture owns and cleans its extra namespaces and links. Tests retain
failure diagnostics through the Zi residue helper.

The ordinary directory `make test` target also includes the optional Ztcp and
Ztls application drivers below. Use explicit native targets while developing TCP.

## Driver coverage

- `zutcppairtest` launches the Ztcp client/server examples with statically
  configured MxPlugin plugins for ordinary transfer and half-close over both IP
  families. Protocol-private modes run native protocol roles in the test driver;
  they do not expose test controls through application APIs. It checks exact 7200-byte request/echo, half-close, SYN/setup loss,
  established DATA loss, duplication, reordering, ACK loss, partial ACK, SACK holes
  and reneging, option negotiation/absence, ECN, TLP and Fast Open. Middle-bridge
  filters affect actual packets; capture/checker evidence establishes the intended
  fault, byte progress, recovery and FIN completion. It does not use kernel TCP
  as a native fallback.
- `zutcppooltest` checks Zi allocators backed by one shared Zpkt arena:
  factory geometry, registered backing, borrowed/native views, allocation versus
  kernel leases, exhaustion, retirement and safe return/reuse. Allocation is
  immediate; producers do not queue for grants from a depleted pool.
- `zutcpstacktest` exercises actual raw Rx/Tx service, independent peer bytes,
  native first-send backing identity, replay/recovery lifetime, control admission
  under DATA pressure, close/restart and owner/kernel retirement. Dedicated test
  probes wait on actual admission/CQ state; the thin raw API has no completion
  callback. Same-instance restart retains the allocated UMEM.
  A gated sender admits a final frame before shutdown, then verifies exact peer
  receipt after stop, independently of the backing-retirement checks.
  Eight held-frame profiles cover IPv4/IPv6, TS/SACK options and FIN wire flags.
  Refused endpoint attempts change ACK/window/options without changing any
  outstanding byte; an independent peer compares the snapshot and a separate
  bytewise checksum. FIN flags in these profiles are codec inputs, not a proof
  of the connection's FIN retention lifecycle. Completion-driven progress cases
  cover first DATA, control-buffer exhaustion, reset/close cancellation and
  duplicate-ACK recovery with the application producer idle. Recovery must drain
  its existing queue through `resumeTx()` after CQ and deliver both wire copies.
  Retained-source cases cover both ACK/CQ orders for IPv4/IPv6 DATA and actual
  DATA+FIN references, including three refused endpoint attempts and exact final
  slot return after duplicate ACK. These directly constructed references test
  the supported API; the native packetizer still sends FIN separately from DATA.
  Named-heap diagnostics separate first admission, three held-source refusals,
  actual queued-loss recovery and final owner teardown. They report cache plus
  heap request/return counts and rounded object bytes, with native slot spacing
  reported separately. Snapshotting and printing are test-only, not latency data.
  IPv4/IPv6 graceful-close gates hold the local FIN under a kernel lease, receive
  and retire peer FIN, and refuse final-ACK allocation until CQ returns the slot.
  They verify one completion-driven retry, exact close/owner drain, control-slot
  reuse and independent wire checksums, including IPv4 Ethernet padding.
- `zutcpiptest` uses an independent raw-IP peer codec for both families. It checks
  zero/odd/full-MTU TCP payloads, pseudo-header checksums, protocol/ECN fields,
  borrowed ingress, exact/wildcard endpoint routing, duplicate binding refusal,
  owner-checked unbind and port selection. Generic framing uses fixture-only IP
  protocol 253; TCP uses protocol 6. Route/static-neighbor lookup runs directly
  on Rx and returns a structured result. Ordered lookup/stop coverage checks
  completion before plugin teardown; no route worker or cancellation API remains.
- `zutcpmxtest` installs real MxPlugin plugins on two Zi multiplexers and checks
  mapped connection callbacks, association/watch lifecycle, exact bidirectional
  contexts, nonzero offsets, detached metadata before CQ, one-slot pool reuse,
  stopped acceptors and active stop. Further cases cover DATA/control loss,
  zero/positive/lost receive windows and persist backoff, FIN/RST, pending limits,
  retained tuples and incarnation reuse, delayed packets, PAWS, simultaneous close,
  restart and resource drain. FIFO gates block only the fixture control caller.

The wire helpers `zutcprecovery.py`, `zutcpoptions.py` and `zutcptfo.py` observe
standard TCP independently of the production codec. They validate packet checksums,
sequence/ACK/window positions, original/recovery byte ranges and actual fault
witnesses. Captures require zero kernel capture drops; one profile does not stand
in for another. The fixed zero-window hold is a protocol stimulus allowing the
real one/two-second persist intervals; capture evidence, rather than elapsed time
alone, proves probes and reopening. Transport ACK releases the allocation lease;
independent CQ/kernel retirement gates physical reuse.

## Independent kernel peer and optional application consumers

The separate [interop matrix](../interop/README.md) uses ordinary Linux kernel
TCP peers in both roles over IPv4 and IPv6. Native pair tests alone do not establish
kernel interoperability.

`zutcpapptest` and `zutcptlsapptest` compile the same application fixture against
Ztcp and Ztls respectively. They exercise selected kernel/native backend pairs
after TCP functional completion. Neither library is a Zutcp dependency. Build
only those explicit drivers when their libraries already match the Zi build.
Ztcp, configured Ztls and configured Zdbus belong to the allowed dependency
closure. Preserve the current configuration gates; do not enable optional
libraries to extend coverage. Record missing optional prerequisites separately
from passed native/Ztcp checks.

```sh
make -C zutcp/itest -j8 zutcpapptest zutcptlsapptest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  bash -c 'cd zutcp/itest && prove zutcpapptest zutcptlsapptest'
```

For latency/throughput/profiling use the [benchmark](../bench/README.md) in a
consistent optimized, non-ASan closure. Functional veth success is separate from
performance and physical-NIC acceptance.

## UDP and mixed transport fixture

`zutcpudptest` runs two Zutcp plugins over a private veth pair. It verifies
connected UDP sends, addressed replies, zero-length datagrams, datagram
boundaries, peer addresses, TOS, and delivery into the prepared typed Rx buffer.
Mixed mode also transfers TCP bytes through the same multiplexers and checks
that the typed TCP and UDP allocators share an ingress pool. Both peers close
their UDP endpoints and repeat the exchange after restarting with the original
allocation. In UDP mode the fixture removes and restores the client's permanent
neighbor and replaces its route on a live socket. It also alternates between two resolved
peer IPs and checks that Tx opens no additional route-query sockets.

```sh
make -C zutcp/itest zutcpudptest
sudo -n unshare --mount --net --propagation private \
  env ZPKT_RUN_FIXTURE=1 bash zutcp/itest/zutcpudpfixture.sh 4 mixed
sudo -n unshare --mount --net --propagation private \
  env ZPKT_RUN_FIXTURE=1 bash zutcp/itest/zutcpudpfixture.sh 6 udp
```

The runner accepts either address family with either mode.

## AF_PACKET TCP

`zutcpafptest` opens interfaces through the public Zpkt Env and registers typed
Ztcp allocators through ZiMultiplex. It transfers distinct 4,096-byte streams in
both directions over IPv4 and IPv6 using the AFP socket backend.

```sh
make -C zutcp/itest zutcpafptest
sudo -n env ZPKT_RUN_FIXTURE=1 prove -v zutcp/itest/zutcpafptest
```

Each exchange gives the client and server their own namespaces and veth
interfaces. Counted nftables input rules match the test interface, address
pair and server port 18187. AFP receives packets before the input rules drop
them, and both rule counters must advance. The fixture disables offloads and
captures both directions at peer ingress. It verifies IP and TCP checksums,
exact wire payloads, both FIN acknowledgments and zero TCP resets.
TAP diagnostics report the rule counts and verified byte totals. Failed
exchanges retain their captures and endpoint logs.

## DPDK software ports

Use the current configuration with libdpdk 25.11 or later and its stock
`net_ring` PMD. Build only the allowed configured dependency closure through
zfix, sharing the eight-job make jobserver, then run the driver with privileges
to create a private network namespace.

```sh
make -C zutcp/itest -j8 zutcpdpdktest
sudo -n prove -v zutcp/itest/zutcpdpdktest
```

`zutcpdpdktest` creates its own namespace with `iproute2`. It opens `net_ring_loopa`
through `_loop` and opens `net_ring_zutcp1` through the separate peer name `zdpdkb`.
Static IPv4 and IPv6 routes and permanent neighbors live on Linux dummy links
with different MACs and MTUs from the ports. The driver verifies that Zutcp uses
the configured routing links and the native wire geometry. The independent
software wire copies each frame into a slot from the destination's DPDK Rx pool.
Setup failures fail TAP.

The driver uses real ZiMultiplex/Zutcp ownership transfers and Ztcp stream
callbacks. It runs TCP-only, UDP-only and mixed traffic for both address families,
checks typed endpoint delivery on accept and reconnect, verifies the original
native ingress pool on retained packets and stops without injecting a wake
packet. Separate EAL processes verify fatal ownership violations. Allocation and
activation failure cases exercise rollback, frozen-registry retry, smaller Tx
requests, retained Tx backing through failure and restart, and same-port reuse
after finalization. Interposed allocation and activation failures are verified
through the loaded module with `preload false`. Each caller retains Env until
TCP and all Nif references have retired.

Before starting its forwarding wire, the driver holds a real ring-PMD mbuf while
three connection/endpoint attempts change ACK/window/options. Eight profiles
cover both families, TS/SACK and ordinary DATA/DATA+FIN references. FIN is retained
through the actual send/recovery protocol history and cumulative ACK covers its
extra sequence position. It verifies unchanged whole-frame bytes,
callback entry, the PMD's sole native reference and admission, then same-slot recovery
with independent checksum/header/payload checks. Both ACK/driver-retirement
orders return the slot once. A held-PMD shutdown case releases the reference
only after MxPlugin Closing is observed, then verifies stop/restart and retirement.
