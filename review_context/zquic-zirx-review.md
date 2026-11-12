# Independent Zquic / ZiRx implementation review

Reviewed the final packet node/allocator, EndpointCxn admission, ZiTx hooks,
posted physical binding, teardown, Retry helper, protected-packet rejection
accounting and ZiRx framing paths against the combined repair plan and
`docs/AUDIT.md`. Review scope is these changes, not all unrelated worktree edits.

## Confirmed invariants

- TxUDP allocation constructs intrinsic `TxPkt : ZiTxBuf` metadata with address
  and ECN; Rx/plaintext metadata remains separate. No wrapper queue remains.
- Final admission is Tx-owned, rejects at 4096 total including active head,
  counts pressure/drop separately from socket errors, and counts submission
  bytes only after the explicit capacity/closing checks.
- Ordinary admission uses ZiTx's FIFO machinery. Priority uses the public
  iterator insertion after the active head. Repeated priority packets precede
  prior pending work without changing the completion head.
- `sending` installs both address and TOS for every active packet, including
  clearing an earlier mark for NotECT. `sent` reports socket completion only;
  no protocol recovery retirement or producer wake is inferred.
- Offowner posts snapshot only raw physical identity and the endpoint generation.
  The captured pointer is compared, never dereferenced or strongly retained.
  Final Tx admission takes a local reference to the current connection, checks
  its pointer against the snapshot, then checks both current endpoint and
  physical generations. Old or inconsistent snapshots drop; pointer reuse
  cannot admit a different generation. No per-packet owner reference is added
  to either buffer or posted callback. Existing endpoint owner-drain handoffs
  and physical close continuations retain their lifetime responsibilities.
  Buffer `owner` transfers to EndpointCxn only at final admission.
  Protected-packet direct false paths retain the existing discard/unwind,
  while posted drops stay in recovery.
- Retry passes priority through the existing filtered helper and retains its
  existing successful-output diagnostics.
- Physical retirement first completes the context, then cleans queue links.
  Endpoint teardown preserves Rx/Tx owner handoffs. Retained backend references
  remain independent from application queue retirement.
- ZiRx connection slots are assigned only from the connection's network
  allocator with initial length zero and no outbound-MPS cap. Network input
  is never grown. Incomplete/trailing message storage is App storage held
  outside the context; exact complete packets keep original backing.
- ZiRx moves captured pending state into local references before Hdr/Body,
  pins and compares the executing callback identity, and does not touch the
  retired closure after replacement/completion. Explicit rearm goes through
  the owner-directed connection API. Empty network stubs are not parsed.
  The builtin/stub owner matrix and clean Valgrind run support these paths.

## Findings and focused corrections

The first review identified duplicate endpoint close submissions. A safe
synthetic backend counts every physical close request while performing physical
retirement only once. Two public disconnect calls made within one Rx turn
produced three physical requests in total (one old rebind socket plus two
requests for the current socket), where exactly two were required. Both public
completion callbacks still ran. This is recorded in
`/home/ubuntu/zquic-zirx-execution/sock-review-repro-nonblocking.log` as
`physical close requests=3`, with the exact-count assertion failing.

The narrow correction reuses `m_closing`: `beginCloseRx` performs test-and-set
and `closeCxn` posts owner close work only for the first transition. No new
state/counter or generic Zi lifecycle change is introduced.

The review also found an advisory-telemetry sampling underflow: offowner
`txQueued` could observe queue count zero, then a newly installed active head.
It now returns zero immediately for a sampled zero count before subtracting
an actual pending head. It adds no lock, atomic or stable-snapshot requirement.

The final ownership audit removed an unnecessary strong physical connection
reference captured by every posted packet. Such a reference was safe but added
owner-reference churn contrary to `docs/AUDIT.md`; it was unnecessary because
admission never intentionally dereferences or sends through a retired physical
connection. The raw identity/generation snapshot above preserves exact binding
and stale rejection. The local current connection reference exists only while
Tx admission executes, as in the original endpoint path. This does not add a
counter, scheduler API or alternate lifetime model. Inspection of the final
`Endpoint_::send` and `send_` confirms the captured pointer is never dereferenced.
`final-owner-src-build.log`, `final-owner-sock-build.log` and
`final-owner-sock-test.log` record the subsequent library/focused rebuild and
passing five-test socket suite. `final-source-fence.json` records the coordinator's
final header rebuild fence before broader dependent verification.

The stale-post test is nonblocking: a synthetic provider posts against the old
physical connection within the Rx rebind turn, then publishes replacement.
The test accepts both legal races (pressure rejection on the full old queue,
or stale identity rejection after publication), checks reference release and
that the replacement queue remains empty. The recorded reproduction observed
`stale=1 physical pressure=1`, establishing the actual stale-drop path. No
scheduler owner is blocked to arrange the race.

Final verification after both corrections: `zquic-review-final-src.log` records
successful shared-jobserver library rebuild with no warnings. The dependent
focused rebuild (`sock-review-final-build.log`) and `ZquicSockTest` direct/prove
runs pass (`sock-review-final.log`, `sock-review-final-prove.log`, five top-level
tests). The final direct run records `stale=1 physical pressure=1` and exactly
`physical close requests=2`; both public disconnect callbacks run and the
endpoint-down notification remains once-only. `git diff --check` passes for
the reviewed implementation and fixture files. No further code findings remain
within this review scope.

## Established limits

The frozen ZiTx/ZiMultiplex positive-short UDP behavior is a separately recorded
backend-contract limitation: the focused test characterizes retained suffix
state without transmitting a second datagram. This review does not claim that
arbitrary backend short-positive completion is safe. Native UDP remains
all-or-error. See `zquic-zitx-contracts.md` and execution results for the
characterization, fairness/profile and interoperability gates.

The earlier TCP truncation was an example bug in `zimxserver`, not a library
bug. Its whole-response completion correction and 32-response large-output
verification are documented in `zirx-execution.md`.

A subsequent test-only native writer characterization strengthens the short-UDP
evidence: injected 1-of-4 completion leads to a real 3-byte suffix send through
the unchanged common writer. The receiver and local diagnostics prove the
misclassification directly. See `zquic-zitx-contracts.md` and
`zquic-itest-short-evidence.log`; no production source changed after this review.
