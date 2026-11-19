# Select Zutcp or the regular kernel TCP backend

Zutcp implements TCP for applications using this repository's `ZiMultiplex`
connection and allocator interfaces. Install `Zutcp::Instance` on the multiplexer to
select AF_XDP or DPDK. Leave the plugin unset to use regular kernel TCP. Existing Zi/Ztcp
application framing and callbacks remain in use. Ztls can run above either TCP
backend; it is not a dependency of libZutcp.

This is an application integration choice, not an `LD_PRELOAD` library or a
replacement implementation of arbitrary POSIX `socket()` calls. Applications
using raw kernel sockets continue to use the kernel until adapted to Zi/Ztcp. `zutcpkernelpeer` deliberately uses ordinary kernel sockets;
`zutcppair` deliberately uses the actual native Instance, without socket fallback.

## Select the transport before requesting allocators

The application configures the XDP sender slot before constructing the
multiplexer. Initialize XDP on that stopped multiplexer, move it into Zutcp,
and move the initialized Zutcp plugin into the multiplexer before requesting
any allocators. The [application integration example](../README.md#application-integration)
shows both transfers and the structured error checks.

Applications using kernel TCP construct a fresh multiplexer and obtain its
built-in allocators before starting it. Transport selection is permanent.
`rxBufSize` sets the kernel socket byte limit in kernel mode or the ingress
frame count in native mode. The typed receive handle is selected for each
endpoint, and native Tx allocators can supply registered backing after
`mx.alloc()` or successful start.

Protocol Rx and Tx run on the selected mx owners. Keep application dispatch and
buffer lifetime rules intact. The client/server examples use Ztcp streams and
callbacks with static Instance bootstrap; they do not construct Zutcp connection
classes. The binary [zutcppair.cc](zutcppair.cc) uses the same Ztcp path. Its
server observes EOF through the existing generic Zi receive context, forwards
data to Ztcp, and queues its reply on application Tx. Only the control caller
waits for the Tx barrier and ZiConnection::sync() before requesting physical
graceful disconnect. The Ztcp link remains attached while the peer finishes.

Protocol-specific TFO authorization, tuple history, fault controls and native
pool/drain checks are private roles of [zutcppairtest.cc](../itest/zutcppairtest.cc),
not alternate application transport APIs. Fixtures select those roles when a case
requires private protocol access. Ordinary application runs retain the Ztcp path.
The [FIX benchmark](../bench/README.md) uses the mapped ZiConnection application path.

Stop application work and listeners, drain callbacks, then stop mx. Return all
dependent buffers before destroying mx. The multiplexer finalizes Zutcp, which
finalizes the packet device and releases the backing.

For DPDK, initialize EAL explicitly and select `Zxdp::Type::Dpdk`. The
device parameters carry the DPDK port name, the Linux routing-interface
index, and the optional idle interval. Use a control-plane link with the
same MTU and MAC as the port. Install routes with that output interface and
permanent neighbors for the next hops. Keep the same stopped device/plugin
handoffs and typed Tx and per-endpoint Rx allocation. The existing utility
commands use AF_XDP with the topology shown below. The DPDK bootstrap and
software fixture use matching dummy control-plane links.
See the [DPDK bootstrap](../README.md#dpdk-device-selection) and
[DPDK test commands](../itest/README.md#dpdk-software-fixture).

## Run a self-contained privileged interoperability check

Install the [dependencies](../../docs/DEPENDENCIES.md) first. On a fresh Linux
release tree, configure and build this explicit closure unprivileged:

```sh
./z.config -c -L /chosen/install/prefix
for module in zu zm zt ze zf zi zv zxdp ztcp zutcp; do
  make -C "$module/src" -j8 || exit
done
make -C zi/test -j8 libZiTest.la
make -C zutcp/util -j8
make -C zutcp/example -j8
make -C zutcp/itest -j8
make -C zutcp/interop -j8
```

For an existing build, retain its current `z.config` options and prefix when
regenerating with `-c`; avoid mixing build modes. The first command above selects
Clang release (`-O3 -DNDEBUG`, without ASan); adding `-p` enables profiling in a
separate consistent build. See [BUILD.md](../../docs/BUILD.md). `libZiTest` supplies
the shared fixture/residue helper, not an additional transport dependency.

Run the privileged checks from the repository root:

```sh
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/interop && make test'
```

This uses the existing configured build; it does not configure or rebuild libraries
under sudo. The outer launcher creates only private fixture links and exports the
existing `ZXDP_*` fixture variables. Each interoperability case creates its own
endpoint namespaces, static neighbors and routes, then cleans them up. No local
`review_context` file is needed. Success requires the actual native endpoint and
real Linux kernel peer, exact request/echo and FIN/drain, with recorded backend,
peer settings and packet evidence. See [interop profiles](../interop/README.md).

For the five native drivers, the mapping fixture adds its own scoped routes:

```sh
sudo -n unshare --net bash zutcp/util/zutcpfixture.sh \
  bash zutcp/itest/zutcpmxfixture.sh \
  bash zutcp/itest/zutcpipfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove \
    zutcppairtest zutcppooltest zutcpstacktest zutcpiptest zutcpmxtest'
```

The same launcher can run existing `zxdp/test` and `zxdp/itest` targets. A skipped
privileged case is not a successful native/backend test. Required fixture tools
include sudo/root, unshare, iproute2, ethtool, tcpdump, Bash, Python3 and Perl/prove.
The kernel must supply usable AF_XDP/BPF and namespace facilities; see
[Linux packet prerequisites](../../docs/DEPENDENCIES.md#linux-packet-io).

## Use the endpoint tools on an owned topology

Exact CLIs (the optional count repeats connections in the same process):

```text
zutcppair server INTERFACE LOCAL_ADDRESS PORT COUNT
zutcppair client INTERFACE SERVER_ADDRESS PORT REQUEST_FILE [COUNT]
zutcpkernelpeer server LOCAL_ADDRESS PORT COUNT
zutcpkernelpeer client SERVER_ADDRESS PORT REQUEST_FILE [COUNT]

zutcpd INTERFACE LOCAL_ADDRESS PORT [COUNT] [--wait-eof]
zutcp INTERFACE SERVER_ADDRESS PORT [REQUEST]
```

The first four tools exchange a binary file: client sends request then FIN;
server reads EOF, echoes `echo: ` plus the exact request, then FIN. Client stdout
contains the response; stderr reports role/backend and completed mx stop. Private
protocol roles additionally verify and report native admission/provider drain.
`zutcp`/`zutcpd` are the smaller application examples; their request is
a command-line string rather than the binary utility's file. They also use actual
native transport. These are ordinary TCP application protocols, not wire envelopes.

For a manually provisioned topology, run each command on the appropriate opposite
host or endpoint namespace, with its own interface/address/routes. For example:

```sh
# Kernel-server endpoint:
sudo -n zutcp/util/zutcpkernelpeer server SERVER_IP 18181 1
# Native-client endpoint, with its own owned INTERFACE and request.bin:
sudo -n zutcp/util/zutcppair client INTERFACE SERVER_IP 18181 request.bin > response.bin
```

Reverse roles with `zutcppair server INTERFACE SERVER_IP 18181 1` on the native
endpoint and `zutcpkernelpeer client SERVER_IP 18181 request.bin` on its peer.
For native-to-native, use the native utility at both opposite endpoints:

```sh
# Native server endpoint:
sudo -n zutcp/util/zutcppair server SERVER_INTERFACE SERVER_IP 18181 1
# Native client endpoint:
sudo -n zutcp/util/zutcppair client CLIENT_INTERFACE SERVER_IP 18181 request.bin > response.bin
```
Do not run both commands against local loopback and call it AF_XDP interoperability.
Kernel sockets alone do not require root on these high ports; the fixture examples
use sudo consistently for their namespace and native packet setup.

Native setup requires an existing, up interface with usable queue/AF_XDP support,
explicit appropriate IPv4/IPv6 routes and resolved static neighbors (including a
next-hop neighbor when routed). Zutcp performs interface-constrained cold kernel
route/neighbor queries; it does not implement ARP/ND or a parallel routing service.
Provision these resources deliberately or use the private fixture instead of a
control/management NIC. Interface/queue and XDP attachment ownership must be
exclusive: existing foreign XDP users or UMEM reservations are not overridden.
Use the optional `InstanceParams::netmask` only for the intended destination scope;
without a mask, raw classification selects the whole interface. Keep unrelated
kernel services outside that selected traffic scope. Check backend/mode and
teardown diagnostics rather than assuming hardware zero-copy. The recorded veth
checks prove software-path function, not physical-peer or NIC certification.

For comparable latency/throughput/CPU observations, use the separate
[benchmark scripts](../bench/README.md), preserving the actual workload/settings,
profile build and kernel/native role identities. They create private resources;
no transport/backend change or profiling build is hidden in this quickstart.
