# zquic recovery findings

These findings concern the existing QUIC implementation, independently of any
proposed reuse for TCP. They were identified by source inspection against
RFC 9002. Source repairs and permanent regression sources have now been added.
Native x86-64 Clang debug validation now passes the full QUIC suite, including
the permanent recovery/ACK/RTT regressions. The independent pre-repair baseline
fails the intended recovery-boundary, ACK-growth and falling-RTT assertions with
setup controls passing. Phase 4 QIR has completed: core is green under the accepted third-party policy,
and all 16 supported loss/long-RTT cells pass. Supplemental rebinding retains
verified baseline failures. See `zquic_status.md`
for exact source/configuration, artifacts, accepted exceptions and coverage
limits, and `zquic_repair_audit_inventory.md` for source dispositions. The
descriptions below describe the original defects.

1. **Recovery epochs can cause repeated congestion-window reductions.**

   In `zquic/src/ZquicRecovery.hh:549`,
   `NewReno::congestionEventAt()` stores the lost packet's send time in
   `m_recoveryStartTime`. The runtime caller,
   `Link::applyLossUpdateTx_()` in `zquic/src/ZquicLink.hh:6792`, supplies
   the lost packet send timestamp.

   Consequently, two packets sent before recovery began, but reported lost
   separately, can each trigger a congestion-window reduction if the second
   packet has a later send timestamp. This incorrectly treats losses from
   one recovery epoch as separate congestion events.

   The recovery boundary should be the time recovery begins. Packet send
   timestamps should be compared against that boundary to suppress further
   reductions for packets transmitted before or at the start of recovery.
   See [RFC 9002, Appendix B.6](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-B.6).

2. **ACKs can grow the congestion window during recovery without checking
   their transmission epoch.**

   In `zquic/src/ZquicRecovery.hh:525`, `NewReno::ackd()` accepts acknowledged
   bytes and a PMTUD-probe flag, but no packet send timestamp. It increases
   the congestion window for ordinary acknowledged bytes without checking
   whether those bytes were transmitted before recovery began.
   `Link::applyAckUpdateTx_()` in `zquic/src/ZquicLink.hh` passes aggregate
   `normalAckdBytes` directly to this method without that filtering.

   This allows acknowledgements of packets sent before recovery began to
   increase the congestion window. Such ACKs should retire bytes in flight
   but should not grow the congestion window. Aggregation must preserve or
   first classify the information needed to distinguish eligible ACKs.
   See [RFC 9002, Appendix B.5](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-B.5).

3. **Time-threshold loss detection can become too aggressive when RTT falls.**

   In `zquic/src/ZquicRecovery.hh:487`, `RttEstimator::timeThreshold()`
   selects the latest RTT when available instead of taking the maximum of
   latest and smoothed RTT. Its comparison with minimum RTT does not
   compensate for this difference.

   For example, after RTT samples of 100 ms and 10 ms, with no ACK-delay
   adjustment, the smoothed RTT is 88.75 ms. The implementation computes a
   loss threshold of 11.25 ms, whereas the required calculation produces
   approximately 99.84 ms. This can declare packets lost prematurely and
   trigger unnecessary retransmissions and congestion responses.

   The threshold should be
   `max(granularity, (9 / 8) * max(latest_rtt, smoothed_rtt))`, with the
   appropriate initial-RTT handling before samples are available.
   See [RFC 9002, Appendix A.10](https://www.rfc-editor.org/rfc/rfc9002.html#appendix-A.10).
