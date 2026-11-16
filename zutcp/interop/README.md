# Independent kernel TCP peer

`zutcpkerneltest` runs nine private-veth profiles in both native client/kernel
server and kernel client/native server roles over IPv4 and IPv6 (36 cases). The kernel utility uses
ordinary TCP sockets. The native utility installs an actual `Zutcp::Stack` and
uses public client/server roles over AF_XDP; there is no kernel socket fallback.

Build `zutcp/util` and `zutcp/interop` after the required lower modules. Run
`make -C zutcp/interop test` under the repository's privileged private-network
fixture launcher with `ZXDP_RUN_FIXTURE=1`. An intentionally unavailable
privileged environment produces a visible driver skip; such a skip is not
interoperability acceptance.

The application sends a binary request followed by FIN. After EOF, the server
sends `echo: ` followed by exactly those bytes and FIN. The baseline request is
8192 bytes and includes NUL and every byte value. Both utilities also support
repeating connections within the same process; the baseline uses one connection.

Each case retains request/output/error files, role identities, kernel version,
kernel SACK/WS/TS/ECN/TFO settings, offload settings, two directional captures and
`tcp-evidence.json` in its test residue directory. The standalone Python checker
verifies checksums, standard TCP options, raw application payload, sequence
positions, EOF framing and final FIN acknowledgments. Native stderr additionally
reports the stopped Stack's empty connection/Tx admissions and returned controls.
This veth evidence does not establish physical-NIC zero-copy capability or
independently prove canonical backing-pointer identity.

The baseline leaves kernel options at recorded namespace defaults. Native offers
also use transport defaults; `ZUTCP_PEER_OPTIONS` selects SACK/WS/TS/ECN bits
1/2/4/8. The fixture profile argument selects `baseline`, `absent`, `loss`,
`window`, `repeat`, `reset`, `tfo`, `tfo-fallback` or `reorder`. No Ztcp or Ztls
application fixture is invoked by these tools.

* `absent` disables native SACK/WS/TS/ECN offers and the corresponding kernel
  namespace policies. Captures require actual SYN/runtime absence, including
  Not-ECT IP carriage, while preserving MSS and exact byte/FIN completion.
* `loss` learns the actual client sequence origin before releasing a main-thread
  FIFO. A middle-bridge filter drops the first large DATA original at that exact
  sequence, leaving handshake controls untouched. Actual qdisc drops and separate
  source/destination captures prove loss, out-of-order receipt, retransmission and
  exact gap recovery. The kernel state machine performs its own recovery when it
  is the sender; no scripted peer ACK or retransmission is substituted.
* `window` sends 32768 request bytes while the server application holds input.
  The kernel server uses its actual `SO_RCVBUF` setting; the native server keeps
  its existing Reader unretired under a 4096-byte receive window. The application
  main thread waits for an observer to witness zero-window advertisement, an
  actual sender probe and a repeated zero-window reply. Only then does it release
  the read FIFO. Captures require window reopening, remaining exact DATA and both
  FIN directions; native owner/provider drain remains mandatory.

FIFO waits run on utility main threads. Native callbacks only signal readiness or
retain the existing Reader and return. The observer reads raw frames but never
transmits a TCP segment. Kernel version, effective namespace TCP policies,
offloads, pcaps, fault counters and `tcp-evidence.json` remain in each residue.
These profiles complement the controlled backing-lifetime assertions. Repeated
sessions, reset and opt-in kernel TFO have the separate profiles below.

The `repeat` profile performs two complete exchanges using the same utility
processes, native App/Stack and server listener. Each flow has independent tuple
and SYN-origin decoding, exact bytes and both eventual FIN acknowledgments. Native
clients report completion of each public disconnect callback before opening the
next connection. Kernel clients open after the prior response EOF and local close;
acknowledgment of their earlier FIN may still arrive after the next SYN. The
checker retains that complete FIN evidence without imposing cross-flow packet
serialization. The final native provider/connection drain remains mandatory.

The `reset` profile first sends a 260-byte request prefix without FIN. A read-only
observer waits for actual peer cumulative acknowledgment before releasing the
client main thread. The native client uses public numeric disconnect with a full
64-bit local error; the kernel client uses `SO_LINGER{1,0}` and ordinary close.
The first connection must carry RST without FIN or reflection. The same listener
and App/Stack then perform a fresh complete binary request/echo/FIN exchange.

For the kernel-client reset case only, the native server explicitly seeds a
same-key closed-history record after actual establishment while its live owner
and slot remain present. This is labeled fixture history, not a prior wire flow.
Both SYNs must negotiate timestamps. The real kernel's reset options are recorded
without alteration; missing-timestamp exemption evidence is reported only when
that actual reset omits timestamps. The live peer-reset callback must run once
after its slot is released, and the
normal close path must replace the seeded record with a new non-FIN history ID.
This checks live-owner routing and the missing-timestamp RST exemption together.

The opt-in `tfo` and `tfo-fallback` profiles each use two real connections. The
kernel namespace explicitly enables client/server TFO bits, and the independent
kernel utility uses `TCP_FASTOPEN` on its listener or blocking `MSG_FASTOPEN` for
its first 260 request bytes. The first flow must request and obtain an actual
cookie. The second authorized flow must send that cookie plus exactly 260 SYN
bytes and receive a SYNACK acknowledging them. Remaining request bytes, response,
FIN and native drains stay ordinary TCP and exact.

Fallback denies the native sender's second early authorization after its cookie
was learned, or denies the native receiver's second early delivery. Captures
require no SYN data for sender denial, and actual kernel ordinary-byte retry
after a SYN-only acknowledgment for receiver denial. Application logs verify
once-only prefix enqueue/delivery and authorized early callback ordering.
Unsupported kernel sysctl, socket option or send operation is an explicit error
with recorded errno; ordinary fallback alone never counts as positive TFO proof.

The `reorder` profile delays the exact first large request DATA sequence by 20ms
at the middle bridge without dropping it; later DATA bypasses that delay. A
read-only observer must see the exact original frame at the source and then at
the destination after a higher-sequence arrival. Captures remain open until
that original arrives and the actual qdisc backlog is empty, even if TCP recovery
finishes the application first. Exact bytes/FIN/native drain and zero qdisc drops
remain required. Retransmissions are allowed and do not imply a unique wire ID.
This fault delay is not a throughput or latency acceptance threshold.

During `reorder`, a queued old DATA copy can reach the receiving host after both
FINs were acknowledged and its application endpoint has closed. Only this profile
recognizes the exact standard CLOSED response: bare RST, reversed same tuple,
sequence equal to that DATA's ACK, zero ACK/window/options/payload. Every reset
must follow both FIN acknowledgments and have its own unmatched, source-exact
first-sequence DATA copy queued before close and delivered afterward. New source
output or reset reflection remains forbidden. The report attributes this to the
receiving host after close, not to an active native backend. Ordinary flow FIN,
no-loss, original-frame identity and qdisc drain checks remain unchanged.
