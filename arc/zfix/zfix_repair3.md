# Simplify FIX message dispatch

## Objective and scope

Replace `ZfixDispatch`'s combined-schema parsing and position-tree projection
with a two-stage selection model: look up MsgType, optionally look up one
unique discriminator field, then decode the selected application type normally.

This repair targets mainstream order, execution-report and market-data FIX
messages using SOH-delimited tag/value fields. Binary Data and Length/Data
handling are out of scope for discriminator inspection. Do not introduce a
binary-aware scanner or retain schema merging to support binary selectors.
This is a task scope decision, not a claim that FIX has no binary fields.

Retain typed application callbacks, explicit message catalogs, facets, selected
object decoding, and ordinary repeating-group support. Different application
objects may still share MsgType; selection does not require their layouts to be
combined. Do not require one C++ object type per MsgType.

Implement against the current working tree. There are extensive existing zfix
changes; preserve unrelated work. This document is a plan, not authorization to
rewrite session, persistence, transport, or codec facilities unrelated to dispatch.

## Contract

- The wire prefix is `8=BeginString<SOH>9=BodyLength<SOH>35=MsgType<SOH>`:
  MsgType (35) must be the third field, immediately after BodyLength (9),
  not an arbitrarily located body field. The FIX session specification treats
  another ordering as a garbled message. See the
  [FIX Transport 1.1 specification](https://www.fixtrading.org/wp-content/uploads/download-manager-files/FIX_Transport_1.1.pdf),
  "What constitutes a garbled message".
- Framing reads and validates this fixed field sequence and exposes MsgType in
  `FrameView::msgType`. This is already implemented in `ZfixFrame.cc`; preserve
  it. Dispatch uses that value directly, without searching the content for tag
  35 or consulting candidate schemas to discover it. `FrameView::content` starts
  after the leading MsgType field.
- MsgType (35) selects either one entry or a secondary selector definition.
- Entries sharing MsgType use one discriminator tag and disjoint values, as
  already enforced by `MTList`.
- The discriminator is a unique, non-repeating root field. It cannot also be a
  field inside a repeating group. In particular, ExecType (150) in an
  ExecutionReport is not a nested party or sub-party field.
- Selection is by the exact discriminator value. Do not trial-decode candidates
  or depend on catalog declaration order.
- Scan actual tag/value fields, not substrings. A value containing `150=F` is
  not itself tag 150.
- Missing selector returns `Required`; an unknown selector value returns
  `Unresolved`; duplicate selector occurrences return `Duplicate`. No callback
  occurs on selection or selected-type decoding failure.
- Empty catalogs and unknown MsgTypes retain their existing unresolved behavior.
- Callback values retain the existing borrowing and ownership conventions.
- Frame validity remains the framing layer's responsibility. Selected field
  types, required fields, group counts and group ordering remain the ordinary
  selected-schema codec's responsibility.
- Keep frame/field bounds and wire-relative error locations. Selector inspection
  needs no position tree, group-entry allocation or parseBytes charge for one.

## Why the current machinery can go

Selection needs only the two-stage lookup described above.
`ZfixDispatch` currently adds recursive source-schema merging, synthetic scanning fields,
combined position storage, selected-schema projection, and a special direct path
to avoid projection for identical source layouts.

The tests that place tag 150 both at the root and in nested groups model an
invalid FIX message definition. They must not be used to justify group-aware
discriminator selection. With a unique discriminator, group membership is
irrelevant to selection: repeated group fields can be scanned and ignored until
the discriminator is encountered.

The intended runtime flow is:

```text
validated FrameView
  -> MsgType index
  -> no discriminator: ordinary selected MessageHandler
  -> discriminator: flat field scan -> secondary value index
       -> ordinary selected MessageHandler
  -> typed callback
```

The selected handler reparses the content. Accept that explicit tradeoff to
remove the combined tree and projection; do not claim a latency improvement
without measurement.

## Implementation sequence

### 1. Introduce one small selector inspector

Use the existing bounded tag/value scanner where its contract fits. Inspect
`FrameView::content` or an encoded body as a borrowed span and return only the
selector's offset and length plus `Error`. Prefer a small non-template routine
in the codec implementation, with a runtime selector tag, over duplicating the
scan for every catalog entry.

Scan the complete relevant span to detect duplicate selector tags even when the
first occurrence already selects an entry. Check scanner progress, syntax and
the applicable existing byte limits. Do not build a flat hash of every field:
the lookup needs one tag, so a single saved position is sufficient. Do not track
group scopes, decode scalar business values, construct candidate objects, or
allocate storage. Required-field errors have no fabricated byte offset; actual
field errors are converted to wire-relative offsets at the frame boundary.

Keep inspection and selection separate: the inspector locates the unique field;
the catalog index maps its value to an entry. Neither performs object decoding.

### 2. Reduce dispatch to lookup and ordinary decode

Retain the small primary/secondary indexes and compile-time callback dispatch.
For a selected entry, always use the ordinary
`MessageHandler<Entry::Object, Entry::Mapping>` path currently used for entries
without a discriminator.

Delete these facilities once their dependents have been migrated:

- synthetic `Scope`, `Array`, `Leaf`, `Layout`, `Field`, `Fields`, `Key` and
  `KeyField` scanning types and their schema specializations;
- `RawLeaves`, tag-offset calculation and recursive combined `Wire` generation;
- `project()`, projected-position decoding and `decodeParsed()`;
- the `Direct`/combined-layout split and its second live position-tree budget;
- includes and internal metadata used only by those mechanisms.

Keep concise catalog traits only where they have a surviving consumer. Do not
retain deleted types as compatibility aliases or forwarding interfaces.

### 3. Migrate dependent uses before deleting shared traits

Search all zfix source, test, utility, example, benchmark and interop directories
for `Dispatch_` uses and selector-specific codec hooks.

- **Tx body encoding and restore (`ZfixTxMsg.hh`):** replace synthetic body/frame
  scans with the same flat selector inspector. Preserve matching of modeled
  selectors, existing insertion of unmodeled body selectors, exact archive-byte
  retention, and checks performed after envelope-owned fields become available.
  Keep ordinary restore validation; do not decode alternatives.
- **Header inspection (`catalogHeader`):** retain header-prefix extraction as a
  separate concern. Where prefix termination needs catalog body-tag knowledge,
  consult ordinary candidate schemas' root tag indexes or a small tag-only
  predicate. Do not synthesize fields, merge groups, or create position trees
  for candidate bodies. Preserve the existing treatment of header extensions
  and unknown MsgTypes unless a concrete defect requires a separate change.
- **Trace metadata (`ZfixTrace.hh`):** keep its existing per-schema metadata
  inspection. Retain or relocate only the small entry/source traits it needs;
  remove its dependence on combined dispatch schemas. Binary trace behavior is
  not part of this repair.
- **Codec selector hooks (`ZfixFmt.hh`):** after migrating consumers, remove
  root-selector capture state and template parameters that exist only for the
  old dispatch/Tx mechanism. Keep ordinary scope, group and envelope parsing.

Do not delete existing binary codec, framing or tracing support merely because
binary discriminator inspection is out of scope.

### 4. Make the catalog assumptions explicit

Document the unique root discriminator contract next to `Discrim` and `MTList`.
Retain existing MsgType/tag/value consistency assertions. Where schema metadata
can prove a discriminator is defined inside a candidate's repeating group,
reject that catalog at compile time. This check must not generate a runtime
merged schema or add per-message work.

Retain support for selectors omitted from the business object's fields: the
flat inspector can still select the entry, and ordinary decoding can ignore
that extension field. Do not add arbitrary selector restrictions unrelated to
the agreed contract.

### 5. Replace tests of unsupported structures

Revise `ZfixDispatchTest` and affected schema/selector/Tx tests around valid FIX
definitions. Keep nested groups, but use distinct field tags rather than
reusing the root discriminator inside them.

Cover the following behaviors with TAP tests:

- unique MsgType dispatch and empty/unknown catalog cases;
- framing acceptance of the required 8/9/35 prefix and rejection of misplaced
  MsgType, retaining existing coverage where present;
- shared MsgType with the same object/facet and with different objects/facets;
- discriminator before and after ordinary fields and valid repeating groups;
- missing, unknown and duplicated discriminators;
- discriminator-like text inside an ordinary field value;
- selected required/value/group errors, with no callback;
- unselected business-field types do not get decoded;
- callback entry identity, refusal errors and borrowed-value lifetime;
- frame/field bounds and correct error offsets;
- Tx selector matching/insertion and exact-byte restore;
- header-prefix and trace consumers after internal trait removal;
- compile-time rejection of a discriminator declared inside a group.

Remove tests requiring binary/Length selectors or group-scoped duplicates of
root selectors. Remove assertions tied to combined-plus-projected position
capacity. Keep ordinary selected-codec memory/depth/count limit tests and
standalone binary codec tests outside this dispatch repair.

### 6. Build, verify and measure

Build with the current configured toolchain using `make -j8`, rebuilding `src`
before dependent tests. Build default targets before invoking their test
targets. Run the affected codec, dispatch, selector, schema, Tx, trace and link
tests, then the zfix unit suite and relevant session/ingress/restore integration
tests. Use existing test entry points and `libtool exec` for source-tree programs
under debugging tools. Run the repository-required sanitizer/leak checks in the
appropriate configuration; report unavailable checks explicitly.

Use the existing benchmark/load facilities to compare unique-MsgType and
secondary-selector traffic, including realistic execution reports with groups.
Record toolchain/build configuration, message sizes, allocation behavior and
latency before/after. The discriminator scan must allocate nothing; selected
object/group allocations remain codec/application behavior. A second content
scan may affect latency, so report the measured tradeoff without recreating
projection as an unmeasured optimization.

## Completion criteria

- Dispatch consists of MsgType lookup, optional unique-field inspection,
  secondary lookup, and one ordinary selected-type decode.
- Selection has no candidate position tree, recursive schema union, projection,
  speculative object decoding, or selector-specific allocation.
- Typed callbacks, facets, valid repeating groups, Tx/restore identity checks,
  header inspection and trace consumers work with the simplified internals.
- Tests reflect valid discriminator placement and the agreed non-binary scope.
- Documentation describes the supported selection model and the measured
  scanning tradeoff. All relevant checks pass, with limitations recorded.

## Execution record — 2026-10-31

Implemented unique-field inspection followed by ordinary selected-type decoding.
Removed combined schemas, position projection, the same-schema shortcut and
codec selector-capture hooks. Migrated Tx/restore checks, catalog header
inspection and trace dependencies; retained ordinary group and binary codecs.
Catalog checks and fixtures now enforce unique root discriminators. The process
group fixture uses text payloads within the agreed scope.

`make -C zfix -j8` passed without warnings. The final ASan/LSan unit run passed
29 binaries/351 TAP scopes and the compile-failure fixtures. The final TCP/TLS,
demo and native-peer integration runs passed. Auxiliary dispatch/codec/group/frame
Valgrind consumers reported zero errors and zero definite/indirect leaks, using
current codec sources with existing staged unsanitized foundation libraries.

The required top-level build was attempted and completed Zfix, then failed in
unrelated ZhttpTLS/Ztls code involving private `m_closing` access and a `bool`
where an atomic interface was expected. Those modules were left unchanged.

The instrumented comparison shows the explicit tradeoff: same-schema secondary
dispatch has approximately 10–15% higher median latency in the measured fixtures,
while different-schema dispatch has approximately 6–9% lower median latency.
The inspector showed no allocation attempts in the separate observer fixture.
See [the verification report](zfix/bench/REPAIR3.md) and its raw observations for
commands, configuration, methodology and limits. These are not release numbers.
