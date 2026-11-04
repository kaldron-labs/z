# zfix implementation plan

> Current connection-replacement authority: [zfix_repair2.md](zfix_repair2.md),
> implementation and authorized current verification complete; evidence is in
> [zfix/REPAIR2_AUDIT.md](zfix/REPAIR2_AUDIT.md). Prior repair authority:
> [zfix_repair.md](zfix_repair.md). Its always-app
> resend, body-free archive notification, readiness and naming decisions supersede
> earlier implementation/checkpoint text below. Earlier passing gates describe
> their captured pre-repair source. The user subsequently authorized current-build
> Zfix rebuild/retest without reconfiguration or dependency rebuilds; current
> passing evidence is recorded in [zfix/STATUS.md](zfix/STATUS.md).

Pre-repair status: implementation, queue repairs and Linux x64 delivery gates
were complete for the captured snapshot.
Updated 2026-10-28. The then-current standalone examples, post-Link native/installed
checks and GCC/Clang verification pass. The deterministic codec-input repair
and fresh288-case/18-codec-row baseline pass strict current-hash verification.
The [measured report](zfix/bench/RESULTS.md) and
[raw archive](zfix/bench/results/2026-10-28.tar.xz)
are published and reproduced with checksum/integrity verification. Archive
SHA256: `bb066944cf075af408fed5210c7199182b306e42bf7152adfb23092e9b4a8587`.
[zfix_plan.md](zfix_plan.md), [zfix/STATUS.md](zfix/STATUS.md)
and the user's Zfix-only/Linux-x64 scope corrections govern older execution
instructions below.
The separately requested Zpb missing-field correction is complete and is not
an outstanding task in this plan.

## Scope and reference basis

Implement two independently usable layers:

1. A FIX TagValue format adapter for `ZfStruct`, with FIX field properties,
   facets, typed conversion, nested components and repeating groups.
2. A session and application layer declaring the FIX control messages and
   exposing Zrest-like typed requests/responses for both clients (initiators)
   and servers (acceptors).

Repository references:

- [GUIDELINES.md](docs/GUIDELINES.md): metadata, integer field lookup, allocation,
  sharding, timers, transport lifetime and teardown requirements.
- [ZfStruct.hh](zf/src/ZfStruct.hh): type codes, properties, accessors,
  constructors, defaults, field filters and facets.
- [ZuDateTime.hh](zu/src/ZuDateTime.hh): built-in FIX date/time scanning and
  formatting, including precision defaults and overrides (Q007 resolved).
- [ZfJSON.hh](zf/src/ZfJSON.hh): `AsDeflt`, `AsObject`, `AsArray`, `AsString`,
  ADL `ZfJSON_Fmt`, `Handler`, `ctor`, `alloc`, `new_`, `load`, `update`, `save`.
- [ZfURI.hh](zf/src/ZfURI.hh): facet configuration and nested UDT dispatch.
- [ZfTOML.hh](zf/src/ZfTOML.hh): explicit `AsUnion` handler and deferred
  union resolution, alongside `AsObject` and `AsArray`.
- [ZfTreeLoad.hh](zf/src/ZfTreeLoad.hh): shared metadata-driven construction,
  presence checks, filters and load/update handling used by tree adapters.
- [Zfb.hh](zfb/src/Zfb.hh): `ObjectHandler`, typed wire views, field defaults,
  verification and constructor/load/update paths.
- [Zpb.hh](zpb/src/Zpb.hh): message handlers, constructor state, presence,
  load/update/merge and repeated-field wire semantics.
- [ZfStructTest.cc](zf/test/ZfStructTest.cc): array-formatted UDTs, custom
  mappings, constructors and mutable fields.
- [Zrest README](zrest/README.md), [shared API](zrest/src/Zrest.hh),
  [client](zrest/src/ZrestClient.hh), [server](zrest/src/ZrestServer.hh): typed
  catalogs, `call`, `Reply`, `Completion`, admission, failure and retirement.
- [ZmPQueue.hh](zm/src/ZmPQueue.hh): `ZmPQTx`/`ZmPQRx` sequencing and
  archive/retrieval integration.
- [Ztcp](ztcp/README.md), [Ztls](ztls/src/Ztls.hh) and
  [ZiIOBuf](zi/src/ZiIOBuf.hh): pooled streams and Rx/Tx transport ownership.
- [zhttp utilities](zhttp/util/Makefile.am), [interop](zhttp/interop/README.md)
  and [benchmarks](zhttp/bench/README.md): reusable client/server tools,
  independently selected peers, reproducible runs and measurement reporting.

Use the FIX Trading Community's
[TagValue standard](https://fixtrading.org/packages/fix-tagvalue-encoding/)
and [session standard](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Layer_June_2020.pdf)
as protocol references. Pin the editions and relevant application dictionaries
in the implementation's conformance matrix. Successful message exchange alone is
not sufficient evidence of standard behavior.

## Required implementation facilities

| Facility | Implementation |
| --- | --- |
| Field metadata and access | Use `ZfStruct` metadata, accessors and existing field types. |
| Tag lookup and emission order | Derive scoped numeric tag indexes and emission order from the selected facet. |
| Enum/flags conversion | Reuse framework properties with FIX wire formatting. |
| Strings, objects and groups | Use framework strings, typed objects and array UDT handlers. |
| Required fields and defaults | Apply the metadata/filter and absence rules specified below. |
| Schema customization | Use compile-time handlers, accessors and alternate facets; no runtime dictionary layer. |
| Binary values | Use paired length/data metadata over one byte value. |
| Framing | Use a transport-independent incremental parser and pooled stream integration. |
| Sessions and endpoints | Separate session identity from connections; compose Ztcp/Ztls and ZmEngine lifecycle. |
| Sequencing and replay | Use `ZmPQTx`/`ZmPQRx` and application archive/retrieval hooks. |
| Application notifications | Provide the queue, load, replay, rejection and recovery hooks specified below. |
| Heartbeat correlation | Echo opaque TestReqIDs; track locally issued requests independently of peer ID syntax. |
| Typed dispatch | Support explicit secondary discriminators when MsgType is insufficient. |
| Standalone use | Support codec use without sessions; no separate file transport is required. |
| Operations | Integrate scheduling, limits, failover, traces and counters. |

Do not impose a fixed group-depth limit, MTU-sized message storage or global
maximum-tag tables. Use configured bounds and schema-scoped membership; the
same tag may occur in separate groups. Track required-field presence explicitly.
Keep Rx and Tx state on their owning shards. Replay metadata must retain the
original sending timestamp, and TestReqID must not be constrained to an integer.

## Layer boundaries and proposed files

Use `Zfix` as the module's public prefix and namespace. Proposed lower-level
entry point: `<zlib/ZfixFmt.hh>`, namespace `Zfix::Fmt`; proposed ADL hook:
`Zfix_Fmt(T *)`. The `Zfix` prefix is resolved by Q002; the adapter entry
point and hook are proposed APIs.

Keep codec headers free of networking, session, application and database
dependencies. Build a small `libZfixFmt` against the foundation through `zf`;
build `libZfix` against the codec and `ztcp`/`ztls`. This keeps standalone
conversion usable without linking a session engine. Durability and storage
backends belong to the application; Zfix has no database dependency or built-in
persistent store (Q009 resolved).

Keep standalone framing and header/trailer value types in the codec library.
Its input views and output sinks are transport-independent; the session library
provides Ztcp/Ztls and pooled-buffer integration. References below to pooled
Rx/Tx storage describe that integration, not a networking dependency of the
standalone codec.

| Location | Responsibility |
| --- | --- |
| `zfix/src/ZfixLib.hh`, `.cc` | Library exports/version conventions. |
| `zfix/src/ZfixFmtLib.hh`, `.cc` | Independent codec library exports/version; no dependency on the session library. |
| `zfix/src/ZfixFmt.hh`, `.cc` | Facet/properties, typed handlers, scalar conversion, scoped tag lookup. |
| `zfix/src/ZfixFrame.hh`, `.cc` | Standalone header/trailer types, envelope scanning, BodyLength, checksum and complete-frame writing. |
| `zfix/src/ZfixSession.hh`, `.cc` | Built-in session-message metadata using the codec's header/trailer types. |
| `zfix/src/Zfix.hh`, `.cc` | Catalogs, request/reply/context/failure contracts. |
| `zfix/src/ZfixLinkState.hh`, `.cc` | Session state, sequencing, recovery and timer policy. |
| `zfix/src/ZfixHooks.hh` | Application queue, replay, notification and sequence-recovery hooks. |
| `zfix/src/ZfixClient.hh`, `ZfixServer.hh` | CRTP endpoints and transport composition. |
| `zfix/test/` | Isolated TAP codec, schema, state-machine and API tests. |
| `zfix/util/`, `example/` | High-rate `zfix`/`zfixd` order/execution tools and reusable application examples. |
| `zfix/interop/` | QuickFIX/C++ `quickfix`/`quickfixd` fixture peers and four-pairing correctness runner. |
| `zfix/itest/`, `interop/`, `bench/` | End-to-end, external-peer and performance verification. |

Factor common non-template code into `.cc`; do not duplicate parsers for the
two endpoints. Revisit file splits as concrete dependency boundaries emerge.

## Lower layer: metadata and conversion

### Facets and field properties

Declare `ZuStructFacet(FIX)` and properties in `ZuFieldProp::FIX`.
`FIX::ID<N>` identifies a positive numeric wire tag. A field declaration follows
the requested shape, with the actual C++ type included in the facet tuple:

```cpp
struct Foo {
  ZtString foo;
};
ZfStruct(, (Foo, FIX),
  (foo, (FIX::ID<4200>), String));
ZfStructImpl(Foo);
ZfStructImpl(Foo, FIX);
```

This is intended syntax for the new adapter. `ZfStruct` itself already expects
`(Type, Facets...)`; `(FIX)` alone is not a named structure declaration.
Use `ZfStructRender`/alternate facets for venue-specific tag IDs, ordering and
field selection without changing the application object layout.

All format/schema customization is compile-time (Q004 resolved). Do not implement
runtime tag remapping, field suppression, default/enum overrides, mutable schema
registries or a runtime dialect dictionary. Ordinary session configuration such
as identities, declared version, transport mode and limits remains configurable;
it does not alter the field metadata or conversion rules.

Follow ZfURI/Zfb/Zpb's facet-configuration pattern: default configuration,
NTP overrides, an ADL `Zfix_Config(Facet *)` and `Config<Facet>` lookup. Keep
`Zfix_Fmt(T *)` for type-handler selection and a separate `Zfix_StringFmt(T *)`
for custom scalar conversion, matching ZfJSON/ZfURI's separation. These are
proposed Zfix names, not new runtime registries. Reuse `ZuFieldProp` property
detectors and `ZuActiveFields`/standard filters rather than a second metadata
model.

Reuse `Ctor`, `Mutable`, `Required`, `Deflt`, `Reset`, range and enum/flags
properties. Add only format-specific properties, initially:

- `FIX::ID<N>`: ordinary tag, or NumInGroup tag for an array UDT.
- `FIX::Len<N>`: length tag associated with the field's data tag.
- FIX number/time formatting where generic properties do not express the wire
  representation. Emission follows existing filters and Q005; no additional
  FIX-specific missing/empty-value policy.

Schema checks require IDs for wire-bearing scalar fields and array count fields,
but not for flattened object components, whose child fields supply the IDs.
Reject duplicate IDs within
one effective scope, invalid group delimiters, collisions introduced by
flattened components, invalid length/data pairs and conflicting reserved
envelope tags. Duplicates across distinct group scopes remain valid.
Derive group entry boundaries using the following ordered-group mechanism rather than
requiring a separate delimiter property: on receive, the first recognized field
in the group establishes its delimiter; recurrence begins the next entry. On
send, the selected field order supplies the first field of each entry. Ensure
that the emitted first field is present consistently in every entry.

### UDTs and repeating groups

Implement structured fields and repeating groups using the established
format-handler patterns and `ZfFieldTC` codes (Q003 resolved). Scalar/vector
codes select their existing value conversions; `UDT` delegates through ADL
`Zfix_Fmt(T *)` to the appropriate handler:

- `AsObject` is the default structured-object handler. Its active fields supply
  the wire sequence; nested object components use those fields directly without
  a new component marker.
- `AsArray<ElemCode, ElemProps>` describes array contents using the established
  element type codes/properties. For repeating groups of structures, use
  `AsArray<ZfFieldTC::UDT>` and a count-tag ID on the containing field.
- `AsUnion` handles alternative typed values through the same handler dispatch.
  Retain a bounded parse view for deferred resolution and save the resolved
  alternative using its handler/type code. The enclosing decode context retains
  the backing storage for views; do not invent a wire union wrapper or guess an alternative by trying
  every schema. Explicit application/catalog discrimination resolves ambiguous
  alternatives. Reject saving an unresolved alternative unless an explicit
  handler supplies a valid wire representation; JSON's raw-node passthrough
  does not establish a FIX representation or its group boundaries.

Keep the established `AsString` fallback/custom scalar formatting for types
without structured fields. Support the existing pointer-to-object pattern with
an explicit ownership/allocation contract. No parallel POD system, special
component classification property or mandatory `FIX::Delim` property is needed.

Illustrative proposed declarations:

```cpp
struct Party {
  ZtString id;
  ZtString src;
  int role;
};
ZfStruct(, (Party, FIX),
  (id,   (FIX::ID<448>, Required), String),
  (src,  (FIX::ID<447>, Required), String),
  (role, (FIX::ID<452>, Required), Int32));

struct Parties : public ZtArray<Party> {
  ZuDerive_(Parties, ZtArray<Party>);
  friend Zfix::Fmt::AsArray<ZfFieldTC::UDT> Zfix_Fmt(Parties *);
};
struct Body {
  Parties parties;
};
ZfStruct(, (Body, FIX),
  (parties, (FIX::ID<453>), UDT));
```

Tag 447 is represented as a string here; a field property/custom scalar handler
or application validation enforces its single-character value. Session modes
do not supply business-field validation or select business dictionaries.
Add the usual `ZfStructImpl` definitions in the actual fixture. The example
container must also receive the module's identified heap parameters before
becoming production code.

`453=2<SOH>` precedes two entries, each beginning with tag 448. Container length
is the authoritative count on output; no separately maintained count member.
Nested groups recurse through the same mechanism. A plain object UDT is a
flattened component, not an invented FIX subdocument. Primitive vectors need
explicit MultipleValue formatting or a declared group-entry wrapper; do not
treat repeated scalar tags as a general-purpose FIX array encoding.

Parse groups using count, delimiter, current scoped schema and parent scope.
Require exactly the advertised number of entries, including nested groups;
handle zero counts as zero-length vectors without entering an entry. Loading
an explicit zero count replaces existing vector contents with an empty vector;
saving a zero-length group emits its count tag with value zero. Do not omit an
empty vector merely because the group is optional. Detect missing delimiters,
premature group exit, excess entries and duplicate recognized fields within
one entry. Keep absent group and explicit zero count distinguishable for
required-presence checks and diagnostics. Both may produce an empty vector
under Q005's default-assignment rules; absence does not preserve a selected
destination vector. Depth and count limits have
framework-style defaults configurable at the hub level (Q015 resolved), with
schema depth providing a useful bound.

Apply Q014's rules: malformed/corrupt data is
an error; unknown tags cannot be semantically validated and are skipped. Do not
reject a tag merely because it is absent from the selected schema, and do not
infer types, group membership or value constraints for unknown tags. Continue
parsing recognized fields normally. Wire framing and safe field-boundary
handling still apply, including known length/data pairs; skipping unknown tags
does not make corrupt framing valid. Out-of-order tags are errors when their
ordering violates FIX protocol requirements, not merely because they differ
from the preferred output order.

### Parsing and construction

Use bounded spans into the input and compact offsets/lengths for fields and
group entries. No JSON DOM, no allocation per tag and no temporary conversion
of every value to a null-terminated string. Retain a compact parse view where
constructor ordering requires indexed access; ordinary mutable-field loading
may use the same view without constructing a second representation.

For integer tag dispatch follow GUIDELINES exactly: populate a
`ZmLHash<..., ZmLHashStatic<...>>` once from compile-time metadata, then map tags
to field indexes and record positions in a field-indexed lookup. Scope the
index by schema/facet/group; never construct it per message. Size lookup and
presence storage from schema metadata, using framework scratch storage when
needed. Avoid scans comparing a received tag against every field.

Expose the familiar `Handler<O, Facet>` operations: validity checking, `ctor`,
placement `new_`, `alloc`, `load`, `update`, and static `save<Filter>`; facade
functions select `Facet` as in the other adapters. Existing `alloc` precedents
use the object's `new`/identified heap, not an invented allocator argument.
Keep checked parse results
and field errors available to both standalone callers and the session layer.
Support constructor arguments in `Ctor` order, setters/accessors and existing
Load/Save/Init/Upd filters. Do not assume that JSON's implementation already
enforces every FIX requiredness or scalar-format rule.

Q005 resolves empty values and missing tags as follows:

- `tag=<SOH>` supplies the value `""`. Pass that zero-length span through the
  same field-type conversion as any other value. Do not reinterpret it as a
  missing tag, null, default request or special clearing instruction. Whether
  conversion succeeds is determined by the field's ordinary value parser.
- Zero-length arrays/vectors are values as-is, including repeating groups.
  Preserve them through loading and saving; do not suppress optional empty groups.
- A missing tag is distinct from a present value. Handle it consistently with
  established adapter semantics, using `Zfb` and `Zpb` as read-only precedents.
  Changes to Zfb or Zpb are outside this plan's scope.

Use Zfb's metadata-driven missing-field behavior as the reference. Supply
`Ctor` fields in constructor order. For `Init`, `Load` and `Upd`, assign every
selected writable field, using its decoded value when present and its
metadata/type default when absent. Missing selected fields are not preserved
merely because they lack `Reset` or `Deflt`. Use `F::deflt()` and the established
type-code fallback rather than a separate FIX default table. `Upd` selects
primary keys and `Mutable` fields; non-selected and read-only fields remain
untouched. Required-presence validation uses the operation's selected fields
and format rules, separately from assignment.

Zfb is authoritative for missing-field assignment. The separately authorized
Zpb correction is complete; no further adapter alignment is required by this
plan. ZfJSON/ZfTreeLoad remain handler
and construction precedents, but their conditional missing-field assignment
must not override this Zfb-aligned contract. Account for
format-specific behavior: protobuf repeated-field merge/append semantics and
FlatBuffers scalar-default elision are not FIX group or presence rules.
Compare presence-sensitive cases only where the wire format can represent
them; an absent protobuf repeated field cannot generally distinguish an empty
vector from omission. Do not change the wire format to manufacture presence.
Keep invalid-value
conversion distinct from absence/default handling. Reject recognized duplicates
in one scope; duplicate unknown-field policing is unnecessary.

Rollback on failure is never required (Q006 resolved). Parse, construct, load
and update directly; failure may leave the destination partially modified.
Do not add transactional staging, object snapshots, undo paths or a validation
pass solely to preserve the prior value. Report failure and do not dispatch a
failed decode as a successful application message. Normal lifetime/resource
cleanup remains required; tests must not require restoration of prior values.

### Scalar formats and binary data

Reuse `ZuBox`, `ZuFixed`, `ZuDecimal` and enum/flags metadata. Use the built-in
FIX format in `ZuDateTime.hh` for date/time conversion: `ZuDateTimeScan::FIX`
for scanning and `ZuDateTimeFmt::FIXDeflt` for default output. FIX-specific field
properties can override precision and other formatting defaults through the
existing `ZuDateTimeFmt::FIX<NDP, Null>` facilities. Do not implement a separate
FIX timestamp parser/formatter or duplicate its precision rules (Q007 resolved).
Provide strict bounded consumption of the field span;
reject overflow/trailing garbage. Prefer fixed/decimal fields for prices and
quantities. Implement FIX `Y`/`N`, single-character values, enum wire tokens,
space-delimited MultipleValue fields as format policies. Round-trip negative
values, zero, null and built-in date/time defaults and field-level precision
overrides.

Represent data as one byte span/container with `ID<dataTag>, Len<lenTag>`.
Derive length on output and consume exactly that many octets on input,
including embedded SOH, NUL and `=`. Validate the paired tag and following
delimiter. The header scan and discriminator scan must use the same
length-aware scanner; an early scan must not mistake embedded data for tags.
Keep pair metadata available even when the application does not load that
field. A genuinely unknown length/data pair cannot be inferred from arbitrary
bytes: only known pair metadata permits skipping embedded delimiters. Do not
promise arbitrary unknown binary-field recovery; retain Q014's framing errors
when a safe boundary cannot be established. Encoded text stays byte-preserving by default; MessageEncoding is
independent of the built-in date/time format.

### Envelope, streaming and output

Provide separate body conversion and complete-message framing APIs. In managed
session modes the session supplies standard header values; application bodies
cannot overwrite sequence numbers, identities, lengths or checksums through
accidental tags. Standalone callers and raw-mode session managers supply their
own typed header; the framer still calculates BodyLength and CheckSum.

The frame parser returns incomplete, complete with consumed length, or error.
Recognize `8`, `9`, `35` in their required leading positions and `10` at the
computed trailer boundary. Count BodyLength from immediately after tag 9's
delimiter through the delimiter before tag 10; checksum covers original bytes
before tag 10 modulo 256, printed as three decimal digits. Preserve the wire
bytes until checksum verification. Do not search for `10=` inside data.

Handle arbitrary fragmentation and multiple messages per read. Bound header
digits, frame size, arithmetic, retained bytes and incomplete input before
growth. A length failure must not trigger unbounded resynchronization scans;
the session decides whether to disconnect. Use Ztcp receive-stream facilities
and retain pooled backing for borrowed values. If a fragmented field requires
assembly, copy only the needed retained data once and measure the cost.

Write in selected facet order with explicit header/trailer placement and group
ordering. Reserve envelope headroom, serialize directly into pooled Tx storage,
then finish length/checksum without materializing a second whole-message string.
Hold the frame's pooled buffers until encoding and admission succeed, then
publish them in order. Do not encode an unvalidated frame directly into a live
Ztcp Tx stream: its buffer submission and destructor flush can publish bytes
before encoding finishes. This buffer ownership boundary is not object rollback.
Prototype exact-size measurement versus headroom/backpatching; choose using
latency and copy measurements, including digit-width changes. No incomplete
message may reach the network on encode/admission failure. Preserve sticky
stream failure and check explicit flush. The standalone sink API must also
surface output-limit/format failures.

## Higher layer: messages, sessions and endpoints

### Control message catalog

Implement the built-in session-level messages specified by Q008. Declare
the following seven message types with the same codec used by applications,
including header/trailer metadata. Use the field set below as the scope;
session behavior follows the independently configured transport mode (Q001).

| MsgType | Structure | Principal fields |
| --- | --- | --- |
| `A` | Logon | 98 EncryptMethod, 108 HeartBtInt, 141 ResetSeqNumFlag, 383 MaxMessageSize, 789 NextExpectedMsgSeqNum, 1137 DefaultApplVerID. |
| `0` | Heartbeat | Optional opaque 112 TestReqID. |
| `1` | TestRequest | Required opaque 112 TestReqID, echoed unchanged. |
| `2` | ResendRequest | 7 BeginSeqNo, 16 EndSeqNo. |
| `3` | Reject | 45 RefSeqNum, 371 RefTagID, 373 SessionRejectReason, 58 Text. |
| `4` | SequenceReset | 36 NewSeqNo, 123 GapFillFlag. |
| `5` | Logout | 58 Text. |

Inbound and outbound Heartbeat/TestRequest use these same wire types. Preserve
opaque TestReqID handling as described above. BusinessMessageReject (`j`),
user-management, network-status and other application/control extensions are
not required built-ins. Applications can declare them through the ordinary
typed catalog when needed; do not generate a complete business-message catalog.

### Declared version and session transport (Q001 resolved)

Configure the declared FIX version independently of the
session transport. The declared version populates transmitted header version
values and matches received header version values. It does not select session
behavior, gate available tags, or automatically select a different application
schema/catalog. Application facets and catalogs remain explicitly selected.
Use FIXT BeginString and application-version fields where applicable; do not
introduce a staged version-support roadmap.

The session transport selects behavior:

| Transport | Session behavior |
| --- | --- |
| `raw` | External session management; bypass the FIX session machinery while retaining FIX message conversion/framing. |
| `4` | FIX 4 session behavior: Logon negotiation, sequence recovery, resend/gap fill, heartbeat and logout as specified below. |
| `4.4A` | Alternate FIX 4.4 session behavior using Logon NextExpectedMsgSeqNum synchronization. |
| `T1.1` | FIXT 1.1 session behavior, including application-version declarations. |

Here “transport” means this FIX session mode. TCP versus TLS is an independent
byte-transport selection. Derive the detailed mode-specific transition rules
from the FIX session specification and the contracts below, and test them
explicitly. References to session profiles below mean these modes, not a
behavior switch on the declared FIX version.

### Session state and recovery

A session outlives individual TCP connections. Its sole attachment and durable
key is the opaque, application-configured `SessionConfig.id`. The application
chooses its contents, including any qualifiers it needs; the library does not
construct a key from wire headers or impose a routing policy. `HeaderConfig`
holds the separately configured version, local/peer CompIDs and optional
sub/location IDs. Mode and application version remain separate configuration.
Validate incoming headers, including Sender/Target orientation, against the
current connection's configuration. The acceptor resolves the first Logon and
invokes the application's admission/authentication callback before attaching
the connection. FIX-layer authentication is optional and
application-defined; no credentials or authentication scheme are mandatory.
The application selects the persistent logical link. A newly bound connection
immediately displaces its predecessor using abrupt `ZiMultiplex::close`; do not
drain the predecessor or reject the successor because the link is active.
Preserve queued logical work and sequence assignments for the successor.
Authentication and normal readiness remain separate from association.

Model Disconnected, Accepting, Logon, Established, Logout and Closing
explicitly. Recovery of inbound gaps and outbound replay are independent flags
or substates: they can overlap. Publish a transition table with event, guard,
sequence effects, timer changes, emitted message and callback before coding.

Required behaviors:

These requirements apply to managed session modes (`4`, `4.4A`, `T1.1`).
In `raw`, the external session manager supplies header/sequence values and
owns admission, control traffic, recovery and reset; Zfix still provides the
codec and byte transport without running these session procedures.

- Application Logon admission/authentication callbacks, declared-version header
  matching, heartbeat negotiation, reset
  negotiation and logon timeout. Admit new application traffic to the wire only
  when ready; the unsequenced application queue may retain submitted work while
  disconnected or recovering, subject to application admission and hub limits.
- Independent inbound expected sequence and outbound next sequence, original
  send high-water mark, replay cursor and session epoch. Replay must not rewind
  the counter used for new messages.
- Allocate outbound wire sequence numbers on Tx when selecting new messages
  into transmission order, never when the application submits them. Session
  control messages consume the same sequence space and can precede queued
  application messages. Replay retains the original sequence number;
  gap-fill/reset control headers follow their specific protocol rules rather
  than blindly consuming the next number as an ordinary new message would.
- High inbound sequence: bounded read-ahead and ResendRequest. Drain contiguous
  messages after recovery; prevent duplicate application delivery within the
  session's defined processing boundary. Handle overlapping/repeated gaps.
- Low inbound sequence: profile-correct PossDup/OrigSendingTime handling;
  distinguish ordinary duplicate replay from invalid sequence regression.
- SequenceReset gap fill versus reset mode; never let a stale gap fill move
  expected sequence backwards. Handle overlaps and open-ended resend ranges.
- Request replay content from the app for each attempt, preserving its requested sequence,
  OrigSendingTime, PossDup, new SendingTime and recomputed BodyLength/checksum. Coalesce allowed
  skipped administrative/non-replayable ranges into gap fills.
- Resolve unavailable replay records through application retrieval hooks and
  the specified gap-fill behavior; history availability is application policy.
- Heartbeat on send-idle, TestRequest on receive-idle, exact-ID response matching
  and timeout. Use opaque IDs and separate liveness from business acknowledgement.
- Logout handshake, bounded close timeout, reconnect/failover, controlled reset
  and epoch rollover. No silent sequence wrap; live reset requires the agreed
  counterparty procedure described under Q017 below.
- Error classification: framing/transport failure, session reject/logout,
  application reject, local decode error and unknown-message policy.

Produce profile-specific decisions for invalid messages' sequence advancement,
admin messages during gap recovery, timestamp tolerance and application-reported
Logon refusal. These must be fixture-backed and centralized in the transition table. Apply Q014's rules: skip unknown tags, report malformed
or corrupt data, and reject protocol-invalid ordering. Concentrate validation
at the wire boundary; do not add venue-specific tolerance switches for corrupt
data or impose output order on fields that the protocol allows in another order.

### Session reset and rollover policy (Q017 reviewed)

Public exchange specifications reviewed on 2026-10-26 show several established
practices, not one universal reset cadence. The following describes the named
interfaces only; it is not a claim about every service at an exchange.

| Exchange / interface | Published session behavior | Consequence for Zfix |
| --- | --- | --- |
| [Cboe US Equities FIX](https://www.cboe.com/document/tech-spec/document/technical-specifications/cboe-titanium-u.s.-equities-fix-specification) | Both sequence numbers reset to 1 during nightly downtime. Intraday broken connections resume the session; Logon `141=Y` is an emergency facility, not the normal daily procedure. | Separate reconnect from a scheduled new session. |
| [NYSE Bonds CGW, §5.1–5.3](https://www.nyse.com/publicdocs/nyse/NYSE_Bonds_FIX_Gateway_Specification.pdf) | Both directions reset at midnight Eastern Time; clients must log out beforehand. Continuous 24-hour connections using ResetSeqNumFlag are unsupported. Intraday recovery uses resend requests. | Permit scheduled logout/reset/reconnect without requiring an in-band reset handshake. |
| [MOEX derivatives FIX, §3.2.4–3.2.5](https://ftp.moex.com/pub/FIX/Spectra/test/docs/spectra_fixgate_en.pdf) | The published 2025-11-20 specification permits intraday Logon reset: first check continuity with TestRequest/Heartbeat, then exchange Logon `141=Y`, sequence 1; subsequent messages start at 2. Normal reconnect can preserve sequence state and recover gaps. | Retain coordinated live-reset capability; do not prohibit it globally. |
| [Binance Spot FIX](https://developers.binance.com/en/docs/products/spot/fix-api) | Logon requires `141=Y`, is allowed only once per connection, and ResendRequest is unsupported. Maintenance notifications request a replacement connection. | Support reset-on-connect integrations without assuming replay availability or live Logon reset. |
| [Coinbase Exchange FIX 5.0 order entry](https://docs.cdp.coinbase.com/exchange/fix-api/order-entry-messages/order-entry-messages5) | Logon uses sequence 1 in both directions. Customer outbound numbering resets on disconnect; server outbound recovery can be requested with `141=N`, followed by SequenceReset and paged resend recovery. Replay excludes messages older than one hour. Maximum MsgSeqNum is 2147483647; customers must reset before exceeding it. | Recovery may be asymmetric; expose directional state and application hooks rather than treating every reconnect as a symmetric reset. |
| [Gemini FIX](https://developer.gemini.com/trading/fix/overview/session-level-messages/when-can-sequence-numbers-reset) | Server Logon resets require an explicit client request through `141=Y`. Gemini recommends considering client resets on logon, logout, disconnect and error. | Reset triggers are counterparty/application policy, not universal engine rules. |

Implementation policy from this review and the user's Q017 clarification:

- By default, reconnecting with the same application session ID and without a reset flag
  resumes the existing session and sequence numbers. Perform resend/recovery
  in both directions as needed before resuming regular application operation;
  reconnect alone does not start a new sequence epoch. Do not automatically
  reset on disconnect, local midnight or a calendar
  interval. Applications select the counterparty-agreed reset policy; the
  canonical FIX 4.4/TCP+TLS interop default remains Q016.
- Provide hooks and operations for scheduled new sessions, reset
  Logon on reconnect, and coordinated live reset where the selected session
  mode and counterparty support it. The standard reset path is to drain both
  directions, then disconnect and reconnect with a reset. Also support the
  seldom-used protocol procedure that drains both directions and coordinates
  a sequence reset while remaining connected. Establish a drain boundary:
  finish already admitted old-epoch traffic and its recovery in both directions,
  while holding or refusing subsequent application submissions. Continue the
  control traffic needed to drain and coordinate reset, then resume regular
  operation. Unsequenced queued work may be retained for the new epoch by
  application policy; it has no old wire sequence to replay. Merely
  emptying the local Tx queue does not establish that both directions have
  drained. Keep gap fill, SequenceReset reset mode and
  starting a new sequence epoch distinct. Do not reinterpret every sequence-1
  Logon as permission to discard the existing state.
- The application owns daily/weekly schedules, trading calendars, timezones,
  DST handling, maintenance notices and operator-triggered recovery. It invokes
  engine operations through the existing session/reset hooks. No built-in
  exchange calendar or runtime dictionary customization is needed. Venue
  deviations use compile-time handlers/facets and application callbacks;
  declared BeginString alone must not select such behavior (Q001/Q004).
- Coordinate Tx/Rx queue state and epoch changes, including pending replay and
  delayed callbacks, so records from an old epoch cannot be replayed as new
  messages. Expose directional recovery state through Q009's hooks; persistence,
  archive retention and recovery of business state remain application concerns.
- Check sequence arithmetic against the supported/configured peer limit before
  allocating a new sequence number. Provide an approaching-limit notification
  with sufficient configurable headroom for reset/logout control traffic; stop
  new application sends before exhaustion. Never silently wrap. The application
  chooses a supported reset or reconnect procedure; a peer-specific limit such
  as Coinbase's is not a universal FIX limit. Apply Q015's hub-level defaults
  and configuration pattern.

These are engine capabilities and integration examples, not a requirement to
ship complete exchange adapters or certify all listed venues in the first
release. Recheck the selected venue specification when implementing an adapter.

### Authentication and byte transport (Q012 resolved)

FIX-layer authentication is an application concern. Provide callbacks to prepare
outbound Logon fields and inspect/admit/refuse inbound Logon using the relevant
session, message and connection context. Applications may declare additional
authentication fields through compile-time metadata. Zfix does not prescribe
passwords, credential storage, identity verification or an authentication scheme.
An application may use no FIX-layer authentication, including deployments on
private networks. Protocol header matching/session routing remains separate from
the application's authentication decision.

Delegate TCP connection/listener setup, including ports, to `Ztcp`. Delegate
TLS setup, certificates, peer verification and mTLS to `Ztls`. Expose/combine their
existing configuration and callbacks without duplicating their settings or
implementing a FIX-specific certificate/transport policy. Application callbacks
can use the relevant transport context for their own admission decisions.

### Application durability and recovery hooks (Q009 resolved)

Durability is an application concern. Implement the recovery capabilities and
hooks below using current Z framework types; do not introduce a
Zfix-owned journal, database backend, durable/transient storage modes or a
mandatory commit-before-send policy. The application chooses storage, commit
boundaries, retention and business-processing guarantees.

Provide the following application hooks:

| Hook | Required responsibility |
| --- | --- |
| `enqueue` / `dequeue` | Hand pending, unsequenced application messages to the application queue and obtain the next candidate to send. Submission does not allocate or reserve a wire sequence number. |
| `load` | Call the application with the specific business-message identity/context and its allocated outbound sequence number when Tx makes the association in transmission order. Implement the acceptance/refusal behavior specified below and session-message notifications without application payload. |
| `reload` | Obtain a message by sequence number for sending/replay, including acknowledged-state input and message-type/payload outputs. |
| `unload` | Notify the application when unwinding loaded but unsent messages. |
| `sent` | Report local send completion with message context. |
| `ackd` | Report sequence progress through acknowledgement notifications; retention decisions remain with the application. |
| `reject` | Notify the application of rejection with sequence/payload context. |
| `stateChange` | Report ordered session state changes with state-change sequence, state and flags. |
| `reset` | Notify the application of sequence reset with acknowledged/loaded progress. |
| `session` | Observe incoming/outgoing session messages. |

Provide owner-shard restoration operations for received/processed progress,
loaded/sent/acknowledged outbound progress, recovery ranges and pending resend
state. Accept a selective session-state record that distinguishes omitted
members from supplied values. Restore a consistent session/epoch before
resuming dependent activity; never interpret omitted members as zero. Provide
owner-shard snapshots/accessors so applications can persist and recover that
state without accessing Zfix internals.

Use typed metadata/payload handles and current `ZmFn` or
CRTP callback conventions. Define hook ordering, argument lifetimes, failure
behavior and owning shard from the contracts in this plan. Application
storage must not block an I/O shard; permit application-prepared data/queues and
continuations where needed while preserving the hook's role and ordering. State
restoration runs on the owning shards before dependent session activity resumes.

Expose replay metadata needed to preserve original message content, sequence and
SendingTime. Applications may recover this from their own storage. FIX session
sequencing does not impose a transaction model on application side effects.
Local send completion, session progress and business completion remain distinct
notifications.

### Sequence queues and application replay history (Q010 resolved)

Use `ZmPQTx` and `ZmPQRx` from `ZmPQueue.hh` for the underlying transmit and
receive sequencing machinery. Map the specified session operations and recovery hooks
onto these facilities instead of implementing parallel sequence queues,
read-ahead trees, send cursors or archive schedulers. The bounded FIX replay driver always fetches
from the app rather than using generic resend's resident-node shortcut.

Keep application submission/queue identity distinct from FIX `MsgSeqNum`.
Pending application messages remain unsequenced until Tx selects them for
transmission, accounting for intervening session control traffic. Allocate new
application and control messages from the same outbound sequence space in their
actual transmission order; do not preassign numbers to an application backlog.
Any submission handle identifies queued work, not a promised wire sequence.
The `load` callback must inform the application of the allocated
sequence number and the specific business message to which it belongs; this
notification is required, not optional diagnostic metadata. Deliver it before
the message can produce a peer response, so the application can establish its
sequence-to-business-message mapping before a subsequent Reject referencing
that sequence arrives. Include session/epoch context to disambiguate sequence
numbers reused after reset. Preserve that reference context in reject callbacks
so the application can apply the rejection at the business level as well.
The application owns the mapping and business response policy; Zfix must retain
the information needed to make that correlation without imposing a business registry.
Define this ordering across shards: the Tx-side hook runs before publication.
If the application keeps its correlation state on Rx or another owner, complete
the association there through a continuation before Tx publishes the frame.
Do not block Tx or let the hook directly mutate another shard's state. Route
subsequent reject notifications to that same application owner in order.
A refused `load` must not advance committed
sequence progress or leave an artificial wire gap. Distinguish the proposed
association from successful load, and document unload/error notifications for
accepted messages that cannot subsequently be transmitted.
Once a message is sequenced, follow the load/unload/replay lifecycle specified here;
partial writes and retransmission must not allocate a fresh number for it.
Pending asynchronous assignment callbacks must not permit later new messages
to overtake an already committed sequence. Perform fallible body conversion
before committing the wire association where possible; after successful load,
retain or unload an unsent message through the defined hooks instead of silently
dropping its sequence. A transport failure after publication uses ordinary
session recovery; complete-frame buffering is not a guarantee of atomic TCP
delivery or permission to renumber a partially transmitted message.

Keep the entire `ZmPQTx` side Tx-owned and the entire `ZmPQRx` side Rx-owned.
Connect new-send/gap and receive/gap-request callbacks to the FIX
session protocol; schedule their work on the owning shard. Implement the
acknowledgement, reload and gap-fill behavior specified here.

FIX has no universal per-message acknowledgement. Keep queue progress and
archive eligibility separate from socket send completion and business
acceptance. Provide acknowledgement hooks and locally generated
TestRequest watermark tracking where applicable, while echoing arbitrary peer
TestReqIDs unchanged; an arbitrary received numeric-looking ID is not evidence
of acknowledgement. Map the queue's new-send and gap callbacks, plus app-sourced replay, to FIX
semantics explicitly; generic `ZmPQTx` retransmission does not define the wire
sequence-number rules by itself.

The application supplies every resend attempt through
`fixRetrieveTx_(TxContext, acknowledged, ticket)`, returning its chosen content,
no body for gap-fill, or Pending. Content may be suppressed, repriced or replaced
with another type/application ID; Zfix does not enforce original-payload equality.
No core replay cache or retained-history source is permitted. Operational nodes
retire on contiguous publication; admitted output remains alive only to complete
the physical operation. Held unpublished abort preserves its assigned sequence.

Generic archive completion informs the app through
`fixArchiveTx_(epoch, begin, end)`. The app moves its own records to long-term
storage or deletes them; subsequent replay still requests the app. No `archived()`
core-payload-release handshake or mandatory persistence barrier remains. Checkpoint
restore carries sequence metadata, not saved physical queue occupancy.
`Checkpoint.id` owns only the application ID; matching requires that ID and the
epoch, not saved wire headers, mode or version. An application may retain its
ID while changing connection headers without implicitly resetting sequence
state. The new connection still validates its current configured headers.


### Zrest-like application API

Reuse the shape of Zrest, not HTTP mechanics. Proposed descriptors have an
`Object`, `Facet`, `MsgType` and declared response types. Business correlation
and workflow policy belong to the application (Q011 resolved).
Explicitly selected catalogs/facets select codecs by message type and optional
secondary discriminator. The declared FIX version only supplies/matches header
values; it does not implicitly switch codecs. Compile-time checks reject
ambiguous registrations.

Proposed interactions (API sketches, not currently compilable declarations):

```cpp
client.call<Req>(session, requestObject, call);
// call.process(Zfix::Reply<Res>) or call.failed(Zfix::Failure)

app.request(Req *, const Req &, const Zfix::RequestContext &,
  ZmRef<Zfix::Completion<Req, Endpoint>>);
token->complete(Zfix::Reply<Res>{...});

endpoint.send<Msg>(session, object); // unsolicited/one-way message
app.receive(Msg *, const Msg &, const Zfix::RequestContext &);
```

Both client and server can send, receive and initiate application calls. Their
distinction concerns connection establishment, not business-message direction.
Use shared endpoint machinery with thin client/server wrappers.

FIX has no universal request identifier and no HTTP stream that associates a
reply automatically. Correlation keys/indexes, response cardinality, terminal
predicates, subscriptions, application timeouts, cancellation and reconnect
behavior are application concerns. Zfix must not impose an order lifecycle,
a mandatory pending-call registry, a single-terminal-response rule, or automatic
failure/resumption of business calls when a connection changes.

The `call` and completion-token sketches above describe optional typed API
conveniences driven by application-owned correlation/completion hooks. They are
not a built-in business request manager. Applications can use typed send/receive
and reply operations directly, or implement their own call tracking. Preserve
clear local admission/error results and payload/token lifetimes; local send
completion is not a business reply. Report protocol/session failures and typed
rejects to the application with their reference metadata.

Expose session epoch, sequence, duplicate/replay metadata and declared version
in `RequestContext`. The application decides how those events affect its pending
work, whether a response is terminal and whether to retain correlation across
reconnect/restart. Session replay follows the recovery rules specified here; it
must not create a new business request or invent application completion rules.

### Ownership, bounds and lifecycle

| Owner | State |
| --- | --- |
| Rx | Framing/parse views, `ZmPQRx` sequence/read-ahead state, session state, receive/liveness timers, typed dispatch/token state and routing. |
| Tx | `ZmPQTx` send/replay/archive state, outbound sequence allocation, encode buffers, Tx hook ordering and send-idle/throttle timers. |
| Application storage, if any | Application-owned persistence/recovery; communicates through hooks and owner-shard continuations. |

Transmit fixed snapshots of Rx-owned session data with Tx control commands.
Application submission carries business data and identity, not a frozen wire
header. Tx uses its current owner-shard connection/epoch snapshot, allocates
MsgSeqNum and sets SendingTime at transmission; it must not reuse stale values captured
before recovery or reset. Return
Tx high-water/progress snapshots to Rx; never read another shard's live fields.
Cross-shard variable data travels in retained pooled buffers or destination-owned
queues, with handles and fixed metadata in captures. Public entry points use
`rxInvoke`/`txInvoke` and trailing-underscore owner-shard implementations.
Do not introduce locks to permit non-owner access.

As in ZfJSON/Zfb/Zpb, standalone borrowed spans/views do not acquire ownership
of their backing input: the caller keeps input and parse storage alive through
use and deferred union resolution. In the session layer, retain that backing
once in the enclosing receive context/token, rather than adding reference
counts to each field or group. Owning objects copy only data they retain. Off-shard sends retain an
owning object/serialized buffer until Tx consumption; borrowing a caller's stack
object across an asynchronous post is invalid. Expose lifetime choices as
explicit descriptor/API contracts, following Zrest's `Borrowed` precedent.

Provide named default limits, following other Z framework adapters and protocol
implementations, with configuration at the hub level (Q015 resolved). The hub
passes its effective limits to the codec and session/endpoint machinery; do not
require applications to configure each link, message or field separately.
Standalone codec use receives the same defaults and can accept an explicit
limits configuration without requiring a networking hub. Keep these operational
limits separate from compile-time field/schema customization (Q004).

Bound frames, incomplete receive storage, group entries/depth, read-ahead count
and bytes, admitted typed operations, server work, queued output and replay work.
Application-owned call registries and history retention are bounded by the application.
Account for posted ingress before it reaches the owner queue. Use a bounded
work batch for replay/draining so one session cannot starve others. Define
backpressure/refusal at each boundary, and reserve configurable capacity for
administrative progress without violating wire sequencing.
Include these bounds and work-batch settings in hub-level configuration. Retain
existing transport buffer-pool tuning through Ztcp/Ztls configuration. Document
default values and whether each bound applies per message, session or hub;
test both defaults and hub overrides at their enforcement boundaries.

Use `ZmScheduler::Timer` members and absolute deadlines. Cancel timers and drain
late callbacks on their owner shard. Use `ZmEngine` for endpoint lifecycle;
stop admission/listening/reconnect, cancel work, drain Rx then Tx continuations
and outstanding hook continuations, release connection/session ownership, then report stopped.
Raw owner back-pointers in buffers are valid only with that drain guarantee.
No blocking from Rx/Tx callbacks; main-thread synchronous lifecycle wrappers may
wait. Restart and cleanup after failed startup must have defined tests; restoring
the prior state is not required.

Use identified tunable heaps and intrusive queue/hash nodes. Add counters for
frames/bytes, rejects, gaps/replays, queue occupancy and hook failures, with
owner-shard snapshots for reporting. Business call latency belongs to the
application/tools that define correlation and completion. Traces must retain their
logged values correctly and redact authentication material; raw wire capture
is opt-in and bounded/rotated.

## Implementation sequence and acceptance gates

### P1 — contracts, schema and build skeleton

Apply the Q001 version/transport, Q002 naming and Q003 handler/type-code decisions.
Add module exports, Makefiles and configure registration. Document APIs, limits,
ownership and state transitions. Build minimal declarations for scalar fields,
an alternate facet, nested groups, union alternatives and typed request/response catalogs. Add
compile-fail fixtures for ambiguous schemas/catalogs. Do not add compatibility shims.

### P2 — standalone codec and envelope

Implement properties, scoped indexes, scalar handlers, binary pairs, groups,
parse views and frame writing. Implement Zfix load/update semantics using the
Q005 contract and existing adapters as read-only references.
Test constructor/setter/update paths, required presence, defaults, null/zero
distinctions and failure diagnostics. Include
tag 4200 and private high-numbered tags without version-based maxima.

Gate: deterministic golden-wire fixtures plus round trips for all implemented
types; each byte split of representative frames; concatenated frames; corrupt
length/checksum; incomplete values; overflow; binary embedded delimiters;
zero/one/many/nested groups; union resolution and backing lifetime; default
object dispatch; repeated tags in distinct scopes; count mismatch;
unknown-tag skipping; legal alternate field order versus protocol-invalid
ordering; malformed known values (including optional fields); depth/size limits
and output failure. Fuzz framing and
schema-aware decode with allocation/retention bounds. Round trips alone are
insufficient because encoder and decoder can share a mistake.

Gate: Zfix fixtures verify the Q005 missing-field contract for construction,
load and update, including reused destinations. Document relevant differences
from the reference adapters; changing or aligning Zfb/Zpb is not a gate.
Separately test present empty strings through ordinary scalar conversion and
explicit zero-length FIX vectors replacing previously nonempty vectors. Cover
absent versus present-empty values without conflating wire presence with value.

### P3 — control catalog and pure session engine

Declare the seven built-in session messages and their built-in fields. Implement
the state machine against fake clock, transport and application hooks. Cover
simultaneous inbound recovery/outbound replay, duplicate gap fills, invalid
sequence regressions, reset epochs, original timestamps, heartbeat ID echo,
logon/logout timeouts and hook refusal/failure. Base relevant cases on the FIX
[session test cases](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Session_Testcases_June_2020.pdf).

Gate: transition tests explicitly assert sequence state, emitted messages,
application callbacks and timer effects, including every error exit.
Queue multiple application messages, insert session control traffic before and
between their transmission, and verify shared sequence allocation in wire order
and the corresponding `load` notifications. Assert that submission allocates no
wire sequence numbers, and that partial writes/replay retain assigned numbers.
Verify that the assignment callback identifies the exact business message and
runs before an immediate peer Reject can be delivered to the application.
Exercise the cross-shard continuation path and refused load without consuming
a committed sequence number, plus encode failure before frame publication.
Queue work before reconnect/reset and verify that Tx uses the current epoch,
header state and SendingTime. Delay assignment callbacks to verify that new
messages cannot overtake committed sequences and stale continuations cannot
publish on a replacement connection. Cover gap-fill/reset header sequencing
separately from ordinary new control-message allocation.
Have an application fixture use that callback to correlate a sequence-keyed
Reject to its business request, including interleaved control traffic and
sequence-number reuse in a later epoch.
Cover all four session modes and independently vary declared versions to verify
that only header population/matching changes. Verify raw-mode bypass separately.
Add Q017 fixtures for reconnect without reset, externally scheduled daily/weekly
epoch changes, reset-on-connect, coordinated live reset, refused/unsupported
reset, directional recovery and sequence-limit exhaustion. Assert that old
sequenced work and callbacks cannot be mistaken for new-epoch traffic, while
unsequenced submissions follow the application's retain/refuse policy.
Assert that reset does not silently discard an
unresolved recovery operation. Explicitly verify that same-ID reconnect without
a reset flag completes required bidirectional resend/recovery before regular
operation resumes with the existing sequence numbers. Test the standard
drain/disconnect/reconnect-with-reset path and the supported drain/live-reset
path, including in-flight messages and gaps in either direction. Verify that
old-epoch admitted traffic can finish draining while new-epoch application
traffic remains held until reset coordination completes.
Exercise application schedule triggers with a
fake clock; exchange-specific cases are separate from canonical interop.

### P4 — transport, recovery hooks and endpoint integration

Integrate Ztcp client/server links, then Ztls composition without a second FIX
parser. Add application-owned storage fixtures exercising the Q009 hooks and
sequence-state restoration across restart; no storage backend is built into Zfix.
Exercise multiple sessions/listeners, application admission callbacks (including
no FIX-layer authentication, acceptance and refusal), connection displacement,
reconnect/failover, bounded overload, delayed application recovery, partial send, and graceful
stop with outstanding timers/replay/work. Reserve integration ports according
to [TESTING.md](docs/TESTING.md).

Gate: end-to-end loopback and restart recovery; no stale callbacks, unbounded
queues, unintended sequence reuse within an epoch or missed lifecycle/hook
notifications. Business terminal notifications remain application policy.

### P5 — typed dispatch, replies and application examples

Implement catalogs, typed send/receive/reply operations and application hooks.
Demonstrate application-owned correlation and completion policy in examples. Provide both initiator and
acceptor examples using the same business structures, including a repeating
group request/response and an unsolicited event.

Gate: typed dispatch, admission/error reporting, declared rejects, session loss
notifications and borrowed/owning lifetimes. Example application tests cover
immediate/reordered replies, duplicate/unknown keys, multiple responses and
reconnect/replay according to their own explicitly chosen workflow policy.

### P6 — order/execution tools and interoperability (Q013 resolved)

Provide high-rate order/execution client and server tools, with application
examples demonstrating their typed messages, correlation and recovery hooks.
Follow zhttp's reusable utility and independently selectable client/server
pattern. Supply these four executables:

| Tool | Role |
| --- | --- |
| `zfix` | Zfix initiator generating orders and consuming execution reports. |
| `zfixd` | Zfix acceptor processing orders and generating execution reports. |
| `quickfix` | Equivalent order-generating initiator built against QuickFIX/C++. |
| `quickfixd` | Equivalent order/execution acceptor built against QuickFIX/C++. |

The QuickFIX names denote repository-provided fixture applications, not assumed
upstream executable names. Reuse application workload definitions and scenarios
while retaining independent Zfix and QuickFIX wire/session implementations.
Keep reusable Zfix tool support in `util`, independently buildable and usable by
`itest`, `interop`, `bench` and examples. QuickFIX dependencies stay confined to
the external fixture targets. Small documented examples expose the same typed
application API used by the load tools.

Both correctness testing and benchmarking must support all four pairings:

| Initiator | Acceptor | Purpose |
| --- | --- | --- |
| `zfix` | `zfixd` | Zfix end-to-end baseline. |
| `zfix` | `quickfixd` | Zfix client / QuickFIX server interop and performance. |
| `quickfix` | `zfixd` | QuickFIX client / Zfix server interop and performance. |
| `quickfix` | `quickfixd` | Independent reference baseline and fixture validation. |

Use a common, explicit order/execution scenario: NewOrderSingle and correlated
ExecutionReport, with configurable acknowledgement/fill progression and known
terminal outcomes. Optional cancel/replace and group-heavy scenarios are named
separately. These are application policies in the tools (Q011), not library
semantics. Verify order IDs, execution IDs, quantities/status progression and
expected report counts; distinguish replay duplicates from new executions.

Expose consistent workload controls: warmup, measured count/duration, target
order rate or saturation mode, maximum outstanding orders, session count,
message shape and reproducible seed. Select peer endpoints, declared version,
session mode and TCP/TLS configuration explicitly. Report backpressure, rejected
admissions, incomplete orders and timeouts rather than counting attempted sends
as completed work. Provide readiness synchronization, bounded test deadlines,
clean shutdown and result/log collection for all pairings.

Use **QuickFIX/C++** as the required independent peer. Its
[application API](https://quickfixengine.org/c/documentation/getting-started/creating-your-application.html)
supports initiator and acceptor roles. Pin its release/commit and matching
dictionary files in a reproducible `zfix/interop` harness. QuickFIX is a test
dependency, not a production dependency.

Run the complete four-pairing matrix above. Configure dictionaries explicitly,
including separate transport
and application dictionaries for FIXT as described in QuickFIX's
[configuration documentation](https://quickfixengine.org/c/documentation/getting-started/configuration.html).
Supply a fixture dictionary for custom tags/groups rather than disabling all
peer validation. QuickFIX's
[group API](https://quickfixengine.org/c/documentation/working-with-messages/repeating-groups.html)
provides an independent producer/consumer for group fixtures.

The default interoperability profile is canonical **FIX 4.4 over TCP+TLS**
(Q016 resolved): declared version FIX 4.4, session transport mode `4`, and TLS
over TCP. Use this profile for all four pairings, with the pinned canonical
FIX 4.4 dictionary and explicit TLS fixture configuration. TLS setup remains
delegated to Ztls for Zfix peers and the corresponding QuickFIX facilities for
QuickFIX peers; application authentication remains governed by Q012.

Other declared versions, session modes (`4.4A`, `T1.1`, `raw`) and plain TCP
remain explicitly selectable additional coverage, not the default interop
profile. Keep the P3 mode-specific tests. Test `raw` with an externally managed
fixture rather than expecting a canonical peer's normal session engine to
bypass Logon/recovery. Record any additional peer options or dictionaries.

For the default profile:

- Logon, opaque TestReqID/Heartbeat, clean logout, timeout and reconnect.
- Typed application exchange, decimal/timestamp values, binary data, nested
  groups, zero groups and custom tags.
- Sequence gaps, bounded/open-ended ResendRequest, duplicate replay, gap fills,
  original timestamps, reset negotiation and restart recovery.
- Unknown tags skipped without value validation; unknown message types handled
  separately through session/application dispatch. Malformed known fields,
  protocol-invalid ordering, wrong identities and duplicate sessions have
  explicit expected reject/logout/disconnect outcomes.
- TCP/TLS fragmentation/coalescing through a deterministic fault fixture, plus
  TLS establishment and clean teardown with the configured certificate fixture.

Unknown-tag skipping is a Zfix receiver requirement. Exercise it with the
independent peer or fault fixture sending tags outside Zfix's selected schema;
do not require QuickFIX to adopt Zfix's unknown-tag policy or disable its
dictionary validation globally. Record directional expectations for negative
tests separately from the common valid-message interoperability scenarios.

Gate: all four pairings pass the order/execution correctness scenarios, and both
cross-implementation roles pass protocol interop without broad validation
bypasses. Save sanitized wire
transcripts, exact configurations, peer/dictionary versions and assertions.
Record deliberate profile exceptions individually. Run installed-header/library
consumer tests as well as source-tree tests. A zfix-to-zfix loopback is not an
interop substitute.

### P7 — performance, portability and release documentation

Benchmark the same four pairings using the P6 tools, with identical workload,
session configuration, logging, validation and application-selected archive
policy. Record any necessary peer differences explicitly. Run separate client
and server processes on loopback and configurable remote hosts. Provide repeated
warmup/measurement runs and machine-readable results plus a comparison report.

Measure offered/admitted/completed orders per second, execution reports per
second, wire messages/bytes, and client-observed order-to-acknowledgement and
order-to-terminal-execution latency distributions (p50/p95/p99/p99.9/max).
Use a client-local monotonic clock for round-trip latency. For paced loads,
record scheduling delay and latency from intended submission time as well as
actual submission, so overload is visible. Report in-flight work, errors and
unfinished orders alongside throughput. Capture per-process CPU/RSS and Z
heap/copy metrics where available; distinguish framework-specific counters from
metrics comparable across both engines. Disable per-message tracing for timing
runs and retain separate diagnostic runs.

Benchmark warm scalar messages, realistic group-heavy messages, binary values,
fragmentation and replay. Measure allocations/copies, bytes retained, code size,
encode/decode latency percentiles, end-to-end latency and overload behavior;
record compiler, CPU, build flags and workload. Establish numeric budgets from
these measurements instead of inventing throughput claims.

Run current GCC and Clang builds, Linux x64/ARM where available and Windows
MSYS2/MinGW. Complete Clang ASan/LSan and Valgrind leak checks under
`libtool exec`. Use `make -j8`, building library dependencies before dependents;
build `all` before `make test`. Use the prescribed clean top-level rebuild for
release configuration. Document any untested platform explicitly.

Update `configure.ac`, top-level `Makefile.am`, module Makefiles, installed
headers/libraries and dependency/testing documentation. Keep `test`, `itest`, `interop` and `bench`
roles distinct; repository-owned tests use ZuTest/ZuTestUtil TAP. Do not silently
report skipped external interop as a passing release gate.

## Stable-numbered open questions and ambiguities

IDs are permanent: append new questions, never renumber or reuse an ID. Resolve
an entry in place with the decision/date and retain its history. Entries remain
open unless explicitly marked resolved; proposed defaults guide implementation
planning, not approvals.

| ID | Question / ambiguity | Decision or proposed default and consequence |
| --- | --- | --- |
| Q001 | Which FIX application versions, session profiles and extension packs must the first release support? | **Resolved 2026-10-26 (user):** The declared version simply populates transmitted headers and matches received headers; transport (`raw`, `4`, `4.4A`, `T1.1`) controls session behavior. This supersedes the proposed staged 4.4 → 4.2 → FIXT/5.0 SP2 delivery plan and automatic version-based schema selection. |
| Q002 | What public prefix should the implementation use? | **Resolved 2026-10-26 (user):** `Zfix`, not `ZfFIX`. Apply `Zfix` consistently to namespaces, headers, libraries and ADL hooks. Separate codec-library packaging remains a design proposal. |
| Q003 | How should structured fields, alternatives and repeating groups be represented? | **Resolved 2026-10-26 (user):** use established `AsArray`, `AsUnion`, `AsObject` (default) patterns and field type codes. This supersedes the proposed component marker and mandatory explicit delimiter property; use the ordered-group mechanism described above. |
| Q004 | Is runtime per-session customization of tags, suppression, defaults or enums required? | **Resolved 2026-10-26 (user):** no runtime customization is required for `zfix`. Use compile-time metadata, facets and handlers; omit runtime customization/dictionary machinery. This replaces the earlier suggestion to scope a runtime dictionary separately. |
| Q005 | What are omission, explicit empty and zero-group emission semantics? | **Resolved 2026-10-26 (user, scope clarified):** `tag=<SOH>` is the value `""`, parsed like any other value; zero-length arrays/vectors are preserved as-is. Use Zfb as the authoritative missing-tag precedent: every selected writable field receives its decoded value or metadata/type default. The separately authorized Zpb correction is complete; further Zfb/Zpb changes are outside the Zfix plan. This supersedes the proposed FIX-specific omission policy and the plan's earlier scope expansion into Zfb/Zpb changes. |
| Q006 | Must standalone decode/update provide rollback on failure? | **Resolved 2026-10-26 (user):** rollback on failure is never required. Partial destination modification is permitted; omit rollback/staging machinery. Failure reporting and resource cleanup remain required. |
| Q007 | How should FIX date/time formatting and precision be handled? | **Resolved 2026-10-26 (user):** use `ZuDateTime.hh`'s built-in FIX format. FIX-specific field properties can override defaults for precision, etc. Reuse the existing scanning/formatting facilities rather than adding separate FIX date/time logic. Encoded text remains byte-preserving as described above. |
| Q008 | What built-in session-level message support is required? | **Resolved 2026-10-26 (user):** provide these built-in session messages: Logon, Heartbeat, TestRequest, ResendRequest, Reject, SequenceReset and Logout. Use the control-message field table above; broader application/control extensions, including BusinessMessageReject, are not required built-ins. This supersedes the earlier expanded control catalog. |
| Q009 | Who owns durability, and what recovery support must Zfix expose? | **Resolved 2026-10-26 (user):** durability is an application concern. Provide the application hooks and sequence-state restoration contract specified above using current Z facilities. No built-in store, database dependency or imposed durability/commit policy. This supersedes the proposed Zdb replay-store implementation. |
| Q010 | How are replay history availability and sequence queues handled? | **Resolved 2026-10-26 (user):** use `ZmPQTx` and `ZmPQRx` underneath. The application decides replay-history availability by implementing archive/retrieval and related hooks. No Zfix-owned retention policy. |
| Q011 | Who owns business correlation and request/response workflow policy? | **Resolved 2026-10-26:** these are application concerns. Zfix provides typed dispatch/send/reply hooks and session context; the application owns correlation, response completion, subscriptions, timeouts and reconnect policy. This supersedes mandatory single-terminal calls and automatic failure of pending business calls on disconnect. |
| Q012 | Who owns authentication and TCP/TLS setup? | **Resolved 2026-10-26 (user):** authentication is an application concern; Zfix provides the relevant callbacks and permits no FIX-layer authentication. Delegate TLS setup (certificates, mTLS, etc.) to Ztls and TCP setup (ports, etc.) to Ztcp. Do not impose a FIX authentication scheme or duplicate transport configuration. |
| Q013 | Which interop/benchmark tools and examples are required? | **Resolved 2026-10-26 (user):** provide high-rate order/execution tools and examples supporting `zfix` ↔ `zfixd`, `zfix` ↔ `quickfixd`, `quickfix` ↔ `zfixd`, and `quickfix` ↔ `quickfixd` for both testing and benchmarking, following zhttp's client/server combination model. |
| Q014 | How should corrupt data, unknown tags and out-of-order tags be handled? | **Resolved 2026-10-26 (user):** Malformed/corrupt data is an error; unknown tags cannot be validated and are skipped. Out-of-order tags are malformed/corrupt when they violate the protocol. This replaces the proposed venue-specific permissiveness policy. |
| Q015 | How should operational limits be provided and configured? | **Resolved 2026-10-26 (user):** as with other Z framework adapters and protocol implementations, provide default limits configurable at the hub level. Propagate effective limits to codec/session machinery and test defaults and overrides. |
| Q016 | What is the default interoperability test profile? | **Resolved 2026-10-26 (user):** canonical FIX 4.4 over TCP+TLS. Use declared FIX 4.4 with session mode `4` and TLS over TCP for the default four-pairing interop matrix. Other profiles remain explicitly selectable additional coverage. |
| Q017 | What reset schedule and 24x7 sequence-rollover policy is required? | **Resolved 2026-10-26 (user, clarified after public-spec review):** same-ID reconnect without a reset flag performs resend/recovery before resuming regular operation with the same sequence numbers. The standard reset path drains both directions, then reconnects with a reset. Also support the seldom-used coordinated reset while remaining connected, after draining both directions. Schedules remain application-selected; no automatic calendar/disconnect reset or silent wrap. Expose directional recovery and sequence-limit notifications. This supersedes the blanket avoidance of coordinated live reset and makes the default recovery/reset paths explicit. |
| Q018 | When are outbound wire sequence numbers allocated, and how does the application learn the association? | **Resolved 2026-10-26 (user, clarified):** never at application submission. Session control messages consume sequence numbers too; Tx assigns new messages in transmission order from the shared sequence space. Provide the application callback identifying the specific business message and its allocated sequence number before a peer response can arrive, enabling sequence-keyed rejects to be applied at the business level. Preserve session/epoch and message reference context; the application owns correlation policy. Pending queue identities are separate from FIX sequence numbers; replay retains original numbers. |
