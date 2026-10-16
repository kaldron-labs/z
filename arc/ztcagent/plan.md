# `ztcagent` prescribed implementation

## Result

Implement `ztcagent` in the `ztc` module.  The process owns one reader
on the local Ztc telemetry ring, discovers `Ztc::App` publishers, routes
hub requests to their request rings, rewrites response sequence numbers, and
forwards the existing `Ztc::Hdr`-framed FlatBuffers stream over one outbound
mTLS connection.

The implementation is not a `Ztc::App`, opens no listener, emits no telemetry,
adds no wire protocol, performs no steady-state JSON translation, and writes no
identity, credential, telemetry, subscription, or enrollment state to disk.
The held `ZiPIDFile` is process coordination, not persistent agent state.

Complete `ztls.md` first and use its `saveCSR()`, `Ztls::ClientCreds`,
supplemental-CA, and `Ztls::ClientParams` APIs directly.  Do not add another
TLS, HTTP, crypto, resolver, platform, persistence, or buffering abstraction
([Framework fit](GUIDELINES.md#framework-fit),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Authoritative corrections to `goal.md`

The following decisions supplied during plan review supersede conflicting text
in `goal.md`; implementation and tests must follow these corrections directly:

- enrollment returns separate `hub_host` and `hub_port` fields, not `hub_url`;
- the PID filename is always `ztcagent.pid`, independent of the telemetry ring,
  so exclusion is per configured PID directory rather than per ring name;
- the authenticated enrollment server authoritatively assigns the hub host;
  there is no second domain allow-list;
- the operational mTLS key is Ed25519; the agent checks the leaf/key match but
  leaves client-chain trust to the hub and hub-server-chain validation to TLS;
- indeterminate enrollment retries the identical in-memory request at one
  configurable fixed interval, default one second, with no deadline/backoff;
- `ZuCmp<uint64_t>::null()` is the absent sequence sentinel and zero is valid;
- the agent uses one process-wide monotonically increasing request sequence;
- active request and leg maps have no artificial count cap; terminal one-shot
  state is collected immediately and periodic state exists only while active;
- publishers need no proactive liveness probe; ordinary ring failure and a new
  startup announcement are sufficient;
- an agent start or restart first attaches to the shared telemetry publish ring,
  then scans the PID-file registry and requests a one-shot `AppTelemetry`
  snapshot from every live publisher found;
- the agent alone authoritatively configures and creates that shared telemetry
  ring; publishers only idempotently open the existing ring on demand with
  size zero and never create, resize, or reset it;
- `Ztls` owns outbound buffering, fragmentation, ordering, and backpressure;
  the agent adds no outbound queue; and
- image construction is out of scope, although the executable must run inside
  deployment-owned container images.

These are deliberate scope corrections, not compatibility accommodations
([General principles](GUIDELINES.md#general-principles),
[No dogma](GUIDELINES.md#no-dogma),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Files and build wiring

Add exactly these production files:

```text
ztc/src/ZtcVer.hh          protocol-version arithmetic
ztc/src/ZtcRing.hh         shared Ztc ring type
ztc/src/ZtcAgent.hh        AgentCf, AgentEnv, and Agent declaration
ztc/src/ZtcAgent.cc        enrollment, discovery, routing, and hub implementation
ztc/src/ztcagent.cc        CLI/config loading and process lifecycle only
ztc/ztcagent.conf          sample tuning configuration
ztc/README.md              enrollment and operations documentation
```

Add `ZtcVer.hh` and `ZtcRing.hh` to `libZtc`'s `pkginclude_HEADERS`.  Put
`ZtcAgent.hh` in `noinst_HEADERS` and build `ZtcAgent.cc` as the `ztc/src`
no-install convenience library `libZtcAgent.la`, shared by
`ztcagent` and its unit test.  Add `ztcagent` to `bin_PROGRAMS` and make
`ztcagent.cc` its only program-owned source.  Link the private agent library
with `libZtc`, `libZws`, `libZhttp`, and `libZtls`; do not add `zws`, `zhttp`,
or `ztls` dependencies to `libZtc`, and do not repeat transitive dependencies.
Add their source include paths only to the agent library, executable, and
agent-test target flags; do not widen `libZtc_la_CPPFLAGS`.
Distribute `ztc/ztcagent.conf` and `ztc/README.md` through `EXTRA_DIST`; do not
install the sample configuration as live configuration.

Keep `ztc` after `zdb` in the top-level `Makefile.am` `SUBDIRS` ordering; its
`zws`, `zhttp`, and `ztls` dependencies must also precede it.  Add
`ztc/itest/Makefile.am`, put `itest` after `test` in `ztc/Makefile.am`, add
`ztc/itest/Makefile` to `configure.ac`, and invoke its TAP target from `ztc`'s
`test` target.

Add these test entry points:

```text
ztc/test/ZtcAgentTest.cc
ztc/itest/ztcagenttest.cc
```

Extend existing `ZtcSchemaTest.cc` for version/schema/sentinel coverage and
`ZtcAppTest.cc` for publisher delivery, error correlation, and request-sequence
admission.  Do not create single-purpose `ZtcVerTest` or `ZtcRingTest` binaries.
Keep isolated routing/state cases in `ZtcAgentTest`; keep only multi-process,
HTTPS, mTLS, and shared-memory scenarios in `ztcagenttest`
([Module layout](GUIDELINES.md#module-layout), [Tests](GUIDELINES.md#tests)).

Use namespace `Ztc`, with private implementation types under `Ztc::Agent_`.
The private reusable interface names are `Ztc::AgentCf`, `Ztc::AgentEnv`, and
`Ztc::Agent`; none carries `ZtcAPI` or enters `pkginclude_HEADERS`.  Internal
node names are `App`, `Req`, and `Leg`.  Do not add a `ZtcAgent` namespace or
export agent types.  Program/resource basenames,
member prefixes, accessors, trailing underscores, and the 28-byte name limit
must follow [Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes),
[Length and abbreviations](GUIDELINES.md#length-and-abbreviations),
[Accessors](GUIDELINES.md#accessors), and
[Trailing underscores](GUIDELINES.md#trailing-underscores).

## Slice 1: common protocol primitives

### Version API

Implement `ZtcVer.hh` as a small namespace of `constexpr` functions:

```cpp
namespace Ztc::Ver {
  enum { MinorBase = 1000, MajorBase = 100000 };

  constexpr uint32_t make(unsigned major, unsigned minor, unsigned patch);
  constexpr unsigned major(uint32_t);
  constexpr unsigned minor(uint32_t);
  constexpr unsigned patch(uint32_t);
  constexpr bool compatible(uint32_t receiver, uint32_t sender);
}
```

`make()` implements `major * 100000 + minor * 1000 + patch`; callers supply a
minor below 100 and patch below 1000, matching the encoding.  `compatible()`
requires equal major versions and `receiver.minor >= sender.minor`; it ignores
patch.  Replace all duplicate version arithmetic with this API and retain
`Z_VERSION` as the current encoded value.  Do not add legacy forwarders
([Constant literals](GUIDELINES.md#constant-literals),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

### Ring API

Implement `ZtcRing.hh`:

```cpp
namespace Ztc {
  inline unsigned ringSize(const void *);
  ZuDerive(Ring,
    (ZiRing<ZmRingSizeAxor<ringSize, ZmRingMW<true>>>));
}
```

`ringSize()` returns `sizeof(Ztc::Hdr) + hdr->length`.  Delete the private copy
from `ZtcApp.cc` and the test copy from `ZtcTestClient.hh`; use `Ztc::Ring`
everywhere.  Keep `Ztc::Hdr` as the only application frame and use the same
configured complete-frame `maxFrame` locally and remotely
([Code structure](GUIDELINES.md#code-structure),
[Type and API friction](GUIDELINES.md#type-and-api-friction)).

### Message boundary API

In `fbs/ztc_request.fbs`, remove `Error.has_seq_no` and use
`ZuCmp<uint64_t>::null()` as the unsequenced `Error.seq_no` sentinel.  Zero is
a valid sequence number.  Give `Request.seq_no` and `Error.seq_no` the
FlatBuffers default `18446744073709551615` so omission decodes to that sentinel
while an explicit zero remains representable.  Regenerate only through
`fbscodegen` and propagate the breaking schema change directly; do not add a
compatibility field or shim
([Metadata](GUIDELINES.md#metadata),
[Framework fit](GUIDELINES.md#framework-fit),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

Retain `Ztc::msg()` as the structural verifier.  Do not add agent routing policy
to exported `ZtcMsg.hh`.  Define only this file-scope function in
`ZtcAgent.cc`:

```cpp
static bool rewriteSeq(Ztc::Hdr *, uint64_t);
```

The hub parser uses `Ztc::msg()` and directly requires `Body::Request`; the
publisher remains the sole filter parser.  Missing/null/empty `id` is wildcard,
and every filter representation passes through unchanged.  `rewriteSeq()` uses
one `switch` over `Telemetry`, `Ack`, `Error`, and `EOS`, calls only the
generated mutable scalar accessor, rejects an unsequenced `Error`, and runs only
on a verified, outbound-owned mutable frame.  This is the one justified direct
generated-FlatBuffers boundary; construction and native conversion use
`ZfbStruct`
([Metadata](GUIDELINES.md#metadata),
[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Type and API friction](GUIDELINES.md#type-and-api-friction)).

Do not globally tighten `validMsg()` for agent policy and do not reject unknown
optional fields.  Generate schemas only through `fbscodegen`.  Test all version
edges, optional request fields, wildcard filter forms, every response-body
rewrite, malformed FlatBuffers, invalid body shapes, and maximum-frame edges.

## Slice 2: publisher delivery correction

In `ZtcApp.cc`, define a parameter-independent `App_::Delivery` enum with
`Telemetry` and `Control`, and replace `publishRaw_()` with one worker-owned
reservation path:

```cpp
bool publishRaw_(ZmRef<ZiIOBuf>, App_::Delivery);
```

The agent is the sole creator of the shared global telemetry Tx ring and its
`AgentCf::telSize` is authoritative.  Do not add telemetry-ring size or
creation configuration to `Ztc::AppCf`.  A publisher initializes its telemetry
writer with size zero and idempotently calls `open()` only when it has a frame
to publish.  That call may attach to the existing agent-created ring but must
not create, resize, replace, or reset it.  Failure because the agent has not
created the ring yet is an ordinary unavailable-ring result; retain the
existing pending application-snapshot state and return failure through the
existing publisher policy.  Once the agent is up, its registry snapshot
request causes another publish attempt.  Publishers cannot receive such a
request before the agent has created and attached its telemetry reader
([Storage and capacity](GUIDELINES.md#storage-and-capacity),
[Framework fit](GUIDELINES.md#framework-fit)).

Prescribe this publisher loop:

1. If the writer is not open, idempotently open the existing telemetry ring
   with size zero.  If `open()` fails because no ring exists, return failure;
   a size-zero opener must not create it.
2. Call timed `push(frameLength)`; do not use `tryPush()`.
3. On success, copy directly to the exact reservation and call `push2()`.
4. On timeout, read `int status = writeStatus()`.  A nonnegative result means
   the ring remains open with readers but lacks a usable reservation: call
   public `kill()`, recheck stop state, and retry.
5. For a negative result, `switch` over `Zu::NotReady`, `Zu::EndOfFile`, and
   `Zu::IOError`, and return failure without calling `kill()`.
6. `Control` retries until success, shutdown, or a terminal ring state.
   `Telemetry` invokes the same eviction path once and may then follow the
   existing telemetry-drop/degraded policy.

Call the control path for `Ack`, `Error`, and `EOS`; call the telemetry path for
ordinary telemetry.  Do not inspect PIDs, call private GC, sleep for
`killWait`, or implement process termination separately; those operations are
encapsulated by public `kill()`.  Do not add a lock or move this state from its
current worker.  This implements the applicable stalled-reader requirement
using only `ZiRing`'s public contract ([I/O sharding](GUIDELINES.md#io-sharding),
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization)).

Change the declaration and definition of `Ztc::App::sendError_()` to:

```cpp
void sendError_(uint64_t seqNo, int32_t code, ZuCSpan message);
```

Write `seqNo` directly to `Error.seq_no`.  Update every caller explicitly:
invalid FlatBuffers and unexpected message-type errors pass
`ZuCmp<uint64_t>::null()` because they are general app/protocol errors;
`failReplay_()` passes `sub->seqNo` because the replay failure terminates that
request.  Audit any additional caller by the same rule: pass the request
sequence when the error belongs to one request, otherwise pass the null
sentinel.  Do not infer correlation later from error text or code.

In `Ztc::App::request_()`, replace the existing `!seqNo` clear-all test with a
comparison to `ZuCmp<uint64_t>::null()`.  Initialize native `Ztc::Request::seqNo`
to the same sentinel.  This preserves the existing clear-all control meaning
without reserving zero as a special value.

Add one publisher-owned admission watermark, `minReqSeqNo`, initialized to
zero when the `Ztc::App` starts.  For every work-creating request
(`subscribe == true`), after validating the frame, body, and target ID but
before request-specific processing:

1. Ignore the request when `seqNo` or `minReqSeqNo` equals
   `ZuCmp<uint64_t>::null()`, or when `seqNo < minReqSeqNo`.
2. Otherwise accept the gap and set `minReqSeqNo = seqNo + 1`.
3. If `minReqSeqNo` has reached `ZuCmp<uint64_t>::null()`, ignore all further
   work-creating requests until the publisher restarts.

In general, after accepting sequence `N`, ignore every later work-creating
request whose sequence is `<= N`; the next accepted sequence may be any value
greater than `N`.  For example, accepting 1 followed by 3 permits the gap at 2
and causes a later 2 or 3 to be ignored.  Unsubscribe requests bypass this
admission check because they reuse the sequence of the work being cancelled;
the null-sentinel clear-all control also bypasses it.  Test gaps, duplicates,
regression, zero, the null sentinel, and unsubscribe reuse without sleeps.

Extend `ZtcAppTest` to coordinate a responsive and stalled reader with
`ZmBlock`/`ZmSemaphore`, prove eviction and control delivery, and cover every
terminal `writeStatus()`.  It must not use sleeps as completion conditions
([Tests](GUIDELINES.md#tests)).

## Slice 3: configuration and lifecycle

### Configuration types

Define `AgentEnv` as all-public owned startup data:

```cpp
using AgentToken =
  ZtString<ZtStringHeapID<"Ztc.Agent.Token">>;
using AgentDeviceID =
  ZtString<ZtStringHeapID<"Ztc.Agent.DeviceID">>;
using AgentEnrollURL =
  ZtString<ZtStringHeapID<"Ztc.Agent.EnrollURL">>;

struct AgentEnv {
  AgentToken     token;
  Zi::Name       ring;
  Zi::Path       pidDir;
  AgentDeviceID  deviceID;
  AgentEnrollURL enrollURL;
};
```

Only `ztcagent.cc` reads the environment.  It reads `ZTC_ENROLL_TOKEN`,
`ZTC_RING`, `ZTC_DIR`, optional `ZTC_DEVICE_ID`, and the test/development
`ZTC_ENROLL_URL` once, moves them into `AgentEnv`, then stops
using the environment views.  If `ZTC_RING` is unspecified, use `ztc`; if
`ZTC_DIR` is unspecified, use the existing publisher-registry name `ztc`.
Interpret `pidDir` exactly as `Ztc::App` does: a relative registry path below
`ZiFile::tmpDir()`, not an absolute directory.  The default URL is exactly
`https://enroll.devices.kaldron.io:443/v1/enroll`.  Reject an empty token.
These owned types prevent environment views from escaping startup and give
each variable one source
([Strings](GUIDELINES.md#strings), [Lifetime](GUIDELINES.md#lifetime),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

Define `AgentCf` as all-public `ZfStruct` configuration.  It contains:

- `maxFrame`, default `Ztc::AppCf::DefltMaxFrame`;
- `telFrames`/`telBytes` for telemetry-to-route,
  and `reqFrames`/`reqBytes` for hub-Rx-to-route;
- `fanoutBatch`, default 64, limiting potentially blocking publisher-ring
  writes performed by one route turn; this is a scheduling quantum, not a
  registry or request-capacity limit;
- `enrollRetry`, a fixed interval defaulting to one second;
- `reconnMin`, `reconnMax`, `reconnBackoff`, and `reconnRandom`, passed directly
  to `ZmBackoff`;
- `telSize` (default `AgentCf::DefltTelSize = 1U << 21`), `telLL`, `telSpin`, and
  `telTimeout` for the shared ring reader;
- `reqTimeout`, `reqKillWait`, and `reqCoredump` for publisher-ring writes; and
- `mx` (`ZvMxCf`) plus `routeThread`, selecting one configured isolated thread.

Define every default once using the local `AppCf` pattern: integral constants
in the `AgentCf` enum and floating constants as `static constexpr` members,
then use those names for member initialization and document the matching
`ZfStruct` default.  Constrain every field in `ZfStruct` and add a maintenance
comment stating the corresponding protocol bound or maximum resident-memory/
workload assumption.  Require `fanoutBatch > 0`, each frame limit to be
positive `telFrames`/`reqFrames`, `telFrames < UINT_MAX`, each frame limit to
be representable by its byte bound, and `telSize >= maxFrame +
Zm::CacheLineSize`.  Configure frame and byte limits independently.  Do not use
fixed arrays or duplicate a field in the environment or CLI
([Storage and capacity](GUIDELINES.md#storage-and-capacity),
[Metadata](GUIDELINES.md#metadata),
[Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes)).

Use `ZfCLI` only for configuration-file selection, validation, help, and
version.  The configuration file is the only source for `AgentCf`; environment
is the only source for `AgentEnv`; enum values are defaults only for omitted
configuration fields.  Do not implement reload or precedence.

### Agent lifecycle

Give `Agent` the same lifecycle shape as `Ztc::App`:

```cpp
class Agent : public Ztls::Client<Agent> {
public:
  using CtrlFn = ZmFn<void(bool), ZmFnHeapID<"Ztc.Agent.CtrlFn">>;
  struct Link;

  Agent();
  ~Agent();
  Agent(const Agent &) = delete;
  Agent &operator =(const Agent &) = delete;

  bool init(const AgentCf &, AgentEnv);
  void start(CtrlFn);
  bool start();
  void stop(CtrlFn);
  bool stop();
  void final();
};
```

Implement private data in `ZtcAgent.cc` as `Agent_::State`, referenced by one
raw `m_state` pointer, following `Ztc::App`.  `init()` validates configuration
and the enrollment URL and initializes `ZiMultiplex`, but performs no network
or ring I/O.  `start()`
acquires `ZiPIDFile` as `ztcagent.pid` under `AgentEnv::pidDir` before opening the
telemetry ring, starts owners, and begins enrollment.  `stop()` rejects calls
from owned shards, stops asynchronously, and permits only the main thread to
block.  `start(CtrlFn)` and `stop(CtrlFn)` are thin asynchronous dispatchers
callable from any thread; their blocking overloads are main-thread only.
`final()` is idempotent and asserts/drains all dependent state
([Standard function name vocabulary](GUIDELINES.md#standard-function-name-vocabulary),
[I/O Function Organization](GUIDELINES.md#io-function-organization),
[Teardown](GUIDELINES.md#teardown)).

Build the relative name `<pidDir>/ztcagent.pid` with `ZiFile::append()` and pass
it to `ZiPIDFile::init(ZiFile::tmpDir(), name)`.  The basename is exactly
`ztcagent.pid`: the PID-file identity is the agent process, not its selected
telemetry ring, so never derive it from `ZTC_RING`.  Let `ZiPIDFile::init()`
validate the relative path; do not duplicate its path validation.
Initialize the telemetry ring with `env.ring`, the authoritative configured
nonzero size, and its wait parameters; open it `Ring::Read`, creating it when
absent, attach exactly once, and never call `reset()`.  This must complete before
any publisher snapshot request is sent.  A ring surviving an earlier agent may
be attached without disturbing it or its independent readers.  If it has a
different size, let `ZiRing::open()` fail startup; do not attach with different
parameters, resize it, or replace it.  Publishers always open this ring with
size zero and therefore never participate in its creation or size policy.

Startup order is PID file, multiplexer, route owner, telemetry reader attach,
PID-registry scan/snapshot requests, enrollment client, hub client.  The
telemetry reader must be attached before the registry directory is opened, so
startup announcements from publishers racing the scan cannot be missed.
Teardown prevents ingress, cancels timers,
drains the telemetry reader and route owner, finalizes the hub and enrollment
clients, closes/detaches rings, stops the multiplexer, and releases the PID
file last.  Each failed startup step unwinds only completed predecessors in reverse
order ([Timer Teardown](GUIDELINES.md#timer-teardown),
[Teardown](GUIDELINES.md#teardown),
[General principles](GUIDELINES.md#general-principles)).

An agent-only process restart follows this same startup path.  It reattaches to
the shared publish ring, begins consuming first, then scans the existing
publisher PID registry and requests current `AppTelemetry` snapshots.  Do not
require publishers to restart or introduce periodic publisher reannouncement.

## Slice 4: enrollment and hub session

### Enrollment state

Implement these internal states, not overlapping booleans:

```cpp
struct Phase { enum { Down, Enrolling, Connecting, Up, Stopping }; };

struct PendingEnroll {
  ZmRef<Ztls::PK::AnyPK> key;
  Agent_::CSR             csr;
  Agent_::Nonce           nonce;
  Agent_::EnrollBody      body;
};

struct Identity {
  Ztls::ClientCreds creds;
  Ztls::Certs       caCerts;
  Ztls::Host        host;
  ZuID              agentID;
  uint16_t          port;
};
```

Define `CSR`, `Nonce`, and `EnrollBody` immediately outside these
structs as `ZtArray`/`ZtString` aliases with the heap IDs `Ztc.Agent.CSR`,
`Ztc.Agent.Nonce`, and `Ztc.Agent.EnrollBody`.  Reuse `Ztls::Host`; do not add
an agent-specific hostname wrapper.  Bound the
complete enrollment request and response through the existing `Zhttp` body
limit; do not add a per-field limit matrix.  Since every decoded response value
originates within that bounded body, do not duplicate the same bound on each
JSON string, certificate, or chain.  Give every other variable-size member an equally explicit
`ZmVHeap`/`ZtArrayHeapID`/`ZtStringHeapID`.  Order members largest-to-smallest
where logical grouping permits.  Store one `int8_t phase` whose values are
`Phase::Down` through `Phase::Stopping`, plus one `ZuUnion` holding
`PendingEnroll` only while enrolling or `Identity` after successful enrollment;
do not retain empty response arrays in `PendingEnroll` or add parallel
`hasPending`/`hasIdentity` booleans
([Class and struct shape](GUIDELINES.md#class-and-struct-shape),
[Heap allocation](GUIDELINES.md#heap-allocation),
[Framework fit](GUIDELINES.md#framework-fit),
[Casing and member prefixes](GUIDELINES.md#casing-and-member-prefixes)).

### Enrollment transaction

Use one `Zhttp::Client` request with redirects disabled and TLS early data
disabled.  Generate one Ed25519 operational keypair, one empty-subject DER
PKCS#10 CSR signed by that key, and one 32-byte OS-CSPRNG nonce.  Use this key
and the returned certificate only for `ztcagent`-to-hub mTLS.  Encode nonce
directly as base64url and CSR as base64
into bounded output storage.  Use `ZfJSON` to build exactly the request schema
in `goal.md`, and send exactly its required headers.  `device_id` is optional
metadata; omit it when absent.  Never copy the bearer token into the body or
URL.

Retain the complete key/CSR/nonce/body transaction.  Classify a response as:

- `Success`: HTTP 201 with acceptable JSON media type and a fully valid body;
- `Rejected`: a definitive HTTP/protocol/validation failure;
- `Indeterminate`: connection loss after dispatch without a definitive result.

Retry `Indeterminate` with the same method, URL, headers, key, CSR, nonce, and
body at the configured fixed enrollment retry interval, default one second.
Continue until success, definitive rejection, shutdown, or process restart;
do not add a recovery deadline, enrollment backoff, or jitter, and do not begin
another transaction in the same process.  `Rejected` fails startup after a
sanitized log entry.  Do not regenerate any field.  Parse the bounded response
with `ZfJSON`; do not add a second JSON policy layer.  Read and validate the
required protocol version, echoed nonce, assigned ID, certificates, hub
hostname, and port.  Decode the returned DER directly into `Ztls::Certs` and
move it with the generated key into `Ztls::ClientCreds`; let that API reject a
non-Ed25519/mismatched leaf or unusable chain.  Do not add a second client-chain
trust check; the hub validates the presented chain during mTLS.  Treat the hostname returned by the authoritatively
configured and authenticated enrollment server as the assigned hub hostname;
do not apply a second domain suffix or ownership allow-list.  Require a port in
`1..65535`; do not parse a hub URL or URI scheme.  Require the response fields
`hub_host` and `hub_port`; reject `hub_url` as a substitute.  Check the HTTP
status and required response headers
stated in `goal.md` using normal `Zhttp` semantics.  Pass the returned hostname
unchanged to the normal resolver and as the exact TLS SNI/certificate name;
DNS addresses never authenticate the hub.

Parse `hub_ca_chain` and pass it as enrollment-authorized supplemental trust
material directly to `Ztls::ClientParams::caCerts`.  Permit self-signed
certificates and do not impose an additional agent-side CA-chain policy; TLS
determines the usable hub-server chain.  Scope this supplemental trust
to the enrolled hub connection and never merge it into platform or
process-global roots.

This cold path performs one JSON parse and one base64 decode per returned DER
item, moves the resulting Z arrays into TLS, and creates no duplicate PEM/DER
or certificate wrapper
([Metadata](GUIDELINES.md#metadata),
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization),
[Framework fit](GUIDELINES.md#framework-fit),
[Heap allocation](GUIDELINES.md#heap-allocation)).

On success, move key/certificates into `Ztls::ClientCreds`, move supplemental
CAs and normalized host/port into `Identity`, erase the bootstrap token and
serialized pending request, destroy the enrollment client, and create a fresh
hub connection.  On hub reconnect reuse `Identity`; never contact the hub with
the enrollment token.  On process restart discard everything and enroll again.

### Hub client

Use `Agent : Ztls::Client<Agent>` and
`Agent::Link : Ztls::CliLink<Agent, Link>`.  Initialize with `Identity::creds`,
platform roots plus `Identity::caCerts`, exact SNI hostname, and mTLS.  Implement
`Link::process(Ztls::RxStream &)`, `connected`, `disconnected`, and
`connectFailed` in the existing Ztls callback shape.  Keep all private class
data at the bottom of its class and prefix it with `m_`; keep the all-public
`AgentCf`, `AgentEnv`, `PendingEnroll`, and `Identity` members unprefixed.

`process()` incrementally consumes `Ztc::Hdr`; partial header/body returns 0.
It applies `maxFrame`, `Ztc::msg()`, and requires `Body::Request`.  Invalid length,
FlatBuffer, or body returns -1 and closes only that hub session.  Each successful
connection allocates a nonzero monotonically increasing `cxnGen`, resets the
hub admission watermark `minHubSeqNo` to zero, and starts with no requests.
Use `ZmBackoff` for the reconnect delay and have `Agent::reconnFreq()` return
the next interval.  On connect failure or disconnect, log the classification,
close the link, and call the `Ztls::CliLink` base `connectFailed(true)` so its existing
Rx-owned reconnect timer schedules `connect_()`.  Reset the current interval
to `ZmBackoff::initial()` in `connected()` immediately after TLS succeeds.  Do
not add a stability interval, a second reconnect timer, or another backoff
implementation; base teardown cancels and drains the existing timer
([Framework fit](GUIDELINES.md#framework-fit),
[Timers](GUIDELINES.md#timers), [Timer Teardown](GUIDELINES.md#timer-teardown)).

Give `Link` one Rx-owned partial-frame `ZiIOBuf` allocated from
`ZiIOBufAlloc<..., "Ztc.Agent.HubRx">`.  Copy the four-byte header first, call
`loadHdr()` before growing the buffer, then copy exactly the remaining body
across `RxStream::consume()` spans.  Verify and move the completed buffer to
route, reset the partial state, and continue until the Ztls-provided bounded Rx
input is consumed.  Never make a temporary contiguous copy or introduce a
second parsing quantum.

The Rx partial buffer is link-owned and never crosses shards; move only the
completed `ZiIOBuf` reference to routing
([I/O sharding](GUIDELINES.md#io-sharding),
[I/O and system integration](GUIDELINES.md#io-and-system-integration),
[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization)).

Submit each complete outbound `ZiIOBuf` through the existing `Ztls` send API.
`Ztls` owns record fragmentation, buffer retention, transmit ordering,
backpressure, partial writes, and completion.  Configure its existing bounded
send buffer; do not add `TxFrame`, an agent-owned Tx queue, offsets, completion
credits, or outbound frame/byte counters
([Framework fit](GUIDELINES.md#framework-fit),
[Scope and cleanup](GUIDELINES.md#scope-and-cleanup)).

## Slice 5: execution ownership and bounded handoffs

Use three agent contexts because the blocking telemetry read cannot also
service hub commands:

| Context | Exclusive state |
|---|---|
| telemetry `ZmThread` | shared telemetry `Ztc::Ring` reader |
| isolated route thread | app/request/leg indexes and request-ring writers |
| Ztls Rx shard | hub parser, connection generation, reconnect timer |

The internal Ztls Tx shard is owned entirely by `Ztls`; the agent keeps no
state on it.

Never read another context's state and never add a lock to permit it.  Public
cross-context functions post immediately to a trailing-underscore owner
function.  On-owner functions assert their context.  A scheduler `run`/`invoke`
is asynchronous; dependent work occurs only in its continuation
([I/O sharding](GUIDELINES.md#io-sharding),
[Concurrency and sharding](GUIDELINES.md#concurrency-and-sharding)).

Implement two agent-owned frame handoffs: telemetry-to-route and
hub-Rx-to-route.  Each producing owner maintains its own pending frame and byte
counters and increments them before posting a `ZmRef<ZiIOBuf>` plus fixed
metadata.  Hub-Rx frames carry `cxnGen`; telemetry frames do not read or carry
route/Rx-owned generation state.  Route returns telemetry credits through a
bounded SPSC `ZmRing` sized to `telFrames + 1`: one slot per admitted frame plus
one reserved overflow acknowledgement.  The telemetry thread drains those credits before
its next admission.  Route completion posts fixed `{length, cxnGen}` metadata
to hub Rx.  Only the producing owner decrements its counters.  Reject admission
before either configured bound is exceeded.  This
bounds the data held in scheduler callbacks without a lock or shared atomic
counter.  Place each context's data group on its own cache line in
`Agent_::State`.  Each frame callback processes one frame.  The one-time PID
registry scan and cheap in-memory selection traversals complete synchronously;
the deployment invariant is that these registries remain small and never
contain thousands of entries.  Publisher-ring fan-out writes are different:
each can block for its configured timeout/kill policy, so perform at most
`fanoutBatch` of those writes in one route turn and repost the remaining work
([Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs),
[Liveness](GUIDELINES.md#liveness), [Callbacks](GUIDELINES.md#callbacks),
[Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies),
[No dogma](GUIDELINES.md#no-dogma)).

The telemetry thread performs timed `shift()`, checks its stop sentinel after
each return, and validates each shared frame in place.  It checks its owned
frame/byte admission counters before allocation.  If admitted, it copies the
frame once into a pooled `ZiIOBuf`; otherwise it records bounded loss.  It then
calls `shift2()` immediately.  It cannot consult route-owned mappings, so a
valid admitted but unmapped frame incurs that one unavoidable copy.  It never
holds a shared-ring record while waiting for routing or the hub.  Shutdown sets
the stop sentinel and waits at most the configured ring timeout for `shift()`
to return.  Do not inject a wake frame, set shared-ring EOF, or reset the shared
ring because those actions affect independent readers.

On telemetry-handoff overflow, the telemetry thread changes its owned
`telOverflowPending` flag from false to true and posts one fixed
`telOverflow_` event to route.  Further overflows only increment its bounded
loss counters.  Route fences the current generation, asks hub Rx to close it,
then returns a zero-length overflow acknowledgement through the existing
route-to-telemetry credit ring.  Positive credit lengths decrement admitted
frame/byte counters; only the zero-length acknowledgement clears the
telemetry-owned flag.  Route never reads or writes that flag directly.
Hub-Rx handoff overflow closes its carried generation.  Either overflow cancels
that generation's application work, drains its queued frames, logs bounded
loss, and permits the telemetry reader to continue.  Closing TLS causes the
agent to reconnect; the hub must reissue requests on the new session.  Do not
selectively drop, coalesce, reorder, or persist responses.

## Slice 6: registry and routing structures

### Intrusive nodes and indexes

Implement only these logical objects:

```text
AppIdx:  ZuID -> App
ReqIdx:  hub sequence -> Req
App::legs: agent sequence -> Leg
Req::pending: unsent intrusive list of Leg
Req::legs: intrusive list of Leg
Req::excl: app IDs removed from a periodic wildcard Req
```

Implement `AppIdx` and `ReqIdx` as intrusive `ZmHash`es.  `App` owns one
request-ring writer, its current `ztcver`, and its agent-sequence leg hash.
`Req` owns the original native `Ztc::Request`, `cxnGen`, its pending and sent
leg lists, and a lazily populated `ZmHash<ZuID>` exclusion set using the named
heap ID `Ztc.Agent.ReqExcl`.  `Leg`
contains raw `App *`/`Req *` back-pointers and embeds both its app-hash node and
request-list node; that request-list node is in exactly one of `Req::pending`
or `Req::legs`.  List membership is the send state, so do not add a
`fanoutPending`, `sent`, cursor, or batch-state field.  Allocate `App`, `Req`,
and `Leg` from named `ZmHeap`s; do
not separately allocate container nodes or reference counts.  All objects are
route-thread-affined and are deleted synchronously when terminal
([Containers and intrusion](GUIDELINES.md#containers-and-intrusion),
[Storage and capacity](GUIDELINES.md#storage-and-capacity)).

Use one agent-process-wide `nextSeqNo`, starting at one and incrementing once
for every application request the agent sends.  Do not reset it on app or hub
reconnection.  Zero remains a valid protocol sequence, but the agent does not
allocate it because every sequence-zero `AppTelemetry` is unconditionally a
startup announcement.  A process-wide nonreused value prevents a delayed
response from colliding with a new leg, without an app-generation field or a
composite wire key.  Never allocate `ZuCmp<uint64_t>::null()`; after allocating
the preceding value, close the hub session and fail admission of new work until
process restart.  There is no second agent sequence allocator.  Use
`nextCxnGen` only for hub callback generation fencing.

### Discovery

A structurally valid sequence-zero `AppTelemetry` is always a startup
announcement, regardless of `AppTelemetry.start_time`.  Every structurally
valid, compatible `AppTelemetry`, sequenced or not, updates the ID-keyed
registry; a later frame simply outdates the registry information for that ID.
Do not deduplicate startup announcements and registry-snapshot responses or
track which one arrived first.  Sequence zero additionally denotes a publisher
restart: cancel the preceding instance's legs before retaining the new entry.
For a nonzero frame, preserve any existing live legs while updating the stored
metadata; if the ID is absent, create it.
Each publisher creates and owns its app-specific request/Rx ring; the agent
must not create it.  Before inserting a discovered publisher, open that
existing ID-named ring for writing with size zero; opening invokes the public
ZiRing stale-reader cleanup.  Require
`writeStatus() >= 0` and protocol compatibility via
`Ver::compatible(Z_VERSION, ztcver)`.  For an absent ID or a sequence-zero
restart, open the ring before inserting/replacing `App`; for a nonzero update
to an existing `App`, reuse its writer and update only `ztcver`.  Do not reopen
the same ring for every application snapshot.  A sequence-zero announcement
retires the old `App` and its legs before compatibility/open failure can leave
stale state behind.  Never insert the agent itself.

After the telemetry reader has attached, run one startup scan on the route
thread using `ZiDir` over
`ZiFile::append(ZiFile::tmpDir(), env.pidDir)`.  Iterate the directory once in
that route turn, synchronously, blocking, and without batching.  Ignore
`.`/`..`, the exact
`ztcagent.pid` basename, non-`.pid` entries, and names whose stem cannot be held
by `ZuID`.  Treat each remaining stem as a publisher-ID candidate; do not parse
PID-file contents or reproduce platform process checks.  Open its existing
request ring for writing with size zero and require
`writeStatus() >= 0`; this public ring check rejects stale PID entries and
publishers that exited during the scan.  Do not skip a candidate merely because
an `AppTelemetry` for the same ID was already received; duplicate snapshots
need no coordination because the later `AppTelemetry` outdates the earlier one.

For each live candidate, first send the existing null-sequence clear-all
control, resetting its subscriptions and `minReqSeqNo` after an agent-only
restart.  Then allocate one value from `nextSeqNo` and construct exactly one
one-shot `Ztc::Request` with that sequence, exact candidate ID,
`Group::App`, `subscribe = true`, zero interval, and an unchanged missing
filter.  Send it with the normal timed request-ring path and retain no scan
request object, mapping, timer, or response state.  Its `AppTelemetry` response
updates `AppIdx` through the same path as every other `AppTelemetry`; its
unmapped Ack and EOS are dropped normally.

Attaching the telemetry reader before opening `ZiDir` ensures that startup
announcements from publishers created concurrently with the scan are
observable.  The announcement and requested snapshot require no ordering or
deduplication; whichever `AppTelemetry` is processed later becomes current.
After this one scan, do not rescan the PID registry, probe publisher liveness,
scan `AppIdx`, or add per-app timers.  Retire a registered app and its affected
legs only when a normal request-ring operation reports missing/no-reader/error,
or replace it when a later startup announcement establishes a new registered
instance
([I/O and system integration](GUIDELINES.md#io-and-system-integration),
[Liveness](GUIDELINES.md#liveness),
[Containers and intrusion](GUIDELINES.md#containers-and-intrusion)).

### Request admission and fan-out

For each hub Request:

1. Require matching live `cxnGen` and a hub sequence different from
   `ZuCmp<uint64_t>::null()`; zero is valid.
2. For work-creating requests, silently ignore a sequence below
   `minHubSeqNo`; accept gaps and set `minHubSeqNo = seq + 1`; close when that
   increment would produce the null sentinel.
   Unsubscribe addresses an existing `Req` and bypasses this rule.
3. Convert the FlatBuffer once to native `Ztc::Request` through `ZfbStruct`.
   Preserve null/empty filter representation and every alert field.
4. For nonempty `id`, select exactly that registered app.  If absent, construct one
   sequenced `Error` with the ID and `app not running`.
5. For missing/null/empty `id`, iterate the current compatible `AppIdx` entries
   once and allocate all selected legs into `Req::pending` in that route turn.
   This fixes one-shot membership at admission without a snapshot array,
   ordinal index, or cursor.  Retain the selector/template for a periodic
   `Req`.  An exact-ID request whose app is absent ended with the Error in step
   4 and creates no `Req`.
6. For each selected app, allocate one `Leg` and one value from
   `nextSeqNo`, then add the leg to `App::legs` and `Req::pending`.  Retain only
   the native request template and scalar leg sequence while pending; do not
   prebuild or queue serialized frames.
7. Drain at most `fanoutBatch` pending legs per route turn.  For each leg, write
   one application request with `ZfbStruct`, changing only its sequence, then
   use timed `push()`.  On success move its request-list node from
   `Req::pending` to `Req::legs`.  On full-ring timeout call `kill()` and retry;
   on missing/no-reader/error retire the app and affected leg.  If pending work
   remains, post one continuation identified only by the request's hub sequence
   and `cxnGen`; it re-finds the `Req` and drains the next batch.  Never block an
   I/O shard because this runs only on the isolated route thread.

When adding a leg, post a drain only if `Req::pending` was empty before the
insertion.  A drain that leaves pending legs posts its own continuation.  This
maintains at most one scheduled drain chain per request without a boolean or
cursor.

When an `App` is admitted, iterate active periodic `ReqIdx` entries once and
queue a pending leg for each selector that matches and does not already have
one, unless the app ID is in that request's `excl` set.  This reattaches
wildcard subscriptions and an exact-ID subscription after its target starts or
restarts without undoing a prior partial unsubscribe.  One-shot requests never
participate in this traversal.  Delete a one-shot `Req` immediately if initial
selection creates no legs; thereafter delete it only when both `Req::pending`
and `Req::legs` are empty.
Do not add `maxReqs` or `maxLegs` configuration or capacity-failure policy.
The authenticated hub is expected to keep only a small number of requests
active, and mappings exist only for active work.

Process every `AppTelemetry` through the discovery/update path above before
response routing.  A sequence-zero frame ends there.  A nonzero frame then
continues through ordinary leg lookup, so a hub-requested application snapshot
can both refresh the registry and be forwarded; an unsolicited or startup-scan
snapshot with no live leg is retained as discovery state but not forwarded.

For an application `Telemetry`, `Ack`, `EOS`, or an `Error` whose `seq_no` is not the
null sentinel, read
its ID and agent-assigned sequence, find `App`, then `App::legs`, and require matching
`cxnGen`.  Drop it if any lookup fails.  Call `rewriteSeq()` on the already
owned buffer, replacing the agent-assigned value with `Req`'s hub sequence, and admit it
to hub Tx.  Forward one Ack and EOS per app; never aggregate.  An
`AckStatus::Invalid` or
`AckStatus::Failed` retires that leg after forwarding.  An EOS retires a
one-shot leg after forwarding but does not retire a periodic leg: existing
`Ztc::App` emits EOS at the end of every periodic snapshot while retaining the
subscription.  An Error with a non-null `seq_no` is terminal for its leg.  An
Error whose `seq_no` equals `ZuCmp<uint64_t>::null()` is a general app/telemetry
error: validate its app ID and forward it unchanged without a leg lookup or
retirement.

For unsubscribe, find the referenced `Req`.  Missing/null/empty ID addresses
every pending and sent leg and deletes the `Req` after processing them.  An
exact ID addresses only its matching leg.  For a periodic wildcard `Req`, add
that ID to `Req::excl` even if no matching leg currently exists, so a later app
announcement cannot recreate the cancelled leg.  An exact-ID unsubscribe of
an exact-ID `Req` deletes that request; one-shot requests need no exclusion.
Delete a pending leg immediately because no request reached that publisher.
For each sent leg, rebuild the local unsubscribe from the stored request
template, preserving group, filter, and alert position, setting `subscribe =
false`, and substituting that leg's agent-assigned sequence; remove it after
its send decision.  Delete a one-shot `Req` after EOS, failed/invalid Ack, or
sequenced Error only when both its pending and sent lists are empty.  Retain a
periodic `Req` until a full unsubscribe, an exact-ID unsubscribe of an exact-ID
request, disconnect, or shutdown.  A partial unsubscribe from a wildcard
request retains the request and its exclusion set.  A failed/invalid Ack or
sequenced Error is terminal for that app leg, not for the other legs or the
hub's periodic request.  A periodic subscription is still active after a
snapshot EOS and may temporarily have no legs while waiting for a matching
publisher, so neither condition completes it.

Disconnect, overload, and shutdown first fence the generation.  Delete pending
legs without writing.  For every sent leg—one-shot and periodic—build and
attempt an application unsubscribe using its agent-assigned sequence, then
delete the leg regardless of write outcome; the dead hub generation cannot
retain it.  One-shot EOS and a
sequenced Error delete their completed leg without sending another
unsubscribe.  App death deletes its legs because its ring is already
unavailable.

Implement one `delLeg_()` path and one `delReq_()` path.  App death,
unsubscribe, EOS, Error, overload, disconnect, and shutdown call these paths;
no terminal object remains for a later sweep.  Iterator scopes end before
callbacks, posts, or container destruction
([Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow),
[Lifetime](GUIDELINES.md#lifetime)).

## Slice 7: operations documentation and tests

Do not add a Dockerfile, build an image, or prescribe a base image, entrypoint,
user/group, or container runtime layout.  Keep `ztcagent` suitable for inclusion
in deployment-owned images: use only portable Z Framework interfaces; accept
secrets and per-instance values through the prescribed environment variables;
and allow the deployment to provide shared-memory access, the PID-file
directory, configuration, and runtime libraries.  Document those requirements,
PID exclusion, enrollment, reconnect, bounded-loss behavior, shutdown, and log
redaction without defining a specific image.  Watchdog policy, cluster
topology, and secret-manager integration remain deployment concerns.

Implement these named TAP cases.  They cover the still-applicable acceptance
behavior plus the authoritative corrections above; do not preserve a stale
one-to-one numbering against contradictory `goal.md` criteria.

| Behavior | Test/gate |
|---|---|
| agent-only ring creation, publisher size-zero open, reader-before-scan ordering, PID-registry snapshots, last-AppTelemetry-wins updates, and ring-failure retirement | `app_registry` |
| one-shot wildcard frozen membership and empty selection cleanup | `initial_app_snapshot` |
| exact/wildcard selection and publisher-local filter handling | `request_selection` |
| fan-out write batching, continuation ordering, and pending-leg cleanup | `fanout_batching` |
| periodic wildcard late arrival and exact-ID restart | `periodic_late_app` |
| EOS/Error/Ack/unsubscribe/disconnect cleanup | `routing_cleanup` |
| malformed framing, FlatBuffers, body kinds, and bounds | `invalid_input` |
| enrollment, in-memory identity, hub mTLS, and reconnect | `enrollment_reconnect` |
| redirect, trust, hostname, and client-auth rejection | `tls_auth_rejection` |
| agent-only restart reattaches, rescans publishers, and leaves no durable state | `ephemeral_restart` |
| bounded handoffs, Ztls backpressure, loss reporting, and `ZmBackoff` | `hub_overload` |
| framework-only Linux/Windows build | `portable_build` |
| fixed `ztcagent.pid` exclusion and independent ring reader | `pid_exclusion` |
| identical enrollment retry at the configured one-second default | `enrollment_retry` |
| global agent sequence, gaps/stale rejection, fan-out rewrite, and fencing | `sequence_routing` |
| stalled telemetry reader eviction with control delivery | `slow_reader` |
| environment/config separation, omitted-value defaults, authoritative agent ring creation, and existing-ring size mismatch rejection | `config_sources` |

Keep isolated version, message, routing, and state-machine cases in `ztc/test`.
Put processes, shared memory, HTTPS, mTLS, network fragmentation, backpressure,
restart, and residue checks in `ztc/itest`.  Use real `Ztc::App` fixtures, an
independent telemetry reader, a local trusted enrollment issuer, and a raw mTLS
hub.  Use dynamic ports and `ZiTestResidue`; synchronize concurrent work with
`ZmBlock`/`ZmSemaphore`, never sleeps or polling
([Module layout](GUIDELINES.md#module-layout), [Tests](GUIDELINES.md#tests)).

## Build and verification order

1. Complete `ztls.md`; build `ztls/src`, then `ztls/test`, then run its tests.
2. Implement slice 1; build `ztc/src`, build `ztc/test`, run the version/message
   tests.
3. Implement slice 2; rebuild `ztc/src` before `ztc/test`, then run ring/App tests.
4. Implement slices 3-6; build `ztc/src`, `ztc/test`, and `ztc/itest`; run unit
   tests before integration tests.
5. Implement operations documentation; run top-level `make -j` and `make test` with no
   warnings.
6. Run ASan/LSan and valgrind over malformed input, enrollment retry,
   reconnect, overload, app churn, and every teardown path.
7. Configure the release build through `z.config`, run top-level `make clean`,
   then top-level `make -j`; do not mix build types.
8. Build and run the portable subset with gcc/clang on Linux and MinGW/Windows.
   `ztcagent` and `Ztc::Agent` must contain no `_WIN32`, native
   shared-memory/process/CA-store
   code, or direct OS calls.

This order follows [Build configuration](GUIDELINES.md#build-configuration),
[Acceptance](GUIDELINES.md#acceptance), and
[Debugging](GUIDELINES.md#debugging).

## Guideline alignment ledger

Use this ledger in review; an implementation element is incomplete if its row
cannot be demonstrated in code and tests.

| Element | Required guideline alignment and audit disposition |
|---|---|
| files, Automake wiring, private API | Existing module boundaries and direct dependencies only; no installed agent surface or compatibility shim ([Module layout](GUIDELINES.md#module-layout), [Scope and cleanup](GUIDELINES.md#scope-and-cleanup)). |
| `Ver`, `Ring`, schema, `rewriteSeq` | One shared implementation, generated schema path, `ZfbStruct` except the justified mutable-scalar boundary; no duplicated arithmetic, framing, null flag, or reconstruction ([Code structure](GUIDELINES.md#code-structure), [Metadata](GUIDELINES.md#metadata), [Framework fit](GUIDELINES.md#framework-fit)). |
| publisher delivery | Worker-owned state, size-zero idempotent attachment to the agent-created ring, one reservation/copy, `ZiRing` timeout/status/kill semantics, and one common control/telemetry path; no publisher creation/resize/reset, PID probing, lock, sleep, or private GC ([Data movement and initialization](GUIDELINES.md#data-movement-and-initialization), [I/O sharding](GUIDELINES.md#io-sharding)). |
| configuration and lifecycle | `ZfStruct`/`ZfCLI`, one source per value, named workload bounds, Z naming, asynchronous owner dispatch, and reverse-order teardown ([Metadata](GUIDELINES.md#metadata), [Naming](GUIDELINES.md#naming), [I/O Function Organization](GUIDELINES.md#io-function-organization), [Teardown](GUIDELINES.md#teardown)). |
| enrollment and identity | `Zhttp`, `ZfJSON`, and `Ztls` directly; one bounded body and one owned in-memory transaction; no URI parser for host/port, policy wrapper, duplicate chain validation, persistence, or credential logging ([Framework fit](GUIDELINES.md#framework-fit), [Strings](GUIDELINES.md#strings), [Heap allocation](GUIDELINES.md#heap-allocation)). |
| hub session | Incremental `ZiIOBuf` parsing, existing Ztls send/backpressure/reconnect timer, and `ZmBackoff`; no contiguous conversion, agent Tx queue, second timer, or TLS wrapper ([I/O and system integration](GUIDELINES.md#io-and-system-integration), [Timers](GUIDELINES.md#timers), [Scope and cleanup](GUIDELINES.md#scope-and-cleanup)). |
| sharding and handoffs | Three explicit owners, variable data moved by buffer reference, fixed metadata by value, producer-owned admission/credit state, and bounded turns; no cross-owner access, repair lock, or diagnostic atomic ([Cross-Shard Handoffs](GUIDELINES.md#cross-shard-handoffs), [Liveness](GUIDELINES.md#liveness), [Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies)). |
| registry and routing | One synchronous blocking unbatched startup `ZiDir` scan, untracked snapshot requests, last-AppTelemetry-wins updates, intrusive named-heap nodes, Z hashes/lists, global nonreused request sequence, `fanoutBatch` publisher-write turns backed by `Req::pending`, and immediate terminal deletion; no app generation, second request allocator, periodic liveness scan, count cap, registry batching, ordinal/cursor state, or GC sweep ([I/O and system integration](GUIDELINES.md#io-and-system-integration), [Containers and intrusion](GUIDELINES.md#containers-and-intrusion), [Storage and capacity](GUIDELINES.md#storage-and-capacity), [Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow), [No dogma](GUIDELINES.md#no-dogma)). |
| operations and tests | Framework-only portable process, no image artifact, TAP tests synchronized by Z primitives, and deterministic residue checks ([Windows](GUIDELINES.md#windows), [Tests](GUIDELINES.md#tests), [Acceptance](GUIDELINES.md#acceptance)). |

## Mandatory implementation audit

Reject the implementation if any item below remains:

- **[Code structure](GUIDELINES.md#code-structure):** parameter-independent declarations nested in templates;
  invariant calls in loop conditions; repeated indexed access; near-identical
  blocks.
- **[Storage and capacity](GUIDELINES.md#storage-and-capacity):** unexplained capacities; fixed arrays/tables; container GC scans;
  ref-counted objects stored by value; disordered members; false sharing between
  shard-owned groups; storage not chosen for its workload.
- **[Heap allocation](GUIDELINES.md#heap-allocation):** buffers or nodes outside `ZiIOBuf`/`ZmHeap`/`ZmVHeap`; missing
  heap IDs; hot-path scratch allocation; separate object/refcount/container-node
  allocation where intrusion combines them.
- **[Data movement and initialization](GUIDELINES.md#data-movement-and-initialization):** logger captures by pointer/reference; more than one local-frame
  copy; temporary contiguous conversion; default initialization immediately
  overwritten; variable data copied into a scheduler capture.
- **[Algorithm and control flow](GUIDELINES.md#algorithm-and-control-flow):** assuming `run`/`invoke` blocks; manual polling; time-based
  concurrent tests; iterator lifetime beyond traversal; snapshotted traversal
  counts; needless aliases/null checks; chained discrete `if`; deep nesting;
  hand-written hashes; unbounded work on an I/O turn.
- **[Framework fit](GUIDELINES.md#framework-fit) and [Type and API friction](GUIDELINES.md#type-and-api-friction):** reimplemented Z facility; `FILE`/`syslog`; printf
  formatting; separate null-state boolean; unnecessary/char/span/CRTP casts;
  enum-to-integer mapping instead of direct enum use.
- **[Scope and cleanup](GUIDELINES.md#scope-and-cleanup) and [Lifetime](GUIDELINES.md#lifetime):** compatibility shim, dead/speculative code, short-lived
  dependent retaining its owner, stale timer/back-pointer, or agent feature not
  required by `goal.md`.
- **[Unnecessary atomic operations, locking and copies](GUIDELINES.md#unnecessary-atomic-operations-locking-and-copies):** lock added for non-owner access, shared mutable routing state, or
  atomic diagnostic state.  Atomics are permitted only where an existing
  framework primitive requires them, not to repair ownership.

This covers every item in [Audit flags](GUIDELINES.md#audit-flags).  Also audit
[Header layout](GUIDELINES.md#header-layout),
[Indentation](GUIDELINES.md#indentation),
[Multiline layout](GUIDELINES.md#multiline-layout),
[Compact layout and alignment](GUIDELINES.md#compact-layout-and-alignment), and
[Class and struct shape](GUIDELINES.md#class-and-struct-shape).  Use hard tabs,
direct includes, repository header prologues, and no concepts/`requires`,
anonymous namespaces, broad lambda captures, `enum class`, or gratuitous STL.

## Restart watermark resolution

Startup discovery sends the existing null-sequence clear-all control before
its `AppTelemetry` snapshot.  The publisher resets `minReqSeqNo` together with
its subscriptions when it processes that control.  This deterministically
allows the nonpersistent agent's `nextSeqNo` to restart at one and also clears
work created by any other request-ring writer.  Do not use a clock, PID, or
random starting sequence: none guarantees monotonicity.

## Completion

The work is complete only when every behavior row above passes and:

- local and remote paths share `Ztc::Hdr`, `Ztc::Ring`, and one `maxFrame`;
- the agent creates the shared telemetry ring at its authoritative configured
  size before discovery, while every publisher only attaches with size zero;
- a local response is copied at most once before in-place sequence rewrite;
- every transport buffer, handoff, frame, timer, and callback is bounded and
  drained deterministically; every terminal app/request/leg is removed
  immediately, while active maps remain proportional to active hub work;
- a stalled reader or hub cannot pin healthy publishers or make transport
  memory grow without bound;
- reconnect cannot associate old local work with a new hub generation;
- agent code never writes secrets or operational credentials to disk, URLs,
  logs, or the hub protocol, and documentation assigns container-layer secret
  hygiene to deployment; and
- identical framework-only agent code builds on Linux and MinGW/Windows.
