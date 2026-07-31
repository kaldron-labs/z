# `ZvCf` replacement and migration plan

## Target state

Delete the existing `ZvCf` implementation in full: its mutable configuration
tree, parser, generic `get`/`set` API, file logic, and tests.  `ZfCf` in
`libZf` replaces its format-layer behavior: parsing spans, tree ownership,
typed loading, validation, and typed saving.  Then create a new, thin `ZvCf`
wrapper around `ZfCf` in `libZv`.  The new wrapper primarily adds file I/O,
implemented with memory-mapped files; it opens and maps files, supplies
`%include`, and passes every mapped span to `ZfCf::scan`.

This split is mandatory because `zf` is below `zi` in the library hierarchy.
`ZfCf` cannot include or link against the `ZiMMapFile` facilities needed for
file I/O.  The new `ZvCf` wrapper is above both libraries and owns that
integration without moving any `zi` dependency into `zf`.

The replacement should expose only the equivalents of:

```c++
namespace ZfCf {
  using PctFnHeapID = ZmFnHeapID<"ZfCf.PctFn">;
  using PctExpandFn = ZmFn<bool(ZuCSpan), PctFnHeapID>;
  using PctFn = ZmFn<bool(
    Scan &, ZuCSpan, ZuSpan<const ZuCSpan>, PctExpandFn), PctFnHeapID>;

  ZfExtern ZuTuple<int, ZuPtr<const AnyNode>> scan(
    ZuCSpan, PctFn = {}, ZmRef<Defines> = new Defines());
}

namespace ZvCf {
  using namespace ZfCf;

  ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> read(
    const Zi::Path &, PctFn = {}, ZmRef<Defines> = new Defines());
}
```

There will be no compatibility class, generic scalar accessors, mutation API,
merging API, CLI parser, or old text writer.  Callers own the returned root and
use `root->resolve("path")` to obtain a subtree, then load that subtree through
a `ZtStruct` handler.

## Current baseline

`ZfCf.hh`, `ZfCf.cc`, and `ZfCfTest.cc` now live in `zf`; neither `zt` nor `zv`
publishes, builds, or tests this code.  The public header includes `ZfLib.hh`,
exported functions use `ZfExtern`, and all parser namespaces, customization
hooks, heap IDs, diagnostics, symbols, and test targets use the `ZfCf` prefix.
The old mutable `ZvCf` remains unchanged in `zv` during migration.  It is not
the base of the final implementation and none of it is retained for
compatibility: after its dependants migrate, delete it and replace it with the
new mmap-backed wrapper and `%include` handler.  The replacement imports the
format-layer API with `using namespace ZfCf`.

The parser builds an owned tree of object, array, and string nodes.  Every node
is constructed with its immutable `parent`; there is no post-scan parent-fixup
pass.  `resolve()` supports member and array paths, and `path()` uses those
parent links for diagnostics.  On success, `scan()` returns the full input
length and the root.  Syntax, directive, expansion, and trailing-input failures
throw `ZfCfError::badSyntax`, including the exact line, column, byte offset, and
nearby character.  A null span is invalid; a non-null, zero-length span is a
valid empty root object.  `Scan` incrementally maintains a composite state
containing the active span, `SyntaxError`, current offset, current line, and
the offset of that line's start; diagnostics do not rescan from the beginning.
A nested directive expansion saves and restores that state as one value.

The specified duplicate-field behavior is last-value-wins.  This applies
equally to duplicates written in one source span, introduced by `%include` or
another directive expansion, or split across the surrounding source and an
expansion.  The remaining implementation must make this consistent for
`resolve()`, typed handler lookup, and object iteration rather than exposing
different winners through different access paths.

`#` introduces a comment through the end of the physical line.  A comment may
appear where object-entry separation is allowed, but it cannot split a key
from its `:` or a `:` from its value.  Escaped or quoted `#` remains token data.

Percent directives are line substitutions, not inline object entries:

- `%` must be byte zero of the input or immediately follow `\n`; indentation
  before it is invalid;
- the directive, all arguments, and the closing `)` must be on that physical
  line;
- after `)`, only same-line whitespace and an optional `#` comment are valid;
- the complete physical line, including its newline when present, is consumed;
- each argument is parsed by the same `eos<false>` token parser as a field
  value and is owned independently as an `AnyNode::String`;
- argument strings use a `ZtLocalArray` with initial capacity four and the
  `ZfCf.Args` heap ID; a separate local `ZfCf.ArgSpans` array is materialized
  only for the duration of the `PctFn` call;
- `%define(name, value)` mutates the scan's shared `Defines` table and expands
  to nothing;
- other directives call `PctFn(Scan &, directive, args, expand)`.  The handler
  must call `expand` synchronously and propagate its failure.  The expansion is
  parsed as an object body into the object at the directive position.  The
  context exposes the active span, syntax error, and shared definitions for
  handlers that need to inspect or mutate scan state.

Typed loading validates boolean, integer, enum/flags, floating, fixed, decimal,
and date/time input, requires incremental scanners to consume the complete
scalar token, and throws a contextual `ZfCfError` `ZeException` for invalid
input, trailing junk, and inclusive `Range` violations.  Exception text is
owned by `ZeString`.  A missing field uses its declared field default.  A field
with the `Required` property is validated after `ctor`, `new_`, `load`, and
`update`; it is invalid both when absent/default-null and when explicitly
loaded as the type's null value.  Required fields are selected through the
field-property typelist and checked with `ZuUnroll`.

For an explicitly present UDT field, if the node type is incompatible with
that UDT's selected handler (`AsObject`, `AsArray`, `AsString`, or a custom
handler), loading throws a contextual type exception rather than constructing
`T` from its field defaults or returning sentinel null.  The text `null` has
no special meaning in this format: it is a string and is accepted or rejected
according to the selected handler like any other string.

## 1. Delete `ZvCf` and replace it with a thin wrapper around `ZfCf`

Keep `ZfCf` independent of `zi` and file I/O.  `ZfCf::scan(span, pctFn,
defines)` remains the single span parser.  Once dependants no longer use the
legacy API, delete the old `ZvCf.hh` and `ZvCf.cc` implementation and replace
those files with the new `zv`-layer wrapper.  This is a fresh, intentionally
breaking replacement, not a reduced legacy class or compatibility facade.

1. `ZvCf.hh` includes `ZfCf.hh`, declares `namespace ZvCf`, and uses
   `using namespace ZfCf` so callers of the file layer can use the node,
   definition, handler, and formatting API without duplicate aliases.
2. `ZvCf::read(path, appPctFn, defines)` opens the root with `ZiMMapFile`, maps
   it read-only, forms a bounded span, and calls `ZfCf::scan` with that span,
   the include-aware `PctFn`, and the supplied definitions.  Before scanning,
   `ZvCf` populates the shared `Defines` with `TOPDIR` set to the root file's
   directory and `CURDIR` set to the directory of the file currently being
   scanned.
3. `ZvCf` implements the `PctFn` that receives the mutable `ZfCf::Scan`
   context, reserves `include`, and requires exactly one non-empty argument.
   It opens and memory-maps the included file,
   invokes the expansion callback synchronously with the mapped span, and
   returns the callback's boolean result unchanged.  The mapping remains alive
   until the callback returns.  `%include(...)` itself must obey `ZfCf`'s
   line-level directive grammar: it begins in column one, occupies one physical
   line, and has only whitespace or an optional comment after `)`.  Before
   invoking the expansion callback, update `CURDIR` in the same `Defines` to
   the included file's directory; restore the including file's `CURDIR` when
   the synchronous callback returns.  `TOPDIR` remains the root directory for
   the complete include tree.  This makes `${TOPDIR}` and `${CURDIR}` available
   while `ZfCf` parses both ordinary values and directive arguments.
4. For any other directive, the `ZvCf` handler calls `appPctFn` with the scan
   context, original directive, arguments, and expansion callback when an
   application handler was supplied.  With no handler it returns `false`.
5. Relative includes resolve against the directory containing the including
   file.  Maintain an active directory/file stack while expansion is nested,
   so includes within includes use the correct base.
6. Detect an include cycle from the active canonical file stack and report the
   include chain.  Do not impose an arbitrary fixed nesting depth.
7. Root and included mappings use the same open/map/size helper.  Report its
   errors as contextual `ZeException`s containing the relevant path and
   include chain.  Syntax, directive, expansion, and trailing-input rejection
   from `ZfCf::scan` is reported as `ZfCfError::badSyntax`; preserve its line,
   column, byte offset, and nearby character while adding file context.
8. Delete `fileError` and `file2Big` from `ZfCfError`.  They are file-layer
   diagnostics and therefore belong in `ZvCfError`; the new `ZvCf` root and
   include open/map/size paths throw those `ZvCf` exceptions.  `ZfCf` retains
   only span/parser and typed-value diagnostics and remains independent of file
   I/O.

Direct `ZfCf::scan` calls have no built-in file behavior.  Applications can
still supply their own `PctFn`; the standard `%include` implementation is the
one provided by `ZvCf` while reading mapped files.

`%include(foo.cf)` substitutes the complete directive line.  The mapped file is
passed to the synchronous expansion callback and parsed as an object body into
the current object; the directive line contributes no key, value, comma, or
other residual token.  Fields are applied in substitution order, and any later
duplicate replaces the earlier value, including when the two occurrences come
from different files or expansion levels.

## 2. Replace generic access with typed configuration loading

Adopt these rules across all dependent modules:

- Static configuration objects become `ZtStruct` records and are loaded with
  `ZfCf::handler<T>(node).ctor()` or `load()`.  Existing defaults, ranges,
  enums, flags, and required-field checks move into their field metadata and
  explicit validation.  Prefer the `Required` field property where null is
  never valid: current `ZfCf` enforces it after construction, placement
  construction, full load, and update, including explicit assignment of a
  type-specific null sentinel.
- A caller that formerly used `getCf("mx")` uses
  `root->resolve("mx")`, checks for `nullptr`, and loads that node through the
  relevant typed handler.
- Runtime-keyed objects such as `threads`, `hosts`, `tables`, `links`, and
  `multicastGroups` are resolved as object nodes and iterated.  The object key
  remains the runtime identifier; every child value is loaded through a
  `ZtStruct` schema.  Do not reintroduce untyped scalar getters for these maps.
- Replace default-overlay parsing in `ZvRingParams`, `ZvThreadParams`,
  `ZvStackParams`, and `ZvMxParams` with typed patch records whose optional
  fields are applied to the supplied defaults.  This preserves field-presence
  semantics without a generic fallback getter.
- Replace `ZvCfString` and `ZvCfStringVec` with the appropriate `ZtString<>`
  and `ZtArray<ZtString<>>` types.
- Do not retain raw resolved subtree pointers past the owning root's lifetime.
  Prefer fully loading component-owned typed state during initialization.  In
  particular, remove `ZdbPQ`'s retained configuration pointer and parse its
  connection settings during `init`.
- Replace config mutation after CLI parsing with typed overlays or direct
  component construction.  Multiple mutable `fromFile`/merge steps become a
  single root document using includes, followed by typed CLI overrides.
- Keep command-line parsing in `ZtCLI`; neither `ZfCf` nor the file-layer
  `ZvCf` will provide `fromArgs`, `parseCLI`, or an argument-tree API.

## 3. Migrate dependants in buildable slices

Make each slice compile before proceeding, so the final deletion does not
produce one repository-wide debugging step.

1. **Core `zv`:** convert `ZvRingParams`, `ZvThreadParams`, `ZvStackParams`,
   `ZvMxParams`, connection options, and `ZvEngine` configuration entry points
   from `const ZvCf *` to typed records or `const ZfCf::AnyNode *`.  Preserve
   explicit scheduler SID, multicast-address, affinity, stack, and range
   validation.  Update `ZvEngineTest` to own the root result and use `resolve`
   for dynamic links.
2. **Database core:** convert `Zdb::TableCf`, `HostCf`, and `DBCf` to complete
   `ZtStruct` schemas.  Resolve and iterate dynamic table/host objects, replace
   their old string aliases, and make `ZdbStore::init` accept a typed backend
   configuration boundary rather than `ZvCf *`.
3. **Database backends and dataframe/user services:** convert `ZdbMemStore`,
   `ZdbPQ`, `ZdfStore`, `ZumServer`, and `zuserdb` configuration.  Backend
   module selection remains typed at the database layer; each selected backend
   immediately loads its own schema.  Update all `zdb`, `zdb_pq`, `zdf`, and
   `zum` tests so command-line overrides modify typed options, not a config
   tree.
4. **Networking and applications:** convert `ZtelClient`, `ZtelServer`, `zws`,
   `zrest`, `zdash`, and their tests to typed TLS, timeout, and service records.
   Remove the now-unused `ZvCf.hh` include from `ZcmdHost.hh`.
5. **Commands and proxy:** replace stale `ZvCf::fromArgs`/`parseCLI` use in
   `zcmd/test/cmdtest.cc` and `zproxy` with `ZtCLI` schemas.  Interactive
   command handlers use the current `Zcmd::Argv`/`ZtCLI` path; startup config is
   read once through `ZvCf::read` and loaded into typed application records.
6. **Workspace-only consumers:** the currently untracked `ZiEngine.hh` also
   refers to the old type.  If it is intended to become repository code,
   migrate its configuration signature before the zero-reference gate; do not
   silently add or modify it as part of this change otherwise.

After every slice, search both declarations and method names, not just the
`ZvCf` type, for surviving dependencies: `getCf`, generic `get*`, `set`,
`assure`, `merge`, `fromString`, `fromFile`, `toFile`, `fromArgs`, and
`parseCLI`.

## 4. Convert configuration sources

The new format is intentionally source-incompatible with the old `ZvCf`
format.  Do not add a legacy grammar mode or silently accept old artifacts.
Convert every embedded literal, tracked `.cf` file, generated configuration,
example, and test fixture to `ZfCf` syntax:

- require `:` between each object key and value;
- require `,` between a value and the following object key unless an
  intervening `#` comment supplies object-entry separation, and require `,`
  between array elements; for example, `a 1 b 2` becomes `a: 1, b: 2` within
  the appropriate object delimiters;
- convert quoting and escaping to the new JSON-like rules, including escapes
  such as `\u...` and `\t`, while preserving the supported shell-style
  expansions;
- convert `%include file` to a column-one `%include(file)` line and `%define
  NAME value` to a column-one `%define(NAME, value)` line;
- never indent or place a directive after another token.  Keep its arguments
  and closing `)` on the same physical line, and permit after it only
  whitespace and an optional `#` comment.  Do not append a comma to a
  directive line: the entire line is replaced by its expansion;
- ensure the object field before a directive is already syntactically
  separated as required by the ordinary object grammar.  After the directive
  line, `ZfCf` resumes at the next beginning of key; expansion text is parsed
  synchronously as an object body in the current object;
- convert comments to `#` through end of line.  Comments may separate object
  entries but cannot split a key/value pair; quote or escape a literal `#`;
- make environment substitutions (`${name}` in unquoted or double-quoted
  values), numeric forms, and all remaining lexical details follow
  `zf/test/ZfCfTest.cc` rather than the old `ZvCf` lexer.

This includes the database fixtures under `zdb/test`, all inline configs in
module tests and applications, documentation examples, scripts, templates,
sample files, and code that emits configuration text.  Search for old `ZvCf`
artifacts by content as well as by filename or type reference: many inputs are
ordinary string literals and will not mention `ZvCf`.  For runtime keys that
are not identifiers (addresses, numeric IDs, and similar), quote or escape the
key according to `ZfCf`'s object-key grammar.

## 5. Replace tests and delete the old implementation

Delete `zv/test/CfTest.cc`, `zv/test/CfFlatten.cc`, and `zv/test/test.cf`, and
remove their targets from `zv/test/Makefile.am`.  They test the parser and
generic mutable API being removed; do not mechanically port those assertions.

Keep span parsing, grammar, `AnyNode::resolve`, typed-handler, validation, and
saving coverage in `zf/test/ZfCfTest.cc`.  Its directive coverage must retain
column-one enforcement, whole-line consumption, optional trailing comments,
rejection of inline/indented/multiline directives and trailing tokens,
independently owned arguments, more than four arguments (local-array heap
fallback), mutable `Scan` context, nested expansion, and exact syntax
line/column/offset diagnostics.  Required-field coverage must exercise missing
and explicitly null values after `ctor`, `load`, and `update`.  Add direct and
expanded duplicate-field tests proving that `resolve()`, typed loading, and
iteration all implement last-value-wins.  Add the mmap and include coverage to
`zv/test/ZvCfTest.cc`:

- root-file `read`, relative include, nested include, and include within an
  object;
- definitions/environment expansion in include arguments;
- root `TOPDIR`, root `CURDIR`, nested-include `CURDIR`, restoration of parent
  `CURDIR` after expansion, and stable `TOPDIR` throughout the include tree;
- application-directive forwarding and expansion;
- enforcement of the same column-one, single-line `%include` syntax;
- propagation of `false` from the application handler and expansion callback;
- rejection of unknown directives, malformed include arguments, include
  syntax failures, and recursive includes;
- missing, unreadable, empty, read-only, and oversized root/include files;
- `ZvCfError::fileError` and `ZvCfError::file2Big` source/path diagnostics,
  with no corresponding file helpers remaining in `ZfCfError`;
- validity of parsed keys and values after all mappings have been released;
- correct source path/include-chain context for I/O errors.

Once all dependants compile against the replacement API, delete the existing
`ZvCf.hh`, `ZvCf.cc`, their mutable tree, parser, generic access/mutation logic,
and old tests.  Add new `ZvCf.hh` and `ZvCf.cc` files containing only the thin
wrapper around `ZfCf`, mmap-backed root-file loading, the include-aware
`PctFn`, and their error/context machinery.  Keep `ZfCf.hh`, `ZfCf.cc`,
`ZfCfTest.cc`, their build targets, namespaces, error namespace, customization
hooks, heap IDs, diagnostics, and dependent format references under the
`ZfCf` name.  Clean stale descriptions from `CODEBASE.md` and `TODO.md`.  Do
not leave deprecated aliases or forwarding shims for any removed legacy API.

## 6. Verification and completion gates

1. During migration and after final cutover, build and run `ZfCfTest` followed
   by the mmap/include-focused `ZvCfTest`.
2. Build each migrated module and run its standalone tests: `zv`, `zdb`,
   `zdb_pq`, `zdf`, `zum`, `zcmd`, `zproxy`, `zrest`, `zws`, and `zdash` where
   enabled.  Run the repository's configured `make -j8` and `make test` as the
   final integration check.
3. Exercise the read-only mmap/include tests on Linux and the corresponding
   path/mapping tests under MinGW.  Compile at least once with current GCC and
   Clang.
4. Run a final tracked-source search.  Completion requires no references to
   old `ZvCf` node/string types or removed methods, no old configuration
   grammar in fixtures, embedded strings, examples, scripts, templates, or
   configuration emitters, and no component retaining a resolved node beyond
   the owning root unless that lifetime is explicit and tested.
5. Confirm that `ZvNewCf` no longer appears.  The public `ZfCf` behavior is
   span scanning, node resolution, typed loading, validation, and saving; it
   has no file dependency.  The public `ZvCf` behavior is mmap-backed root
   file loading and `%include`, delegating every mapped span to `ZfCf::scan`.
   Confirm that no code from the deleted `ZvCf` implementation survives in the
   new wrapper.  All application data access is through `resolve` plus
   `ZtStruct` handlers.
6. Confirm that `ZfCfError` contains no `fileError` or `file2Big`, that the new
   `ZvCfError` owns and throws both diagnostics, and that all root and nested
   scans receive the shared `Defines` with correct `TOPDIR`/`CURDIR` values.
   Confirm last-value-wins behavior for duplicates within a file and across
   every include/expansion boundary.
