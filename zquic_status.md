# zquic repair handoff through phase 3

Status: source transferred and committed for handoff; phase 3 has not started.
The user's earlier instruction to pause before phase 3 remains in force.
This transfer request did not authorize compilation or test execution.

## Checkout and transfer

- Repository: `/home/ubuntu/src/z.zquic`, independent local clone of
  `/home/ubuntu/src/z` (not a linked Git worktree).
- Branch: `zquic_repair`.
- Parent/base: `bc710942eefda5cc5572db282c84927abc460976`.
- Original repair checkout HEAD: `96afee36256c480e1285d598792750ee4eaae082`.
- Transfer commit: the commit containing this report; identify with
  `git log -1 --format=fuller` and `git show --stat HEAD`.
- Transferred the local tracked delta under `zquic` relative to original HEAD,
  plus the untracked `zquic/test/ZquicRttLossTest.cc`. Did not copy the entire
  newer module or unrelated I/O changes elsewhere in the source repository.
- Copied all five `zquic_*.md` documents byte-for-byte. Their original revision,
  line numbers, temporary paths, and prerequisite observations describe the
  original checkout. This report governs the transfer and next steps.
- The local Stream friend additions and owned-stream RuntimeTest additions
  predate the recovery work but were included as requested local zquic changes.
  Do not misattribute them to the recovery agents.

Applied the local patch with Git three-way application. Only RuntimeTest
conflicted: retained its locally added ownership test and calls, kept the older
base's surrounding code, and omitted the newer HEAD's contextual closeClient
helper (not part of the local delta). Verified identical ordered added/removed
lines in all ten tracked files between original and destination local diffs.
Verified new RTT test and all five copied Markdown files byte-for-byte.
`git diff --cached --check` passed before commit. These are source transfer
checks, not C++ validation. Original checkout was not modified by this transfer.

## Phase 1 — implemented source, execution deferred

Two feature subagents implemented disjoint sections concurrently; coordinator
integrated and reviewed them:

- Recovery boundary stores Tx event-processing time, compares the triggering
  packet's send time inclusively, and migrates loss and ECN callers.
- ACK classification captures inFlight before removal and separates ordinary
  byte retirement from eligible growth. PMTUD remains retirement-only.
- Bounded ACK work retires immediately, stages growth until ECN/loss completes,
  and invalidates credits using a separate congestion epoch. Reset generation,
  discarded-space guards, and persistent-congestion invalidation are retained.
- RTT threshold uses max(latest, smoothed), 9/8 multiplier, precision and floor,
  retaining initial fallback.
- Permanent recovery/congestion/API regressions and new registered RTT test
  cover epochs, equality, mixed ACKs, late/duplicate ACKs, PMTUD, same-ACK
  loss/ECN, independent ECN response, continuations, reset/discard, shared
  spaces, exact 99.84375 ms falling RTT and deadline edges.

See `zquic_repair_feature_a.md` and `zquic_repair_status.md` for invariants and
source coverage. No configuration, compilation, compiler checks, tests, or
images were run during phases 1–2 or transfer.

## Phase 2 — audit performed; not a clean pass

Original source audit covered all present C++ directories and included an
independent recovery review. Applied small layout fixes (Diag, LinkDiag,
OpenSSLCipherCtx, PktTxUpdate), removed an unused polling helper, and repaired
regression setup for initialized diagnostics, explicit ECN activation, and timer
teardown. See `zquic_repair_audit_inventory.md` for evidence/dispositions.

Remaining audit findings include active fixture polling and existing C++ policy
violations; those were recorded, not silently repaired. Existing I/O API mismatch
observations refer to the original newer checkout, not this older base.
Re-audit affected declarations and callers after this transfer before asserting
phase 2 readiness.

Transfer-specific source concerns already observed: the copied pre-existing
owned-stream RuntimeTest additions use `ZQUIC_CHECK_RT`, `pendingMigDrop`, and
`Zi::RxRange`, while the older destination does not supply their macro/member
or the range declaration at the inspected zi locations. These additions had
newer-base dependencies outside their local diff. Preserve the requested delta
in this handoff; decide explicit necessary prerequisite repairs on resumption.
This is a source compatibility concern, not a compiler result. Other cleanly
applied patches may also depend on newer APIs: clean application proves neither
compatibility nor runtime correctness.

## Phase 3 — pending, no results

Do not claim phase 3 success. No compiler version, configured flags, dependency
readiness, warning-free build, TAP results, baseline failure run, or runtime
recovery result has been established for this checkout. No generated Makefiles
or build artifacts were copied. Phase 4/QIR is also unstarted.

When authorized to resume:

1. Read repository guidance and `zquic_repair.md`, then review this report and
   the transferred commit. Recheck dependencies and API compatibility against
   this exact base; resolve necessary prerequisites through phase 2 audit.
   Do not import the entire unrelated I/O migration just to clear failures.
2. Record revision/local patch identity, compiler version, dependencies and
   configuration. Use native x86-64 Clang debug only, with `/usr/local` prefix.
   Inspect `z.config` at this base before choosing required optional switches.
3. Configure via `./z.config -c -L -d -Q /usr/local` with necessary environment
   settings. Verify actual flags/debug defines and absence of sanitizer/cross
   flags. Top-level clean on configuration change, then `make -j8`; require
   warning-free compilation. Retain commands and logs.
4. Build every dependent test directory's default target before `make test`.
   Run complete zquic/test and zquic/itest where present, affected zhttp/zws
   suites, and zhttp/interop endpoint local tests. Recheck directory existence
   at this older base; missing suites are a coverage gap to resolve explicitly.
5. Inspect exact recovery/ACK/RTT assertions and continuation results. If a
   coherent pre-fix snapshot is available, demonstrate regression failures in
   a separate Clang debug build; never revert active fixes or claim blocked
   baseline runs passed.
6. Route necessary source repairs back through phase 2 audit, then rebuild and
   rerun affected checks. Update this report with actual outcomes and remaining
   blockers. Do not label isolated-header checks as runtime-suite coverage.

Representative commands are in the plan. Phase 4 uses a fresh image from the
same audited Clang debug build and runs the documented QIR matrix only after
phase 3 passes. No release/sanitizer/GCC/cross validation matrix is authorized
by the existing plan.
