# Fresh implementation-readiness review

## Result

**Not ready for implementation under the EGM implementation-readiness standard.** One Blocker, six Majors and two Nits remain open. This fresh review supersedes the prior planning-completion assessment. All three requested fresh-context reviewers completed; no production edits, builds or runtime tests were performed. No plan fixes were applied.

## Scope and isolation

- Frozen main plan: `review_context/packet-allocator-readiness-r1/design_draft.md`, detailed lines 650–1205; feature context read where needed.
- Frozen TLS migration: `review_context/packet-allocator-readiness-r1/ztls_fix.md`, all lines 1–133.
- Roles: implementation readiness, codebase details and style/internal consistency. Each read its bundled persona and implementation-readiness standard and used no parent conversation, concepts brief or historical reviews.
- Context and target hashes: [context.json](review_context/packet-allocator-readiness-r1/context.json). Source guidance includes relevant repository I/O/build/framework/audit rules and current Zi/Ztcp/Ztls/Zhttp sources plus the selected local zpicotls dependency.
- This is one review round, not two-round convergence. Reviewer overlap is deduplicated below; differing severities retain the highest reported level.

## Findings

### IR-1 — Blocker: Packet-reservation failure has no producer admission/retry contract

Scope: ztls_fix.md:42–48,82; design_draft.md:728.

Evidence: Current Ztls.hh:1028–1040 moves plaintext into txInvoke, discards the later send_ result and returns dispatch success. ZiTxStream.hh:120–130 consumes the submitted buffer and makes rejection a sticky writer failure. The allocator and ZiTx interfaces do not supply the claimed existing packet-availability subscription.

Consequence: The required all-or-nothing reservation can fail later than the caller sees success, with no defined plaintext return/retention or retry signal. This contradicts the plan’s claim of an existing mechanism.

Required planning repair: Specify synchronous and off-thread submission results, ownership on rejection, bounded pending state, result/readiness notification signatures and owner shard, final-return wakeup and missed-wakeup prevention, cancellation, destructor-flush behavior and producer retry steps. Inventory every new API/caller. Keep the author-selected rule: reserve all packets, release all on failure, and change no crypto/wire state before admission.

Reviewer traceability: readiness-r1-1, style-r1-1, codebase-r1-2.

### IR-2 — Major: Existing Tx streams do not roll over at the returned registered capacity

Scope: design_draft.md:724,991–996,1183.

Evidence: Ztcp.hh:409–410 initializes Zi::TxStream with TxBufAlloc::Size. ZiTxStream.hh:134–148,173–182 calculates space from that maximum and can call ensure/append beyond a capped allocation. Ztcp.hh:396–400,419–424 resets skip.

Consequence: A valid producer write can attempt to grow a fixed registered packet and reach the planned fatal guard. The stated preservation of allocator headroom is also not implemented by unchanged callers.

Required planning repair: Define runtime-capacity-based stream rollover and the chosen headroom representation. Explicitly inventory zi/src/ZiTxStream.hh and any required ztcp/src/Ztcp.hh edits, or supply a concrete geometry arrangement proving the latter unchanged path is valid. Test normal stream output spanning multiple capped packet buffers.

Reviewer traceability: codebase-r1-1.

### IR-3 — Major: Layered packet geometry has no selected metadata propagation mechanism

Scope: design_draft.md:674–686,696,708–724,962; ztls_fix.md:16,26–28,72.

Evidence: IOBufPlugin provides max, alloc(size)->pointer and free(ptr,size). ZiIOBuf.hh:525–532 then constructs IOBufStub with capped size and zero skip. The provider cannot initialize that later object’s metadata. Both plans defer this to an unnamed derived/layered adapter.

Consequence: An implementer must choose payload-origin versus nonzero-skip geometry and its propagation API. Plausible interpretations double-subtract headers or expose reserved bytes.

Required planning repair: Select and document one representation with equations for slot origin, returned pointer, max, size, skip, writable extent, wire range and canonical free identity at each layer. Name the exact adapter/hook and files. Give numerical MTU/header/request examples proving capacity is deducted once.

Reviewer traceability: readiness-r1-4, style-r1-2, codebase-r1-3.

### IR-4 — Major: Connection-specific MSS does not reach packet chunk sizing

Scope: design_draft.md:722–732,964–968; ztls_fix.md:26–30,40–44.

Evidence: The provider cap derives from interface MTU, while peer MSS can be smaller and varies per connection. The current registered allocator receives no connection or per-attempt payload limit. The plan says derive/validate compatible limits but does not choose the initial-peer policy.

Consequence: Ztls can protect an output buffer larger than the peer permits; the chosen one-buffer/one-packet layout has no selected no-copy repair after that. The later-path-reduction failure rule does not resolve initial negotiation.

Required planning repair: Specify initial/absent MSS and option-overhead policy. Either define establishment refusal and callbacks for unsupported peers, or define how per-connection limits constrain producers and TLS chunks before encryption without mutating shared provider state. Test a peer MSS below provider capacity.

Reviewer traceability: readiness-r1-3.

### IR-5 — Major: Record completion association and cancellation transitions are undefined

Scope: ztls_fix.md:44–46,82–84,129–130.

Evidence: ZiTx.hh:83–90 reports sent(false) for the head then queue.clean() discards remaining buffers without per-buffer sent callbacks. The plan requests a pending-record operation but leaves fields, packet association and terminal transitions unspecified.

Consequence: A packet-count completion aggregator can strand records represented only in a discarded tail, or complete the original submission too early. Replacement and rejected queued setups need explicit terminal handling.

Required planning repair: Define operation fields/packet association, success on final local-send completion, once-only failure for every canceled accepted submission and the retirement hook for queue-cleaned tail nodes. Specify plaintext lifetime and generation/close transitions. Test multiple queued records, middle-record failure and pending setup cancellation.

Reviewer traceability: readiness-r1-2.

### IR-6 — Major: Serial record-output and backend API remain architecture assignments

Scope: ztls_fix.md:60–72,86,94–102.

Evidence: The dependency currently exposes contiguous output, and handshake uses Rx-owned ptls_handshake with ptls_buffer_t while application output uses detached Tx-owned ptls_tx_t. Fusion incremental functions are assertion stubs or absent, as the plan accurately acknowledges.

Consequence: The plan says to define a sink/sizing/protection contract without declarations, error/ownership contracts or handshake admission integration. Fusion state/carry implementation likewise requires decisions beyond mechanical coding.

Required planning repair: Write concrete C declarations and sizing/validity/span/commit/error/state-transition contracts, including KeyUpdate prefixes and cleanup. Define Rx handshake/control output entry points and reservation boundaries. Specify incremental state/carry and release-build backend failure handling for each selected Fusion variant. Preserve one nonce/authentication state per TLS record and no full-record ciphertext staging.

Reviewer traceability: readiness-r1-5, style-r1-4, codebase-r1-4.

### IR-7 — Major: Dependent write inventory and buildable migration checkpoint are incomplete

Scope: ztls_fix.md:100–115, especially105–109.

Evidence: Mandatory packet-allocator configuration changes occur before a later dependent-update step, with no explicit atomic grouping. Existing direct callers zhttp/itest/zhttplifecycletest.cc:469–476 and zhttp/interop/zhttp3interoptest.cc:1144–1151,1348–1354 are omitted from the list and bypass the inventoried factories.

Consequence: The declared write allowlist leaves known callers failing initialization, and a purported intermediate phase need not build. A future repository search is assigned in place of a complete inventory.

Required planning repair: Complete caller discovery now, list exact affected files and group the allocator API migration with all required callers into one buildable phase. Give it an explicit build/test checkpoint before independent packet tests and Zutcp phase2.

Reviewer traceability: readiness-r1-6, style-r1-3, codebase-r1-5.

### IR-8 — Nit: Two test descriptions retain the superseded vector/large-buffer model

Scope: design_draft.md:810,1084.

Evidence: The text still requests fragmented large Tx buffers and vector resend/header stability despite adopting independently allocated contiguous packet buffers.

Consequence: Test implementation could target the removed abstraction.

Required planning repair: Describe TLS records spanning separate packet buffers and retransmission of original contiguous packets; remove obsolete vector wording.

Reviewer traceability: style-r1-5.

### IR-9 — Nit: Feature summary still excludes all Ztls changes

Scope: design_draft.md:10,43,51,64.

Evidence: Earlier feature statements say Ztls remains unchanged/excluded, while detailed scope requires the separately implemented ztls_fix.md prerequisite.

Consequence: The combined deliverable’s dependency/scope is easy to misread even though detailed sections generally distinguish the scopes.

Required planning repair: Name the separate Ztls prerequisite in the feature summary while preserving the distinction from Zutcp’s own write set.

Reviewer traceability: style-r1-context-note.

## Source changes during review

`ztls/src/Ztls.hh` changed after the initial source fingerprint capture. Readiness and style reviewers performed focused reconciliation against current source and confirmed their findings unchanged. The codebase reviewer re-read relevant current sections before completing. The reviewed Ztls source hash reported by the readiness reviewer is `53bd2ee41537a06f8912eadb9e4a5bcb862853ef0da00e6bd433a9d0520b98b4`. Final source fingerprints and any subsequent changes are recorded alongside this report. No target-plan edits occurred during review.

## Confirmed foundations

The inspected registered-cap logic, registered-only provider/heap-adapter separation, plugin close delegation and selected non-sharded free-list contract match current source. Reviewers treated Rx stubs, connection state/counter helpers and serial Fusion protection as explicitly planned changes, rather than falsely flagging them merely because they are not implemented. The author-selected allocator layering, one packet per registered backing, serial encryption and all-or-nothing packet reservation are retained requirements. The findings identify missing executable contracts around those requirements.

## Disposition

All findings are open, with repair guidance supplied. None has been accepted as a trade-off, silently downgraded, fixed or re-reviewed. The next task is to resolve the design/API choices and complete the inventory, then rerun readiness on the resulting revision. The prior report is preserved at `review_context/packet-allocator-readiness-r1/previous-implementation_readiness_report.md`.
