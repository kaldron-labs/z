# Goldfish Annotated Review Report

**Target:** [design_draft.md](./design_draft.md), full feature specification. Round-1 findings refer to the [641-line snapshot](./review_context/stage7-round1-design.md).

**Status: resolved.** All three Major findings and four style Nits from round 1 have dispositions: design-1-1 is resolved by the author's source change and corresponding design clarification; design-1-2 and design-1-3 are resolved by author-adopted adaptations of existing Zquic mechanisms; the four style fixes are applied. Structural reviewer completed with no findings. No backend implementation/tests were performed by this design review. Author instructed proceeding onwards; handoff to Stage 8.

## Prioritized findings and proposed fixes

### design-1-1 — Design — Major: Initial backing exhaustion

**Disposition:** Resolved. Source inspection confirms ZiIOBufPluginAllocator::allocBuf now returns null before constructing Buf when initial backing allocation returns null. ZiTxStream propagates that result through its existing sticky failure path. The draft records this behavior. No tests/builds were run; backend exhaustion and caller recovery tests remain implementation verification. The proposed edit below records the original round-1 recommendation.

- **Affected scope:** Snapshot lines 239–261 and 306–312, buffer adapter and pool contracts.
- **Discrepancy:** Fixed-pool exhaustion is described as backpressure, but the selected adapter always creates an IOBuf even when the backing provider returns null.
- **Rationale:** A backingless object is neither an empty buffer nor marked failed. Waiting can block the Tx owner; fatal failure conflicts with ordinary bounded exhaustion. See ZiIOBuf.hh::IOBufPluginAllocator::allocBuf and ZiTxStream.hh::allocBuf.
- **AI suggestion: PROPOSE FIX.** Make the initial-allocation failure path explicit and compatible with existing consumers. This requires a narrowly scoped Zi adapter change in the future implementation plan; it does not require Ztcp/Ztls changes.
- **Proposed edit:** In the current allocation contract, add: “When an initial pool allocation has no frame, the provider returns null immediately. Zi::IOBufPluginAllocator::allocBuf must check the returned backing and return an empty reference without constructing a buffer object. Ordinary exhaustion never exposes backingless storage, blocks an I/O owner, or invokes heap fallback. ZiTxStream retains its existing failed-allocation behavior: the current writer fails rather than silently becoming a pending allocation. Retry or later-send admission follows the existing caller's failure contract once capacity returns; no automatic retry of a failed writer is promised. Allocation admission and failure recovery must be verified for each supported upper-layer caller.”
- **Implementation/verification consequence:** Enumerate the ZiIOBuf adapter guard and tests in the detailed plan. Exercise empty pools through the actual mx factory, including all payload frames retained for peer ACK, and verify empty-result propagation, protocol progress, and a later valid allocation after recycling. Keep the fatal growth rule separate.

### design-1-2 — Design — Major: Receive recovery capacity

**Disposition:** Resolved by author decision. Reuse and adapt Zquic's existing receive admission, flow control, packet-backed reassembly and credit retirement to fixed UMEM frame availability. Preserve ingress allowance for control and gap-filling data, and verify recovery under retained-suffix pressure. Apply admission before acknowledgment commitment; preserve accepted data and each intermediate protocol's acknowledgment contract. Concrete bounds/priorities remain implementation planning. The broader original proposed edit below is historical; the adopted resolution is the narrowed adaptation in the draft. No production implementation or re-audit has run.

- **Affected scope:** Snapshot lines 302–312, 386–390 and 539–545, Rx reserves and receive windows.
- **Discrepancy:** Retained suffix packets can exhaust Rx backing and prevent reception of the missing leading retransmission needed to release them. Reverse-direction ACK/control ingress needs backing too.
- **Rationale:** FILL must not reuse retained frames; free-list pressure and byte windows alone do not resolve the dependency under short packets or shared reserves.
- **AI suggestion: PROPOSE FIX.** Add a receive-retention/admission invariant and control/gap-recovery progress tests. This supplements the accepted bounded-reserve model without copying or reclaiming application-owned data.
- **Proposed edit:** In OQ-3.2, add: “Bound transport-retained Rx frames so reassembly cannot consume all ingress capacity. Preserve a free/replenishable receive allowance for control traffic and gap-filling input. Drop/recycle excess out-of-order packets, or retire transport-owned out-of-order retention where necessary, instead of exhausting that allowance; reflect discarded selective reception correctly. Account for packet count and shared connections as well as byte windows. Application-held buffers remain valid until released; apply receive backpressure when the application retains them, while preserving control ingress. Specify admission priorities, per-connection/shared accounting and reserve bounds in Detailed Implementation.”
- **Implementation/verification consequence:** From the first slice, test an exhausted retained suffix behind a hole, small packets, multiple connections, reverse-direction ACKs/window updates, and application-held buffers. Recovery must not depend on timeout destruction or external release of the out-of-order suffix; no premature FILL reuse or payload-copy fallback is permitted.

### design-1-3 — Design — Major: Fast Open replay eligibility

**Disposition:** Resolved by author decision. Preserve Zquic's existing default-off, application-authorized early-data hooks and eligible-stream gating when adapting to Fast Open. Application policy owns replay eligibility; transport duplicate handling remains connection-local. The draft and phase-7 checks now carry this policy explicitly. The proposal below preserves the original recommendation; the adopted resolution reuses the existing mechanisms.

- **Affected scope:** Snapshot lines 442 and 563–569, optional Fast Open and phase 7.
- **Discrepancy:** Connection-local duplicate suppression is described, but cross-connection replay eligibility is absent from the target despite its presence in design_options.md.
- **Rationale:** A new connection can deliver a replayed Fast Open request. TCP byte-delivery deduplication does not establish once-only FIX transaction execution.
- **AI suggestion: PROPOSE FIX.** Carry the already documented application eligibility constraint into the main design; do not add transaction deduplication to the transport.
- **Proposed edit:** In the Fast Open profile and phase 7, add: “Fast Open early data is application-requested and must be replay-safe or covered by the application's request deduplication. Retransmission/fallback suppression guarantees apply within the connection attempt, not to transaction execution across connection incarnations. Ordinary data waits for establishment when early-data eligibility is absent. Preserve this choice through the native/plugin path without changing Ztcp/Ztls or implementing application transaction deduplication inside Zutcp.”
- **Implementation/verification consequence:** Add a cross-connection replay scenario distinguishing byte delivery from application execution, plus disabled/ineligible early-data cases. Retain ordinary-handshake fallback and all connection-local duplicate tests.

## Author-authorized style repairs

| Finding | Original severity | Disposition |
| --- | --- | --- |
| style-1-1: stale decision wording | Nit | PROPOSE FIX — authorized and applied |
| style-1-2: untested equals operational failure | Nit | PROPOSE FIX — authorized and applied |
| style-1-3: inactive certification disclaimer | Nit | PROPOSE FIX — authorized and applied |
| style-1-4: repeated performance exclusion | Nit | PROPOSE FIX — authorized and applied |

The exact findings and applied changes are recorded in [critic_report.md](./critic_report.md). No severity was changed. There are no CONTEST, ESCALATE or DECLINE dispositions in this round. design-1-1 and design-1-2 are resolved as recorded above. design-1-3 is resolved by the adopted early-data policy port. This round raised unsettled Major findings and is not a clean round; no convergence is claimed.

## Resolution handoff

Accepted/applied: four style repairs, the allocator failure-path documentation aligned with the author's source fix, receive-admission adaptation, and preservation of application-owned early-data eligibility. Contested: 0. Escalated: 0. Declined: 0. Unaddressed round-1 findings: 0. Original severities are preserved.

The original round was not clean before fixes; this resolution does not claim two clean rounds or review-loop convergence. The author adopted the resolutions and instructed proceeding onwards. Return to the parent interactive review workflow for Stage 8, Intermediate Zoom Out; implementation-readiness review remains inapplicable until a detailed implementation section exists.
