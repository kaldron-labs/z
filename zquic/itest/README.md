# QUIC endpoint integration

Build with `make -C zquic/itest -j8`, then run
`make -C zquic/itest test`.

`zquictxqueuetest` uses real endpoints on ephemeral loopback ports. It checks
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

The rebind case holds active and queued packets on the old physical socket,
rebinds, and verifies that only replacement output reaches the receiver. It
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

For a repeatable profiling sample after the combined ZiRx/Zquic repairs have
both been built, run `./zquic/itest/zquictxqueuetest` and retain its TAP
diagnostic line. For syscall/CPU profiling, run
`./libtool --mode=execute perf stat -e task-clock,context-switches,cpu-migrations ./zquic/itest/zquictxqueuetest`
where host perf permissions permit. Do not compare the measured debug drain
turn with a release throughput result or infer protocol pacing from it;
QUIC pacing remains above the completed-packet queue.
