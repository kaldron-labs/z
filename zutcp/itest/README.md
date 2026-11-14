# Native TCP integration tests

These Linux tests exercise actual Stack/Zxdp packet I/O, standard TCP headers
and raw application bytes. They require usable AF_XDP/BPF, sudo/root, network
namespaces, iproute2, ethtool, tcpdump, Python3 and Perl/prove. Missing privileges
or fixture variables produce visible skips; a skip is not native acceptance.
See the [kernel/native quickstart](../util/README.md) for application integration,
provider lifetime, interface ownership, routes, static neighbors and sudo setup.
Physical Ethernet operation remains unverified in the current environment.

## Build and run

Build the configured Zi/Zxdp/Zutcp dependency closure first, then `zutcp/src`,
`zutcp/util` and `zutcp/example`, using `make -C DIRECTORY -j8`. Build the five
native drivers explicitly to avoid building optional application consumers:

```sh
make -C zi/test -j8 libZiTest.la
make -C zutcp/itest -j8 zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  bash zutcp/itest/zutcpmxfixture.sh \
  bash zutcp/itest/zutcpipfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
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

- `zutcppairtest` launches the actual native client/server examples over both IP
  families. It checks exact 7200-byte request/echo, half-close, SYN/setup loss,
  established DATA loss, duplication, reordering, ACK loss, partial ACK, SACK holes
  and reneging, option negotiation/absence, ECN, TLP and Fast Open. Middle-bridge
  filters affect actual packets; capture/checker evidence establishes the intended
  fault, byte progress, recovery and FIN completion. It does not use kernel TCP
  as a native fallback.
- `zutcppooltest` checks the Zutcp-owned Zi provider against real Zxdp arenas:
  factory geometry, registered backing, borrowed/native views, allocation versus
  kernel leases, exhaustion, retirement and safe return/reuse.
- `zutcpstacktest` exercises actual raw Rx/Tx service, independent peer bytes,
  native first-send backing identity, replay/recovery lifetime, control admission
  under DATA pressure, close/restart and owner/kernel retirement. Dedicated test
  probes wait on actual admission/CQ state; the thin raw API has no completion
  callback. Bounded lifecycle retries allow asynchronous kernel queue release.
- `zutcpiptest` uses an independent raw-IP peer codec for both families. It checks
  zero/odd/full-MTU TCP payloads, pseudo-header checksums, protocol/ECN fields,
  borrowed ingress, exact/wildcard endpoint routing, duplicate binding refusal,
  owner-checked unbind and port selection. Generic framing uses fixture-only IP
  protocol 253; TCP uses protocol 6. Cold route/static-neighbor lookups run outside
  Rx/Tx; a held worker tests cancellation and shutdown drain.
- `zutcpmxtest` installs real Stack plugins on two Zi multiplexers and checks
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
only those explicit drivers when these optional consumer checks are intended and
their libraries match the Zi build; do not rebuild a broad dependent closure.

```sh
make -C zutcp/itest -j8 zutcpapptest zutcptlsapptest
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove zutcpapptest zutcptlsapptest'
```

For latency/throughput/profiling use the [benchmark](../bench/README.md) in a
consistent optimized, non-ASan closure. Functional veth success is separate from
performance and physical-NIC acceptance.
