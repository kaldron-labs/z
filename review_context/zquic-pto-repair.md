# Zquic PTO progress and Handshake key retirement

Implemented and verified with the supported Clang debug gates. This follows the skeptical
review of the pre-client-confirmation NAT failure. It does not claim that a
Zquic server can recover an external client that only sends Handshake packets
from a new NAT mapping after the server has retired its Handshake keys.

## Recovery changes

`PktTxSpace::reclaimOnPTO` uses the oldest eligible retained packet, preserving
the existing `ptoReclaimed`, outstanding-frame, ACK and loss accounting. A
regression continually adds newer retransmission packets and requires progress
through the original HANDSHAKE_DONE and STREAM ranges.

The common PTO path services the selected packet-number space. Both queued
retransmission selection and the retained-CRYPTO fallback are restricted to that
space during PTO. Ordinary ACK/loss recovery retains its existing sixteen-frame
batch and cross-space selection. A PTO retransmission batch is bounded by actual
protected-packet attempts, with room for an acknowledgment-eliciting companion
probe when another space has data in flight and usable transmit keys. The
companion requires primary-send success; consuming a packet number before local
send rejection does not count as admission. A failed primary is not replaced
with an unsolicited probe in another space. No new retry queue or timer is added.

Application Data cannot independently select PTO before confirmation. An existing
Handshake PTO can additionally probe outstanding 1-RTT traffic, consistent with
[RFC 9002 section 6.2.4](https://www.rfc-editor.org/rfc/rfc9002.html#section-6.2.4).
Candidate path proof remains ahead of ordinary recovery on both server passive
rebinding and client active migration. The existing client candidate sender can
retry a currently validating candidate without activating or rearming it.

The Stream regression covers successful retained-CRYPTO retransmission,
Handshake/AppData key and in-flight combinations, repeated PTOs, selective primary
rejection, rejection after protected-packet recording, mixed-space pending loss,
the two-packet budget, and confirmation gating. Existing synthetic established
AppData tests now explicitly establish their confirmation precondition.

## Handshake key retirement

Server TLS completion schedules retirement on its Rx owner after the current
CRYPTO delivery callback unwinds. That continuation clears the actual Rx-owned
Handshake traffic secrets/protectors and posts retirement of the separate Tx
protector and retained recovery state. Both loss and PTO timers are recomputed
immediately after packet-space retirement. Client receipt of HANDSHAKE_DONE uses the
same actual retirement machinery. Repeated retirement is idempotent; stale TLS
crypto snapshots and ACK posts cannot repopulate retired spaces. Server retirement
no longer waits for acknowledgment of HANDSHAKE_DONE.

A real UDP pair on separate Rx and Tx owners holds HANDSHAKE_DONE at the existing
frame-admission hook. It checks server retirement before client confirmation,
client key retention while confirmation is held, stale-snapshot protection,
client retirement after release, continued protected transfer and orderly stop.

This follows [RFC 9001 sections 4.1.2 and 4.9.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.9.2).
The crypto unit helper `completeHandshake` was not a production retirement path;
clearing only packet-space bookkeeping did not previously erase the keys.

Initial-key storage remains outside this Handshake repair: Tx currently reads
`m_crypto.initialKeys()` directly while Rx owns the crypto object. Clearing that
material on Rx before its Tx retirement handoff would introduce a race. The
existing Initial bookkeeping behavior is preserved; its key ownership requires
separate work.

## Diagnostic corrections

A positive short UDP result violates the backend's all-or-error contract. The
existing injected backend fixture is named `testBadUDPBackend`; it does not
identify a ZiTx defect and no ZiTx repair is required. The prior execution and
contract documents now use that attribution.

The offline `qir-script-test.sh` fixture used a synthetic Python runner but
called the host Docker daemon for metadata and cleanup. Its cleanup forcibly
removed the live QIR containers during one reverse address-rebinding case
(all three exited 137 and the runner could no longer copy their logs). The
fixture now places a local Docker stub on its child PATH and verifies the stub
version in metadata. The isolated fixture passes. The interrupted result is
preserved; its single unchanged-image rerun after the other peer checks
passed. The accepted matrix has 30 unique cells and retains the interrupted
31st observation separately. No protocol timing or simulator parameters change.

The original pre-confirmation NAT failure remains historical evidence. QUIC
assumes stable handshake addresses; complementary client probing can provide an
authenticated new-path observation, but changing Zquic's client does not change
an external ngtcp2 client. This repair does not retarget traffic based on an
unauthenticated source tuple or retain Handshake keys past confirmation.

## Verification ledger

Clang debug; reused incremental builds; shared eight-job make jobserver from
REBUILD.md. Frozen Zi/Multiplex/IOBuf/ZmList fingerprints remain unchanged.
Artifacts are under `/home/ubuntu/zquic-pto-execution`.

- Initial focused diagnostic: new fairness and key-retirement tests passed;
  uncovered client candidate-proof priority regression, subsequently corrected.
- Full intermediate run: 21 files / 205 tests; existing runtime including
  deliberate migration loss passed. One new mixed-space fixture failed because
  it assumed a single packet in the existing duplicated Handshake flight;
  fixture now derives the actual packet number.
- Final timer-corrected QUIC unit suite: 21 files / 205 tests pass.
- Final endpoint integration suite: one file / four tests pass.
- Final Stream (55 cases) and real UDP Runtime (eight cases) Valgrind runs pass
  with `OPENSSL_ia32cap=0`, zero errors and zero definitely, indirectly or
  possibly lost bytes, without suppressions. Default accelerated OpenSSL
  reports an undefined-stack-value warning in AES-GCM finalization. A standalone
  EVP-only reproducer with initialized inputs reproduces it; the same reproducer
  is clean with capabilities disabled. This establishes provider dependence, not
  that the warning is benign. The default-provider Runtime diagnostic also had
  an existing close-qlog assertion failure; final native and scalar runs pass.
- Final HTTP: 27 files / 278 unit tests; eight files / 91 integration tests;
  five files / 31 interoperability tests plus seven H2 external-tool checks.
  The isolated offline QIR-script fixture also passes after its Docker fix.
- Final QIR image: `sha256:36939aa9d67811ee9d5937e2fe3aa7f320a6bf3c415e976e5dbc427c5ca2de3e`.
  Exactly 30 unique accepted cells: 28 succeeded, two cached MsQuic HTTP/3 cases
  unsupported, zero accepted failures. The infrastructure interruption and its
  sole same-image replacement remain explicitly recorded among 31 observations.
  Current production source and exact installed image-binary hashes match.
- Final affected downstream builds and unit suites all pass:

| Module | Files | Tests |
| --- | ---: | ---: |
| Zws | 5 | 28 |
| Zfb | 5 | 53 |
| Zpb | 4 | 65 |
| Zgrpc | 5 | 44 |
| Zrest | 2 | 24 |
| Zjrpc | 4 | 37 |
| Zmcp | 5 | 28 |
| Zum | 28 | 55 |
| Ztc | 9 | 48 |
| Zdash | 1 | 1 |

`root-final-validation.json` independently checks every module marker against
current source hashes, native suite results, final scalar Valgrind evidence,
QIR coverage and the frozen lower contracts. `qir-audit.json` retains each cell,
capability evidence and the interruption exclusion. The runner registration was
restored, containers and staging root removed, and owned failed runtime fixture
families archived under `residue/` outside the worktree. Existing unrelated dirty
source and pre-existing residue remain untouched.

The shared build jobserver was stopped after all drivers completed. The externally
deleted `zquic_repair.md` plan was left deleted; this execution report is retained.
