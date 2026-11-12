# Implementation readiness report

Target: [design_draft.md](design_draft.md), Detailed Implementation Plan. No production implementation, build or runtime test was performed during planning.

## Current revision

The author has superseded the registered large-buffer/SG layout with MTU-sized packet backing and layered IOBufPlugin::max caps. The generic adapter already enforces min(Size, max) for registered requests and leaves unregistered heap requests uncapped. design_draft.md DP-2/5/9/18 and related inventory now prescribe one-buffer/one-packet I/O; ztls_fix.md describes the separate prerequisite. This revision has not undergone a new full readiness review or runtime verification. Earlier review results below apply to their recorded snapshots.

## Previous result

Detailed implementation planning is complete. Round 11 implementation-readiness and codebase reviewers independently returned clean for lines 648–1217. The style reviewer found one stale statement in the earlier phase-1 summary (line 524), while reporting no additional detailed-section findings. That statement now binds protocol Rx/Tx directly to mx I/O owners and removes independent Zutcp thread selectors, matching DP-8 and the detailed execution sequence.

No unresolved implementation decision or author-policy question remains. The exact file inventory, independent Zxdp gate and eight cumulative working-client/server slices define the implementation work. This is a planning-completion disposition, not a claim of two consecutive wholly clean review rounds or of implemented/tested functionality.

The records below preserve findings as reported at their historical snapshots. Their original “pending” and scope-question wording is historical; subsequent rounds and the author-supplied API changes supersede it. In particular, the null-guard scope question was withdrawn after the Registered/stub contract and current Ztcp/Ztls guards were reconciled.

## Findings and dispositions

| ID | Reviewer/source scope in snapshot | Severity | Finding | Disposition |
| --- | --- | --- | --- | --- |
| implementation-1-1 / codebase-1-2 / style-1-1 | DP-5, lines 710–724; geometry/readiness lines 901/925/957/1176 | Major | Header tables freeze before peer MSS and path changes; capacity/error policy unfinished. | PROPOSE FIX applied: K simultaneous header slots are reusable only after header DMA completes; N>K uses bounded batches over unchanged payload. Per-buffer outstanding descriptor demand stays bounded by K. Invalidating a retained original packet through a smaller path budget aborts that connection rather than silently changing the chosen retransmission policy. Added boundary tests. Pending re-review. |
| codebase-1-1 | DP-6/7 and allocation assumptions, lines 726–740/800/929 | Major | Current ZiRx/Ztcp/Ztls callers dereference null pool allocations, despite safe adapter null propagation. | PROPOSE FIX prepared in DP-16. Needs author's decision: include narrow guards in three production headers or require author-supplied prerequisite fixes before phase 2. No initial heap fallback, hidden wait or pretend capacity guarantee. Not resolved. |
| implementation-1-2 | Slice 7, lines 839–843/1122–1125/1169 | Major | TFO cookie format, authenticated identity, key lifecycle and bounded client cache unspecified. | PROPOSE FIX applied in DP-14: 16-byte keyed cookie, canonical endpoint/client identity, instance key lifetime and capacity-bounded Rx cache, explicit kernel opaque-cookie handling and tests; three exact new implementation/test files inventoried. Pending re-review. |
| implementation-1-3 | Shared classifier, lines 688–690/812–814/1014–1015/1029 | Major | Multiple instances have no shared attachment owner/join/leave/last-detach mechanism. | PROPOSE FIX applied in DP-15: process-local namespace/interface registry, explicit membership, serialized lifecycle, fixed maps, ownership collision/refusal and last-member detach, with rollback and two-instance tests in existing inventory. Pending re-review. |
| style-1-2 | Preparatory step 1 line 1151 versus feature 514–516 | Major | Copy/rename precedes independent Zxdp gate in one execution sequence. | PROPOSE FIX applied: baseline recording first; independent Zxdp steps 2–7; copy/rename checkpoint step 8, then phase 1. Pending re-review. |
| style-1-3 | DP-3 line 692 | Nit | Stale instruction to finish lifecycle despite later adopted DP-12 rules. | PROPOSE FIX applied: point to DP-12; distinguish unregistering from unmapping. |
| style-1-4 | Allocation paragraph line 935 | Nit | Per-allocator “arena” wording contradicts one shared arena with pool regions. | PROPOSE FIX applied: pool/region terminology. |
| style-1-5 | Readiness paragraph line 1176 | Nit | Long retrospective obscures actual remaining action and claims a complete plan. | PROPOSE FIX applied: concise finalization disposition and explicit DP-16 prerequisite. |

## Evidence of allocation mismatch

- zi/src/ZiIOBuf.hh IOBufPluginAllocator::allocBuf propagates null backing safely.
- zi/src/ZiRx.hh receive trailing/replacement allocation and recvMem/recvMemSync paths assume a non-null buffer.
- ztcp/src/Ztcp.hh Link::allocTxBuf_ immediately sets skip/length on the returned buffer.
- ztls/src/Ztls.hh rxTxBuf, allocTxBuf and key-update output allocation helpers access size before checking a null result. Existing handshake code already handles a null rxTxBuf result as PTLS_ERROR_NO_MEMORY.

## Reviewer completion

- stage9_readiness_r1: completed. Three Majors; no Blockers/Nits. Read persona/standard, snapshot and relevant context source/guidance.
- stage9_codebase_r1: completed. Two Majors; no Blockers/Nits. Verified all 67 tracked Zquic paths have renamed inventory destinations. Safe adapter null guard, Initial transition, non-sharded free-list algorithm, plugin routing and scheduler ownership match source.
- stage9_style_r1: completed. Two Majors and three Nits; no Blockers. Read persona, snapshot, guidance and targeted source.

Round 1 is not clean. Two consecutive clean rounds required by the review-loop standard have not occurred. This report is review evidence, not a production implementation or correctness claim.

## Subsequent author API revision

The author changed Initial/initial() to Registered/registered(), added allocBuf's explicit registered selector and ZiMxPlugin::recv, supplied Ztcp/Ztls Tx null guards, and specified registered Rx context stubs with post-demultiplexing backing attachment. The current plan incorporates that contract in DP-6/7/16, plans explicit ZiIOBuf adapter support/tests and keeps Ztcp/Ztls source out of scope. The old question about choosing production null-guard scope is withdrawn. The round-1 findings above retain their historical source/snapshot meaning; they are not a readiness result for this new receive contract. Updated-contract review remains pending.

The author further clarified that allocBuf(registered=false) cannot return nullptr: heap OOM is handled by ZmHeapFail in ZmHeap/ZmVHeap. ZiRx framing callers therefore need no null-result guard for this request. Null-result handling is required only where registered=true can be requested; a successful registered Rx stub remains a non-null metadata object without backing. The active plan and verification requirements now state this explicitly.

## Round 2 — Registered receive contract

All three fresh-context reviewers completed on stage9-round2-design.md, detailed range 648–1243. Readiness and codebase reviewers returned clean. Style reported two Majors: retained header-address wording conflicted with DMA-completion reuse (line 965), and zero-count pool wording conflated physical backing failure with valid Rx stub construction (973/1007). Both were repaired after the round: retain header reconstruction fields/payload ranges through ACK, and qualify zero-count failure as physical backing acquisition only. Rx stub construction and unregistered framing remain non-null. The round was not clean under the loop rule because those Majors existed before repair. Final registered-frame return wakeup was also specified so a depleted FILL pool resumes without waiting for unrelated traffic. Round 3 checks the revised snapshot. No production writes/builds.

## Round 3

All reviewers completed on stage9-round3-design.md, range 648–1243. Implementation-readiness and codebase reviewers returned clean. Style found one Major (pre-start backing-acquisition wording still prohibited valid Rx stubs) and one Nit (broad crypto-removal wording obscured retained MAC primitives). Repairs qualify physical backing acquisition and TLS/AEAD removal. The round was not clean before these repairs. A further contract clarification places the fixed registered non-Rx growth guard in ZiIOBuf mutators before replacement, keeping allocBuf(false)/unregistered backing callbacks non-null heap operations for every supported role. The explicit ZiIOBuf write inventory includes this guard; no new production files were changed.

## Round 4

All three reviewers completed on stage9-round4-design.md, range 648–1245. Readiness and style returned clean. Codebase found one Major: plugin delegation bypasses private ZiConnection direction-flag clearing, leaving public up() true after close. Repair is explicit in DP-1 and the inventory: public direct-owner rxDown_/txDown_ helpers clear their respective atomic flags, with no socket operations/notifications; Zutcp invokes them at its defined Rx/Tx closing transitions. Existing kernel behavior and Ztcp/Ztls remain unchanged. Plugin tests assert up() before/during/after graceful, abrupt, peer and repeated close/half-close. The round was not clean before repair; round 5 checks this revision.

## Round 5

All reviewers completed on stage9-round5-design.md, range 648–1251. Style returned clean. Codebase found a Major for the unspecified physical ingress provider/count; readiness found Majors for bounded multi-frame receive assembly and off-owner admission charge retirement. Repairs DP-17/18/19 now define one internal Ingress provider and R source, bounded chain parsing/delivery, and Rx-only account-token reconciliation when returned slots are popped from the existing free slist. No alternate backing-lifetime counter, per-frame return queue or background collection is introduced. Two isolated tests are explicitly inventoried. The round was not clean before repairs. The Registered API revision started a new review run after the historical Initial-era round 1; the next snapshot is its fifth review round.

## Round 6

All reviewers completed on stage9-round6-design.md, range 648–1285. Readiness and codebase returned clean. Style found three Majors confined to repeated stale wording: N rather than K header capacity, unconditional per-request arena regions despite Rx stubs, and application-side frame-counter retirement despite DP-19. Those references are corrected and redundant integration/restatement tables are consolidated into one Integration rules section referencing the authoritative DP definitions. No transport policy changes were introduced by this editorial consolidation. The round was not clean before repair. Final confirmation uses the consolidated revision rather than treating repetitive stale passages as new architecture questions.

## Round 7

Readiness returned clean, style found one Nit (an old crypto-test deletion row needed TLS/AEAD-specific wording), and codebase found a Blocker under its current-source-contradiction rule: the plan said all ZiRx framing allocBuf requests pass false, while network recv uses a true stub plus ensure for a trailing record. Both were repaired without changing architecture or ZiRx production scope. The plan now distinguishes direct memory-framing heap allocation from metadata-stub-to-heap conversion and tests the actual coalesced network path. The round was not clean before repair.

## Round 8

Style returned clean. Codebase found a Major for unchanged Ztcp/Ztls telemetry reading private ZiConnection traffic counters that plugin objects alone cannot update; the repair adds exactly inventoried owner-directed countRecv_/countSend_ helpers and tests byte/call semantics through actual link getters. Readiness found a Major for unspecified timestamp-free full-cycle behavior. The plan now explicitly uses ordinary TCP acceptance without an artificial wrap timer, documents its inherent full-cycle alias limitation, preserves PAWS when negotiated, and adds profile-specific tests. This clarifies the already adopted optional-timestamp/minimal-timer posture instead of introducing a new wire field or claiming an impossible timestamp-free epoch check. The round was not clean before repair.

## Round 9 — clean

All three fresh-context reviewers completed on stage9-round9-design.md, detailed range 648–1211. Implementation readiness, codebase details and style all returned clean with no Blocker/Major/Nit. The root reconciled the working-tree copy inventory independently: 67 tracked Zquic paths, 67 copy rows, no omissions/unmatched rows/missing source files. The source reviewer reported 66 in its summary; the explicit root git/path comparison is the recorded count. Final editorial clarification keeps libZtls linkage target-local to the application TLS integration fixture, outside libZutcp. The last confirmatory pass checks that final candidate.

## Round 10

Style and codebase returned clean. Readiness found one Major for classifier packaging and attachment API/mode. DP-15 now explicitly selects C++-emitted bpf_insn, direct program/map syscalls and map-FD patching; native-preferred/no-replace rtnetlink attachment with unsupported-native generic fallback; queried program IDs; and expected-FD-protected detach. All work remains within the inventoried ZxdpBPF files and classifier/lifecycle tests. The round was not clean before this repair. Further final confirmation is performed under the user's continuing finalization instruction; no unresolved finding is treated as accepted or silently downgraded.

## Round 11 — detailed planning complete

Implementation-readiness and codebase reviews returned clean on stage9-round11-design.md, detailed range 648–1217. The codebase reviewer verified all 67 tracked Zquic paths in the copy inventory. Style reported one Major outside that range: the feature-level phase-1 working-result paragraph retained independently configured protocol threads. The paragraph is repaired to direct mx Rx/Tx execution with the copied selectors removed; the detailed section already prescribed this behavior. No additional architecture or scope decision was required. All earlier findings have recorded repairs or superseding author decisions. No production writes, builds or runtime tests were performed.

The round-11 style reviewer performed a focused read of the repaired line 524 and confirmed the finding resolved, with no residual contradiction from that wording.

## Subsequent AF_XDP vector verification

The author requested a direct kernel/NIC support check after planning finalization. [Source verification](review_context/af-xdp-sg-verification.md) confirms the API and an ice hardware-driver implementation, but establishes a material portability qualification: single-buffer AF_XDP zero-copy does not imply multi-buffer zero-copy, and the inspected mlx5 path does not support the required vectors. DP-5 now explicitly requires capability/mode reporting and per-packet segment-limit validation in the independent Zxdp gate. This clarification has not undergone another full review round or a physical-NIC runtime test. The current TLS layout's hardware zero-copy claim is conditional on AF_XDP multi-buffer driver support.

## Registered-only provider API reconciliation

Re-read current zi/src/ZiIOBuf.hh and zi/src/ZiMultiplex.hh. IOBufPlugin now has max plus alloc(size)/free(ptr,size) exclusively for registered storage. The IOBufAllocator adapter retains its registered selector, routes false directly through the ordinary inline-builtin/VHeap policy, and owns all heap release routing. The mx template order is Type, Buf, Size, Align, HeapID, MaxSize, Grow; the plugin factory receives type, size, align, count. Both design_draft.md and ztls_fix.md now match these interfaces, remove provider heap callbacks and preserve current explicit IOBufStub/IOBufBuiltin ownership instead of scheduling their implementation anew. Remaining Rx stub and fatal registered Tx-growth work stays explicitly planned. No production files or tests were changed/run during this reconciliation; earlier snapshot reviews do not cover this revision.
