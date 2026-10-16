# Goal: derive FlatBuffers serialization directly from ZfStruct

Implement `ZfFB` in the `zf` layer as a native Z FlatBuffers codec driven by
`ZfStruct` metadata, replacing and deleting the `zfb` module entirely. Make
that metadata the authoritative declaration for C++ object
mapping, wire representation, serialization, verification, and runtime
reflection. Remove the production dependency on the canonical FlatBuffers
libraries, independently maintained `.fbs` schemas, and associated codegen.

Interoperability with canonical FlatBuffers implementations is mandatory.
Canonical readers and verifiers must accept `ZfFB` output, and `ZfFB` readers
and verifiers must accept canonical output for the supported schema features.
Preserve the FlatBuffers wire format. Propagate breaking C++ API changes
through dependent modules without compatibility shims. Backwards/legacy
compatibility is a non-goal: existing persisted data, deployed peers, C++
APIs, and application schemas impose no compatibility requirement. Propagate
new contracts through all current consumers; do not add legacy readers or
data-migration tooling for old formats. This resolves Q1 in `plan.md`.

Changing an application's schema does not relax the canonical interoperability
requirement. Do not introduce a Z-specific wire dialect or require canonical
producers to adopt the new writer's layout choices.

The primary benefits are a single source of truth and dependency removal.
Improved latency and reduced serialization overhead are measurable objectives,
not assumptions. Follow `AGENTS.md` and the authoritative `GUIDELINES.md`.

## Existing foundation

- `ZfStruct` already provides extensible field properties, type codes,
  defaults, constructor ordering, facets, and runtime descriptors.
- `ZfbStruct` already derives save, construction, update, and deletion
  operations from those fields. Its generated field adapters call the
  canonical builders and accessors; replace those calls with operations
  parameterized by wire traits.
- `Zfb::IOBuilder` already builds directly into detachable pooled `ZiIOBuf`
  storage. Primitive vectors already support uninitialized destination
  storage. Preserve these properties when comparing implementations.
- Database stores consume `reflection::Schema` at runtime. Replacing only
  the annotated object serializer is insufficient to remove FlatBuffers.
- Serialized objects also participate in Z field adaptation, key extraction,
  printing, and serialization directly from borrowed views. These uses are
  independent of constructing a C++ row object.

## Audited dependency inventory

The source audit covers declarations and callers across `src`, `test`,
`itest`, `example`, `bench`, `util`, and `interop` where present, plus schema,
build, and documentation references. Search both qualified and imported APIs,
generated `fbs::` types, and consumers of adapted fields and buffer aliases.
The following groups define the migration and verification coverage; repeat
the inventory against the implementation tree before declaring completion.

| Existing source / consumer | Required replacement behavior |
| --- | --- |
| `zfb/src/ZfbStruct.hh`, `zfb/test/ZfbTest*.cc` | Field adaptation; constructor, allocation, placement construction, load/update; facets and filters; borrowed scalar/string/vector access; nested UDT transformers; buffer lifecycle and reflection verification. |
| `zdb/src/Zdb.hh`, `ZdbBuf.hh` | Type-erased row serialization, reflected schemas, buffer-cache indexing directly over serialized keys, row/key/selection projections, verified span-to-stream printing. |
| `zdb/src/ZdbMsg.hh`, `Zdb.cc` | Framed replication/recovery/commit/heartbeat messages, typed and erased roots, nested row buffers, direct payload copying, retained buffers and trusted access. |
| `zdb/src/ZdbMemStore.hh`, `ZdbMemStore.cc` | Runtime field resolution, scalar/default and nested/vector reads, dynamic tuple construction, key groups/order, update/delete filtering, recovery envelopes. |
| `zdb_pq/src/ZdbPQ.hh`, `ZdbPQ.cc` | Reflected FlatBuffer-to-PostgreSQL parameter conversion and reverse construction; borrowed text/bytes, binary arrays and endian conversions, selected key/row tuples, query/result lifetimes. |
| `zdb_sqlite/src/ZdbSL.hh`, `ZdbSL.cc` | Reflected field resolution, direct text/blob binding, ordered storage encodings and vector blobs, reverse construction from statement columns, statement/source/scratch lifetimes. |
| `zdb/src/ZdbSaga.hh`, saga tests/examples and store integration tests | Runtime-selected saga payload verification, placement construction with extra constructor arguments, owned payload snapshots, catalog and replay handling. |
| `ztc/src/ZtcFB.hh`, `zdb/src/ZtcDB*.hh` | Telemetry metadata, synthetic fields, enums, packed `ZuSemVer`, span-valued keys, and schema order distinct from declaration/constructor order. |
| `ztc/src/ZtcMsg.hh`, `ZtcApp.cc`, `ztcagent_daemon.cc`, `ztchub*.cc` | Composing prebuilt union offsets, optional field presence, copying unchanged serialized payloads, sequence mutation, framing, alert file persistence, raw binary stream transmission. |
| `ztc/src/ZtcFilter.hh`, `ztchub_daemon.hh`, telemetry tests/examples/integration paths | Generated enum/type dependencies and routing/filter semantics, including consumers without a direct `Zfb` reference. |
| `zdash/src/zdash.cc` and tests | Request builders, enum iteration/names, verified-buffer handoff, borrowed keys for lookup, view-to-object construction and selective updates. |
| `zdf/src/ZdfSchema.hh` and tests | Series/block row schemas, grouped and descending keys, raw block byte fields and store round trips. |
| `zum/src/zumd*.hh`, `zumd_daemon_admin.cc`, tests and probes | Persistent rows, JSON facets, administrative saga images, custom vectors of deletion records, schema version defaults and nested payload ownership. |
| `configure.ac`, `Makefile.am`, module build rules, `scripts/fbscodegen` | Remove module traversal, includes, links, generated headers/binary schemas and schema distribution, including link-only dependents such as `zgtk`. |

Include `zu/src/ZuStruct.hh`'s obsolete documentation references in cleanup.
The old `zdash/src/fbs/*.fbs` schemas are not in the current dashboard build;
delete these orphaned artifacts rather than recreating their old protocol.
Classify textual search matches before acting: unrelated byte strings and
unrelated functions named `Verify` are not FlatBuffers dependencies.

## Metadata contract

### Feature scope

Support the FlatBuffers features used by this repository, including those
needed for interoperable BFBS reflection, with these explicit Q3 decisions:

- Map canonical `required` to the existing `Required` property. Apply the
  canonical restrictions on eligible wire kinds and diagnose unsupported
  property combinations; do not add a separate FB-required marker.
- Express forced alignment through a `ZfFB`-specific field property. Carry
  the requirement through layout, allocation, nested buffers, verification,
  reflection, and interoperable schema export.
- Support size-prefixed and unprefixed roots. Select the mode with a
  compile-time parameter of `load`/`save`, propagated to verification and
  root finishing, without runtime mode detection.
- Exclude sorted application vectors and their sorting/search APIs. Retain
  the canonical ordering of BFBS metadata vectors and reflection name lookup.
- Exclude wider offset variants; use ordinary 32-bit FlatBuffers offsets.
- Support missing scalar fields that read as their wire default, as existing
  `zfb` does: implicit zero/false and explicit nonzero defaults, omission of
  default values, presence access, and forced-present default values.
- Support metadata-only `Skip` accessors through `ZuStruct`/`ZfStruct` (Q6).
  For `ZfFB`, `Skip` implies canonical `deprecated`; retain the field's schema
  metadata and slot without a C++ member or object getter/setter.
- Nullable scalars declared with `= null` return a distinct absent/null
  result instead of a scalar default. These and vectors of unions have no
  repository schema use and are out of scope. Support for individual union fields remains
  required.

Apply this scope consistently to typed operations, reflection, exporters,
and interoperability acceptance. Schema inspection may expose metadata for
unsupported features without promising payload operations for those features.

### One declaration API

Use `ZfStruct` directly for all object declarations, with FlatBuffers
properties in its existing extensible property lists. Do not introduce a
`ZfFBStruct` declaration macro or retain `ZfbStruct` under another name.
Derive FlatBuffers operations from ordinary `ZfStruct` metadata through
`ZfFB`.

Migrate every `ZfbStruct` declaration to `ZfStruct` and every
`ZfbStructImpl` definition to `ZfStructImpl`. Remove the entire `Zfb*`
declaration and binding API, including `ZfbRoot`, `ZfbType`, `ZfbBuilder`,
`ZfbSchema`, and `ZfbEnableShim` variants. Express any needed root or wire
type information through the new metadata contract; generated-type and
generated-schema bindings have no replacement role.

Extend `ZuStruct`'s accessor taxonomy with `Skip`, inherited by `ZfStruct`.
Retain the field name, explicit type information, properties/default, and
declaration position without referring to a live member or generating object
accessors. The underlying C++ struct/class need not have any corresponding
data member or member function; `Skip` neither requires member lookup nor
creates storage in the object. `ZfFB` maps these entries to canonical deprecated fields in BFBS
and textual FBS. For every format, `load` and `save` simply skip these fields
without ever calling `get` or `set`; this is a shared framework contract.
Their metadata remains solely for metadata uses such as schema generation,
reflection, and wire-ID accounting. Keep them in complete schema metadata and numbering; exclude
them at compile time from object construction, load/save/update/delete,
tuple/key extraction, object print/JSON, and object accessor factories.
Retain incidental constructor/key/`Mutable` properties as metadata while
excluding these fields from every object operation. Diagnose only combinations
that cannot describe a supported canonical schema, such as `Required` on a
deprecated field. Do not use dummy getters or no-op setters to satisfy consumers
that should skip the field. Schema inspection retains deprecated metadata
without requiring a native object-field association. This Q6 feature extends
the scope without introducing a legacy compatibility requirement.

### Field IDs

Add an optional FlatBuffers field ID property, defaulting to declaration
order in the complete wire-field declaration.

- Resolve IDs before applying facets or save/update/delete filters.
- Preserve original IDs through field adaptation and key projections.
- Follow canonical FlatBuffers rules (resolved Q5): all table fields use
  implicit declaration-order IDs or all declared fields specify explicit
  IDs. Reject mixed declarations at compile time. Explicit IDs may reorder
  fields independently of declaration order.
- Account for union discriminator and value slots. Follow the canonical
  convention that an explicit union ID identifies the value slot, with
  the discriminator in the preceding slot.
- Require unique resolved slots contiguous from zero, counting hidden union
  discriminator slots. Reject collisions, gaps, and overflow at compile time
  regardless of whether `.fbs` export is requested. Declared `Skip` fields
  consume IDs: one for an ordinary field, two for a union's discriminator
  and value. They participate in the all-explicit rule and contiguity checks
  even though omitted from new payloads. Do not introduce unoccupied reserved
  slots or invented placeholder fields. Historical application
  IDs and union discriminator values need not be retained.
- Apply these constraints to the complete wire declaration. Filtered
  payloads may omit fields; projections and filters must not renumber IDs.

Do not infer existing IDs from current `ZfbStruct` order. The audit found
concrete mismatches: `HashTelemetry` declares `shadow`, `linear`, and `bits`
near the beginning in `ZtcFB.hh`, while its schema places them later;
`CxnTelemetry`, `QueueTelemetry`, `HubTelemetry`, and `AlertTelemetry` also
have differing orders. Define the new IDs, including implicit union
discriminator slots, consistently across metadata and consumers without
requiring the old schema order. `Ctor<N>` remains an independent ordering.

Union alternatives are identified by discriminator, not solely by payload
type: `Zdb_.fbs.Body` maps both `Replication` and `Recovery` to `Record`.
Support multiple alternatives with the same payload type and stable distinct
tags, as well as empty tables such as telemetry `Shutdown`.

### Field names and metadata translation

Provide an optional FlatBuffers field-name property using the same approach
as `ZfJSON`'s `JSON::ID<...>` and `GetID<Field>`. Resolve alternative IDs from
the selected facet's effective field properties and use explicit overrides
verbatim. Otherwise derive the FlatBuffers field name from `Field::id()`
using the exact existing `ZtCase::camelSnake` behavior: replace each uppercase
character with an underscore and its lowercase form (`roleIDs` becomes
`role_i_ds`). Compute these names at compile time for schema generation and
use them consistently in BFBS, FBS, and reflection associations, including
`Skip` metadata. Textual field names are independent of numeric wire IDs
and C++ member names; JSON and FlatBuffers overrides are format-specific.

Normalize Z <-> FlatBuffers metadata by semantic equivalence (Q8). Map
metadata with equivalent meanings on both sides; otherwise the information
is lost in translation. Do not preserve unmatched metadata through custom
attributes or opaque extensions. Native metadata needed by Z consumers
remains available locally, but need not be recoverable from exported schemas.
Apply this rule to both BFBS and textual FBS within the supported feature set.

### Wire types and defaults

Derive wire kinds, scalar widths, vector element types, table versus inline
struct representation, and union alternatives from compile-time traits.
Retain ADL-based transformers for Z types and application UDTs.

Define inline struct size, alignment, padding, and member offsets explicitly;
do not serialize arbitrary native C++ object layouts.

Reuse the existing default-value logic (Q9): `Deflt` if set, otherwise the
same type-specific fallback used by other Zf adapters. Consume the resolved
value through `Field::deflt()` without reimplementing fallback selection in
`ZfFB`. Do not introduce a separate FlatBuffers default property or replace
resolved Z defaults with an independent zero/false policy. Map representable
defaults consistently into canonical scalar defaults, typed/reflected reads,
writer omission, and BFBS/FBS exports. Apply Q8 to defaults with no canonical
metadata equivalent; do not invent a noncanonical default encoding. Audit
existing differences (nonempty string defaults and numeric null sentinels)
when migrating declarations; legacy behavior is not a compatibility target.

- Scalar wire defaults govern both omission during writing and reads of
  absent fields.
- Define missing string, vector, table, and inline struct handling.
- Missing inline values produce a defined null/default using existing Zf
  default resolution (`Deflt` if set, otherwise the normal fallback). If the
  field is `Required`, absence is an error (Q11). Apply this consistently to
  typed and reflected loads, including Z value transformations, without
  inserting a field into the source span or re-verifying trusted data.
- Provide presence access where absent, empty, and explicitly supplied
  values must be distinguished.
- Support forcing a scalar field to be present for subsequent in-place
  mutation, including when its initial value equals the default.
- Preserve the current constructor, load, update, and deletion semantics
  unless an intentional change is identified and propagated.

Use existing nonzero defaults and presence-sensitive callers as concrete
fixtures: telemetry Request/Error sequence numbers default to `UINT64_MAX`,
many `zum` version fields default to 1, and telemetry distinguishes an absent
publisher ID from a present empty string. Mutation must report an absent
scalar slot without silently rebuilding the message; forced-default writes
must support the later sequence-number rewrite paths.

Preserve the distinction between wire membership and operation filters.
Telemetry serializes synthetic fields such as `allocated` and `rag` using
`ZfFieldFilter::All`; these fields must have stable wire metadata even when
ordinary `Save` excludes them. Preserve `Upd` as mutable fields plus the
primary key, and `Del` as all key fields; store-side deletion may consume
only the primary key. Do not turn a filtered update into a generic patch
whose meaning depends solely on physical field presence.

Cover the existing representation families explicitly: signed/unsigned
8/16/32/64-bit scalars, booleans, floating-point wire widths, strings, bytes,
scalar/string/byte-vector vectors, inline 128-bit integers, fixed/decimal
values, times/dates and their vectors, bitmap tables, IPv4/IPv6 array-backed
union members, nested tables, and custom vectors of tables. `BytesVec` uses
wrapper tables rather than a nonexistent native vector-of-vectors encoding.
Support primitive UDT mappings such as `ZuSemVer` to `uint32`, in addition
to inline and nested UDTs. Preserve enum/flag widths, numeric values and
name mappings using Z metadata, removing the `ZfbEnum*` family and generated
`EnumValues*`/`EnumName*` dependencies.

## Implementation

Place serialization, wire traits, views, verification, and runtime wire
descriptors in `zf` under `ZfFB`. Delete `zfb` entirely after migrating its
consumers; do not retain it as an adapter or compatibility module.

Expose exactly two headers: `ZfFB.hh` for the main codec and
`ZfFBReflect.hh` for reflection and schema output. Follow the established
adapter organization and style: field properties and type metadata, input
and output support, field/object handlers, then public load/save functions.
Do not split implementation helpers into additional `ZfFB` headers.

Respect the layer boundary: `zf` must not acquire a dependency on `zi`.
Provide a buffer interface suitable for direct serialization into caller-owned
storage, with pooled `ZiIOBuf` integration implemented in `zi` or the relevant
higher-layer consumer. Keep `ZiIP` transformations above `zf`, using the
extensible transformer mechanism.

### Wire reader and writer

Implement typed offsets, table/string/vector views, field access, and a
builder that can write directly into supplied storage. Its higher-layer
`ZiIOBuf` integration must preserve pooling and detachable buffers without
an intermediate copy. Support borrowed access as well as
construction into ordinary C++ objects. Direct message construction must not
require temporary owning objects.

Support composition from already-built strings, tables, and union payload
offsets. `Ztc::saveMsg` and `saveTelemetry` currently use generated builders
because `ZfbStruct` cannot express these prebuilt union boundaries. The new
API must cover this use without a second declaration system.

Provide direct filling of uninitialized primitive and inline-struct vector
storage, callback-driven vectors of tables/strings, and access to the final
contiguous payload before or after detach. Keep offsets valid across builder
growth and state clearly when raw pointers become invalid.

Provide the behavior of `Zfb::Save::nest`: construct a complete nested root
inside a byte vector in the same builder, without an intermediate buffer or
copy. Treat the nested vector as an independently readable FlatBuffer. Its
reachable objects and vtables must remain within its span, including when
deduplication is enabled or prior objects already occupy the builder. Preserve
required nested alignment when copying an existing row payload into a new
recovery envelope (`AnyTable::mkBuf`).

Reproduce the current downward-writing arena behavior and custom
`Zfb::IOBuilder` integration, including allocation, growth, ownership transfer,
and alignment (Q16). These are baseline implementation requirements.
Evaluate changes only against the custom pooled implementation and require
no performance regression.

The reader must accept valid FlatBuffers layouts from other implementations,
including different field layouts, omitted fields, shared vtables, and signed
vtable displacements. Correctness must not depend on the new writer's chosen
layout. Preserve little-endian encoding, alignment, offset widths, lengths,
and string termination requirements.

### Borrowed views and structured stream output

Provide typed and type-erased root/table access directly over a bounded
`ZuBSpan`, with an explicitly trusted path for locally produced or previously
verified data. Expose strings as `ZuCSpan` and byte vectors as `ZuBSpan`
without allocation, retaining lengths, embedded zero bytes, and separate
presence information. Vector views must support indexed/iterated access;
inline struct vectors must use wire stride rather than `sizeof(pointer)`.
The existing `Zfb::GetFieldV` workaround is evidence of this distinction.

Make typed serialized views participate in `ZuFields`, `ZuUnder`,
`ZuStructKey`, and read-only field adaptation, preserving facets, original
field identity, and span-valued key types. `Zdb::RepBuf` indexes serialized
rows through adapted fields; `zdash` looks up objects with keys extracted
directly from telemetry views. Neither path should require materializing an
owning row.

Use `memcpy` of a complete self-contained serialized span for unchanged
payload copies, including the migration of `ztchub::copyTelemetry`. Preserve
all payload bytes, physical field presence, and internal offset relationships.
Its caller adds routing metadata to the outer envelope; build that envelope
separately. Carry the source buffer span/length and owner rather than only a
table pointer, arranging an independently copyable payload boundary or
retaining the complete containing buffer with its relative root location.
Preserve alignment; an arbitrary nested table pointer is not a standalone
buffer span. Propagate any schema changes through consumers as permitted by
Q1. Copying trusted bytes requires no re-verification (Q4).

For already serialized spans, the original serializer has made the field
inclusion/exclusion decisions (Q10). Preserve that result without rerunning
field filters, default omission, or other object-saving selection logic.
Present default-valued scalars and empty strings/vectors remain present;
absent fields remain absent. Do not materialize missing values from `Deflt`
or reconstruct the payload field by field. Raw copying preserves all bytes,
including unknown fields, and invokes no `get`/`set`, including for fields
now marked `Skip`. Apply operation filters, defaults, and the `Skip` rule
when serializing objects. Preserve
constructor arguments, ordered `Ctor` fields, post-construction `Init`
fields, placement construction, and selective updates for callers that do
need objects; saga reconstruction passes an additional base argument.

Preserve structured output directly from views to generic Z print streams
and `ZuVStream`, and facet-aware `ZfJSON` output through field adaptation.
`AnyTable::rowPrintFB(ZuVStream &, ZuBSpan)` currently verifies a row buffer
and streams its view; `Record_Print`, `IOBuf_::Print`, and logging depend on
this. Preserve formatting properties and invalid-input handling without
allocating an intermediate row or formatted string. The JSON helper in
`ZfbTest3.cc` indicates an intended integration but is not itself an executed
assertion; add meaningful coverage for it during implementation.

Views and spans borrow their backing storage. Preserve existing owner-held
buffer lifetimes across caches, callbacks, asynchronous sends, and shard
handoffs. Do not hide copying, allocation, or per-field reference-count
changes in view access. Owned snapshots in `ZdbSaga` and `Zum::SagaImage`
must remain valid after their builders are destroyed.

### Buffer, file, and binary stream integration

Keep the core codec independent of file and network APIs. Higher-layer
adapters must preserve pooled allocation/growth limits, front reservation,
`skip`/`length`, detach and reattach/reuse, owner metadata, and cleanup on
abandonment or exceptions both before and after finishing. Preserve the
existing front-reservation and growth tests in `ZfbTest`, and abandoned
builder tests in `ZfbTest3`.

Use `Zfb::IOBuilder` as the concrete reference, together with `ZiIOBuf` and
the current canonical downward-growing arena. Reproduce direct allocation
into the supplied pooled buffer; initial aligned capacity minus `skip`;
one codec-selected growth policy passed through the I/O adapter without a
second growth multiplier; preservation of front scratch and back payload
regions; and copying only live regions during reallocation. Detach the same
buffer by transferring ownership and adjusting `skip`/`length`, without
copying or later freeing its payload through the builder. Preserve rebind
and reset/destruction ordering, including exceptions before finish.

Preserve the current 8-byte baseline alignment and aligned headroom/capacity.
Propagate stronger alignment required by field metadata through actual
storage allocation, growth, roots/nesting, and copying. Keep these mechanics
in the appropriate `zf`/`zi` layers and maintain their allocation/copy
efficiency through the templated stream interface.

Retain the custom four-byte little-endian length framing in `ZdbMsg` and
`ZtcMsg`, maximum-frame handling, incremental/partial receive behavior,
and the distinction between a framed message span and its root payload.
Prepending a header must not relocate a completed payload where the current
reserved-headroom path avoids relocation. Framing and semantic message
validation remain higher-layer responsibilities.

Support binary output of completed buffer spans through existing stream
APIs, distinct from structured printing: `ztchub` writes a retained frame
with `tx << ZuBSpan{...}` and flushes a binary WebSocket message; telemetry
alert storage writes complete framed buffers with `ZiFile::pwrite` and
replays them later. Preserve transmit lifetime, partial-I/O handling, and
existing transport/file framing without adding another serialization pass.
This requires contiguous completed output, not a promise that a backward
builder can emit arbitrary bytes directly to a non-seekable sink as it builds.

`Zfb::Save::save` and `Load::load` expose whole-file I/O but have no repository
callers in this audit. Likewise the sorted-table vector helpers and fixed
string convenience helpers have no callers outside their declarations.
Remove unused wrappers rather than recreating them mechanically. If retaining
whole-file convenience functionality, place it above `zf` and preserve its
size/error contract and callback-scoped borrowed-buffer lifetime. Fixed arrays
themselves remain necessary for IP wire layouts; name lookup in reflection
must survive independently of canonical sorted-table containers.

### Verification

Derive verification from the same metadata. Verify all external data before
loading, including reflection-driven loads. Data generated and parsed within
the same process requires no validation or re-verification; use trusted
access for those payloads and generated BFBS spans. After external data has
been verified at ingress, use trusted access without repeated validation.
Cover offset arithmetic, bounds, lengths, alignment, vtables, nested objects,
unions, and bounded traversal. Preserve relevant existing framing and root
handling.

Provide verification through a runtime descriptor as well as typed entry
points: `ZfbTest` currently calls `Verify(schema, root, data, size)` and then
uses `GetAnyRoot` and reflected field reads. Checking an envelope's `[ubyte]`
field does not verify the FlatBuffer stored inside it; cover external nested
rows or saga payloads against their selected descriptor before loading them.
Do not repeat verification already performed for that nested payload, or
add verification to locally generated nested payloads.
Keep structural verification distinct from application checks such as allowed
telemetry tags, request groups, and required nonempty publisher IDs.

### Runtime reflection

Treat reflection as a first-class `ZfFB` capability and a critical migration
workstream. `ZfFB` must provide compile-time-generated reflection capabilities
equivalent to those currently used by `zdb` and every other FlatBuffers
reflection dependent. A working typed codec alone does not satisfy this goal.

Generate reflection descriptors and operations at compile time from the same
`ZfStruct` metadata and resolved wire traits used by the typed codec. Expose
them for runtime use, including by dynamically loaded stores that do not know
the concrete C++ row type. Preserve canonical BFBS binary schema reflection:
compile-time generators specialize streaming code templated on destination
`S`, which implements `operator <<` (Q12). The normal reflection path uses
a buffer-backed destination and passes its completed `ZuBSpan` directly to
the consumer. Save operations follow this existing Z convention, accepting
`ZiTxStream`, `ZiFileTxStream`, `std::cout`, `ZuStream`, or other compatible
stream types. Keep `zi` dependencies above `zf`; do not require destinations
to provide reserve, seeking, mutable storage, or a builder API. Only buffer
destinations need to expose a span. Stream exact bytes and lengths, including
embedded zeros, using established Z span/printing facilities.
Resolve schema-generation decisions at compile time, without runtime metadata
branching, graph discovery, or interpreted descriptor traversal during binary
schema emission. Buffer capacity, error handling, byte-copy primitives, and
anything relating to actually streaming the data are exempt, including within
the emission call path (resolved Q2 in `plan.md`). This does not require a
prebuilt static byte blob or a
constant-evaluated output buffer. Do not require external codegen or a second
independently maintained description.

Emit BFBS in output order using its compile-time-known layout. Honor the
destination's existing allocation, capacity, error, and streaming behavior.
Payload encoding may retain internal builder storage where necessary, but
must preserve direct pooled-buffer output without imposing extra copies;
the public save destination still uses the templated `S << ...` contract.

Canonical schema verifiers, accessors, and reflection must accept and use
the emitted BFBS; native `ZfFB` schema views must read canonical-produced
BFBS. The normal reflection path never persists a schema file. Each `Table`
generates its BFBS buffer during construction and owns that buffer for its
lifetime (Q13). Stores and reflection consumers borrow spans/views from it;
complete their operations, callbacks, and close before destroying the table
and releasing its buffer. Each table owns its own buffer, with no shared
schema-buffer cache, deferred generation at store open, or per-row generation.
Runtime access/verification of input data remains
data-dependent; the no-runtime-branching requirement applies to the binary
schema generator's metadata decisions.

Audit all reflection consumers before finalizing the reflection API. Record
each capability used, its callers, its `ZfFB` replacement, and how equivalence
will be verified. Include uses through wrappers and generic FlatBuffers
table/vector operations, not just explicit `reflection::` references.

The capability inventory must cover, wherever currently used:

- Root and nested type discovery, field enumeration, and field lookup by
  name and stable wire ID.
- Scalar kinds, widths, signedness, wire defaults, and presence semantics.
- Table versus inline struct representation, inline size/alignment/member
  offsets, and table field vtable slots.
- Vector element kinds and layouts, nested object references, enums, union
  alternatives, and discriminator/value relationships.
- Type-erased field access to serialized buffers, including strings,
  vectors, inline structs, nested tables, and unions.
- Dynamic construction of serialized rows and partial records using
  reflected wire types, IDs, defaults, and nested values.
- Associations with `ZfVField`, keys, update/delete projections, and custom
  Z wire transformations used by store adapters.

Use compact descriptors directly associated with the corresponding
`ZfVField`, and generate access/build operations where needed. Define stable
descriptor lifetimes and type identity across module boundaries, without
assuming separately instantiated descriptor addresses are equal across DLLs.
Keep all
descriptor relationships usable without the concrete application type.
Keep the descriptor contract in `zf`; higher-layer transformations supply
their metadata and operations through its extension mechanism.

Replace `reflection::Schema` and `reflection::Field` in `ZdbStore` and the
memory, PostgreSQL, and SQLite stores with native `ZfFB` schema views over
the emitted BFBS spans and generated field associations. Remove the current
name-based join between Z field metadata and independently generated
FlatBuffers metadata. Preserve access to the interoperable binary schema;
native companion descriptors must not replace it with only a private format.
Support dynamic reading and writing without canonical library classes. The
scope is full equivalence for capabilities actually consumed by the repository,
rather than wholesale reproduction of unused FlatBuffers tooling.

Give `zdb`, `zdb_pq`, and `zdb_sqlite` dedicated migration and verification
coverage. Exercise row decoding and construction, default handling, keys and
indexes, partial update/delete records, and supported nested/vector/UDT
representations through their runtime reflection paths. Audit other modules
for reflection use and apply the same capability mapping and verification.

Preserve backend-specific contracts, not just generic round trips:

- Memory store: typed value dispatch, borrowed string/byte values where
  supported, key-group prefixes, descending comparisons, full-row tuples
  versus selected keys, and filtered update/delete tuples.
- PostgreSQL: zero-copy borrowing of scalar text/byte parameters from the
  source buffer, binary endian/array/bitmap/IP transformations, OID mapping,
  and reverse construction before `PGresult` storage expires. Keep source
  and scratch storage alive for the actual query-send lifecycle.
- SQLite: `SQLITE_STATIC` bindings into source/scratch spans, explicit empty
  text/blob handling distinct from SQL NULL, ordered numeric/blob encodings,
  vector element lengths, and reverse construction before statement storage
  is reset. Preserve statement guard ordering and source-buffer lifetime.

Record supported wire-to-store mappings per backend; do not assume each
backend currently supports every codec type. Preserve logical field names,
SQL names and type identities when eliminating name-based metadata joins.
Expose union discriminator metadata instead of scattering the current
`field.offset() - 2` convention through store code. Keep field IDs, vtable
byte positions, inline byte offsets, and projected tuple indexes distinct.

## Dependent migration

Inventory and migrate all generated API and schema dependencies, including
`zfb`, `zdb`, `zdb_pq`, `zdb_sqlite`, `ztc`, `zum`, `zdf`, and `zdash`, plus
their tests, examples, utilities, and build rules where applicable.

Introduce metadata declarations for protocol messages currently expressed
only through schemas and generated builders. Preserve borrowed message views
and in-place mutation used by telemetry and message forwarding.

Migrate all consumers to `ZfFB` and remove `zfb` entirely, including its source
and test directories, installed headers and library, module traversal,
configure entries, package metadata, and dependent include/link references.
Move useful tests to the appropriate `zf` or higher-layer test directories.
Remove obsolete generated interfaces, schema bindings, schema files,
`scripts/fbscodegen`, and FlatBuffers configure/link requirements once their
dependents have migrated. Do not retain forwarding APIs solely for source
compatibility.

Provide `ZfFB` library functions for binary BFBS and textual FBS schema
output, templated on generic `typename S` streams using `operator <<`
(Q12/Q14). Each application compiles in its own `ZfStruct` metadata and
selects the root type/facet at the C++ call site. Do not create standalone
command-line exporters or a fixed application schema catalog in `zf`.

The binary function emits canonical BFBS; an existing generated span can
simply be written without conversion or re-verification. The textual
function renders the same schema metadata as one self-contained schema per
selected root, including reachable types and namespaces. A caller-supplied
file stream can persist the output as `.bfbs` or `.fbs`; any other compatible
stream is equally valid. Application metadata remains above `zf`. Test these
functions through application/test instantiations in the interop harness.
Conversion of arbitrary external BFBS to textual FBS is not required (Q4).
Exported files are derived artifacts, not independently maintained sources
or prerequisites for normal Z builds or reflection consumers.

Update current architecture/build documentation (`README.md`, `CODEBASE.md`,
and affected module documentation) to describe `ZfFB` and the removed module.
Regenerate Autotools build products through the normal build tooling rather
than hand editing `Makefile.in`. Remove stale externally generated FlatBuffers
headers and old embedded `.bfbs` artifacts along with their generation rules.
Replace binary schema artifacts with BFBS spans streamed in memory by `ZfFB`.

## Canonical interoperability

Interoperability is a required release gate, even though canonical tooling
is confined to a separate interoperability build and is absent from normal
production builds. Align the interoperability baseline with the current
`Zfb` FlatBuffers dependency (Q15): canonical C++ headers/library and matching
`flatc`, with the existing `flatbuffers >= 24` requirement. The inspected
workspace uses 25.12.19. Record actual versions with fixtures and results;
this observation does not introduce a new hard-coded version pin. Exercise
the complete supported feature set with that canonical C++ implementation;
no separate version matrix or second-language acceptance gate is required.

- Write with `ZfFB`, verify and read with canonical implementations; write
  canonically, verify and read with typed and reflected `ZfFB` paths.
- Pass BFBS spans emitted by `ZfFB` directly to canonical schema verification
  and reflection over matching payloads. Read canonical BFBS spans with native
  schema views. Check both schema-output functions: canonical tooling must accept binary
  export and compile textual export into a semantically equivalent schema.
- Cover alternate physical field order, omitted/default-valued fields,
  absent versus empty offsets, shared vtables on either side of a table,
  unknown fields, schema evolution, unions with repeated payload types,
  all used scalar/struct/vector layouts, and nested byte-vector buffers.
- Extract nested buffers and verify them independently, including copies
  between envelopes and builders with prior content and vtable reuse.
- Include interoperability for forced-present mutable scalars and filtered
  key/update/delete records, alongside application framing tests.
- Compare semantics and structural validity, not byte identity; retain
  independently sourced canonical fixtures to catch matching mistakes in
  the new writer and reader.
- Exercise malformed/truncated inputs and arithmetic/traversal boundaries
  without requiring identical rejection policies for every unused feature.

Generate schemas for the canonical harness from the authoritative metadata
where useful; keep independent fixtures or contract checks for existing
schemas so a shared metadata/export error cannot validate itself. Such
interop fixtures are test oracles, not a second production schema source.

## Consumer verification coverage

Follow the later build instructions recorded in `plan.md`: retain the current
Clang debug configuration and artifacts, batch changes, and rebuild
incrementally at the prescribed milestones: `make -j8` before Zum and
`make -j4` for Zum and beyond (including top-level traversal). The four-job
limit prevents memory exhaustion; do not exceed it with overlapping builds.
Use `z.config`
for autoreconf and reconfiguration. ASan with `make clean` is an authorized
exception, not a required additional configuration. Valgrind is required as
the final leak/corruption gate and may also be used for needed debugging;
do not run it on ASan binaries. Keep the code suitable for GCC and
MinGW/Windows, but their builds/tests and other configurations are out of
scope, apart from the explicitly required Clang release performance build.
Do not claim unexecuted configurations passed or treat these explicitly
excluded builds/tests as outstanding requirements.

Carry forward the existing meaningful checks and add coverage for the
audited contracts that currently lack assertions:

- `zfb/test/ZfbTest*.cc`: reflection-driven verification/read, nested/custom
  vectors, empty property lists, constructor/allocation paths, key views,
  reserved headroom, growth, detach/reuse, and exception cleanup. Add actual
  direct-view structured printing and JSON assertions.
- `ztc/test/ZtcSchemaTest.cc`, app/alert/hub/filter tests, `ztc/itest` wire
  and load tests, and `zdash/test/ZdashTest.cc`: schema order and defaults,
  presence, synthetic fields, keys, semantic verification, mutable sequence
  slots, routing, stream transmission, and file replay.
- `zdb/test`, `zdb/itest`, and saga examples: buffer-cache keys, query key
  projections, update/delete records, nested replication/recovery payloads,
  saga placement construction and replay, and span-based diagnostic output.
- PostgreSQL and SQLite unit/integration paths, including
  `zdb_sqlite/itest/zdbsltypes.hh`: every supported store mapping, signed and
  unsigned extremes, defaults, empty strings/blobs/vectors, inline-struct
  vectors, IP variants where supported, selected keys and rows, and borrowed
  input/result lifetimes. Establish the actual coverage per backend.
- `zum/test/ZumTest.cc`, maintenance saga paths and telemetry probes, and
  `zdf` tests: application row and custom-vector round trips, retained saga
  images, and byte-backed series blocks.

Add canonical tests under the appropriate `interop` directory and document
their invocation. A disabled PostgreSQL/SQLite/GTK feature does not count as
verified; exercise affected optional configurations or report the specific
unverified configuration. Demonstrate that `zf` and its codec have no build,
link, or runtime dependency on `zi` or canonical FlatBuffers, as well as
testing the higher-layer adapters in enabled dependent builds. Under the
later build scope, use the retained build's compile/link/dependency evidence
and installation-rule audit; do not add a second clean build or uninstall
dependencies to establish this isolation.

## Performance evaluation

Run performance acceptance only in release builds, as required by the latest
user instruction. Native and canonical comparisons must use matching release
flags, storage tuning and workloads. Debug builds serve correctness and memory
checks; their timings neither establish release regressions nor satisfy this
gate. Use `z.config` for the transition and clean incompatible objects before
rebuilding. This release requirement overrides earlier restrictions against
changing the retained debug configuration for performance measurements.

Compare against the existing `Zfb::IOBuilder` with equivalent verification,
default omission, and buffer allocation policies. Measure serialization,
borrowed field access, object construction, and relevant store operations.
Include representative rows, nested messages, strings, vectors, and unions.

Performance must not regress (Q16). The baseline is the custom pooled
`Zfb::IOBuilder`, including its allocation, growth, ownership-transfer, and
alignment behavior. Record those mechanics as well as latency; do not replace
the comparison with a generic heap-backed builder. Reproduce the existing
direct-buffer path without extra staging allocations or copies. Dependency
removal does not justify a performance regression.

Prescribe allocation, growth, ownership transfer, downward writing, and
alignment explicitly as implementation constraints. Derive them from the
canonical wire-layout requirements and the existing buffer/performance
contract; do not leave them as open requirements or discretionary design
choices. Their explicit treatment prevents implementation pitfalls.

Report latency distributions, allocations, bytes copied, encoded size, and
any material code-size or compile-time effects.

Evaluate these opportunities:

- Compile-time table layout and vtable construction.
- Reserving table storage once to reduce per-field builder bookkeeping.
- Reusing a vtable lookup when accessing several fields.
- Eliminating runtime matching between independent reflection descriptions.
- Specializing serialization filters and common wire representations.

Treat forward writing with offset patching, exact sizing passes, and vtable
deduplication as measured tradeoffs. Less writer work may increase message
size or traversal cost. Generated scalar accessors are already inexpensive;
removing generated headers alone is not evidence of faster reads.

## Execution sequence

1. Inventory existing wire contracts and consumers as capability references,
   without requiring legacy compatibility. Define the metadata
   contract, including numbering, defaults, presence, and runtime descriptors.
   Complete the reflection capability inventory for `zdb` and all other
   dependents before fixing the reflection API.
2. Implement a representative `ZfFB` codec slice in `zf`, plus higher-layer
   pooled-buffer and IP adapters, covering a row, an IP union, and a nested
   message with vectors. Include compile-time-generated reflection and a
   representative type-erased store read/write path in this first slice.
3. Verify interoperability in both directions against the canonical
   implementation. Include malformed input, missing fields, defaults,
   schema evolution, shared vtables, inline layouts, and mutation behavior.
   Include nested-buffer isolation, runtime descriptor verification, direct
   borrowed-view printing/JSON, and canonical fixtures. Compare decoded
   semantics rather than requiring identical byte layouts.
4. Benchmark the representative slice against the existing implementation
   and use the evidence to choose writer and access strategies.
5. Complete wire-type coverage, migrate store reflection and dependent
   messages, and propagate API changes throughout the repository.
6. Delete `zfb` entirely and remove production FlatBuffers dependencies and
   codegen, then complete the affected builds/tests in the retained Clang
   debug configuration and the final Valgrind leak/corruption gate. The
   permitted ASan/clean exception does not add a required configuration or
   replace that final memory gate; use Valgrind earlier only when debugging
   memory corruption requires it.
7. After debug correctness/memory checks, use `z.config` to switch to Clang
   release, clean incompatible objects and rebuild. Run performance acceptance
   exclusively with matching release implementations; debug timings are not
   a substitute.

## Acceptance criteria

- `ZfStruct` metadata is the single authoritative declaration for supported
  object mappings and FlatBuffers wire contracts.
- `ZfStruct` is the sole object declaration API. No `ZfbStruct`, related
  `Zfb*` declaration/binding APIs, or renamed serialization-specific struct
  declaration macros remain.
- `ZfFB` resides in `zf`, with no dependency on `zi`. Pooled I/O buffer and
  IP integrations reside above `zf` and retain direct buffer construction.
- The `zfb` module, library, headers, and build dependencies are deleted
  entirely; all consumers use the new implementation.
- Production builds and runtime operation require neither canonical
  FlatBuffers libraries nor `flatc` or generated FlatBuffers headers.
- All dependent serialization, reflection, partial records, borrowed views,
  and required in-place mutations work through the new implementation.
- Buffer spans can be read, indexed, re-serialized, and formatted to streams
  directly; binary transmit/file output preserves existing ownership and
  framing. Nested FlatBuffers remain independently canonical-readable.
- Compile-time-generated `ZfFB` reflection provides every capability in the
  audited dependent inventory. Equivalence is demonstrated through runtime
  consumer paths, including the memory, PostgreSQL, and SQLite stores;
  typed codec tests alone are insufficient.
- Compile-time-specialized schema generators stream canonical-compatible
  BFBS into buffers without runtime metadata branching. Consumers receive
  borrowed binary spans with correct ownership, without file persistence.
  Both binary BFBS and textual FBS library functions stream to generic `S`
  destinations from caller-compiled metadata and pass canonical
  interoperability checks.
- Canonical interoperability and any explicitly retained historical data
  compatibility are demonstrated. The separate canonical interoperability
  suite is mandatory for acceptance; canonical tooling is a test dependency
  only and is not required to build or run production code.
- Untrusted input verification is tested independently from successful
  round trips, including arithmetic and traversal boundary cases.
- Performance results establish the actual gains and tradeoffs against
  the existing pooled-buffer implementation; material regressions are
  resolved, with allocation, growth, ownership transfer, and alignment
  conforming to the prescribed implementation constraints.
- Affected builds and required checks pass, with no obsolete compatibility
  wrappers or independently maintained duplicate production schemas left
  behind. Audit every inventoried use to a replacement or justified deletion.

## References

- `zf/src/ZfStruct.hh`
- `zfb/src/ZfbStruct.hh`
- `zfb/src/Zfb.hh`
- `zdb/src/ZdbStore.hh`
- `zdb/src/ZdbMemStore.hh`
- `zdb_pq/src/ZdbPQ.hh` and `zdb_pq/src/ZdbPQ.cc`
- `zdb_sqlite/src/ZdbSL.hh` and `zdb_sqlite/src/ZdbSL.cc`
- [FlatBuffers internals](https://flatbuffers.dev/internals/)
- [FlatBuffers schema](https://flatbuffers.dev/schema/)
- [FlatBuffers evolution](https://flatbuffers.dev/evolution/)
- [Canonical builder](https://github.com/google/flatbuffers/blob/master/include/flatbuffers/flatbuffer_builder.h)
