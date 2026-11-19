# Native TCP and UDP integration tests

These Linux tests exercise actual Instance/Zxdp packet I/O, standard TCP headers
and raw application bytes. They require usable AF_XDP/BPF, sudo/root, network
namespaces, iproute2, ethtool, tcpdump, Python3 and Perl/prove. Missing privileges
or fixture variables produce visible skips; a skip is not native acceptance.
See the [kernel/native quickstart](../util/README.md) for application integration,
provider lifetime, interface ownership, routes, static neighbors and sudo setup.
Physical Ethernet operation remains unverified in the current environment.

## Build and run

Use [the repair plan](../../arc/zutcp/zutcp_repair3.md)'s scoped Clang release rebuild and
freeze restrictions. Rebuild the listed dependency libraries before `zutcp/src`
and its consumers, sharing one make jobserver with total concurrency 8. The
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
links and exports the existing `ZXDP_*` fixture variables. The mapping fixture
adds private routes and static neighbors and disables checksum offload. Native
addresses are not assigned to kernel interfaces. Each subprocess fixture owns
and cleans its extra namespaces/links. No private `review_context` script is
required. Tests retain failure diagnostics through the Zi residue helper.

The ordinary directory `make test` target also includes the optional Ztcp and
Ztls application drivers below. Use explicit native targets while developing TCP.

## Driver coverage

- `zutcppairtest` launches the Ztcp client/server examples with statically
  configured Instance plugins for ordinary transfer and half-close over both IP
  families. Protocol-private modes run native protocol roles in the test driver;
  they do not expose test controls through application APIs. It checks exact 7200-byte request/echo, half-close, SYN/setup loss,
  established DATA loss, duplication, reordering, ACK loss, partial ACK, SACK holes
  and reneging, option negotiation/absence, ECN, TLP and Fast Open. Middle-bridge
  filters affect actual packets; capture/checker evidence establishes the intended
  fault, byte progress, recovery and FIN completion. It does not use kernel TCP
  as a native fallback.
- `zutcppooltest` checks Zi allocators backed by one shared Zxdp arena:
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
- `zutcpiptest` uses an independent raw-IP peer codec for both families. It checks
  zero/odd/full-MTU TCP payloads, pseudo-header checksums, protocol/ECN fields,
  borrowed ingress, exact/wildcard endpoint routing, duplicate binding refusal,
  owner-checked unbind and port selection. Generic framing uses fixture-only IP
  protocol 253; TCP uses protocol 6. Route/static-neighbor lookup runs directly
  on Rx and returns a structured result. Ordered lookup/stop coverage checks
  completion before plugin teardown; no route worker or cancellation API remains.
- `zutcpmxtest` installs real Instance plugins on two Zi multiplexers and checks
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
Ztcp belongs to the allowed rebuild set; do not rebuild Ztls, Zdbus or another
unlisted module to enable optional coverage. Record missing optional prerequisites
separately from passed native/Ztcp checks.

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
allocation.

```sh
make -C zutcp/itest zutcpudptest
sudo -n unshare --mount --net --propagation private \
  env ZXDP_RUN_FIXTURE=1 bash zutcp/itest/zutcpudpfixture.sh 4 mixed
sudo -n unshare --mount --net --propagation private \
  env ZXDP_RUN_FIXTURE=1 bash zutcp/itest/zutcpudpfixture.sh 6 udp
```

The runner accepts either address family with either mode.
