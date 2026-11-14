# Ztcp and Ztls applications through Zxdp

`zutcpapptest` and `zutcptlsapptest` compile the same application driver against the
existing Ztcp and Ztls APIs. Each runs IPv4/IPv6 with an installed Zutcp plugin and
then repeats the equivalent cases without a plugin using kernel TCP. They exchange
an exact 4096-byte request and echo, verify the selected connection backend, then
drain application and transport shutdown. Application Rx/Tx use isolated threads
separate from the mx I/O owners. Native factory setup checks default Tx count
forwarding while mx is stopped, before any start or I/O use.

The fixture creates two private namespaces with matched single-queue veth ends,
explicit source routes and static neighbors. The TLS variant generates an ephemeral
CA/certificate with both test IP addresses, configures that explicit trust anchor,
and verifies the negotiated TLS version and ALPN. All successful test network and
file fixtures are removed; failure output remains for diagnosis.
Ztcp and Ztls are linked only by the corresponding test executables; libZutcp
retains its Zi/Zxdp dependency closure.

```sh
sudo -n unshare --net bash review_context/zutcp-execution/privileged-fixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove zutcpapptest zutcptlsapptest'
```

# Zi connection mapping through Zxdp

`zutcpmxtest` uses two installed Stack plugins on the private veth pair. IPv4
and IPv6 establish through the existing plaintext protocol, using explicit
local connect ports. The fixture checks that both Zi/native association pointers
are installed before callbacks, fresh-stub receive notifications dispatch through
the native connection pointer, and close clears the association before notification.
Stopping accepts preserves established connections. Separate active-stop cases
drain both connected owners and a pending connect before backend shutdown.
The mapped Zi Tx/Rx contexts exchange exact bytes in both directions, including
nonzero send offsets. One-slot application Tx pools remain held while the submitted
metadata detaches and is destroyed before CQ processing. Packet/recovery backing
is independent of that metadata. After cumulative byte acknowledgment and NIC
retirement, both one-slot pools must become free while still connected; a second
exchange verifies backing reuse with the opposite send offsets.
Separate IPv4/IPv6 loss children use zutcpmxlossfixture.sh and 1024-byte messages
with one-slot Tx providers. Two extra veth pairs and a middle Linux bridge let
netem drop actual established DATA while excluding Initial and small ACK/control
packets. Both bridge ports must report drops before removal of the faults. The
mapped contexts then recover, return their original slots and perform a second
exact exchange. The wrapper removes its own links on success and failure; output
and drop witnesses are retained in the test's ordinary residue directory.
The stopped-acceptor cases also exchange bytes
after stopListening(). No kernel socket is created for the mapped connection.
State assertions cover `up()`, completed contexts and exactly one add/remove watch
notification. Cold enumeration runs on control and Tx; queue snapshots remain valid
when their callback closes the connection and stops its listener. Repeated startup
uses the same bounded control-side AF_XDP retirement wait as the carriage fixtures.

After the normal unprivileged build, run:

```sh
sudo -n unshare --net bash review_context/zutcp-execution/privileged-fixture.sh \
  bash zutcp/itest/zutcpmxfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove zutcpmxtest'
```

The fixture installs only private routes and static neighbors for the two native
endpoints. It disables checksum offload on both veth ends. The endpoint addresses
are not assigned to kernel interfaces, so their exchange traverses Zxdp.

# Native client/server through Zxdp

Phase 3 adds IPv4/IPv6 established-DATA loss cases. A fixed-header u32 filter
selects large Short packets, excluding Initial and small ACK/control traffic.
Both fault directions require a nonzero drop counter before removal; the pair
must recover the exact request and echo through DATA RTO. These supplement the
setup-loss and duplication regressions below; covering retransmission and the
remaining Phase 3 loss/ACK matrix are still open.

`zutcppairtest` runs the actual examples in two scoped network namespaces with a
bridge/veth hop. Both IP families verify an exact 7200-byte request and echo,
first-setup-flight loss in each direction, and 100% duplication of the selected
transport traffic. Netem operates on the middle ports because AF_XDP NIC Tx
bypasses its egress qdisc. Setup-loss/duplication filters select the IP family
and TCP test port; DATA-loss filters select the fixed-header geometry above.
Target loss/duplication counters are checked. Both processes run
with ASan/leak detection and must drain successfully.

Examples use `zutcpclient INTERFACE SERVER PORT [REQUEST]` and
`zutcpserver INTERFACE SERVER PORT [COUNT]`. They install Stack before application
initialization. Native transport storage comes from Stack-owned pools; only
external framework/application requests use the mx allocator factory. Rx/Tx are
the dedicated mx owners; a third existing worker performs cold setup.

This is the Phase 1 native transport regression pair. Preserve the copied
protocol behavior while checking the TLS removal and UDP-to-IP/Zxdp conversion.
Broader resource-pressure, fairness and lifecycle-race matrices are subsequent
work. See ../zutcp_plan.md for the corrected phase boundary.

# Direct IP carriage through Zxdp

`zutcpiptest` uses an independent Linux raw-socket fixture peer for IPv4 and IPv6.
The Zutcp side receives and submits through Stack/Zxdp. Generic IP cases use
fixture-only protocol 253; TCP cases use protocol 6 with ports and the base header.
The peer independently constructs/checks TCP bytes and pseudo-header checksums,
while Linux supplies its IP framing. Both families cover zero/odd/full-MTU TCP
payloads, borrowed ingress backing, protocol/ECN fields and exact output.
Cold route/static-neighbor lookup remains covered separately.

This verifies IP/TCP framing and raw packet service, not the
Initial/response/ACK transport working-pair gate. TCP sequence/state/window
semantics are not implemented by these codecs. The native protocol regression
pair above supplies the completed Phase 1 working-pair evidence.

The TCP cases bind native Stack endpoints and verify exact-over-wildcard routing,
duplicate refusal, owner-checked unbinding and unused port selection. They check
successful and failed cold lookup on a non-I/O mx worker, and hold that worker to
prove shutdown gates ingress and waits for its pending result. Receive views keep
the original registered backing. These tests use three mx threads so cold setup
has a worker outside Rx/Tx.

Linux can return EBUSY while a recently closed AF_XDP queue is still retiring.
Carriage startup and the Stack restart case explicitly retry on the fixture's
control caller for at most two seconds, preserving the same providers and logging
attempt counts. This does not claim single-attempt immediate queue reuse and adds
no delay to a packet owner.

Build unprivileged with `make -j8`, then run the compiled integration programs
inside the fixture (no privileged compilation):

```sh
sudo -n unshare --net bash review_context/zutcp-execution/privileged-fixture.sh \
  bash zutcp/itest/zutcpipfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove zutcppairtest zutcppooltest zutcpstacktest zutcpiptest'
```

The added fixture configures only the private single-queue pair: a kernel peer
address, an on-link route to the raw destination, and a permanent neighbor for
each family. It disables peer Tx checksum offload so veth carries completed wire
checksums instead of CHECKSUM_PARTIAL metadata. Namespace destruction cleans up
these resources. Without this setup the new program visibly skips.

# Phase-1 packet Stack integration

`zutcpstacktest` registers `Zutcp::Stack` as the mx plugin and exercises the
single-queue veth pair supplied by the same scoped fixture below. It checks
factory geometry, backingless Rx stubs, independent application/control pools,
submission detachment, unchanged backing on refusal, exact peer bytes, direct pool
CQ recycling, mx wake delegation, depleted ingress recovery after an off-owner
return, asynchronous shutdown and restart after final application release.
It also depletes ingress and replaces two receive aliases with heap storage off
the Rx owner, then verifies provider notification resumes reception while both
replacement descriptors remain alive.
Slot-accounting checks charge aliases once, reconcile before reuse and preserve
an account across backend shutdown until a late final application return.
After restart, actual connection DATA admission fills the configured shared cap
across two connections, checks per-connection/shared refusal, services contiguous
and gap input without retaining another physical frame, and verifies capacity
restoration after teardown and final application return. Cold validation rejects
an oversized reserve without changing pool geometry; snapshots show requested
and effective limits for default and nondefault configurations.
Startup rejection/retry remains separate work. This establishes packet service;
the native pair above supplies the completed Phase 1 working-pair evidence.

The internal two-slot recovery case keeps a sent-record caller lease across CQ
and declared loss. A late ACK returns its exact slot while a copied `SentPkt`
snapshot stays alive. Refusal leaves metadata attached and its recovery lease
empty. Accepted backing survives submitted metadata destruction and backend
close, then returns on recovery reset before restart. The actual native pair
also exercises the builders' lease handoff. The source-storage case transfers a caller lease into the existing queued-range
node and clears submitted Zi metadata. A retained frame can still supply its
original byte after the sent record retires and the backend closes; final frame
release drains the pool before restart. This tests the storage primitive.
The native writer case fills both producer slots and submits each original
payload address without a final allocation. Zero initial credit rejects a writer;
the first positive update enables a new one, and partial subsequent credit leaves
a whole buffer queued. An oversized ACK uses a separate reserved control slot before data.
An independent peer verifies both original payloads. These checks validate the
native first-send wiring; covering retransmission, cap-resegmentation and CQ
waiter test extensions have been removed with their implementation.
The raw odd-offset CQ regression is in zxdp/itest/zxdppackettest.

Control pressure fills all six application slots across independent pools.
With capacity eight and three reserved credits, five data packets submit, the
sixth stays attached on refusal, and two control packets still reach the peer.
The snapshot verifies logical charges return at CQ while recovery retains one
control buffer. Resize while held fails without invalidating its bytes; after
final return, stopped Hub reinitialization changes only the control count. The
writer case separately sends its oversized ACK from reserved control backing
before submitting its two original producer buffers. These are backend resource
and native builder checks; remaining connection pressure/fairness gates are open.

The depleted-pool case retries peer traffic within a bounded deadline after
off-owner release. A packet can arrive before asynchronous FILL replenishment
and be dropped. The retry posts no extra mx work or wake; success requires
delivery into the one released frame. It does not assume that the first packet
survives an empty FILL ring.

Build unprivileged before entering the fixture so build artifacts retain their
normal owner. The namespace needs root for AF_XDP; it does not need to rebuild
programs as root. Reuse the Clang/debug/ASan tree with leak detection enabled.

# Phase-1 raw pool/provider integration

`zutcppooltest` verifies the Zutcp-owned Zi provider against a real raw Zxdp
instance. It requires root and `ZXDP_TEST_IFINDEX` naming a fixture-owned Ethernet
interface; otherwise it visibly skips. The existing scoped namespace fixture in
`review_context/zutcp-execution/privileged-fixture.sh` supplies a two-queue veth.
Build from the top level with `make -j8`, then run the suite inside that fixture
with `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`.

The test checks provider geometry before activation, independent application and
control reserves, unchanged backing through header prepending, metadata destruction
before CQ, deferred pool recycling, retained backing past CQ, sequential raw
resubmission and caller-held backing after instance close.
This is initial provider composition, not the stripped-protocol working-pair gate.
Native client/server coverage above replaces the copied kernel endpoint tests.

# Captured byte recovery regressions

The native pair includes IPv4/IPv6 reorder, ACK-loss and partial-ACK cases.
These cases require tcpdump and Python 3 as well as the existing iproute2/netem
and ethtool tools. Captures run on middle bridge egress after qdisc processing.
The fixture checks complete request/echo bytes and every decoded DATA slice,
requires actual out-of-order first-seen byte positions in each direction, and
records repeated original packet extents with exponential retry spacing.

ACK-loss cases select established ACK-only TCP headers with byte frontiers of
1024 or greater while delaying all server DATA and FIN packets, including small
final segments. Partial-ACK cases clone a real dropped peer ACK's packet number
and ACK ranges, then inject TCP ACK 536 with PING about 150 ms into the flight.
That smaller frontier passes the fixture's ACK-loss selector. The helper recomputes
IP/TCP checksums. Captures require receipt acknowledgment, RTO restart about
200 ms after progress and retransmission of the full original covering packet.

The fixture-only zutcprecovery.py helper generates that controlled peer input and
decodes the current plaintext intermediate format offline. It validates the
default eight-byte CIDs from captured Initial packets, checks zero kernel capture
drops, and retains injection details, pcaps and regression-evidence.json in the
normal test residue directory. It is independent of the production I/O path.

# Live mapped zero windows

The mapped fixture also runs IPv4/IPv6 zero-window/persist cases through the same
Zutcp plugin and one-slot registered Tx providers. StackConfig::maxData forwards
the existing initial receive byte limit to both mapped roles. Its configured
default remains 16 MiB; the current unscaled native profile caps the effective
limit at 65535 bytes. Each case uses a 1024-byte limit and initially unarmed Zi receive contexts.
The first packet is acknowledged while application input remains queued. A second
locally admitted send retains its metadata/backing until receive is armed after
3.3 seconds; it then transmits without another application send. Exact delivery,
once-only slot return and normal callback/observer shutdown remain asserted.

The existing middle-bridge loss fixture has a window mode. Two tcpdump captures
observe probes near one and three seconds, closed-edge responses and opening
advertisements followed immediately by queued DATA. zutcprecovery.py window checks
probe spacing, all 2048 expected bytes per direction, response values and zero
packet-capture drops, recording window-evidence.json. TCP header SEQ, ACK and
window fields carry byte progress and receive credit. Native output no longer
uses extended byte-ACK, MaxData or DataBlocked records. Persist uses the retained
intermediate PING with SEQ one byte behind the send frontier; packet ACK ranges
remain for bootstrap/control bookkeeping.

The lost-window cases drop the first opening ACK on both bridge ports. Captures
require later persist probes near seven seconds, fresh opening responses and
queued DATA resumption, with positive drop witnesses on both ports. A responsive
zero window pauses DATA RTO without reducing the congestion window. These tests
retain the intermediate setup envelope; they do not establish TCP interoperability.

The restart variant runs two full lifetimes on the same mx/Stack and registered
allocators, changing receive credit from 1024 to 512 bytes between stopped starts.
It repeats held-receive, probe/backoff/reply, immediate reopening, one-slot return
and observer/connection drain checks in both incarnations. The offline decoder
separates their current intermediate destination IDs and verifies each profile
independently, writing restart-evidence.json. This covers settings refresh and
owner retirement; final TCP closed-tuple/sequence history remains later Phase 4 work.

The native pair also runs IPv4/IPv6 `wait-eof` cases. The example server's optional
`--wait-eof` flag defers its existing echo until the peer has delivered FIN and all
request bytes. This sample mode holds the bounded request in the existing reader;
the fixture request fits the initial receive window. The default server still
streams its response. The exact request/response framing is unchanged. A callback
guard and `half-close-evidence.json` establish positive response generation after
peer EOF, with FIN-before-first-response captures and ordinary backing/owner drain.
