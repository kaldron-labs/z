# Direct IP carriage through Zxdp

`zutcpiptest` uses an independent Linux raw-IP fixture peer for IPv4 and IPv6.
The Zutcp side receives and submits through the raw packet Stack/Zxdp. It checks
zero/odd/full-IP-MTU payloads, borrowed ingress backing, protocol/ECN fields,
exact output and cold route/static-neighbor lookup. There are no UDP headers or
ports. Protocol 253 is a fixture value passed explicitly to the generic IP
writer; this does not select the production intermediate transport profile.
IP framing does not supply a transport payload checksum. This verifies the IP
carriage primitive, not the Initial/response/ACK transport working-pair gate.
The copied live protocol still uses kernel UDP, an open stripping violation.


Build unprivileged with `make -j8`, then run the compiled integration programs
inside the fixture (no privileged compilation):

```sh
sudo -n unshare --net bash review_context/zutcp-execution/privileged-fixture.sh \
  bash zutcp/itest/zutcpipfixture.sh \
  env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
  bash -c 'cd zutcp/itest && prove zutcptxqueuetest zutcppooltest zutcpstacktest zutcpiptest'
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
Startup rejection/retry remains unverified. This establishes packet service;
the stripped native transport working-pair gate is still incomplete.

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
The following copied endpoint test remains baseline coverage during adaptation.

# QUIC endpoint integration

Build with `make -C zutcp/itest -j8`, then run
`make -C zutcp/itest test`.

`zutcptxqueuetest` uses real endpoints on ephemeral loopback ports. It checks
FIFO datagram delivery through the common ZiTx completion path.

On Linux it also interposes `sendmsg` in the test executable to return EAGAIN
for one selected physical socket. This holds the active head while 4095
followers are admitted. The next packet must fail admission, with one drop
and one backpressure increment, without increasing submitted bytes. Releasing
the fault calls the original Linux syscall for all packets; the production
writer, queue and completion loop are unchanged.

Four successive full queues exercise 16384 completed datagrams. Each drain
has another scheduled Tx task waiting, which submits to a second endpoint.
The test requires that task and endpoint to progress after every bounded
turn, and logs `maxTurnUS`, `maxThreadCPUUS` and `maxScheduledDelayUS`. Thread
CPU time separates the drain's processing cost from wall-clock descheduling.
These are observed debug build latencies, with no machine-independent latency
threshold. ZiTx services the queued successors in one common completion turn; it does not reproduce
the former per-packet scheduler continuation. The primary receiver permits
kernel UDP drops under this burst, but requires increasing packet numbers
without duplicates; the independent receiver must receive every progress
marker. Completion counters and intercepted successful syscalls must account
for all 4096 packets per drain.

The reopen case holds active and queued packets on the old physical socket,
disconnects and drains it, then opens a new socket and verifies that only new
output reaches the receiver. It
then stops with another held active queue, restarts and verifies the next
packet's source and sequence. All endpoint closes finish before mx shutdown.
Successful runs leave no files or listeners behind.

`testBadUDPBackend` characterizes an injected backend contract violation using the
same native common writer. It injects a positive short result of one byte for
a four-byte UDP output, without issuing that first syscall. The unmodified
writer then calls `sendmsg` with the original pointer plus one and length
three; the real receiver obtains that three-byte suffix as a datagram, while
local completion accounts for four bytes. The test records behavior after invalid backend input; suffix retry is not
supported UDP behavior, and this does not identify a ZiTx defect. Native UDP output is expected to complete
the full datagram or fail. Zi production code remains unchanged by this
characterization, and the normal output tests never inject a short result.

For a repeatable profiling sample after the combined ZiRx/Zutcp repairs have
both been built, run `./zutcp/itest/zutcptxqueuetest` and retain its TAP
diagnostic line. For syscall/CPU profiling, run
`./libtool --mode=execute perf stat -e task-clock,context-switches,cpu-migrations ./zutcp/itest/zutcptxqueuetest`
where host perf permissions permit. Do not compare the measured debug drain
turn with a release throughput result or infer protocol pacing from it;
QUIC pacing remains above the completed-packet queue.

`zutcpiptest` now includes cold IPv4/IPv6 route and static-neighbor resolution.
The scoped fixture supplies direct, gateway/route-MTU, missing-neighbor and
dynamic-neighbor cases. Tests check interface/source/MAC selection, explicit
source preservation, failure publication and refusal of the wrong interface.
The direct-IP carriage cases use Zxdp and the independent raw-IP fixture peer. This helper coverage does not
establish native connection integration or physical NIC behavior.
