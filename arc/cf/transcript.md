# `ZfYAML` corrective-work transcript

This transcript begins when the `yaml.md` planning goal was complete. User
prompts are reproduced verbatim. Assistant entries record the resulting
engineering work and conclusions; repetitive compiler and TAP output is
summarized rather than copied in full.

---

**Assistant — goal completion**

Completed `yaml.md` as an implementation prescription for `ZfYAML` and
`ZvYAML`, based on the preceding `ZfTree` precursor plan. The completed plan
specified a shared mutable `ZfTree::AnyNode`, a YAML parser and direct YAML
emitter, `resolve()` and `flatten()` schema transforms, `ZvYAML` file loading,
mainstream JSON-interoperable YAML behavior, aliases with unique-tree
expansion, and focused build/test integration.

---

**User**

> `ZfTreeLoad.hh`:
> - `ScalarMask` should use ZtFlags

**Assistant — corrective work**

Changed `ZfTreeLoad::ScalarMask` to the framework `ZtFlags` representation and
propagated its typed flag value through the shared scalar-admission API. The Cf
and YAML policies were kept structurally parallel, with YAML distinguishing
string, number, boolean, and null scalar kinds while Cf continued to treat its
owned tokens as strings.

---

**User**

> audit the YAML implementation for alignment with `GUIDELINES.md`, checking all audit flags and repairing as needed

**Assistant — audit and repairs**

Read `GUIDELINES.md` and audited the YAML implementation against every red and
amber flag. Repairs included:

- Removed anonymous namespaces, typed enums, non-specific lambda captures,
  unnecessary casts, STL use, and other disallowed local idioms.
- Kept parser helper names concise and file-local in `ZfYAML.cc`.
- Factored repeated UTF-8, cloning, traversal, scalar-loading, and conversion
  work rather than copying Cf or JSON implementations.
- Removed hidden string/byte copies, temporary contiguous conversions,
  invariant calls in loop conditions, redundant parent walks, dead paths, and
  unnecessary initialization.
- Gave every dynamic Z container and retained string allocation an explicit,
  tunable heap ID.
- Reordered structs for layout and reduced the iterative flatten frame from
  approximately 528 bytes to 80 bytes, verified with layout inspection.
- Kept `resolve()` and `flatten()` iterative with named `ZtArray` heaps and
  mutable `ZuPtr<AnyNode>` ownership. Parent links are repaired after cloning,
  reference expansion, replacement, and merging.
- Preserved alias limits, alias-depth limits, node limits, unique ownership,
  duplicate-anchor behavior, cyclic-alias rejection, and deterministic cleanup.
- Kept `ZfTreeLoad.hh` format-neutral: Cf and YAML supply policies for scalar
  admission, number parsing, booleans, diagnostics, and nested handlers.
- Audited struct padding, fixed arrays/tables, container traversal, reference
  ownership, allocation paths, blocking/polling, logging captures, framework
  capability reuse, and API naming. No concurrency or I/O-sharding mechanisms
  were introduced by the parser.

The test suite was expanded to cover block and flow collections, quoting,
Unicode, comments, document markers, aliases and limits, invalid syntax,
handler construction/load/update, schema resolution and flattening, and golden
transform output.

Verification at this stage included warning-clean Clang builds, direct GCC
compilation, focused `ZfYAMLTest`, `ZfCfTest`, `ZfTreeTest`, and `ZvYAMLTest`
runs, and a top-level build. A full repository `make test` exposed unrelated Zi
network failures/hangs. Valgrind could not start because the installed stripped
dynamic loader lacked Valgrind's required `memcmp` redirection symbol. ASAN ran
the YAML tests without sanitizer findings; leak-sanitizer itself was unavailable
under the ptrace environment.

---

**User**

> ensure that `ZfYAML` does not perform unnecessary handling of non-decimal integers. unless YAML specifies to the contrary, align with `ZfCf` for both output formatting and default input formatting of numbers, but if YAML permits literal prefixes such as `0x` to override, accept that.

**Assistant — corrective work**

Centralized Cf-style integer parsing in `ZfTreeLoad::intEOV()`. `CfPolicy`
delegates directly to it, preserving Cf behavior. `YAMLPolicy` uses the same
field-property-selected default parser unless an unquoted YAML number has a
valid YAML integer spelling that explicitly establishes its radix.

This avoided a general-purpose non-decimal parsing path for every integer and
kept `ZuFieldProp` responsible for default input and output formatting.

---

**User**

> support binary/implicit-octal/sexagesimal forms on input; `ZuFieldProp` should control output and establish a default for input, but valid YAML supersedes

**Assistant — corrective work**

Implemented allocation-free YAML integer recognition and conversion for:

- decimal, including leading `+` and `_` separators;
- binary `0b`;
- YAML 1.2 explicit octal `0o`;
- YAML 1.1 leading-zero implicit octal;
- hexadecimal `0x`;
- YAML 1.1 sexagesimal groups.

Valid YAML syntax now overrides the field property's default input radix only
for an unquoted node classified as a YAML number. Quoted strings continue to
use the field-property parser. Output remains entirely controlled by the field
properties and does not preserve the input spelling.

Added sentinel-aware range and overflow checks using the boxed type's
`Cmp::minimum()` and `Cmp::maximum()`. Corrected the signed-minimum path to
avoid negation overflow. Schema-array equality used by `flatten()` was also
updated so equivalent spellings such as hexadecimal and decimal compare by
numeric value rather than by a decimal-only conversion.

Tests cover valid and invalid spellings, signed prefixes, underscores,
sexagesimal groups, overflow/error paths, field-property defaults, YAML radix
override, and arrays containing mixed YAML integer forms. Cf regression tests
confirmed unchanged behavior.

---

**User**

> how does `ZfYAML` re-use `ZfJSON`'s object emitter? JSON should NOT be the default output (it should be YAML). Of course, if UDT metadata specifies "as JSON", then JSON is fine (`ZfURI` has this capability)

**Assistant — assessment and correction**

Determined that default object/array emission must not delegate to the JSON
emitter. Implemented separate, direct-to-stream YAML collection handlers:

- mappings are block mappings with one field per line;
- sequences use block `-` entries;
- nested collections use two-space indentation;
- empty mappings and sequences use `{}` and `[]`;
- collection output does not build an intermediate node tree or string.

Only individual scalar quoting and established field-property-controlled scalar
formatting are reused where their syntax is valid YAML. Added `ZfYAML::AsJSON`,
selected through `ZfYAML_Fmt(T *)`, as the explicit whole-UDT JSON escape hatch,
matching the role of `ZfURI::AsJSON`. Tests assert that an ordinary reflected
UDT emits YAML while an explicitly tagged UDT emits JSON.

Updated `yaml.md` to prohibit default object or array delegation to `ZfJSON`.

---

**User**

> `ZfCf` can only do that because it's format mimics JSON. However there are key differences: keys are unquoted (for example). But that is just the `ZfCf` format. It is not YAML.

**Assistant — assessment and correction**

Treated Cf, JSON, and YAML structural syntax as independent formats. Confirmed
that the YAML emitter had its own block mapping/sequence path and did not reuse
the Cf or JSON object emitter.

Changed YAML keys from always-double-quoted output to YAML-aware emission:
mainstream-safe and non-ambiguous keys are plain, while keys that could be
interpreted as YAML 1.1 booleans/nulls or contain unsafe syntax are quoted.
Thus ordinary output now resembles conventional YAML without copying Cf's bare
token rules.

Replaced calls to JSON's internal `saveValue_` helper with its public scalar
formatting entry point. Added exact-output and reparse tests for block YAML,
plain keys, ambiguous quoted keys, nested sequences, and explicit JSON output.
Updated `yaml.md` to state explicitly that neither the JSON nor Cf collection
emitter is used.

---

**User**

> for scalars, YAML-specific field property should (`ScalarFmt`) should support the following enum values:
> `Plain` (default): unquoted scalar, or whatever the default is for the type of the scalar
> `SingleQuoted`: single-quoted scalar
> `DoubleQuoted`: double-quoted scalar
> `BlockLiteral`: | block syntax, preserves line breaks in multiline text
> `BlockFolded`: > block syntax, multiline text where most line breaks should be folded into spaces

**Assistant — corrective work**

Added YAML-only `ZuFieldProp::YAML::ScalarFmt<I>` with the requested enum values
and shorthand properties:

- `YAML::Plain`;
- `YAML::SingleQuoted`;
- `YAML::DoubleQuoted`;
- `YAML::BlockLiteral`;
- `YAML::BlockFolded`.

`Plain` is the default. Strings are emitted as plain scalars only when the
spelling remains unambiguously a string for mainstream YAML loaders; otherwise
they fall back to double quotes. Single-quoted output doubles apostrophes and
falls back to double quotes for control characters that single-quoted YAML
cannot represent. Double-quoted output uses YAML-compatible JSON escaping.

Block literal and folded output select strip, clip, or keep chomping based on
the value's trailing newlines and indent content relative to its mapping or
sequence position. Literal blocks preserve line breaks; folded blocks expose
YAML's normal folding behavior.

The property also applies deliberately to other scalar field types. Their
existing bytes/number/time formatter first determines the lexical value, after
which `ScalarFmt` applies presentation. Because quoting a YAML boolean or number
makes it a YAML string to schema-oblivious consumers, the typed YAML loader only
accepts that string-form spelling when the corresponding field metadata opts
into a non-plain scalar format. Null sentinels remain unquoted YAML `null`.

Scalar-formatted values use a named `ZfYAML.ScalarBuf` heap only on the explicit
non-plain formatting path; the default direct-stream path remains allocation
free apart from the destination's own growth. Scalar UDT handlers are
distinguished from flow JSON UDT handlers so `ScalarFmt` applies to the former
without accidentally restyling an `AsJSON` object.

Tests cover plain, single-quoted, double-quoted, literal, and folded strings;
apostrophe and newline handling; quoted booleans and integers; typed reload;
and exact canonical output. `yaml.md` was updated with the behavior and the
interoperability caveat.

---

**User**

> as with `ZfJSON`, `ZfYAML` should be able to specify `ID`, `BytesFmt`, etc.

**Assistant — initial interpretation**

Initially began replacing `ZuFieldProp::YAML`'s imported JSON property
vocabulary with separate YAML-owned copies of `ID`, `BytesFmt`, `NumberFmt`,
`TimeFmt`, and `Optional`, and started parameterizing the JSON scalar emitter by
a save policy. This was not retained: the next prompt correctly called for a
closer examination of facet behavior before expanding the property system.

---

**User**

> don't facets already provide per-facet customization of properties? be careful

**Assistant — research, correction, and verification**

Inspected `ZuStructFacet`, `ZuFields<O, Facet>`, `ZuStructRender`,
`ZfStructRender`, and the generated facet field subclasses. Confirmed that
facets are already the isolation mechanism:

- each facet selects a different `ZuFields<O, Facet>` list;
- `ZfStructRender(O, Facet, ...)` derives facet-specific field types;
- each derived field gets its own enriched `Props` list;
- the property type vocabulary may therefore be shared without coupling the
  JSON and YAML configurations.

Reverted the just-started duplicate YAML property types and the unnecessary
`ZfJSON` save-policy refactor. `ZuFieldProp::YAML` intentionally imports the
existing JSON vocabulary, exactly as `ZuFieldProp::Cf` does, so spellings such
as `YAML::ID`, `YAML::Opt`, `YAML::Base64`, `YAML::Number`, and YAML time formats
remain available. `ScalarFmt` remains a distinct YAML property because it has
no JSON counterpart.

Added a dual-facet test whose JSON facet emits `json-value` as a JSON number and
whose YAML facet emits `yaml-value` with YAML-specific double quoting. This
proves that the same property vocabulary yields independent facet metadata and
output. Updated `yaml.md` to prescribe facet-specific `Field::Props` rather
than duplicate format-property types.

Re-ran warning-clean focused Clang builds and the complete YAML and Cf tests;
both passed. Direct GCC compilation accepted the templates. The required
top-level `make -j8` also completed successfully after rebuilding downstream
libraries and tests.

---

**User**

> write a copy of this transcript to `transcript.md`, starting with the goal completion and ending this this prompt, documenting all the corrective work performed subsequent to the completion of `yaml.md`
