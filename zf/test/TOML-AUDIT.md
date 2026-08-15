# TOML implementation audit

This record covers `ZfTOML`, `ZvTOML`, their common-tree precursor, and their
focused tests. Measurements are from the x86-64 Clang 22.1.8 debug build on
2026-08-15; sizes are bytes. The same source is separately verified with GCC,
sanitizers, and Valgrind by the acceptance commands in `toml.md`.

## Storage and allocation paths

| Path | Storage and heap identity | Result |
|---|---|---|
| Parsed nodes, owned keys/scalars, object/array spill | `ZfTree::newNode` and the common `ZfTree.Node` heap | One canonical uniquely owned tree; TOML adds no DOM or wrapper allocation. |
| Table-definition metadata | 32-record `ZtScratch<ZfTOML.TableState>`; heap fallback uses `ZfTOML.TableState` | The common case is on stack and the fallback has a named heap. |
| Table-state lookup | Linear through 32 records, then local `ZmLHashKV` using `ZfTOML.TableStateIndex` | No fixed table-count limit and no quadratic large-document path. |
| Dotted key segments | Eight-element `ZtScratch<ZfTOML.KeyPath>` | Conventional paths stay on stack; arbitrary depth spills to the named heap. |
| Explicit scalar restyling | 128-byte `ZtScratch<ZfTOML.ScalarBuf>` | Used only when a preformatted string representation must be restyled. Native/basic strings, bytes, UDT strings, numbers, and dates stream to the destination. |
| Emitted header path | 128-byte `ZtScratch<ZfTOML.Path>` per reflected table context | Schema-bounded recursion; arbitrary path length spills to the named heap. |
| File input | Read-only `ZiMMapFile` (`ZiMMapFile`'s framework heap paths) | No input copy; every retained key/value is copied into `ZfTree.Node` before unmap. |
| File output | `ZiFileTxStream` | Uses the established transactional file-stream buffer path. |
| Diagnostics | `ZeString`, `Zi::Path`, and `ZeException` framework storage | Cold failure path, with existing named framework heaps and value captures only. |
| Curated fixtures | `ZfTOML.Fixture` and `ZfTOML.FixturePath` | Test-only, cold-path C `FILE` access; it avoids introducing a `zi` dependency into `zf/test`. |

The parser writes decoded strings directly into their final node strings and
moves completed children into parent containers. Numeric lexemes are retained
in their final node string and converted directly by `TOMLPolicy`; there is no
decimal-normalization buffer or second parse. The canonical native/basic
emitter uses forwarding stream adapters for TOML control escaping and removal
of JSON framing quotes; neither adapter stores payload data or allocates.

## Capacity and layout

The 32-record table threshold is 512 bytes of record storage and covers the
ordinary application configurations represented by the focused fixtures. The
40-table test crosses the threshold and proves hash promotion. Eight key
segments cover normal configuration paths while retaining heap fallback.
Scalar and path scratch capacities cover ordinary formatting and headers and
are not document limits. `Limits::DepthMax` bounds recursive array/inline-table
parsing; `Limits::NodeMax` bounds tree storage below `ZvTOML`'s 1 MiB file cap.

| Type | Size | Layout observation |
|---|---:|---|
| `ZfTOML::Scan` | 40 | span 16, limits 8, error 16; no internal hole. |
| `Scan::Impl` | 104 | references/span/index first, then limits/counters and two node pointers; no hole. |
| `SyntaxError` | 16 | three unsigneds and three one-byte fields; one unavoidable tail byte. |
| `TableState` | 16 | node pointer plus two state bytes; tail padding follows pointer alignment. |
| `StateIndex` | 32 | local hash owner only; destroyed with the scan. |
| `StateIndex::Node` | 24 | pointer key and unsigned index in the established hash node. |
| `AnyNode::String` | 48 | common-tree builtin string with `ZfTree.Node` spill. |
| `NumberValue` | 32 | one `long double` and one `ZuDecimal`. |
| `SourcePos` | 12 | three unsigneds, no padding. |
| `HeaderPlan` | 16 | pointer and unsigned; tail padding follows pointer alignment. |

Clang debug stack frames measured from generated prologues are: `Scan::scan`
240, `run` 208, `pair` 224, `header` 224, `checkHeader` 528,
`commitHeader` 336, `value` 256, `array` 144, and `inlineTable` 272. Header
parsing originally produced one 1,120-byte frame; separating syntax,
preflight, and commit reduced the largest header-state frame to 528 and keeps
path scratch out of it. At the default maximum, the larger recursive
`inlineTable`/`value` pairing is below 68 KiB of stack. Header and dotted-path
walks are iterative. Reflected emitter recursion is compile-time schema-bounded
and carries only references, small state, and 128-byte path scratch per table
context.

## Red and amber flags

- Language and style: searches and review find no anonymous namespace,
  concepts/`requires`, typed enum, `enum class`, nonspecific lambda capture,
  STL container/algorithm, C++ wrapper replacing an equivalent C header,
  varargs formatter, or unnecessary cast in the TOML library files. File-local
  `.cc` helpers are `static`; discrete states use integer enums and `switch`.
- Structure and reuse: handlers publicly derive from the common
  `ZfTreeLoad::{Object,Array,String}` implementations. The only common-loader
  addition is the generic numeric policy seam and the direct common date/time
  branch. Field matching, required/default/update behavior, vectors, ranges,
  and diagnostics are not copied. UTF traversal uses `ZuUTF`; object fields
  use the common parent/path implementation.
- Loops and algorithms: invariant lengths are cached. Object-field duplicate
  lookup is intentionally linear for small reflected/configuration objects.
  Table-state lookup is linear only through the documented threshold and then
  hash-indexed. There is no garbage-collection scan, hand hash, polling,
  blocking, scheduler work, or long-running I/O-thread loop.
- Data movement and initialization: source spans and mmap storage are borrowed;
  retained text is copied once into final owned nodes. Children receive their
  parent at insertion. Array/object slots use Z container push/placement-new
  conventions. Explicit scalar restyling and path construction are the only
  temporary contiguous payloads and use named stack-first scratch.
- Ownership: nodes are uniquely owned by `ZuPtr`; parser metadata stores raw
  stable back-pointers and dies with `Scan::Impl`. Pair/header paths preflight
  collisions and node budgets before linking implicit nodes. Failure returns
  no tree. The published root is `const` and has a null parent.
- Framework fit: production code uses `ZtScratch`, `ZmLHashKV`, `ZuUTF`,
  `ZfTree`, `ZfTreeLoad`, `ZiMMapFile`, `ZiFileTxStream`, Z formatting, and Z
  exceptions. Direct `FILE` use is confined to the repository-fixture reader
  in a `zf` unit test and is documented above. No logging, atomics, locks,
  reference-count cycles, shared mutable state, or I/O sharding applies.
- Format separation: TOML has its own key, collection, string, table, and
  array-of-tables emitters. Only public JSON scalar formatting and shared
  property types are reused. Exact-output tests prove conventional bare keys,
  independently quoted path segments, TOML assignments/tables, and independent
  JSON/TOML facet metadata. No Cf directive or YAML schema-transform path is
  reachable.
- Header/build hygiene: headers follow the component skeleton and direct
  dependency ordering; source utilities remain in their owning module. Heap
  IDs and new identifiers are within the repository's 28-byte limit. Leading
  indentation follows the hard-tab repository style. Lower common headers
  include no TOML parser/emitter dependency.
- Inapplicable flags: the implementation has no queues, intrusive hash values,
  cache-line-shared state, scheduler callbacks, timers, logging lambdas,
  concurrency tests, persistence, network I/O, or rx/tx ownership.

The focused tests pin exact node/depth boundaries, stack-to-hash promotion,
first-failure locations, source/mmap independence, parent paths, all scalar
styles and collection contexts, strict-style failures, both array layouts,
and compile-time invalid property/type combinations.

## Guidelines re-audit (2026-08-16)

The implementation and its common-tree precursor were re-reviewed against
every red and amber flag in `GUIDELINES.md`. The review repaired repeated
indexed reads in key and numeric scanning, named and documented the decimal,
exponent, and RFC 3629 UTF-8 bounds, and replaced the raw four-byte UTF-8 array
with `ZuCArray`. It also removed the focused TOML test's dependency on the WIP
YAML facade; common-tree identity remains checked directly and against Cf.

The allocation, ownership, layout, iterator, control-flow, framework-fit,
header, naming, and data-movement conclusions above remain valid. In
particular, all new names satisfy the current 28-byte limit, all scratch and
spill allocations have named Z heaps, and no new atomic, lock, scheduler,
logging, I/O-sharding, or lifetime concern applies. The scalar-format matrix
exercises `Native`, `Basic`, `Literal`, `MultilineBasic`, and
`MultilineLiteral` for direct fields and repeats them in inline objects,
ordinary arrays, table arrays, and nested table sections; strict literal edge
failures and all four accepted input string forms are covered separately.

## Verification record

- The 2026-08-16 audit retained the configured Clang debug flags
  (`clang++`, `-g`, `-DZDEBUG`), cleanly rebuilt only `zu`, `zm`, `zt`, `ze`,
  and `zf`, and passed `ZfTreeTest`, `ZfCfTest`, `ZfTOMLTest`, and the Python
  `tomli` v1.1 interoperability gate. No module above `zf` was built.

- The Clang debug top-level build and `make test` pass, including the focused
  tree, Cf, YAML, TOML, and mmap/file tests and the Python `tomli` v1.1
  interoperability gate.
- A separate GCC debug worktree builds and passes the focused `zf` and `zv`
  tree/Cf/YAML/TOML matrix without a warning from the changed files.  Its full
  repository build stops later in the unrelated `ZhttpServerIdleTest` due to
  an ambiguous `Headers` lookup; it also reports a pre-existing
  `-Wcast-user-defined` warning while compiling `ZtcHub.cc`.
- A clean Clang ASAN/LSAN configuration, produced through `z.config` after a
  top-level clean, passes the focused `zf` and `zv` matrix with leak detection
  enabled.
- `libtool --mode=execute valgrind --leak-check=full` reports zero errors and
  zero definite, indirect, or possible leaks for both TOML binaries.  The
  first run exposed a test-only borrowed-`CString` lifetime error; retaining
  the parsed tree for the reflected value's lifetime repaired it.
