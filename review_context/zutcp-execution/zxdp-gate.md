# Independent Zxdp gate audit

The goal remains the entire `zutcp_plan.md`. This ledger covers its prerequisite
only; passing it cannot establish a completed Zutcp slice, TCP interoperability,
hardware result or performance result. The independent veth gate has passed; see `independent-gate-passed.json`.

Current-source Clang release and ZDEBUG ASan/LSan execution passes eight unit
programs / 33 subtests, five privileged programs / 20 subtests / 258 checks,
and the directly run kernel-coexistence probe. `asan-current/` records the
sanitizer execution, successful clean full-tree build and successful UAPI
configure/follow-up build. All 21 observed packet/dependency binaries remain
byte-identical to the tested versions.

`isolated-release-validation/` records matching release runtime suites, all eight
unit and four native Memcheck programs, the directly instrumented kernel probe,
three short endpoint modes and four consecutive 10,000-message finite-wait
Memcheck runs, with zero errors and lost allocations. The isolated clean full-tree release build completed successfully with zero
warnings/errors; `isolated-release-build/` preserves its complete logs.

## Eight independent packet requirements

| Plan requirement | Existing evidence and its scope | Current disposition |
| --- | --- | --- |
| 1. Bidirectional veth packets, wrap and crossing batches | `nif-pair.txt`: public Nif UDP request/reply; four real Tx entries primed to cursor one, then a four-descriptor batch over two original buffers crosses the ring boundary. `packet-loop.txt` and `packet-loop-artifacts/`: separate utility processes exchange 32 messages per mode, including full-MTU and odd-length UDP with IP/UDP checksums. | Prior debug evidence captured. Release and later ZDEBUG ASan/LSan suites pass; matching current-source release runtime checks pass. |
| 2. Retain Rx payload after releasing descriptors | Public Nif pair holds the first original Rx object across further traffic, close and restart; bytes remain unchanged and its slot stays unavailable until final release. All Nif references disappear before the final application-buffer release. | Prior debug/native Memcheck evidence captured; current release and Memcheck pass. |
| 3. Off-owner final return, contention, origin and Rx-only FILL | Pool test runs six concurrent workers through 30,000 pop/free cycles each, checks unique slot ownership and all payload bytes, then reacquires the exact reserve. Ownership/ingress/Rx tests exercise off-owner final references. Native Nif tests return an old-epoch frame on Tx and reconcile its ID before Rx republishes it; pool-only wake restores an exhausted receive reserve without new traffic. | The contention test uses real fixed pools, with synthetic kernel peers in queue tests; native handoff/wake evidence is separate. Current renamed release targets and Memcheck pass. |
| 4. Exhaust Rx and Tx, delay CQ, recover without fallback | Public Nif pair exhausts its two Rx frames. Tx pressure retains four kernel associations over two physical slots, refuses further admission during the same Tx owner turn, then consumes actual CQ and preserves an application reference. Pool tests establish reserve isolation and null exhaustion. | Prior debug/native Memcheck evidence captured. New registered stream case additionally checks three-slot exhaustion; current release and unit Memcheck pass. |
| 5. Spin/wait/LL, driver wake, scheduler work/timers, quantum and pending re-entry | Public plugin fixture covers indefinite receive wait, finite wait/spin and LL, 512 scheduler notifications per mode, due timers, quantum one and final wakeup. Tx controls apply bounded spin/continuations/timers. Two-queue traffic checks per-turn bounds. | Current release and direct kernel-probe Memcheck verify two actually queued descriptors and a marker between their service turns. All three native modes complete 10,000 exchanges with active 1ms timer ticks. The finite-wait server recovers 3,278 temporary Tx shortages; the older long instrumented deadline failure remains recorded, and its current-source counterpart now passes four consecutive times without changing the two-frame reserve or 15-second deadline. |
| 6. Idle/wait/outstanding stop and mapping lifetime | Kernel-retirement fixture keeps pending FILL and Tx references through permission denial, namespace mismatch and observer rebind; Tx/Rx release on their own owners after proof. Native pair covers close/restart/final with retained Rx. Standalone utility exposed and now covers whole-mux stop without relying on its blocked control/timer thread. | Prior debug/native Memcheck evidence captured, including six standalone endpoint Memcheck runs. Current ASan/LSan suite passes; matching release and native Memcheck checks pass. Physical DMA validation remains a separate coverage gap. |
| 7. Supported binding ownership and frame accounting | Instance has one configured binding and one complete arena batch with independent Rx/Tx/control regions. Four native instances share classifier ownership across two interfaces/two real queues while keeping arenas and ring owners independent; one pair retires during continued traffic, then the remaining pair transfers after first-owner destruction. Registry tests cover conflicts, rollback, final-member detach and foreign attachment preservation. | No general-purpose concurrent/shared-ring API is claimed. Current release and Nif Memcheck pass for the selected binding model. |
| 8. Metadata-only buffers, fixed-capacity rollover, Rx growth, fatal invalid Tx in debug/release | Ingress tests verify metadata-only objects, stable original identity, ordinary VHeap growth/registered return and stub transfer. New ownership stream case writes 4100 logical bytes into three fixed 1514-byte buffers with 54-byte framing, then exhausts the same reserve with 5000 bytes. Public Nif now turns private invalid-Tx diagnostics into an always-enabled fatal invariant; geometry already has release assertions. Seven exec-isolated probes require SIGABRT and the correct boundary diagnostic, each in its own namespace. | Ingress/growth evidence exists. Stream tests pass in release and Memcheck; all seven fatal probes pass in release. All seven fatal probes also pass with ZDEBUG under ASan. Expected aborts are not leak-clean child exits. |

## Setup, artifact and build obligations

* Direct AF_XDP/BPF and queried ring offsets are implemented without libxdp or
  libbpf. Real kernel fixtures validate the loader/verifier, endpoint decisions,
  namespace/queue membership and protected attachment/retirement. Retain those
  results with current source fingerprints.
* The standalone utility and helper are independently usable. Setup owns its
  anonymous namespace, veth pair, endpoint tuples and neighbor entries, records
  them, preserves failure logs and removes only owned resources. Physical mode
  invokes caller-specified endpoints without reconfiguring their interfaces.
* `ZxdpDiag.hh`, `ZxdpOwnershipTest` and `ZxdpRxChainTest` are configured,
  built and executed in release. Eight obsolete generated test artifacts were
  removed before subsequent builds.
* The clean full-tree Clang release build completed; later Autotools regeneration
  and top-level builds include the current library, tests and utility. All unit
  and configured privileged programs execute and pass.
* All Zxdp suites and the direct kernel probe pass under Clang ASan/LSan with
  ZDEBUG. The clean full-tree sanitizer build and its UAPI follow-up build now pass with
  zero warnings/errors. The tested binaries remain byte-identical.
* `pending-uapi-configure.patch` is now applied in the original tree. Both its
  main-tree configuration and follow-up top-level build pass. The isolated
  Autoconf positive, missing-cookie and non-Linux-guard cases also pass.
* Current-source release runtime and Memcheck checks now pass in the isolated
  tree, whose 48 Zxdp source hashes exactly match the successful sanitizer
  snapshot. The earlier fixture-launch permission error is retained separately;
  it occurred before packet setup and was fixed by invoking Bash explicitly.
* The isolated clean full-tree release build completed with exit zero and no
  warnings/errors. Its runtime checks and the matching sanitizer checks are
  complete; the independent veth prerequisite is satisfied.
* Review all deliberately unavailable/skipped cases and preserve their coverage
  gaps. In the configured native fixture, every required program must execute.

## Physical coverage and the subsequent work

The plan explicitly establishes this prerequisite on veth and does not make
hardware availability a prerequisite to its veth deliverable. Veth reports copy
mode; it proves neither physical zero-copy performance nor physical driver DMA
retirement. The local ENA interface carries the default route and no online Linux
Tailscale peer was found. The user has been asked to identify a second host and
test interface/queue; local build/test work continues independently.

After this gate is evidenced, capture a fresh complete Zquic source inventory,
copy it byte-for-byte, verify identity before deletion, and make the case-preserved
rename separately inspectable. Only then register and verify the renamed module
and begin slice 1. All eight cumulative working-pair slices, the phase-2 TLS Rx
prerequisite, actual kernel TCP peers in both roles, physical runs, captures,
sanitizers, Clang `-pg` profiling and comparative optimization measurements remain
in the full objective. None is replaced by this gate ledger.
