# Plan: SQLite backing store for Zdb

## Objective and completion criteria

Implement `zdb_sqlite`, a persistent local-filesystem SQLite backend using the
`ZdbSL` prefix and the broad source structure of `zdb_pq`. It must implement the
complete `Zdb_::Store` / `StoreTbl` contract and preserve the full Z vocabulary.

SQLite does not replicate storage, so `ZdbSL` reports
`InitData::replicated = false`. `Zdb` performs replication as directed by the
application configuration. `ZdbSL` neither implements nor enables replication;
it implements the ordinary persistence, metadata, and recovery operations that
`Zdb` calls. Replication message buffers and sequence metadata in the store API
are part of that contract, not a store-owned replication mechanism.

Completion requires idempotent startup schema population and validation,
indexed queries, idempotent writes with the configured durability, correct reopen
and recovery after trailing deletes, internal saga tables, Zdb-level replication and
promotion integration tests, build/install integration, and operational documentation.
Preserve field values subject to the defined Fixed/Float normalization, and
preserve row identity. For `select*`, backend column ordering
and collation take precedence; return rows in SQLite query order, as `ZdbPQ`
does for PostgreSQL.

This plan uses the current working-tree versions of `zdb/README.md`,
`zdb/src/ZdbStore.hh`, `zdb/src/ZdbTypes.hh`, and `zdb_pq/src/ZdbPQ.{hh,cc}`.
Implementation must follow `GUIDELINES.md`; unrelated working-tree changes
must be preserved.

### Guideline constraints used throughout

Treat this as a constraint list for every implementation item below, rather
than a final review checklist:

- Match `zdb_pq` only where its responsibility follows from the Zdb store
  contract. Delete its libpq socket, pipeline, queue and OID machinery instead
  of translating it mechanically. This applies `General principles`, `Audit
  flags / Scope and cleanup`, and `Audit flags / Framework fit`.
- Keep all SQLite state on one configured `ZmScheduler` isolated thread and use
  direct scheduler posts for public asynchronous entry points. Do not add locks,
  atomics, an event loop, a work queue or ordering layer. This applies
  `Leveraging Key Z Framework Capabilities / Concurrency and sharding`, `Audit
  flags / Unnecessary atomic operations, locking and copies`, and `I/O
  sharding`.
- Use Z reflection, FlatBuffer, callback, container, error, string, formatting,
  configuration and scratch facilities. Do not hand-code facilities already in
  `Zu`, `Zm`, `Zt`, `Zf`, `Ze`, `Zi` or `Zfb`; do not cast between directly
  convertible Z spans/strings/arrays. This applies `Audit flags / Framework
  fit`, `Audit flags / Type and API friction`, and `Leveraging Key Z Framework
  Capabilities`.
- Allocate persistent state with named Z heaps/containers and temporary runtime
  storage with `ZmScratch`, `ZtScratch`, `ZmLocal` or `ZmAlloc`, eliminating the
  temporary entirely when SQLite can bind or return the value directly. This
  applies `Audit flags / Storage and capacity`, `Heap allocation`, and `Data
  movement and initialization`.
- Use concise camel-case names, the `ZdbSL` component prefix, `m_` only for
  private class data, unprefixed public struct members, overloaded accessors and
  trailing underscores only for worker-only/internal variants. Keep every name
  within 28 bytes. This applies `Naming` and `Code style / Class and struct
  shape`. Retained Zdb API and matching PQ role names (`m_connection`,
  `m_conn`, `m_storeTbls`, callback types) follow local precedent; new names use
  the guideline abbreviations (`cxn`, `stmt`, `param`, `rec`, `evt`). The only
  snake-case C++ symbol is SQLite's mandated extension entry point.
- Validate configuration and persistent schema once at their trust boundary.
  At runtime perform only checks needed for SQLite errors, memory-safe decoding
  and the store contract; do not re-prove schema constraints for every row.
  This applies `General principles / No dogma`, `Audit flags / Algorithm and
  control flow`, and `Audit flags / Scope and cleanup`.

The implementation review must use this complete `GUIDELINES.md / Audit flags`
disposition; an item fails review if it introduces the prohibited shape:

| Audit flag group | ZdbSL disposition |
| --- | --- |
| Code structure | Put SQLite storage structs and non-dependent declarations outside templates. Cache invariant descriptor counts and indexed values before loops. (`Audit flags / Code structure`.) |
| Storage and capacity | Use metadata-sized `ZtArray`/`ZtBuiltin` containers and `ZmScratch`/`ZtScratch`; no unexplained capacities, fixed lookup tables or separately maintained lengths. Use thread-affined intrusive `ZmHash` registry nodes with `ZmNoLock`, remove them deterministically, order class members by ownership/alignment/size, and keep all mutable operational state on one worker so no cache-line sharing pad is needed. (`Audit flags / Storage and capacity`.) |
| Heap allocation | Bind directly where possible; otherwise use stack-first scratch with named heap fallback. Use named `ZmHeap`/`ZmVHeap` for persistent dynamic state and pooled `ZiIOBuf` for result/message buffers. Do not split refcount, object and container-node allocations. (`Audit flags / Heap allocation`.) |
| Data movement and initialization | Capture log values by value with `ZeString`; retain only pooled input/output handles across posts. Encode directly into one uninitialized canonical-conversion scratch buffer and serialize SQLite row spans before stepping/resetting. Do not add intermediate contiguous copies or initialize storage before overwrite. (`Audit flags / Data movement and initialization`.) |
| Algorithm and control flow | Factor scalar/vector codecs and statement execution with compile-time dispatch/local utilities; use `switch` for type/state dispatch and early exits for errors. Never wait, poll, sleep or treat scheduler `run`/`invoke` as a synchronous result. Scope iterators, count visits during traversal, avoid redundant iterator null checks/aliases, use Z hashing, and keep each select turn bounded by its requested limit. (`Audit flags / Algorithm and control flow` and `Liveness`.) |
| Framework fit | Use Z assertions, errors/logging, formatting, reflection/configuration, FlatBuffer transforms, containers, scheduler and file/buffer facilities. The unavoidable direct API is SQLite itself; no direct `FILE`, `printf`, hand-rolled hash, JSON, numeric or date parser/formatter is introduced. Use existing Z sentinels rather than companion validity booleans. (`Audit flags / Framework fit`.) |
| Type and API friction | Use direct Z span/string/array conversion, `T(v)` where a real conversion is required, `ZuPun` for the defined sortable bit transform, and enum values directly. No char-pointer casts, `reinterpret_cast` span construction, CRTP base casts or enum/integer mapping switches. (`Audit flags / Type and API friction`.) |
| Scope and cleanup | Implement only the store, its required inspection formatters, build/test integration and documentation. Delete PG-only/dead analogues. Tables use raw owner back-pointers; queued captures retain operation data/callbacks rather than reference-counting the longer-lived store. The owner fences and drains before destruction. (`Audit flags / Scope and cleanup`.) |
| Atomics, locks and copies | The sole worker owns connection, tables, statements and high-water state. Do not add locks, atomics, statistics or snapshots; cross-thread posts carry fixed metadata and pooled handles. (`Audit flags / Unnecessary atomic operations, locking and copies`.) |

## 1. Implement the established backend contract

The following requirements follow from the implementations, rather than needing
new product decisions. Full-featured means the existing Zdb store API and type
taxonomy; arbitrary SQL arithmetic, encryption, and repair APIs are not added
to this task without a separate requirement.

| Area | Required behavior and source |
| --- | --- |
| Replay | Before mutation, skip successfully when the table/shard high-water UN is non-null and incoming UN is less than or equal to it. Both `ZdbMem::StoreTbl::write` and `ZdbPQ::StoreTbl::write_send` do this. UN, not a per-row VN comparison, is the replay gate. |
| Mutation | VN zero inserts, positive updates, negative deletes. Insert loads all fields; update changes only `Mutable` fields and addresses the row by key 0. A delete payload retains every key field so Zdb can index its pending tombstone, while each back end binds only key 0 to delete exactly the row already found by Zdb. See memory `loadUpdTuple`, `loadDelTuple`, `updTuple`, and PQ `prepUpdate_send` / `prepDelete_send`. |
| Missing-row mutation | After the UN replay gate, an update or delete whose primary key is absent must error, matching `ZdbMem`. Align `ZdbPQ` by checking affected-row counts, and require the same behavior in SQLite. A failed mutation must not advance committed high-water state; duplicate-UN replay remains a successful no-op. |
| Insert collision | An existing key 0 makes insertion a successful no-op, preserving the existing row. Both stores advance the stream position; PQ `write_rcvd` persists MRD for a skipped insert as well as a delete. SQLite must persist those positions atomically with the write outcome. |
| Recovery | Exact `(shard, UN)` lookup returns the current live row as a Recovery message. Missing superseded/deleted rows return empty success, not an error. `Zdb.cc` `AnyTable::recSend` explicitly skips these gaps. No historical row log or permanent tombstones are required. |
| Open result | Return live row count, each shard's last UN, and table SN. Combine live-row maxima with MRD, retaining null sentinels for unused shards and empty histories. PQ `maxUN_rcvd` and `mrd_rcvd` establish this; use shard-qualified lookup throughout. |
| Namespaces | Table identity includes `(internal, id)`. Memory uses that tuple as its registry key; PQ uses separate `public`/`zdb` relations and namespace-aware statement/MRD IDs. |
| Group queries | Group length is the position after the last field marked `group` for the key. `count` and initial `select` bind that prefix; a continuation binds the full key. No group means an unrestricted prefix. |
| Ordering | Delegate column ordering/collation to SQLite, using SQL predicates and ORDER BY with the declared field directions and group equality. Return selected rows in the order SQLite presents them. Mixed-direction continuation uses equal prefixes followed by the first differing field, as in PQ `prepSelect_send`; include the boundary only for inclusive continuation. |
| Results | `count` has one callback, no end marker. `select` emits at most `limit` tuples, numbered from 1, followed by an empty end marker on normal exhaustion. Key tuples use the requested key ID; whole-row tuples use `ZuStructKeyID::All`. `find` returns a full Replication message or empty success; `recover` uses the Recovery variant. Preserve error results through the existing DB adapter. |
| Execution | Both stores dispatch table work to the configured store thread. Preserve posted write/query order and buffer ownership; validate that the configured thread is neither the multiplex Rx nor Tx thread. |
| Warmup | Both implementations are no-ops (`ZdbPQ` labels it `LATER`). Implement a no-op; warming the SQLite cache is not a parity requirement. |
| Shards | Both stores reject a configured shard count differing from stored state. Persist and validate this topology for SQLite. |

Source anchors: [memory operations](zdb/src/ZdbMemStore.cc),
[memory metadata, codecs and comparators](zdb/src/ZdbMemStore.hh),
[PQ schema, query and mutation implementation](zdb_pq/src/ZdbPQ.cc),
[PQ type mapping/codecs](zdb_pq/src/ZdbPQ.hh), and
[Zdb recovery](zdb/src/Zdb.cc).

Use `ZdbMem::Value_` and the PQ value mapping as the exhaustive scalar/vector
inventory, including distinct Bytes and UInt8Vec representations. Generate the
operation/type test matrix from these established cases. The contract table
records the required alignment of existing backend differences; these are implementation requirements, not open policy questions.

Guideline references for this contract: `General principles` (integrity,
performance, clean teardown), `Audit flags / Scope and cleanup` (no unrelated
SQL features), `Leveraging Key Z Framework Capabilities / Metadata` and
`Persistence`, and `Naming / Standard function name vocabulary`.

## 2. Design the storage format and indexed encodings

Use one database file per store instance initially. Application tables, internal
tables, format metadata, and deletion metadata share the file so related writes
can be committed atomically. When the application configures Zdb replication, each host uses its own
local store file.

- Define collision-free physical names for application and internal namespaces,
  by prefixing the unchanged logical table ID with the namespace discriminator,
  then quoting it with one shared SQL-identifier utility. Use `ZtCase` only for
  the established field-name conversion. Do not invent a hash, numeric registry
  or reversible name codec. Bind all data values. Metadata identity includes
  namespace and shard where appropriate.
- Persist format version, shard topology, and the logical field/key schema that
  SQLite's three storage classes cannot express. At startup compare this metadata
  once with the requested Z reflection metadata, and inspect SQLite's catalog
  once for the physical columns and indices. These checks cover distinct facts;
  do not repeat them while preparing statements or decoding rows. Idempotently
  create missing application/internal tables and indices, and reject inconsistent
  schemas with actionable errors. `CREATE IF NOT EXISTS` alone is insufficient.
  Store logical schema as normalized relational metadata rows emitted directly
  from `ZfVFieldArray`/`ZfVKeyFieldArray`; do not serialize or parse ad hoc JSON,
  text signatures or hashes.
- For the initial version, align with PQ `mkTable_rcvd` / `mkIndices_rcvd`:
  existing tables must have the expected fields and types, and existing indices
  must match their expected definitions. A missing index on a compatible table
  can be created; an incompatible existing table or index fails open.
  Automatic column additions, renames/removals, type conversions, index
  replacement, and table rebuilds are outside the initial version.
- Complete the single schema check/create pass before preparing operational
  statements or exposing a table as ready. Create a table, its logical metadata
  and indices in one transaction so interrupted creation rolls back and repeated
  startup is idempotent. Do not add per-object phase transactions or validate an
  object before and after creating it. Reject unsupported
  storage-format and shard-topology changes without reinterpreting stored data.
- Store row replication metadata (shard, UN, SN, VN) and typed field values,
  with a unique key-0 index and a unique `(shard, UN)` recovery index. Secondary indices are non-unique, matching PQ;
  find operations return one unspecified match and mutation targets that row.
  Use a relational field layout resembling `ZdbPQ`, with SQLite ordering and
  collation on the stored columns. Do not add key encodings or custom collations
  merely to reproduce in-memory ordering.
- Maintain most-recent-delete/high-water metadata per physical table and shard.
  Use one transaction for each mutation and its associated metadata changes.
  Recover maxima from live rows and metadata without narrowing UN or SN.

### Decided SQLite type representation (format version 1)

Use `STRICT` tables with `INTEGER`, `TEXT`, and `BLOB` columns, all `NOT NULL`.
Require SQLite 3.37.0 or later for STRICT support. Every front-end field maps to
exactly one back-end column, including Time and DateTime. Indices, predicates
and result projections preserve this 1:1 mapping. Persist
the logical type ID and encoding version alongside schema metadata so, for
example, changing a UInt128 BLOB column to a Decimal BLOB column is detected
as an inconsistent schema. Storage class alone is not the logical type.
Put Bool and narrower integer-domain checks in the table DDL once, so reads do
not repeat range validation. BLOB decoders still bound their span before access;
do not duplicate fixed-width CHECK expressions in every generated table.

SQLite supports application-defined functions, collations and virtual tables
through its extension API, but it does not offer PostgreSQL-style extensible
base storage types. A custom declared name is an affinity hint, not a new
binary type; STRICT tables accept only SQLite's supported type names. Implement
the encodings below in `ZdbSL` itself. Also implement the loadable inspection
functions specified below for CLI use. These format values, without adding
storage classes, custom collations, arithmetic or virtual tables. See
[SQLite storage types](https://www.sqlite.org/datatype3.html),
[STRICT tables](https://www.sqlite.org/stricttables.html), and
[SQLite extension facilities](https://www.sqlite.org/loadext.html).

Define the format primitives once as non-template-dependent packed storage
types plus small compile-time adapters. Use `ZuBigEndian`/`ZuByteSwap`,
`ZuPun`, Z integer types and existing Z value conversions; do not hand-code
byte swapping, byte loops, numeric parsing or formatting. Write directly to
the SQLite-bound scratch span. This applies `Core language`, `Audit flags /
Code structure`, `Framework fit`, `Type and API friction`, and `Leveraging Key
Z Framework Capabilities / Compile-time data and matching`.

- `U8/U16/U32/U64/U128(x)`: unsigned integer in exactly 1/2/4/8/16 big-endian
  bytes, without padding. Width is part of the logical type, never value-dependent.
- `S8/S16/S32/S64/S128(x)`: the corresponding unsigned bit pattern with its
  top bit flipped, written using `U`. Decode by reversing the flip. This gives
  a stable signed scalar encoding without a custom SQLite comparator.
- `F64(x)`: normalize either signed zero to positive zero and all NaNs to the
  positive quiet-NaN bits `0x7ff8000000000000`. For the resulting IEEE binary64
  bits `u`, encode `U64(~u)` when its sign bit is set, otherwise
  `U64(u ^ 0x8000000000000000)`. Reverse this transform on read. Finite values and
  infinities retain their value exactly; zero sign and NaN payload/sign are
  intentionally canonicalized. This keeps scalar/key equality consistent for
  zero and the NaN sentinel without SQL NULL or in-memory sorting.
- `V(n, elements)`: `U32(n)` followed by exactly the encoded elements, with no
  dimensions, lower bounds, OIDs, alignment padding or per-value type tags.
  Fixed-width elements have no length prefix; variable-width elements each
  have `U32(byteLength)` followed by their bytes. The column's logical type
  supplies the element type. Empty vectors contain only `U32(0)`.

| Logical type / family | SQLite column | Exact representation and conversion |
| --- | --- | --- |
| Bool | INTEGER | Bind 0 or 1; read as bool. |
| Int8, Int16, Int32, Int64 | INTEGER | Bind the signed value with `sqlite3_bind_int64`; read with `sqlite3_column_int64` and narrow to the declared type. Preserve its Z integer sentinel as an ordinary integer value. |
| UInt8, UInt16, UInt32 | INTEGER | Bind the nonnegative value as int64; read and convert to the declared unsigned width. Preserve the full domain, including its sentinel. |
| UInt64, UN | BLOB | Exactly `U64(value)`, 8 bytes. Never switch between INTEGER and BLOB according to magnitude. |
| Int128 | BLOB | Exactly `S128(value)`, 16 bytes, including the signed null sentinel. |
| UInt128, SN | BLOB | Exactly `U128(value)`, 16 bytes. No narrowing through SQLite INTEGER or REAL. |
| Float (double) | BLOB | Exactly `F64(value)`, 8 bytes, for all values. Do not use REAL or SQL NULL; canonicalization is identical for row data, keys and FloatVec elements. |
| Fixed | BLOB | Exactly 16 bytes: non-null Fixed converts through `.decimal().value` at scale 10^-18 and is encoded as `S128`; a null Fixed explicitly maps to `ZuDecimal::null()` before encoding. Decode the decimal value and reconstruct `ZuFixed`, special-casing null. Numerical value is retained; original mantissa/NDP spelling is normalized, as in PQ. |
| Decimal | BLOB | Exactly `S128(value.value)`, 16 bytes, with ZuDecimal's fixed 10^-18 scale and its native null sentinel. Decode through `ZuDecimal::Unscaled`. |
| Time | BLOB | Exactly 12 bytes: S64(sec) followed by S32(nsec), each sign-flipped and big-endian. Decode directly to ZuTime, preserving the int64 seconds/int32 nanoseconds and Z sentinel without narrowing through a Julian int32. |
| DateTime | BLOB | Exactly 12 bytes: S32(julian), S32(sec), S32(nsec), each sign-flipped and big-endian, in that order. Reconstruct directly with ZuDateTime::Julian and ZfbTransform::DateTime; do not convert through as_time(). Preserve all three components and the null sentinel. |
| String / CString field | TEXT COLLATE BINARY | Bind the explicit UTF-8 byte span with `sqlite3_bind_text64`; read with column_text/column_bytes. Preserve empty strings and embedded zero bytes during transport; no locale or case-folding collation. |
| Bytes | BLOB | Exact byte span, including a zero-length BLOB. No header and no scalar/vector reinterpretation. |
| Bitmap | BLOB | `U32(wordCount)` followed by that many `U64` words in Z bitmap word-index order. Preserve the stored word count, including trailing zero words; do not confuse capacity with logical length. Empty bitmap is `U32(0)`. |
| IP | BLOB | One format-defined family byte: 0 for null (no payload), 4 followed by 4 network-order address bytes for IPv4, or 6 followed by 16 network-order address bytes for IPv6. Exactly 1, 5, or 17 bytes. Preserve IPv4 versus IPv6 identity; no PG inet/cidr header or native structure padding. |
| StringVec | BLOB | `V(n, elements)` with each UTF-8 byte span encoded as length plus bytes; preserve empty elements. |
| BytesVec | BLOB | `V(n, elements)` with each bytes element encoded as length plus bytes. |
| Int8Vec, Int16Vec, Int32Vec, Int64Vec, Int128Vec | BLOB | `V(n, elements)` with fixed-width `S8/S16/S32/S64/S128` elements respectively. |
| UInt8Vec, UInt16Vec, UInt32Vec, UInt64Vec, UInt128Vec | BLOB | `V(n, elements)` with fixed-width `U8/U16/U32/U64/U128` elements respectively. UInt8Vec retains the count prefix and remains distinct from Bytes. |
| FloatVec | BLOB | `V(n, elements)` with fixed-width `F64` elements. |
| FixedVec, DecimalVec | BLOB | `V(n, elements)` with 16-byte elements using the corresponding scalar Fixed/Decimal conversion above. |
| TimeVec | BLOB | `V(n, elements)` with 12-byte elements: S64(seconds) followed by S32(nanoseconds). |
| DateTimeVec | BLOB | `V(n, elements)` with 12-byte elements: S32(julian), S32(sec), S32(nsec), preserving the native three-component representation. |

SQLite has no dedicated date/time storage class, and its built-in subsecond
functions currently resolve milliseconds, not nanoseconds. Do not use REAL
Julian days, floating epoch timestamps or SQLite date functions as the stored
representation. See [SQLite date/time functions](https://www.sqlite.org/lang_datefunc.html).
The requested three-int32 date representation is provided in this tree by
[ZuDateTime](zu/src/ZuDateTime.hh) and
[ZfbTransform::DateTime](zfb/src/ZfbStruct.hh), rather than a ZtDate class.
Time is separately defined as int64 seconds plus int32 nanoseconds and must
retain that wider range.

### One column per field and ordering

Keep a strict 1:1 front-end field to back-end column mapping. Each key field
contributes one indexed column and each selected field consumes one result
column. Use the existing descriptor order for key/row projections, group
prefixes, bindings and continuation buffers. Do not add component columns,
`XField::column`/`nColumns`, `m_nColumns`, or expanded projection maps.
Replication envelope metadata remains separate from the payload field mapping.

SQLite compares these fixed-width BLOBs bytewise. Sign-flipping the high bit of
each signed component and writing it big-endian makes DateTime comparison
lexicographic by julian, sec, nsec, and Time comparison by int64 sec, int32 nsec.
Apply ASC/DESC to the single column for that field. Equality, range predicates,
indices and continuation all use the same encoded value. Neither native-endian
memory dumps nor formatting functions participate in these comparisons.

Fixed and Decimal share one backend decimal representation and codec. As in
ZdbPQ, convert ZuFixed to ZuDecimal before storage and convert the decoded
ZuDecimal back to ZuFixed on read. Store the signed scaled integer as one
16-byte S128 BLOB: flip its sign bit, then write big-endian. Do not store Fixed
mantissa/NDP separately or introduce a second column. Retain the logical
Fixed/Decimal distinction in schema metadata and result serialization.
Both use the same scale for every value; this encoding therefore sorts in
signed numerical order, including negatives. Plain two's-complement big-endian
bytes without the sign flip would put negative values after nonnegative values. F64's sign
transform reverses negative magnitudes and biases positive values, giving
numerical order from negative infinity through zero to positive infinity;
canonical NaN sorts last. These are storage encodings compared by SQLite,
not in-memory reordering. Require numerical-order fixtures for negative and
positive decimals, adjacent scaled values, floating subnormals, infinities and
signed zero, as well as date/time values straddling nanosecond, second and day
boundaries. Keep the previously agreed freedom for Z sentinel ordering and
vector framing; vector ordering is not scalar ordering.

### CLI inspection and dumps

Time/DateTime remain binary in ordinary SELECT and `.dump`. For the requested
readable CLI inspection, implement the smallest loadable extension: entry point
`sqlite3_zdbslinspect_init` and deterministic scalar functions
`zdbsl_datetime(blob)`, `zdbsl_time(blob)`, `zdbsl_decimal(blob)` and
`zdbsl_float(blob)`. Each function checks only its SQLite argument class and
fixed encoded width, decodes with the shared store codec, then streams the Z
value through `ZuPrint`, `ZuFmt` or the type's existing `fmt()` into
`ZtScratch`. Pass that buffer to `sqlite3_result_text`; do not implement a
number, date or time formatter/parser. Malformed input produces a SQL function
error. This applies `Leveraging Key Z Framework Capabilities / Formatting`,
`Metadata`, `Audit flags / Framework fit`, `Heap allocation`, and `Naming /
Native names mandated by an external tool`.

Build/install this extension with the SQLite module and document `.load` and
SELECT/export examples. Share decoding/formatting code with the store; importing
it must not create a store, connection, thread or scheduler. The backend itself
does not require the extension to open, index or query its files. Normal `.dump`
remains a lossless SQL dump: it will still show BLOB literals for Time, DateTime,
decimals, floats and other encoded types. Formatting functions enable readable query
exports; do not claim that loading an extension changes `.dump` automatically.
ORDER BY the original stored columns, not the formatted text.

### Encoding and decoding boundaries

The inventory above covers every non-void alternative of `ZdbPQ::Value_` and
`ZdbMem::Value_`. `void` is an internal dispatch alternative, not a stored
vocabulary type. Missing FlatBuffer fields use their existing reflected/default
Zfb decoding semantics before encoding; empty values and Z sentinels are not
converted to SQL NULL. Schema validation requires the logical type even where
multiple types have identical byte layouts.

Use `sqlite3_bind_blob64` and column_blob/column_bytes for all BLOBs. Empty
TEXT/BLOB values must be bound with a non-null address and zero byte length,
so they do not become SQL NULL. The startup schema pass and STRICT/CHECK
constraints establish scalar storage classes and integer ranges; do not
revalidate those facts for every returned row. Check variable BLOB framing and
fixed BLOB width before accessing bytes because those checks are required for
memory-safe decoding of files that external SQLite tools can modify.
SQL aggregate results may be NULL for an empty history; handle that explicitly
as an absent maximum, never as a persisted field value. Bound all count/length
arithmetic by SQLite's connection limits
and the Z buffer limits, checking overflow before scratch sizing. The 32-bit
vector lengths follow the FlatBuffer vector/string size domain; they are not
an arbitrary allocation capacity. See
[SQLite binding semantics](https://www.sqlite.org/c3ref/bind_blob.html).

Replication metadata uses the same type rules: shard is UInt8/INTEGER, VN is
Int64/INTEGER, UN is U64/BLOB, and SN is U128/BLOB.

The same codecs encode row values, WHERE parameters and continuation keys,
with one column per front-end field throughout.
SQLite's built-in INTEGER/TEXT/BLOB comparison and BINARY text collation are
used by both indices and queries. BLOB vector/bitmap ordering is the bytewise
order of the specified framing, including its count prefix; it need not match
Z elementwise comparison. No separate sort-key column or custom comparator is
introduced. Vector elements always use their specified byte codecs, even when
the corresponding scalar column uses INTEGER. Fixed-width U64/U128 storage
makes full-width metadata maxima
straightforward, with null UN/SN excluded explicitly.

Backend ordering differences, including sentinel values, concern `select*`;
they do not require changes to mutation or `find*` behavior. Cache consistency
is maintained at the individual row level. An SK lookup selects one row and
subsequent update/delete targets that row. Do not reorder SQLite results in C++
or install a Z comparator to force agreement with the memory store.

Gate: codec tests demonstrate the exact format-1 bytes, round trips subject
to the specified Fixed/Float normalization, and correct row/key lookup.
Selection and continuation tests use SQLite's own ORDER BY and predicates as
the ordering oracle, including sentinels and duplicate keys. Agreement with
`ZdbMem` ordering is not an acceptance criterion. Recovery high-water arithmetic
must still use full-width UN/SN values and their defined null sentinels.

Guideline references for this storage design: `Core language`, `Audit flags /
Code structure`, `Storage and capacity`, `Heap allocation`, `Data movement and
initialization`, `Framework fit`, and `Type and API friction`; `Leveraging Key
Z Framework Capabilities / Strings`, `Fixed and floating point`, `Compile-time
data and matching`, `Storage and lifetime`, `Formatting`, and `Metadata`.

## 3. Scaffold the module and build integration

- Add `zdb_sqlite/Makefile.am`, `README.md`, and `src`, `test`, and `itest`
  Makefiles. Traverse `src`, `test`, then `itest`; add `util` or `bench` only
  when needed.
- Add `src/ZdbSLLib.hh`, `ZdbSLLib.cc`, `ZdbSL.hh`, and `ZdbSL.cc`;
  build/install `libZdbSL` with the exported `ZdbStore` entry point. Use
  `namespace ZdbSL` and existing library export/version conventions.
- Add independent SQLite configuration in `configure.ac` using
  `PKG_CHECK_MODULES`, requiring SQLite >= 3.37.0 for STRICT tables.
  Update generated-Makefile declarations, module include-link setup, top-level
  conditional traversal/distribution, and `z.config` help/options.
  Follow the existing optional-PQ convention: explicit configure enable/disable,
  wrapper enabled by default with a disable option. Require SQLite >= 3.37.0
  with STRICT tables and WAL support; do not leave
  the storage format or required features to configuration-dependent selection.
- Link direct Z dependencies and SQLite; keep the backend buildable with
  PostgreSQL disabled. Build/install ZdbSLInspect for CLI formatting with
  shared codecs; cover loading it in the sqlite3 CLI. Do not inherit PostgreSQL-specific extension packaging
  or its currently disabled test traversal.

Follow `Build system / Module layout` and `Build configuration`, `Code style /
Header layout`, and `Naming / Casing and member prefixes`. Do not add `util`,
`bench`, custom build scripts, generated configuration parsers or an extension
subdirectory: the store library, inspection shared library and their tests fit
the standard `src`, `test` and `itest` layout.

Gate: configure with SQLite enabled/disabled and PostgreSQL disabled; build and
load the plugin successfully before implementing the full data path.

## 4. Implement connection ownership and lifecycle

Both `ZdbPQ` and `ZdbSL` use Zdb's sharded threading model with a single
writer thread per store. Application row/cache state stays on its DB shard;
store calls cross to the configured store/writer thread using `run`/`invoke`.
For the initial SQLite implementation, that thread also executes the ordered
read queries on its connection, using direct scheduler posts to the same
store-wide worker.

The writer owns the SQLite connection, prepared statements, schema
operations, transaction state and table high-water state. DB
shards own rows and caches. Rx/Tx threads retain their existing replication I/O
roles. SQLite has no backend socket receive thread; `sqlite3_step` returns
results on the connection owner. Deliver store callbacks there as PQ does;
existing Zdb adapters post row work back to its owner. Never capture SQLite
column pointers in cross-thread posts: materialize a pooled result buffer while
the statement row is valid. Connection ownership follows
[SQLite's threading rules](https://www.sqlite.org/threadsafe.html).

- Use exactly one SQLite worker thread per store, at the configured
  `ZmScheduler` SID. That thread owns the single connection, all prepared
  statements, the statement cache and the entire table registry, and executes
  all SQLite calls, including reads, writes, startup and teardown. Public
  asynchronous methods post retained buffer handles and callbacks to that same
  thread. Rely on `ZmScheduler` for work ordering.
- `ZdbSL` needs no `ZiEventLoop`: there is no server descriptor, handle or
  socket to monitor. Post lambdas with capture-packs directly from any submitting
  thread to the SQLite worker through `ZmScheduler`. Retain buffer handles,
  callbacks and fixed metadata in those captures; keep mutable SQLite state on
  the worker. Use scheduler-managed callable storage and wakeups, with no
  store-owned `Work` queue, sent list, queue pump or event-loop wake callback.
- Use the established `run`/`invoke` dispatch idioms, preserving asynchronous
  public lifecycle completion. A posted worker lambda may execute consecutive
  synchronous SQLite startup/open steps directly; do not repost each step merely
  to mimic libpq send/receive phases. Teardown uses one same-SID tail
  continuation after fencing admission, so work already queued around the fence
  completes or rejects before resources close. Do not stop a
  shared application scheduler from store stop; its owner shuts it down after
  store completion.
- Work ordering is a `ZmScheduler` responsibility. `ZdbSL` relies on its
  guarantees; no store-level ordering machinery or scheduler-ordering audit
  is required.
- Align initialization, startup, table open/close, shutdown, and finalization
  with `ZdbPQ`'s lifecycle and asynchronous completion sequencing. Preserve its
  admission checks and repeat-call behavior, including already-open errors;
  do not reinterpret every lifecycle entry point as an unconditional no-op.
  Validate configuration once and treat failure of an applied SQLite setting as
  startup failure.
- Configuration: `thread`, `connection` as the local database path,
  and `synchronous` (`NORMAL` by default; also accepts `FULL` and `OFF`).
  Always report
  `InitData::replicated = false` as a store capability, not a replication
  enable switch. Replication configuration belongs to the application/Zdb,
  not to `ZdbSL`.
- Use WAL with configurable `synchronous`: default to `NORMAL`, and support
  `FULL` and `OFF`. Let `ZfStruct`/`ZtEnum` parse and reject unsupported values;
  accept the result returned while applying each PRAGMA instead of issuing a
  second verification query. Document each mode's durability semantics and filesystem
  assumptions. SQLite WAL permits
  one writer at a time and requires local shared-memory access. Use the
  SQLite default auto-checkpoint policy; do not expose a knob or add checkpoint
  code without a measured requirement. External readers can delay checkpoint
  progress. See
  [SQLite WAL documentation](https://www.sqlite.org/wal.html).
- Report SQLite busy/locked through the operation callback. Do not add retries,
  timers, sleeps or a configurable busy timeout in the initial version.
- Complete table close after its dependent callbacks, retaining statements for
  same-connection reopen. Store teardown finalizes all statements and closes
  the connection after draining; structural finalization leaves no outstanding
  handles, buffers or callbacks.

Guideline references for this section: `Audit flags / Algorithm and control
flow`, `Framework fit`, `Scope and cleanup`, and `Unnecessary atomic operations,
locking and copies`; `Leveraging Key Z Framework Capabilities / Concurrency and
sharding`, `Callbacks`, and `I/O and system integration`; `I/O sharding /
Cross-Shard Handoffs`, `Lifetime`, and `Teardown`.

### State ownership and data members

Use the following member names in `ZdbSL`, retaining PQ's names for matching
roles. Configuration/structural members are initialized by `init` before work
starts and released by `final` after shutdown completion. During operation,
all mutable store/table state belongs to the one SQLite worker. Submitting
threads retain only the handles and metadata captured for dispatch.

| `Store` member | Type / initial value | Role and lifetime |
| --- | --- | --- |
| `m_connection` | `ZtString<>` | Configured SQLite file path; PQ's connection-setting name. |
| `m_mx` | `ZiMultiplex * = nullptr` | Borrowed application scheduler, as in PQ; dispatch through run/invoke, without a SQLite event loop. |
| `m_sid` | `unsigned = 0` | The single configured SQLite worker SID. |
| `m_nShards` | `unsigned = 0` | Configured Zdb shard count, not a SQLite worker count. |
| `m_failFn` | `FailFn` | Store-fatal notification retained from init until final. |
| `m_storeTbls` | `ZmRef<StoreTbls>` | Thread-affined intrusive `ZmHash` keyed by (internal, logical ID), using `ZmNoLock`; the table object and node share one named-heap allocation. Clean it in final after draining. |
| `m_conn` | `sqlite3 * = nullptr` | Single worker-owned connection; acquire during start, close and null during teardown. |
| `m_stopping` | `bool = false` | PQ-named admission fence; detailed below. |
| `m_synchronous` | `Synchronous::T = Synchronous::Normal` | `ZtEnum` configuration value accepting NORMAL/FULL/OFF text, applied at startup. |

Use SQLite's default auto-checkpointing for the initial implementation. Do not
run an explicit final checkpoint: closing the sole connection and SQLite's WAL
handling supply the required teardown, while a checkpoint could be delayed by
external readers. No store-owned timers or background workers are needed.
Temporary startup inspection statements are scoped to the worker task and
finalized before it returns;
the persistent query statement cache belongs to each table, below. Configure
no operational knobs beyond the required synchronous mode. These are not SLOs.

Do not carry over PQ's `m_connFD`, `m_eventLoop`, `m_queue`, `m_sent`,
`m_syncSRM`, `m_oids`, `m_storedNShards`, `m_startState`, `m_startFn` or
`m_stopFn`. Do not add PQ's snake-case `m_id_` or statement-prefix `m_stmtID`;
SQLite physical names use the quoted namespace-prefixed logical ID directly.
A non-null `m_conn` is the running sentinel; `m_stopping` fences admission.
Startup and stop callbacks live in scheduler capture packs.

| `StoreTbl` member | Type / initial value | Role and lifetime |
| --- | --- | --- |
| `m_store` | `Store * = nullptr` | Raw owner back-pointer, valid through table work and draining. |
| `m_id` | `IDString` | Logical table ID used in results and metadata. |
| `m_relation` | `SQLString` | Quoted physical SQLite relation name. |
| `m_fields`, `m_updFields`, `m_keyFields` | `ZfVFieldArray`, `UpdFields`, `ZfVKeyFieldArray` | Immutable reflected all/update/key field descriptors. |
| `m_xFields`, `m_xUpdFields`, `m_xKeyFields` | `XFields`, `XFields`, `XKeyFields` | Immutable SQLite codec/serialization descriptors for those field sets. |
| `m_keyGroup`, `m_fieldMap` | `KeyGroup`, `FieldMap` | Group-key lengths and field-name lookup, retaining PQ names. |
| `m_bufAllocFn` | `IOBufAllocFn` | Result buffer allocator retained for the descriptor lifetime. |
| `m_internal` | `bool` | Namespace identity, set at construction. |
| `m_openState` | `int8_t = OpenState::Closed` | Worker-owned table state: Closed, Opened or Reopen. `OpenState` is an internal namespace enum. |
| `m_count` | `uint64_t = 0` | Row count recovered during open/reopen, with PQ's role. |
| `m_maxUN` | `MaxUN` | Per-Zdb-shard high-water array sized from m_nShards. |
| `m_maxSN` | `SN = ZdbNullSN()` | Table sequence high-water value. Publish write maxima only after commit. |
| `m_stmts` | `Stmts` | SQLite statement cache: one metadata-sized `ZtArray<KeyStmts>` plus recover, insert, update, del and mrd handles. All pointers start null and are finalized/null on failed open or connection teardown. |

`KeyStmts` is a public data struct containing count, selectKIX, selectKNX,
selectKNI, selectRIX, selectRNX, selectRNI and find handles for one key. `Stmts`
contains one array of these structs sized to the actual key count, using a named
ZdbSL heap, plus its scalar handles. This consolidates eight per-key arrays into
one allocation and keeps handles used together adjacent. Open-phase count and
high-water inspection statements are phase-local. Each schema inspection
statement is fully consumed/finalized in its worker task; never retain SQLite
row pointers.
Successful table close
retains cached statements on the same connection and enters `Reopen`, matching
PQ; closing a table whose open failed enters `Closed`. Connection teardown
finalizes all cached handles and resets every `m_openState` to `Closed`.
OpenFn and CloseFn remain owned by their posted worker captures; no callback
member is needed. Per-operation buffers, callbacks, result progress and errors likewise
live in the worker lambda/capture-pack or scoped execution state, not a Work
queue or new store-wide current-operation member. Transactions execute within
one worker task; commit/rollback and its scoped cleanup finish before returning.

Do not copy PQ's packed transport-phase wrappers. They preserve state across
libpq send/receive turns; SQLite completes each call synchronously in one worker
turn. `OpenState` is an internal namespace enum stored as `int8_t`, not `enum
class`, and contains only the table states listed above. `m_conn` and
`m_stopping` fully describe store lifecycle. Use `switch` for table-state
dispatch. Loop locals cover field, key and shard iteration; the persisted shard
count is a startup local. Cache invariant counts before loops. Set terminal table
state before invoking a completion. This applies `Enums`, `Audit flags / Code
structure`, `Algorithm and control flow`, `Framework fit`, and `Naming`.

### Admission fence: `m_stopping`

Use `bool m_stopping = false;` in `ZdbSL::Store`, with
`bool stopping() const { return m_stopping; }`, matching `ZdbPQ::Store`.
This is SQLite-worker-owned state: read and write it only on the configured
store SID. Public methods post their capture-packs first; they do not inspect
this flag on the submitting thread. No atomic or lock is needed.

- In the worker lambda posted by `stop(StopFn fn)`, set `m_stopping = true` and
  move `fn` into the `stop_1` capture posted to the same SID. This assignment
  closes admission; the caller's invocation of `stop` is not the fence itself.
- At the beginning of the worker lambda for `Store::open` and
  `StoreTbl::{count,select,find,recover,write}`, check `stopping()` or
  `m_store->stopping()` before accessing SQLite resources or admitting work.
  If true, return immediately after delivering the operation's error callback
  with "DB shutdown in progress", using the corresponding OpenResult,
  CountResult, TupleResult, RowResult or CommitResult as PQ does. For `write`,
  pass the retained original buffer to CommitFn as well as the error.
- Work admitted before the fence retains its completion obligations. Its
  synchronous SQLite operation completes before the stop lambda can run because
  both execute on the same scheduler thread. Work queued after that lambda sees
  the fence and completes its rejection callback without touching SQLite before
  the queued `stop_1` tail continuation runs.
- Keep `m_stopping` true throughout draining, connection closure and StopFn
  completion. Failure paths that enter store teardown also set it true on the
  worker before cleanup. Reset it to false only in the next valid worker-side
  `start` initialization. It is an admission fence, not a startup-readiness flag
  or a substitute for lifecycle completion state.
- Rely on `ZmScheduler` queue ordering and the one tail continuation as the drain
  barrier; do not maintain an outstanding count or post polling continuations.
  Keep the store/table objects
  alive through stop completion. Move the StopFn capture once into `stop_1` and
  invoke it exactly once after teardown.

### Lifecycle contract: align with ZdbPQ

This is a primary implementation requirement. Public completion remains
asynchronous; consecutive synchronous SQLite calls inside its worker task need
no artificial asynchronous boundary.

| Stage | Required ZdbSL behavior |
| --- | --- |
| `init` | Synchronous configuration/structural initialization, as defined by the store interface and PQ. Resolve the writer SID, validate configuration, retain shard count/callbacks and initialize the registry. Return InitResult with replicated=false; connection opening and schema work belong to start. |
| `start` | Post to the configured writer with StartFn in the capture pack. If `m_conn` is already non-null, complete success idempotently. Otherwise clear `m_stopping` and run connection/setup there. Returning from start is not completion. |
| Worker startup | Open/configure SQLite, create/validate metadata, initialize/validate shard topology and create MRD sequentially in that task. On success invoke the captured StartFn once; non-null `m_conn` is the resulting running state. |
| Startup failure | Release every partially acquired SQLite resource on the worker, null `m_conn`, then invoke the captured StartFn once with the error. No retained failure state or second cleanup turn is needed. |
| Table `open` / `close` | Preserve asynchronous OpenFn/CloseFn completion, already-open checks, same-connection reopen sequencing and namespace identity. Schema, statement preparation and high-water recovery run sequentially in the posted open task. The queued close task runs after earlier table work and enters Reopen with cached statements after successful open or Closed after failed open. |
| `stop` / `stop_` | Post `stop_`; the first call sets m_stopping, then moves StopFn into one `stop_1` tail continuation on the same SID. Earlier admitted work has completed; queued later work rejects through its callback before the tail runs. A repeated stop posts its completion behind the first tail, or completes success if `m_conn` is already null. No callback list is needed. |
| `stop_1` and StopFn | Finalize statements, close/null SQLite, reset table connection/open state, then invoke the captured StopFn once. No callback or stop-state member is needed. |
| `final` | Synchronous structural cleanup after shutdown completion, matching PQ and the interface. Release callbacks/registry/configuration-owned resources; do not use final as a substitute for asynchronous stop or destroy state while its work is pending. |

Graceful teardown still drains work: FIFO execution on the sole worker means all
previously admitted operations, including commit/rollback and callbacks, finish
before `stop_` begins. `stop_` fences admission and posts `stop_1` behind work
already present on the SID; `stop_1` closes resources and completes StopFn.
Table close uses the same queue-barrier principle. No outstanding counter,
secondary stop state, timer, polling loop or retry continuation is justified.

Retain callback ownership in capture packs and the admission fence. Follow PQ's
callback movement and error behavior, substituting SQLite resources for PG
resources. Eliminate OID discovery, socket/pipeline mechanisms, the event loop,
explicit PQ work queue, phase continuations and their bookkeeping.

Verification during implementation must cover completion ordering, partial
startup failure at each SQLite step, stop with queued queries/writes,
close/reopen, repeated lifecycle calls, and finalization after completion.
Use deterministic scheduler barriers to prove no callback fires twice or
outlives its owner. No build or test execution is part of this plan update.

### Mandatory scratch-storage policy: align with ZdbPQ

Stack-first scratch with heap fallback is an implementation requirement for
`ZdbSL`, not an optional later optimization. The current PQ files use
`ZmScratch` and `ZmLocal` directly; they do not directly call `ZtScratch`.
Use `ZtScratch` where the SQLite implementation needs temporary `ZtArray` or
`ZtString` operations, and `ZmScratch` for runtime-sized spans/tuples and byte
conversion buffers. Preserve the allocation strategy while eliminating temporary
storage that SQLite's typed binding makes unnecessary.

Concrete PQ allocation sites and mandatory SQLite counterparts:

| PQ site | Existing allocation | Required SQLite counterpart |
| --- | --- | --- |
| `Store::sendQuery`, CC:1116–1119 | `ZmScratch(Oid, n)`, `ZmScratch(const char *, n)`, and two `ZmScratch(int, n)` arrays for parameter types/values/lengths/formats | SQLite binds parameters directly; omit all four arrays. |
| `Store::sendPrepared`, CC:1178–1180 | `ZmScratch` parameter pointer/length/format arrays | Use cached statement handles and direct typed binding; omit all three arrays. |
| `IDAlloc`, CC:2247–2249 | `ZmLocal(IDString)` | Omit statement-name construction and this allocation; select the cached SQLite handle directly. |
| `ParamAlloc`, CC:2251–2252 | `ZmScratch(Value, unsigned(nParams), Tuple::VHeap)` | Omit the Value tuple. Borrow reflected String/Bytes spans directly and use at most one scoped `ZdbMem::Value` adapter at a time for the remaining established codecs, encoding into the canonical conversion span before that adapter is destroyed. This reuses the framework type taxonomy without retaining an owning array. |
| `VarAlloc`, CC:2254–2270 | Count variable parameters, allocate `ZmScratch(VarBufPart, nVars, VarBufParts::VHeap)`, calculate total encoded size, then `ZmScratch(uint8_t, varBufSize_, VarBuf::VHeap)` | Retain the sizing pass, runtime-sized offset/length descriptors, and one conversion byte buffer with equivalent ZdbSL-named fallback heaps. Encode each value directly into its assigned span; no separate heap allocation or intermediate contiguous copy per field/vector element. |
| `StoreTbl::select_rcvd`, CC:2366 | `ZmScratch(Value, nc)` | Eliminate the Value tuple: serialize each SQLite column through its `xField`/Zfb adapter into pooled output, retaining at most the current field value. |
| `StoreTbl::find_rcvd_`, CC:2461 | `ZmScratch(Value, nc)`, including four metadata values | Eliminate the Value tuple: serialize metadata and fields from the current SQLite row, retaining at most the current field value. |
| `saveTuple`, HH:1580 | `ZmScratch(Offset, n)` backing the two-pass `Offsets` list | Require equivalent runtime-sized offset scratch for FlatBuffer serialization. Preserve stack-first allocation and heap fallback; do not heap-allocate an offsets container per returned row. |

In PQ, `ParamAlloc` and `VarAlloc` serve query and mutation paths because libpq
needs parallel parameter arrays. SQLite does not: eliminate `ParamAlloc`, bind
fixed values directly, and allocate only the encoded variable bytes that must
remain alive through `sqlite3_step`. If one combined variable buffer needs an
offset list, use `ZmScratch` for both with named `ZdbSL` heaps. Do not preserve a
PQ allocation merely for symmetry.

Framework mechanics to preserve:

- [ZmScratch.hh](zm/src/ZmScratch.hh) uses remaining-stack checks and falls back
  to scoped heap storage. Cache counts/byte sizes in locals before invoking its
  macro, which may evaluate size arguments multiple times. Allocate uninitialized
  storage and construct only the elements needed; manage constructed lengths
  correctly for destruction.
- [ZtScratch.hh](zt/src/ZtScratch.hh) provides stack-backed Z array/string
  operations through `ZmAlloc`, with the container's VHeap fallback. Use it for
  temporary SQL/schema assembly or growable scratch when those operations are
  needed. Initial capacity follows metadata or measured workload size, not an
  arbitrary hard limit. Permanent table descriptors and cached SQL/statement state
  retain their appropriate named Z heap/container ownership. Captured callables
  use scheduler-managed storage.
- [ZmLocal.hh](zm/src/ZmLocal.hh) provides stack-availability-aware local-object
  allocation with heap fallback. Its use in PQ is real allocation policy,
  despite the nearby generic comment mentioning `ZmAlloc`.
- Allocate scratch inside the writer operation that consumes it. Do not
  capture stack-backed spans, pointers, or scratch objects in later continuations.
  Even if a particular invocation falls back to heap, the scratch is still scoped
  storage and must not escape.
- Borrow input String/Bytes data where their retained buffer lifetime permits.
  A SQLite binding that borrows scratch must be consumed and cleared/rebound
  before that scratch expires; resetting a statement alone does not clear its
  bindings. Do not keep bindings pointing into expired scratch between calls.
- Serialize borrowed SQLite column data into the pooled output `ZiIOBuf` before
  advancing/resetting the statement. Output I/O buffers and queued work remain
  pooled/owned objects; do not replace those with stack scratch.
- Scope allocation so repeated row processing cannot accumulate stack storage
  across a long select. Reuse per-query scratch or allocate inside a function
  that returns for each row; avoid repeated alloca-backed macros in one long-lived
  loop frame.

Implementation review must account for each site above, either as an explicit
stack-first equivalent or as an allocation eliminated by direct binding/output.
Future validation must exercise both normal stack allocation and heap fallback
with large field/vector payloads and verify cleanup across errors, phase
transitions and teardown. No SLO or build/test execution is introduced by this plan update.

Guideline references for every allocation choice above: `Audit flags / Storage
and capacity`, `Heap allocation`, `Data movement and initialization`, and
`Scope and cleanup`; `Leveraging Key Z Framework Capabilities / Storage and
lifetime`, `Callbacks`, and `Metadata`; `I/O sharding / Cross-Shard Handoffs`.

## 5. Implement persistence and all table operations

- Build metadata-driven field/key descriptors, create missing tables/indices,
  and validate existing schemas during startup/open before preparing statements.
  Cache statements per table, retain them on successful same-connection close,
  and finalize them on failed open or store teardown.
- Apply the UN replay gate first; replayed writes complete successfully without
  a transaction or high-water change. Otherwise implement the VN-selected
  mutation, index changes and required MRD updates in one transaction. Roll back
  partial failures. Invoke a successful `CommitFn` after that transaction commits;
  retain its original replication
  buffer until completion. This does not change the asynchronous meaning of the
  application's `commit()` return value or imply quorum durability.
- Implement indexed group counts, primary/secondary finds, key and row selects,
  all continuation modes, and exact shard/UN recovery. Preserve the 1:1 column
  mapping throughout SQL generation and result decoding. Preserve result buffer
  shape, sequence metadata, cumulative tuple counts, and callback termination.
- For each public table operation use one thin dispatcher and one worker-only
  trailing-underscore implementation: `count_`, `select_`, `find_`, `recover_`
  and `write_`. A worker function binds, steps, handles rows/errors, resets and
  completes its callback in one scope. Do not reproduce PQ's send/receive/fail
  split for synchronous SQLite execution; factor only shared statement cleanup
  and row serialization used by more than one operation.
- Implement `warmup` as a no-op, matching both existing backends.
- Bind from retained buffers where lifetimes permit; write results directly into
  pooled `ZiIOBuf` storage through Z serialization facilities. Account for binding
  lifetime and statement reset/clear semantics explicitly. See
  [SQLite binding ownership rules](https://www.sqlite.org/c3ref/bind_blob.html).
- Translate SQLite errors into existing result and failure channels, preserving
  useful extended error details. Cover constraint failures, read-only files,
  disk exhaustion, I/O errors, corruption, and contention without duplicate
  callbacks or success after failed commit.

Guideline references for these operations: `General principles` (latency and
clean teardown), `Audit flags / Data movement and initialization`, `Algorithm
and control flow`, `Framework fit`, and `Liveness`; `Leveraging Key Z Framework
Capabilities / Metadata`, `Callbacks`, and `Persistence`.

Gate: the operation matrix passes against a temporary on-disk database across
close/reopen with caches discarded.

## 6. Verify persistence and integration with Zdb

- Unit tests under `test` cover every codec, boundary/sentinel value, vector
  element family, namespace mapping, and query construction. Require golden
  format-1 byte fixtures and the declared Float/Fixed normalization, including
  cross-platform decoding. Test value preservation at integer/decimal/time
  extrema and compare select/continuation
  output to SQLite query results, including sentinels and duplicate keys.
- Test single-column Time/DateTime BLOB fields in composite PKs/SKs, group prefixes,
  mixed-direction ordering and every continuation mode. Include adjacent
  nanoseconds, midnight/day boundaries, negative times and null sentinels.
  Verify decimal and float numerical order independently of the SQL query
  builder. Test CLI loading of ZdbSLInspect, exact formatted values and malformed
  arguments, and lossless .dump/restore of the encoded BLOBs.
- Integration tests under `itest` verify the default `NORMAL` setting, explicit
  `NORMAL`/`FULL`/`OFF` settings, and rejection of unsupported values through the
  generated config handler. Do not duplicate SQLite's own durability test suite.
- Integration tests under `itest` exercise every store method, changed secondary
  keys, conflicting keys, replayed and skipped mutations, all-deleted tables,
  multiple shards, independent namespaces, maximum IDs, and incompatible storage
  formats/topologies. Test fresh schema creation, repeated unchanged startup,
  new tables, missing indices on compatible tables, and interrupted creation.
  Verify that inconsistent existing field/type/index definitions fail open
  without altering stored data. Check preserved data and sequence histories
  after creation, validation failures, and subsequent reopen attempts, including
  internal saga tables.
  Reuse existing Zdb fixtures where suitable without making SQLite depend on
  PostgreSQL availability.
- Use child processes and explicit synchronization to test abrupt process loss
  before commit, after commit but before callback, and during deletion/metadata
  updates. Reopen in a fresh process and verify an atomic old/new state, replay
  safety, counts, and sequence histories. Distinguish process-crash tests from
  actual power-loss guarantees.
- Run existing Zdb replication behavior against separate SQLite files by
  adapting `zdbreptest`/`zdbsagareptest` application configuration. These are
  integration checks of the store contract, not replication work in `ZdbSL`: initial
  catch-up, follower persistence, disconnection/rejoin, leader loss/promotion,
  deletes, and restart with empty caches. Exercise saga internal tables across
  shards, replay, compensation, and interrupted recovery.
- Test shutdown under pending reads/writes and failure, lock contention from a
  separate external test connection, and sustained bounded memory use. The store
  itself still has only one connection.
  Use deterministic synchronization, not sleeps, to await concurrent work.
- Inspect `EXPLAIN QUERY PLAN` once for representative point, range and recovery
  queries so the intended indices are actually used. Review hot paths for the
  audit-flag allocations/copies. Do not add a benchmark harness, telemetry or
  numerical performance gate; no SLO exists. Keep one transaction per admitted
  write and one connection/worker; batching is outside the initial version.
- Build through `z.config` and top-level `make -j8`, then run module and affected
  Zdb tests. Follow clean rebuild requirements for build-mode changes. Cover gcc
  and clang, Linux x64/ARM and Windows MinGW as available; run clang ASan/LSan and
  Valgrind via `libtool exec`. Record unavailable platform checks explicitly.

This test split follows `Build system / Module layout`, `Leveraging Key Z
Framework Capabilities / Tests`, `Audit flags / Algorithm and control flow`,
and `Acceptance`: isolated codec/query tests use `ZuTest`, persistence/process/
replication tests live in `itest`, and concurrent completion uses `ZmBlock` or
`ZmSemaphore`, never timing sleeps. Test each invariant at its owning layer;
do not repeat schema-domain checks in every operation test.

## 7. Document and finish

Document configuration, module loading, dependency versions, type/index format,
single-column date/time encodings, ZdbSLInspect loading and readable exports versus raw
SQL dumps, startup schema creation/validation, rejection of inconsistent schemas, durability and callback
semantics, the boundary between store persistence and app-configured Zdb replication,
SQLite's default checkpoint behavior, error recovery, and test commands in `zdb_sqlite/README.md`.
Explain safe backup/restore of an active database including WAL state, filesystem
requirements, and the supported process-access model. Update `zdb/README.md` and
top-level dependency/build documentation with the new backend.

Use existing Z configuration and value formatting in examples; do not add an
independent documentation formatter or parser. This follows `Leveraging Key Z
Framework Capabilities / Formatting` and `Metadata`.

Final acceptance: all contract/type matrix entries have passing tests, the plugin
installs and loads without PostgreSQL, data and sequence state survive fresh-process
restarts, app-configured Zdb replication and sagas pass their integration checks
with independent persistent stores, and
the allocation/query-plan review and any remaining limitations are documented.

## 8. Function-by-function mapping from ZdbPQ to ZdbSL

This inventory covers `zdb_pq/src/ZdbPQ.hh` and `zdb_pq/src/ZdbPQ.cc`.
HH/CC line numbers identify source functions at the time of review and may
shift with subsequent edits; they are not proposed SQLite line numbers. A declaration and its
out-of-line definition are one function; header-only overloads are listed by
constraint/type family, with macro expansions explicitly enumerated. Local
lambdas are part of their enclosing function. Framework-generated constructors,
ADL tags, and type traits are accounted for separately below.

**Yes** means the responsibility is needed and must have an explicit SQLite
implementation. **No** means the PostgreSQL-specific function is omitted; the
row explains where any remaining responsibility goes. Preserve asynchronous
public startup/open/shutdown completion, while folding PQ `_send`/`_rcvd`
splits that exist only for libpq transport into the owning worker function.
Do not preserve a function, state or name merely for structural resemblance.

All SQL execution, binding, decoding of SQLite columns, and mutable store state
in this inventory belongs to the single writer thread described in section 4.
Pure codecs and metadata utilities run in their caller's context on caller-owned
storage. The isVar constraints below identify PQ overloads, not a requirement
to reuse PQ's SQLite scratch classification. All temporary storage in this mapping must follow section 4's mandatory
stack-first scratch policy and its explicit PQ allocation-site counterparts. Public entry points dispatch to the writer; accessors do not authorize
cross-shard mutable access. Startup runs on the worker; finalization is
synchronous structural cleanup after the lifecycle drain, with no SQLite calls.

SQLite execution uses prepare, bind, step, reset/clear-bindings, and finalize;
`SQLITE_ROW`, `SQLITE_DONE`, and errors replace PGresult/pipeline states. See
[SQLite statement lifecycle](https://www.sqlite.org/c3ref/stmt.html).
DDL inspection uses `sqlite_schema` and table/index PRAGMAs; WAL and synchronous
settings are applied once and their returned result is consumed during connection startup. See
[SQLite PRAGMAs](https://www.sqlite.org/pragma.html).

### Header codecs and utilities

All extended values use the canonical column encodings from section 2. There
are no PostgreSQL OIDs, array lower bounds, or PostgreSQL inet headers in the
SQLite format. Codec adaptation changes representation, not the selected row
or the order returned by SQLite. The mapped `loadValue` responsibilities belong
to compile-time `bindField` adapters, and `saveOffset`/`saveValue` responsibilities
belong to `saveField` adapters. These names describe SQLite work and stay under
the 28-byte naming limit; do not retain PG names as forwarding wrappers.

| ZdbPQ function / overload | Relevant? | SQLite implementation |
| --- | --- | --- |
| `vecSize(n, size) [HH:115]` | Yes | Compute SQLite vector BLOB size from its count/element layout; use the U32 count and specified element widths, without PG VecHdr or per-element headers for fixed-width elements. |
| `vecVarSize(n, l) [HH:120]` | Yes | Sum count/length framing and variable element bytes for the SQLite BLOB, using the sizing callable. |
| `vecInit(buf, oid, n) [HH:126]` | Yes | Initialize SQLite count/framing and advance the destination span; remove the OID argument and PG dimensions/lower bound. |
| `vecAppend(buf, length, l) [HH:132]` | Yes | Write variable-element length/data directly into the destination; fixed-width vectors need no redundant element length. |
| `vecHdr(buf) [HH:139]` | Yes | Read SQLite vector framing and advance a bounded source span; do not reinterpret it as PG VecHdr. |
| `validateVecHdr(hdr) [HH:145]` | Yes | Validate SQLite count/size consistency needed for decoding; no ndim or one-based lower-bound rules. Return decoding failure through the query result. |
| `vecElem(buf, l) [HH:156]` | Yes | Walk a SQLite element using its fixed width or stored length, call the decoder, and advance the source. |
| `IP::ziIP() const [HH:201]` | Yes | Decode the canonical address family and address bytes into ZiIP; no PG family/bits/is_cidr fields. |
| `IP::ziIP(ZiIP) [HH:221]` | Yes | Encode the Z address family and exact address bytes; preserve the agreed null representation without imposing an in-memory sort order. |
| `isVec(unsigned) [HH:322]` | Yes | Retain compile-time vocabulary classification by Z field type ID; no runtime Value union. |
| `isVar(unsigned) [HH:324]` | Yes | Classify SQLite conversion scratch requirements: native INTEGER and borrowed String/Bytes need no encoding buffer; every converted scalar/vector/bitmap/IP BLOB needs its exact section 2 byte count. Do not reuse PG classifications as FlatBuffer offset classifications. |
| `Value::load<I>(data,length)`: void, String, Bytes, variable families, fixed families and IP | No | Do not port the PG result-value union. `xField` adapters read the current SQLite column and serialize it directly before step/reset, checking only BLOB framing/width needed for safe access. |
| `Value::data<I>()`: void, String, Bytes, variable families, IP and fixed families | No | Direct typed SQLite binding replaces every generic PG data-pointer accessor. |
| `Value::length<I>()`: void, String, Bytes, variable families, IP and fixed families | No | Bind text/BLOB spans with their existing length; do not store parallel lengths in a Value union. |
| `Value::print_<I>(s)`: void, scalar, IP, Bitmap and every vector family | No | ZdbSL does not define or print its own Value union. Its per-field adapter reuses `ZdbMem::Value`; store errors print field IDs/types with Z formatting, and CLI functions decode through shared codecs and print the concrete Z type. |
| `Value::print(s) [HH:634]` | No | Delete the discriminator printer with Value. |
| `varBufSize<Type>: !isVar(Type)` | Yes | Adapt this sizing responsibility: return zero for native INTEGER and borrowed String/Bytes; return the fixed section 2 size for converted scalar BLOBs (12 bytes for Time/DateTime) and the family-dependent size for IP. Include these in the same scratch sizing pass as vectors/bitmap. |
| `varBufSize<Type>: Bitmap` | Yes | Calculate the exact canonical Bitmap BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: StringVec` | Yes | Calculate the exact canonical StringVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: BytesVec` | Yes | Calculate the exact canonical BytesVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: Int8Vec` | Yes | Calculate the exact canonical Int8Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: UInt8Vec` | Yes | Calculate the exact canonical UInt8Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: Int16Vec` | Yes | Calculate the exact canonical Int16Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: UInt16Vec` | Yes | Calculate the exact canonical UInt16Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: Int32Vec` | Yes | Calculate the exact canonical Int32Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: UInt32Vec` | Yes | Calculate the exact canonical UInt32Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: Int64Vec` | Yes | Calculate the exact canonical Int64Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: UInt64Vec` | Yes | Calculate the exact canonical UInt64Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: Int128Vec` | Yes | Calculate the exact canonical Int128Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: UInt128Vec` | Yes | Calculate the exact canonical UInt128Vec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: FloatVec` | Yes | Calculate the exact canonical FloatVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: FixedVec` | Yes | Calculate the exact canonical FixedVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: DecimalVec` | Yes | Calculate the exact canonical DecimalVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: TimeVec` | Yes | Calculate the exact canonical TimeVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `varBufSize<Type>: DateTimeVec` | Yes | Calculate the exact canonical DateTimeVec BLOB conversion size from reflection data; omit PostgreSQL array headers/OIDs. Compute the format-1 size from the fixed rules in section 2. |
| `OIDs::name(unsigned) [CC:20]` | No | `xField` selects SQLite storage class and codec at descriptor construction; no runtime type-name registry is needed. |
| `OIDs::oid(unsigned) [HH:765]` | No | No server-assigned type OIDs; direct metadata discriminator selects the SQLite codec. |
| `OIDs::match(oid, type) [HH:769]` | No | Schema inspection compares the `xField` descriptor with physical and logical metadata directly; do not introduce an OID-like matcher. |
| `OIDs::oid(name) [HH:775]` | No | No name-to-server-OID lookup or runtime type discovery. |
| `OIDs::init(i, oid) [HH:782]` | No | No OID cache population; use local SQLite type descriptors. |
| `loadValue<Type>(ptr, field, fbo): isVar(Type) no-op` | No | PG split-dispatch placeholder; SQLite binding dispatch only instantiates a valid conversion path. |
| `loadValue<Type>(ptr, field, fbo): void no-op` | No | Direct field dispatch never binds void; do not retain a placeholder. |
| `loadValue<Type>(ptr, field, fbo): String` | Yes | Borrow the FlatBuffer string span for explicit-length text binding. |
| `loadValue<Type>(ptr, field, fbo): Bytes` | Yes | Borrow the FlatBuffer bytes for explicit-length BLOB binding. |
| `loadValue<Type>(ptr, field, fbo): Bool` | Yes | Read the reflected boolean and bind integer 0/1. |
| `loadValue<Type>(ptr, field, fbo): Float` | Yes | Read the reflected double and encode as the mandatory 8-byte F64 BLOB, including canonical zero/NaN handling. |
| `loadValue<Type>(ptr, field, fbo): Fixed` | Yes | Use ZfbTransform::Fixed, convert ZuFixed to ZuDecimal as PQ does, then use the shared 16-byte S128 decimal codec, including the explicit null mapping; no floating conversion. |
| `loadValue<Type>(ptr, field, fbo): Decimal` | Yes | Use ZfbTransform::Decimal and the shared 16-byte S128 decimal codec also used by Fixed. |
| `loadValue<Type>(ptr, field, fbo): Time` | Yes | Encode S64(sec) followed by S32(nsec) into 12-byte scratch and bind one BLOB column. |
| `loadValue<Type>(ptr, field, fbo): DateTime` | Yes | Load julian/sec/nsec via ZfbTransform::DateTime, encode three S32 components into 12-byte scratch and bind one BLOB column; do not convert through ZuTime. |
| `loadValue<Type>(ptr, field, fbo): Int128` | Yes | Load through ZfbTransform::Int128 and encode the full-width canonical BLOB. |
| `loadValue<Type>(ptr, field, fbo): UInt128` | Yes | Load through ZfbTransform::UInt128 and encode the full-width canonical BLOB. |
| `loadValue<Type>(ptr, field, fbo): IP` | Yes | Load the FlatBuffer union through ZfbTransform::IP and encode SQLite family/address bytes. |
| `loadValue<Type>(ptr, field, fbo): Int8` | Yes | Read the reflected Int8; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): UInt8` | Yes | Read the reflected UInt8; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): Int16` | Yes | Read the reflected Int16; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): UInt16` | Yes | Read the reflected UInt16; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): Int32` | Yes | Read the reflected Int32; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): UInt32` | Yes | Read the reflected UInt32; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): Int64` | Yes | Read the reflected Int64; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, field, fbo): UInt64` | Yes | Read the reflected UInt64; use the type-fixed format-1 representation: INTEGER for signed through 64 bits and unsigned through 32 bits, U64 BLOB for UInt64. Never choose a storage class by value magnitude. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): !isVar(Type) no-op` | No | Remove the unused PG variable-conversion dispatch branch. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Bitmap` | Yes | Encode Bitmap directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): StringVec` | Yes | Encode StringVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): BytesVec` | Yes | Encode BytesVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Int8Vec` | Yes | Encode Int8Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): UInt8Vec` | Yes | Encode UInt8Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Int16Vec` | Yes | Encode Int16Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): UInt16Vec` | Yes | Encode UInt16Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): UInt32Vec` | Yes | Encode UInt32Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Int32Vec` | Yes | Encode Int32Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Int64Vec` | Yes | Encode Int64Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): UInt64Vec` | Yes | Encode UInt64Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): Int128Vec` | Yes | Encode Int128Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): UInt128Vec` | Yes | Encode UInt128Vec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): FloatVec` | Yes | Encode FloatVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): FixedVec` | Yes | Encode FixedVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): DecimalVec` | Yes | Encode DecimalVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): TimeVec` | Yes | Encode TimeVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `loadValue<Type>(ptr, varBuf, part, oids, field, fbo): DateTimeVec` | Yes | Encode DateTimeVec directly into its assigned SQLite BLOB scratch span; use the exact element encoding from section 2, including S/U byte encoding for integer vectors whose scalar columns use INTEGER. Remove the OIDs argument and PG framing. |
| `Offsets::Offsets(Offset *) [HH:1155]` | Yes | Keep a caller-owned scratch offset list for two-pass FlatBuffer serialization, if the Zfb integration still needs it. |
| `Offsets::push(Offset) [HH:1157]` | Yes | Append a completed FlatBuffer offset to the scratch list. |
| `Offsets::shift() const [HH:1158]` | Yes | Consume the next offset when emitting reflected fields. |
| `saveOffset<Type>: String` | Yes | Decode SQLite String storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Bytes` | Yes | Decode SQLite Bytes storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Bitmap` | Yes | Decode SQLite Bitmap storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: IP` | Yes | Decode SQLite IP storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: StringVec` | Yes | Decode SQLite StringVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: BytesVec` | Yes | Decode SQLite BytesVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Int8Vec` | Yes | Decode SQLite Int8Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: UInt8Vec` | Yes | Decode SQLite UInt8Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Int16Vec` | Yes | Decode SQLite Int16Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: UInt16Vec` | Yes | Decode SQLite UInt16Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Int32Vec` | Yes | Decode SQLite Int32Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: UInt32Vec` | Yes | Decode SQLite UInt32Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Int64Vec` | Yes | Decode SQLite Int64Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: UInt64Vec` | Yes | Decode SQLite UInt64Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: Int128Vec` | Yes | Decode SQLite Int128Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: UInt128Vec` | Yes | Decode SQLite UInt128Vec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: FloatVec` | Yes | Decode SQLite FloatVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: FixedVec` | Yes | Decode SQLite FixedVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: DecimalVec` | Yes | Decode SQLite DecimalVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: TimeVec` | Yes | Decode SQLite TimeVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: DateTimeVec` | Yes | Decode SQLite DateTimeVec storage and emit the corresponding Zfb string/bytes/bitmap/IP/vector offset directly into the output builder. Use Zfb transforms for structured elements, not PG vecHdr/vecElem. |
| `saveOffset<Type>: remaining non-variable types` | Yes | No-op branch: scalar/inline-struct values need no FlatBuffer offset pass. |
| `saveValue<Type>: void` | Yes | Harmless empty alternative; do not invent a persisted payload. |
| `saveValue<Type>: String, Bytes, isVar(Type)` | Yes | Attach the offset produced by saveOffset at the reflected field position. |
| `saveValue<Type>: Bool` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Int8` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: UInt8` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Int16` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: UInt16` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Int32` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: UInt32` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Int64` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: UInt64` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Float` | Yes | Emit the native decoded scalar with its reflected default. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Fixed` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Decimal` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Time` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: DateTime` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: Int128` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: UInt128` | Yes | Use the matching ZfbTransform to emit the decoded inline struct. SQLite conversion is finished before this serialization step. |
| `saveValue<Type>: IP` | Yes | Emit the Zfb IP union discriminator and saved union offset together. SQLite conversion is finished before this serialization step. |
| `loadTuple<Tuple_>(...) [HH:1542]` | Yes | Walk the requested descriptor prefix/full tuple and bind directly; preserve mutable-field/key-0 update order and do not construct a Value tuple. |
| `saveTuple(...) [HH:1575]` | Yes | Serialize the selected fields to the required FlatBuffer tuple through Zfb integration, directly into pooled output storage before advancing the SQLite statement. |

### Header lifecycle state and accessors

Retain only the lifecycle states needed across public calls. SQLite's synchronous
worker execution removes PQ's transport phase and loop-progress accessors.

| ZdbPQ function | Relevant? | SQLite implementation |
| --- | --- | --- |
| `StartState::reset()` | No | No store state wrapper; reset means close/null `m_conn` and clear the worker-owned admission fence at valid start. |
| `StartState::create() const` | No | Create/inspect is local to the synchronous schema transaction. |
| `StartState::failed() const` | No | A failed start cleans up and leaves `m_conn` null. |
| `StartState::type() const` | No | Only needed to iterate PostgreSQL OID discovery, which is omitted. |
| `StartState::phase() const` | No | There is no packed phase wrapper or `m_startState`. |
| `StartState::phase(uint32_t)` | No | There is no stored startup phase. |
| `StartState::incType()` | No | No OID-discovery loop. |
| `StartState::{setCreate,clrCreate,setFailed,clrFailed}()` | No | Create/failure are local control flow; delete the packed-flag operations. |
| `OpenState::reset()` | No | Assign `Closed` directly during connection teardown. |
| `OpenState::{create,failed,field,keyID,shard,phase}() const` | No | Create/loop/error progress is local; compare `m_openState` directly for Closed/Opened/Reopen. |
| `OpenState::phase(uint32_t)` | No | Assign the enum directly. |
| `OpenState::{incKey,incShard,incField}()` | No | Ordinary bounded loops prepare/inspect synchronously on the worker. |
| `OpenState::{setCreate,clrCreate,setFailed,clrFailed}()` | No | Delete packed flags; failed open cleans up and stores Closed. |
| `StoreTbl::store() const` | Yes | Return the owning store back-pointer; lifetime protected by store teardown. |
| `StoreTbl::id() const` | Yes | Return the stable logical table ID used in result messages and diagnostics. |
| `StoreTbl::internal() const` | Yes | Return namespace identity used for physical naming and metadata keys. |
| `StoreTbl_IDAxor(const StoreTbl &)` | Yes | Keep the (internal, logical ID) registry key; do not collapse application and internal tables. |
| `Store::stopping() const` | Yes | Return m_stopping, the worker-owned bool admission fence described above; inspect it only after dispatch to the store SID. |
| `Store::run<Args...>(...)` | Yes | Post to the configured single writer SID, preserving Z scheduler queue semantics. |
| `Store::invoke<Args...>(...)` | Yes | Invoke via the configured writer SID; inline only when already on that owner, with no synchronous-return assumption. |
| `Store::oids() const` | No | Replace consumers with immutable SQLite logical-type/codec descriptors; there is no OID registry. |

### Store and table out-of-line functions

Each row below also covers the matching declaration in `ZdbPQ.hh`. Startup,
open and teardown remain asynchronous public operations. Rows marked relevant
may be folded into one worker function where PQ split them solely around libpq
send/receive; the table maps responsibilities, not a required function layout.

| ZdbPQ function (CC line) | Relevant? | SQLite implementation |
| --- | --- | --- |
| `Store::init` (64) | Yes | Parse `thread`, `connection` and `synchronous` through `ZfStruct`; validate the writer SID against Rx/Tx, retain shard count and FailFn, initialize the registry, and return replicated=false. No SQLite calls during init. |
| `Store::final` (100) | Yes | Perform synchronous structural finalization only after asynchronous stop/failed-start cleanup completes. Clear callbacks and registry as PQ does; no outstanding SQLite connection, statement or scheduler-posted work may depend on them. Do not call process-global sqlite3_shutdown. |
| `Store::start` (108) | Yes | Post one writer startup task with StartFn in its capture; return idempotent success when already connected, otherwise clear admission and start. Omit PG event-loop/queue/OID setup. |
| `connError` (147) | Yes | Implement the role as file-static `cxnError`, capturing `sqlite3_errmsg` and the extended code while the connection is valid; copy diagnostic text before deferred logging. |
| `notice_` (154) | No | No libpq notice receiver trampoline. SQLite call errors flow through operation results. |
| `Store::notice` (158) | No | No PostgreSQL notice stream or regex stripping. Use ZiLog for SQLite operation diagnostics; do not install a process-global log callback per store. |
| `Store::start_` (175) | Yes | Open one connection, enable extended errors, apply WAL and `m_synchronous`, then bootstrap metadata sequentially. Consume PRAGMA results once; do not issue verification queries or process-global threading calls. |
| `Store::stop` (211) | Yes | Post the worker stop task. Scheduler ordering drains earlier operations; repeated calls sequence their callbacks behind the first close without stored callback lists. |
| `Store::stop_` (222) | Yes | Set `m_stopping`, then move StopFn into `stop_1` posted to the same SID as the drain tail. |
| `Store::stop_0` (229) | No | The libpq event-loop drain guard has no SQLite analogue; do not add a stop state or outstanding counter. |
| `Store::stop_1` (247) | Yes | After queued rejection callbacks drain, finalize statements, close SQLite, reset table state, then invoke the captured StopFn. |
| `Store::wake` (261) | No | PQ event-loop wake/pump machinery is absent. ZmScheduler wakes the worker when a lambda is posted. |
| `isSync` (266) | No | This tests the PG pipeline-sync flag, not SQLite transaction durability. Remove it; explicit BEGIN/COMMIT controls SQLite writes. |
| `isSRM` (273) | No | PG single-row-mode flag is unnecessary: sqlite3_step naturally exposes a row at a time. |
| `Store::run_` (280) | No | No explicit work-queue pump. Each posted lambda executes its SQLite operation and completion on the worker. |
| `Store::disconnect` (289) | No | Socket-disconnect simulation has no SQLite analogue. Use test-only SQLite failure injection at execution boundaries when testing FailFn/cleanup; never close a connection from a non-owner thread. |
| `Store::recv` (295) | No | No socket input, PQisBusy, PQgetResult or m_sent list. sqlite3_step supplies row/done/error synchronously in the writer operation. |
| `Store::rcvd` (371) | No | Each worker operation handles its synchronous ROW/DONE/error result locally; no central result dispatcher exists. |
| `Store::failed` (409) | No | Each worker operation owns its callback and scoped statement cleanup; it reports its own error and calls FailFn only for store-fatal failures. No central pending-operation dispatcher exists. |
| `Store::send` (452) | No | No network-send dispatcher or pipeline queue. Posted operation lambdas execute SQLite directly; transaction completion precedes successful write callbacks. |
| `Store::start_enqueue` (537) | No | `start` posts the only required scheduler turn; SQLite has no transport operation to enqueue. |
| `Store::start_send` (544) | No | Fold synchronous connection, PRAGMA and metadata work into `start_`. |
| `Store::start_rcvd` (557) | No | SQLite returns each result in the same call; route it locally rather than through a result dispatcher. |
| `Store::start_failed` (569) | Yes | Common worker-local failure path: fence admission, release partial SQLite resources, null `m_conn` and invoke the captured StartFn with the error. |
| `Store::start_failed_` (587) | No | Fold callback movement/completion into `start_failed`; no asynchronous event-loop cleanup remains. |
| `Store::started` (596) | No | `start_` invokes the captured StartFn after all setup succeeds; non-null `m_conn` is the running state. |
| `Store::getOIDs` (609) | No | PostgreSQL pg_type lookup and runtime OID population are absent; SQLite codec/type descriptors are local. |
| `Store::getOIDs_send` (615) | No | PostgreSQL pg_type lookup and runtime OID population are absent; SQLite codec/type descriptors are local. |
| `Store::getOIDs_rcvd` (650) | No | PostgreSQL pg_type lookup and runtime OID population are absent; SQLite codec/type descriptors are local. |
| `Store::mkSchema` (694) | No | Deterministic physical naming handles namespaces when a table descriptor is constructed; no startup operation remains. |
| `Store::mkSchema_send` (699) | No | SQLite does not use CREATE SCHEMA for these logical namespaces. Prefix/encode table/index/metadata identities within the same database file; no ATTACH-based namespace split. |
| `Store::mkSchema_rcvd` (706) | No | No schema-creation server response; proceed directly to metadata table creation after namespace setup. |
| `Store::mkTblMeta` (713) | Yes | Worker-local utility called by `start_`: create or inspect the metadata table once. |
| `Store::mkTblMeta_send` (718) | No | Fold SQLite prepare/step/finalize into `mkTblMeta`; there is no send boundary. |
| `Store::mkTblMeta_rcvd` (725) | No | Fold result handling into `mkTblMeta`. |
| `Store::setNShards` (730) | Yes | Insert the shard topology with `ON CONFLICT DO NOTHING`. |
| `Store::setNShards_send` (735) | No | Fold bind/step into `setNShards`. |
| `Store::setNShards_rcvd` (743) | No | Fold result handling into `setNShards`. |
| `Store::getNShards` (748) | Yes | Read and compare the persisted shard count once during startup. |
| `Store::getNShards_send` (754) | No | Fold bind/step into `getNShards`. |
| `Store::getNShards_rcvd` (760) | No | Fold result handling into `getNShards`. |
| `Store::mkTblMRD` (785) | Yes | Create or inspect the MRD table once, with a unique (physical table identity, shard) key and full-width UN/SN storage. |
| `Store::mkTblMRD_send` (790) | No | Fold SQLite execution into `mkTblMRD`. |
| `Store::mkTblMRD_rcvd` (804) | No | Fold result handling into `mkTblMRD`; `start_` invokes its captured StartFn after all work succeeds. |
| `Store::open` (811) | Yes | Post metadata and callback ownership to the writer, reject shutdown/already-open states, find or allocate the (internal,id) table descriptor, and execute table open there. |
| `Store::enqueue` (853) | No | No store-owned operation queue. Public entry points post lambdas with retained capture-packs directly to the worker using ZmScheduler. |
| `xField` (862) | Yes | Build the reflection accessor, established camel-to-snake field name, SQLite storage class and compile-time codec dispatch for every field type. No Value discriminator. |
| `StoreTbl::StoreTbl` (1029) | Yes | Build immutable field/key/update descriptors, group lengths and namespace-safe names; allocate writer-owned high-water arrays sized by nShards. Track statement handles rather than server-side statement names; retain one SQL column per logical field. |
| `StoreTbl::~StoreTbl` (1095) | Yes | Require drained callbacks and finalized SQLite statements before destroying the descriptor; release any remaining owner-thread resources during close/stop, not arbitrary-thread destruction. |
| `StoreTbl::open` (1099) | Yes | Reset UN/SN to sentinels, keep OpenFn in the worker capture, create/validate schema, prepare statements and recover high-water state. Reuse cached statements on Reopen. |
| `Store::sendQuery` (1113) | Yes | Implement the role as worker-only `query`: prepare/bind/step/finalize cold SQL with direct typed binding; remove OID arrays and SendState. |
| `Store::setSRM` (1142) | No | No single-row-mode handshake; process each SQLITE_ROW directly. |
| `Store::sendPrepare` (1150) | Yes | Implement the role as worker-only `prepare`: use `sqlite3_prepare_v3` with persistent flags and store the handle. SQL/bind descriptors share one metadata source. |
| `Store::sendPrepared` (1164) | Yes | Implement the role as worker-only `exec`: reset/clear, bind, step, serialize and reset/clear. Retain borrowed bytes only through the call. |
| `StoreTbl::open_enqueue` (1197) | No | `Store::open` already posts the table-open task; no internal transport queue exists. |
| `StoreTbl::open_send` (1203) | No | Execute schema, preparation and recovery directly in the posted `StoreTbl::open` worker body. |
| `StoreTbl::open_rcvd` (1228) | No | Handle each synchronous result at its call site; a dispatcher would duplicate control flow. |
| `StoreTbl::open_failed` (1252) | Yes | Common worker-local cleanup: finalize partial statements, roll back if needed, set Closed, then invoke the captured OpenFn once. |
| `StoreTbl::opened` (1266) | Yes | Publish StoreTbl plus live count, per-shard UN array and table SN only when initialization is complete. |
| `StoreTbl::mkTable` (1284) | Yes | In one transaction inspect or create the STRICT relation and logical schema metadata, rejecting mismatches once. |
| `StoreTbl::mkTable_send` (1289) | No | Fold synchronous catalog/DDL execution into `mkTable`. |
| `StoreTbl::mkTable_rcvd` (1338) | No | `mkTable` handles the synchronous result and rejects mismatches. |
| `StoreTbl::mkIndices` (1432) | Yes | Inspect/create all declared indices once inside the schema transaction: key 0 unique, SKs non-unique, shard/UN unique. |
| `StoreTbl::mkIndices_send` (1437) | No | Fold catalog execution into `mkIndices`. |
| `StoreTbl::mkIndices_rcvd` (1495) | No | Fold result handling into `mkIndices`. |
| `StoreTbl::prepCount` (1574) | Yes | Compile and cache each per-key COUNT statement directly. |
| `StoreTbl::prepCount_send` (1579) | No | No send boundary. |
| `StoreTbl::prepCount_rcvd` (1607) | No | `prepCount` stores or finalizes each returned handle directly. |
| `StoreTbl::prepSelect` (1620) | Yes | Compile/cache key/row initial and inclusive/exclusive continuation variants per key. |
| `StoreTbl::prepSelect_send` (1644) | No | Fold SQL assembly and prepare into `prepSelect`. |
| `StoreTbl::prepSelect_rcvd` (1765) | No | Fold handle/error processing into `prepSelect`. |
| `StoreTbl::prepFind` (1781) | Yes | Compile/cache exact key lookups and the shard/UN recovery lookup. SK lookup uses LIMIT 1 without a preferred duplicate. |
| `StoreTbl::prepFind_send` (1786) | No | Fold SQL assembly and prepare into `prepFind`. |
| `StoreTbl::prepFind_rcvd` (1826) | No | Fold handle/error processing into `prepFind`. |
| `StoreTbl::prepInsert` (1839) | Yes | Compile/cache full-row insertion. |
| `StoreTbl::prepInsert_send` (1844) | No | Fold SQL assembly/prepare into `prepInsert`; use ON CONFLICT(key 0) DO NOTHING. |
| `StoreTbl::prepInsert_rcvd` (1878) | No | Fold handle/error processing into `prepInsert`. |
| `StoreTbl::prepUpdate` (1885) | Yes | Compile/cache primary-key-addressed row update. |
| `StoreTbl::prepUpdate_send` (1890) | No | Fold SQL assembly/prepare into `prepUpdate`; update metadata and Mutable fields by key 0. |
| `StoreTbl::prepUpdate_rcvd` (1929) | No | Fold handle/error processing into `prepUpdate`; execution checks affected rows. |
| `StoreTbl::prepDelete` (1936) | Yes | Compile/cache primary-key-addressed deletion. |
| `StoreTbl::prepDelete_send` (1941) | No | Fold SQL assembly/prepare into `prepDelete`; delete by key 0. |
| `StoreTbl::prepDelete_rcvd` (1963) | No | Fold handle/error processing into `prepDelete`; execution checks affected rows. |
| `StoreTbl::prepMRD` (1970) | Yes | Compile/cache trailing delete/skipped-insert high-water persistence. |
| `StoreTbl::prepMRD_send` (1975) | No | Fold SQL assembly/prepare into `prepMRD`; identify the MRD row by namespace-aware table identity and shard. |
| `StoreTbl::prepMRD_rcvd` (1992) | No | Fold handle/error processing into `prepMRD`; execute it inside the write transaction. |
| `StoreTbl::openCount` (1999) | Yes | Read/decode the live row count directly before completing open. |
| `StoreTbl::openCount_send` (2004) | No | Fold execution into `openCount`. |
| `StoreTbl::openCount_rcvd` (2012) | No | Fold result handling into `openCount`. |
| `StoreTbl::maxUN` (2031) | Yes | Loop over shards and recover full-width live-row UN/SN maxima with shard-qualified queries. |
| `StoreTbl::maxUN_send` (2036) | No | Fold execution into `maxUN`. |
| `StoreTbl::maxUN_rcvd` (2047) | No | Fold decoding/merge into `maxUN`. |
| `StoreTbl::ensureMRD` (2088) | Yes | Insert missing per-table/shard MRD rows with `ON CONFLICT DO NOTHING`. |
| `StoreTbl::ensureMRD_send` (2093) | No | Fold execution into `ensureMRD`. |
| `StoreTbl::ensureMRD_rcvd` (2106) | No | Fold result handling into `ensureMRD`. |
| `StoreTbl::mrd` (2119) | Yes | Loop over shards, read MRD and merge non-null full-width maxima. |
| `StoreTbl::mrd_send` (2124) | No | Fold execution into `mrd`. |
| `StoreTbl::mrd_rcvd` (2135) | No | Fold decoding/merge into `mrd`. |
| `StoreTbl::close` (2178) | Yes | Post close to the worker and drain dependent table work. Reset/clear bindings and retain cached statements, set Reopen after a successful open or Closed after a failed open, then invoke the captured CloseFn. Retain the descriptor registry. Connection teardown, not successful table close, finalizes cached statements. |
| `StoreTbl::warmup` (2187) | Yes | Keep the no-op implementation used by both existing stores. |
| `StoreTbl::count` (2189) | Yes | Thin dispatcher: capture key ID and retained key buffer, post to the writer, check admission and execute the operation in that scheduler task. Preserve CountFn result/error contract; do not inspect writer-owned state before dispatch. |
| `StoreTbl::select` (2213) | Yes | Thin dispatcher: capture key ID, selectRow/selectNext/inclusive, limit and retained key buffer, post to the writer, check admission and execute the operation in that scheduler task. Preserve TupleFn result/error contract; do not inspect writer-owned state before dispatch. |
| `StoreTbl::count_send` (2272) | Yes | In `count_`, bind the group prefix and step COUNT directly. |
| `StoreTbl::count_rcvd` (2292) | Yes | In `count_`, decode one scalar, reset and call CountFn once. |
| `StoreTbl::count_failed` (2314) | Yes | `count_` scoped cleanup resets and delivers CountResult{Event} once. No separate function. |
| `StoreTbl::select_send` (2319) | Yes | In `select_`, choose the cached variant, bind prefix/continuation and limit, then step. |
| `StoreTbl::select_rcvd` (2347) | Yes | In `select_`, serialize each SQLITE_ROW in SQLite order; SQLITE_DONE emits the terminator. |
| `StoreTbl::select_save` (2393) | Yes | Serialize the row/key tuple directly into m_bufAllocFn pooled storage, with no replication header, before the next sqlite3_step. |
| `StoreTbl::select_failed` (2400) | Yes | `select_` scoped cleanup resets and delivers the existing error contract once. No separate function. |
| `StoreTbl::find` (2405) | Yes | Thin dispatcher: capture key ID and retained key buffer, post to the writer, check admission and execute the operation in that scheduler task. Preserve RowFn result/error contract; do not inspect writer-owned state before dispatch. |
| `StoreTbl::find_send` (2423) | Yes | In `find_`, bind the full key and step LIMIT 1; any matching duplicate SK row is allowed. |
| `StoreTbl::find_rcvd` (2440) | Yes | In `find_`, return one row or empty success. No separate receive function. |
| `StoreTbl::find_rcvd_` (2447) | No | Share only the row serialization utility with `recover_`; a Recovery template dispatcher is unnecessary. |
| `StoreTbl::find_save` (2520) | Yes | Template on Recovery to build the correct message envelope, logical table ID, shard, UN, SN, VN and nested full payload using pooled Zfb output. |
| `StoreTbl::find_failed` (2542) | Yes | `find_` scoped cleanup resets and invokes RowFn with the error once. No separate function. |
| `StoreTbl::find_failed_` (2546) | No | Fold into `find_`. |
| `StoreTbl::recover` (2552) | Yes | Thin dispatcher: capture shard and UN, post to the writer, check admission and execute the operation in that scheduler task. Preserve RowFn result/error contract; do not inspect writer-owned state before dispatch. |
| `StoreTbl::recover_send` (2567) | Yes | In `recover_`, bind shard/full-width UN and step the unique lookup; no scan. |
| `StoreTbl::recover_rcvd` (2574) | Yes | In `recover_`, serialize one Recovery row or return empty success for a gap. |
| `StoreTbl::recover_failed` (2580) | Yes | `recover_` scoped cleanup resets and invokes RowFn with the error once. No separate function. |
| `StoreTbl::write` (2585) | Yes | Thin dispatcher: capture retained replication buffer, post to the writer, check admission and execute the operation in that scheduler task. Preserve CommitFn result/error contract; do not inspect writer-owned state before dispatch. |
| `StoreTbl::write_send` (2605) | Yes | In `write_`, apply the UN gate, transaction and VN-selected mutation; check changes, update MRD, then commit before publishing maxima. |
| `StoreTbl::write_rcvd` (2680) | Yes | In `write_`, successful replay/commit completes CommitFn with the original buffer. No receive function. |
| `StoreTbl::write_failed` (2708) | Yes | `write_` scoped cleanup rolls back/resets, preserves maxima and completes CommitFn once. No separate function. |
| `ZdbStore` (2717) | Yes | Export the same C plugin entry point returning new ZdbSL::Store; preserve ABI/export conventions. |

### Generated facilities

| ZdbPQ function / facility | Relevant? | SQLite implementation |
| --- | --- | --- |
| `ZuPrintType(Value *)` friend ADL tag | No | ZdbSL defines no Value union or printing path; concrete Z values use their existing printing integration. |
| `ZuDerive_(Value, Value_)` generated constructors/assignment | No | ZdbSL defines no intermediate union. A single scoped `ZdbMem::Value` may adapt the current field to the existing exhaustive codec, but no parameter/result array owns a row of values. |
| `ZuDerive` container/string generated operations | Yes | Retain Z heaps, scratch and containers under ZdbSL names; size from workload/metadata. |
| `Elem_` specializations / `Elem` alias | Yes | Type traits, not runtime functions; keep scalar/vector element dispatch for every enumerated vector family. |
| `ZdbPQ_IntVarBufSize(8/16/32/64)` | Yes | Cover all signed/unsigned widths with one constrained sizing template; do not reproduce the macro expansions. |
| `ZdbPQ_LoadInt(8/16/32/64)` | Yes | Cover all scalar widths with the generic `bindField` template; do not reproduce the macro. |
| `ZdbPQ_LoadIntVec(8/16/32/64)` | Yes | Cover all vector widths with the generic vector codec used by `bindField`; do not reproduce the macro. |
| `ZdbPQ_SaveIntVec(8/16/32/64)` | Yes | Cover all vector widths with the generic vector `saveField`; do not reproduce the macro. |
| `ZdbPQ_SaveInt(8/16/32/64)` | Yes | Cover all scalar widths with the generic `saveField`; do not reproduce the macro. |
| `ZfStruct(... StoreCf ...)` generated configuration handlers | Yes | Define only `thread`, `connection` and `synchronous` (NORMAL default; FULL/OFF accepted). Use `ZfStruct`/`ZtEnum`; do not hand-parse configuration. Always report replicated=false. |
| `ZtEnumNS` / `ZtEnumImplNS(SendState)` generated enum formatting | No | PG send states disappear. Internal `OpenState` needs no runtime formatting; use a namespace enum. Use `ZtEnum` only for configurable `Synchronous`. |
| `IDAlloc()` macro | No | PG statement-name construction disappears; select the cached sqlite3_stmt handle directly and omit this allocation. |
| `ParamAlloc(nParams)` macro | No | SQLite binds directly; delete the parallel Value array. |
| `VarAlloc(nParams, xfields, fbo)` macro | Yes | When converted fields exist, use one sizing pass, one `ZmScratch` offset span and one `ZmScratch` conversion buffer with named ZdbSL heaps. Bind those spans and clear before scope exit. |
| `Work::{Open,Count,Select,Find,Recover,Write,Start,Stop,TblQuery}` | No | Do not reproduce PQ operation records or its explicit queue. Scheduler-posted lambdas hold each operation capture-pack and callback; MRD stays inside the write transaction. |

### SQLite responsibilities with no direct PQ function

Add writer-owned transaction begin/commit/rollback, typed parameter binding,
statement reset/clear/finalize, SQLite schema inspection, configuration validation,
and busy/error reporting. Keep them compact and reuse the operation
responsibilities above. A write callback
acknowledges either a replay-gated no-op or successful SQLite commit under the
configured synchronous mode;
it does not promise FULL-mode power-loss durability when NORMAL or OFF is chosen.

Use fixed-width canonical unsigned encoding for persisted UN/SN so metadata
maxima can be calculated correctly and efficiently; this does not impose Z
collation on application `select*`. Reject inconsistent schemas at startup;
do not add migration or custom-collation work. The inspection extension formats
values for users and does not participate in indexed comparisons.

Inventory review: every named out-of-line definition in CC is represented above
(or OIDs::name in the codec table), including static utilities and the C entry
point. Header-only overloads, state accessors, constructors/destructors and
generated facilities are explicitly classified. During implementation,
update this mapping if those source files change; it is a responsibility map,
not a requirement to reproduce PostgreSQL's network state machine.

Guideline references for this mapping: `General principles`, `Audit flags /
Code structure`, `Algorithm and control flow`, `Framework fit`, `Scope and
cleanup`, and `Naming / Trailing underscores`. A `Yes` row still follows the
allocation, conversion, ownership and validation constraints in sections 2--6;
it does not authorize a second implementation of those facilities.

## Ambiguities and open questions

None outstanding. ZuTime uses the full-range 12-byte S64(sec)/S32(nsec)
encoding. The agreed initial scope uses SQLite ordering/collation for
`select*`, rejects inconsistent existing schemas, and uses WAL with
`synchronous=NORMAL` by default, with `FULL` and `OFF` configurable. No SLOs
are required.
