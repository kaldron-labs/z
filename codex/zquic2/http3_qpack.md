# HTTP/3 Dynamic QPACK Plan

## Goals

Add dynamic QPACK support to `Zhttp::H3` without changing the hot-path shape more than necessary:

- keep one dynamic table for receive/decoding and one for transmit/encoding;
- use `ZtArray`/ring storage for receive-side decoding;
- use `ZmLHash` with no locking for transmit-side encoding lookup;
- default the transmit hash table size to 128 entries, while still allowing `ZmHashMgr` overrides;
- assign explicit heap IDs anywhere dynamic QPACK may allocate heap memory;
- avoid node polymorphism;
- avoid `ZuObject`/`ZuRef` unless a later ownership issue proves plain pointers are unsafe;
- preserve direct stream writes in `Builder` where practical.

## Current State

`Zhttp.hh` currently supports only static QPACK and literal field encodings:

- `H3::Builder` emits field sections with `Required Insert Count = 0` and `Delta Base = 0`.
- `H3::Parser::parseFields_()` rejects any non-zero Required Insert Count or Base.
- `CxnParser` treats peer encoder stream data as an error and drains decoder stream data because we never create dynamic references.
- local SETTINGS advertise zero dynamic QPACK capacity / blocked streams.

Dynamic support should replace that zero-dynamic contract with a bounded dynamic table contract.

## Table Model

Introduce a small `H3::QPackDyn` layer used by both parser and builder code.

Use two concrete tables:

- `QPackRxTable`: decoder-side indexed array/ring populated from the peer
  QPACK encoder stream.
- `QPackTxTable`: encoder-side hash plus ordered state populated by our
  `Builder` and acknowledged by the peer decoder stream.

Do not force both directions into the same data structure. QPACK decoding is
index-based, while QPACK encoding needs reverse lookup by field strings.

Decoder-side table:

```c++
using QPackRxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackRx.String">>;

struct QPackRxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;	// RFC size: name + value + 32
  QPackRxString	name;
  QPackRxString	value;
};

using QPackRxArray =
  ZtArray<QPackRxEntry, ZtArrayHeapID<"Zhttp.H3.QPackRx.Array">>;

struct QPackRxTable {
  QPackRxArray		entries;
  uint64_t		baseAbs = 1;	// absolute index of entries[0]
  uint64_t		insertCount = 0;
  uint32_t		capacityBytes = 0;
  uint32_t		usedBytes = 0;
};
```

`QPackRxTable` should behave as a ring or sliding array. Dynamic references
resolve QPACK relative/post-base indexes to an absolute insert index, then
perform checked array lookup:

```c++
if (abs < baseAbs || abs >= baseAbs + entries.length()) reject;
entry = entries[abs - baseAbs];
```

This is simpler and more faithful than hashing on the decode side. The decoder
normally does not need reverse lookup by name/value.

Encoder-side table:

Z hash tables use `Axor`s, meaning accessor functions, to specify keys. Follow
the same style as `ZiEventLoop.hh`: put the accessor on the value type and pass
it through `ZmLHashKey`.

Recommended encode hash setup:

```c++
static const char *QPackTxHashID() { return "Zhttp.H3.QPackTx"; }

using QPackTxString =
  ZtString<ZtStringHeapID<"Zhttp.H3.QPackTx.String">>;

struct QPackFieldKey {
  ZuCSpan	name;
  ZuCSpan	value;

  bool equals(const QPackFieldKey &) const;
  int cmp(const QPackFieldKey &) const;
  friend uint32_t ZuHashCode(const QPackFieldKey &);
};

struct QPackTxEntry {
  uint64_t	abs = 0;
  uint32_t	size = 0;	// RFC size: name + value + 32
  QPackTxString	name;
  QPackTxString	value;

  static QPackFieldKey FieldAxor(const QPackTxEntry &entry) {
    return {entry.name, entry.value};
  }
};

using QPackTxHash = ZmLHash<QPackTxEntry,
  ZmLHashKey<QPackTxEntry::FieldAxor,
    ZmLHashID<QPackTxHashID,
      ZmLHashLock<ZmNoLock>>>>;

using QPackTxOrder =
  ZtArray<uint64_t, ZtArrayHeapID<"Zhttp.H3.QPackTx.Order">>;
```

`QPackFieldKey` is a lookup key, not stored table state. Its comparison and
hashing should compare the pointed-to bytes by value, allowing lookup from
temporary header spans while `QPackTxEntry` itself owns `name` and `value`.

Construct the encode hash with `ZmHashParams{128}` as the default:

```c++
QPackTxHash tx{ZmHashParams{128}};
```

Use non-local `ZmLHash` if runtime `ZmHashMgr` override/telemetry is required.
`ZmLHashLocal<>` should not be used for the encode hash if it must be
manager-overridable, because it opts out of hash manager registration.

`QPackTxHash` is keyed by full field name/value, because the builder first wants
exact dynamic reuse. If name-only dynamic references become important, add an
auxiliary ordered hint/list outside the hash rather than introducing another
hash table in the first pass.

`ZmLHash` stores its nodes by value, so the “intrusive node” for transmit lookup
is the hash table’s own `Node` type plus a plain entry value. Do not add a
polymorphic base. Use plain pointers returned by `find()`/iteration only within
the lifetime of the table operation or while the table is not resized.

## Heap IDs

Name each heap allocation site explicitly:

- `Zhttp.H3.QPackRx.String`: owned decoded peer dynamic-table names/values.
- `Zhttp.H3.QPackRx.Array`: receive-side dynamic table ring storage.
- `Zhttp.H3.QPackTx.String`: owned transmit dynamic-table names/values.
- `Zhttp.H3.QPackTx.Order`: transmit insertion/eviction order storage if a
  separate ordered array is used.
- `Zhttp.H3.QPackTx`: transmit `ZmLHash` manager/telemetry ID.

`ZmLHashID` is not a `ZmHeap` heap ID; it names the hash for manager overrides
and telemetry. If precise heap attribution is required for `ZmLHash` slot
allocation itself, that is a `ZmLHash` facility change rather than a QPACK table
type choice. Do not leave `ZtArray`/`ZtString` allocations on their generic heap
IDs.

## Entry Shape

Keep the encode/decode entry types compact and non-polymorphic. They may share
a small embedded payload helper if that reduces duplication, but only the encode
table needs a string-key Axor:

```c++
static auto FieldAxor(const QPackTxEntry &);
```

Avoid storing `ZuCSpan` in the dynamic table. Dynamic entries outlive the parsing callback and must own name/value bytes.

Do not rely on the encode hash table alone for QPACK ordering. QPACK dynamic references are by absolute/relative/post-base index and eviction is insertion-order based, so the transmit side also needs ordered state:

- `insertCount`;
- `base`;
- `capacityBytes`;
- `usedBytes`;
- a FIFO/ring/list of absolute insert indexes for eviction;
- a way to resolve `abs index -> entry`.

On the receive side, this ordered state is the table. On the transmit side,
keep it alongside `QPackTxHash`.

## Capacity and SETTINGS

Separate two limits:

- internal hash slots: default `ZmHashParams{128}`, overrideable through `ZmHashMgr`;
- QPACK dynamic table byte capacity: advertised with `SETTINGS_QPACK_MAX_TABLE_CAPACITY`.

Add H3 configuration fields, probably in `CxnState` or adjacent connection settings:

- `localQPackCapacityBytes`;
- `peerQPackCapacityBytes`;
- `localBlockedStreams`;
- `peerBlockedStreams`.

Default dynamic support can start conservative:

- local capacity: enough for 128 average entries, or a fixed byte default such as 8192;
- blocked streams: initially `0` unless blocked-field buffering is implemented.

When sending SETTINGS, advertise:

- `SETTINGS_QPACK_MAX_TABLE_CAPACITY = localQPackCapacityBytes`;
- `SETTINGS_QPACK_BLOCKED_STREAMS = localBlockedStreams`.

When receiving SETTINGS:

- record peer max table capacity;
- reject duplicate SETTINGS parameters;
- reject values above implementation maximum;
- treat peer encoder stream inserts that exceed our advertised capacity as protocol errors.

“Reject overflow” means:

- reject encoder instructions that would make `usedBytes` exceed the current dynamic capacity after required evictions;
- reject entries larger than capacity;
- reject dynamic references whose resolved absolute index has not been inserted or has been evicted;
- reject Required Insert Count values beyond what the decoder can satisfy when blocked streams are disabled.

## Encoder Stream Handling

Change `CxnParser::QPackEncoder` from “any data is an error” to a real QPACK encoder-stream parser.

Support at least:

- Set Dynamic Table Capacity;
- Insert With Name Reference;
- Insert Without Name Reference;
- Duplicate.

Each instruction updates `QPackRxTable` and its ordered eviction state.

Dynamic-name references can point to either the static table or the dynamic table. Literal names/values may be Huffman encoded. For huffman-decoded strings, decode into owned entry storage before insertion. Continue using callback-local stack buffers for field parsing where the string does not enter the dynamic table.

If `localBlockedStreams == 0`, the request/response field parser must reject field sections that require inserts not yet available. If non-zero blocked streams are added later, add explicit blocked stream bookkeeping rather than silently buffering raw payloads in the parser.

## Decoder Stream Handling

Change `CxnParser::QPackDecoder` from unconditional drain to an instruction parser once the transmit side emits dynamic references.

Support:

- Section Acknowledgement;
- Stream Cancellation;
- Insert Count Increment.

These instructions update `QPackTxTable` acknowledgement state:

- highest known received insert count;
- per-stream outstanding dynamic references, if blocked streams become non-zero;
- entries eligible for eviction once no outstanding field section can reference them.

Until the builder emits dynamic references, decoder stream parsing can be implemented but have no effect beyond validation.

## Field Section Decoding

Extend `Parser::parseFields_()`:

1. Decode Required Insert Count and Delta Base correctly.
2. Compute the field section base.
3. Resolve indexed field lines from:
   - static table;
   - dynamic table relative indexes;
   - post-base indexes.
4. Resolve literal-name references from:
   - static table;
   - dynamic table.
5. Preserve the current CRTP callback behavior:
   - method/status/path still use short-lived callback spans;
   - normal headers still call `header_()` immediately;
   - no heap allocation unless the string must be inserted into the dynamic table.

Do not persist path/method/status in heap storage solely because dynamic QPACK exists. Field section data is still processed synchronously once dependencies are available.

## Field Section Encoding

Extend `Builder` so every header has a single helper path:

1. Try static QPACK exact key/value.
2. Try static QPACK name reference with literal value.
3. Try dynamic table exact key/value.
4. Try dynamic table name reference with literal value.
5. Emit literal name/value.
6. If the field was not encoded via static QPACK, insert it into the transmit dynamic table.

The requested policy says: for all `Headers`, if they are not encoded via static QPACK, add them to the dynamic table. Apply this to:

- pseudo-fields not statically encoded, such as non-GET/POST methods and non-`/` paths;
- `:authority`;
- `content-length` when variable;
- typed `Headers`;
- trailers.

Pseudo-header ordering must remain valid: all pseudo-headers before regular headers.

The builder currently does a count pass followed by a write pass. Dynamic insertion cannot happen twice. Add an encoding context with modes:

- `Count`: computes bytes and records planned dynamic insertions without mutating the table;
- `Write`: emits the same bytes and commits the planned insertions/instructions.

Alternatively, build only a small on-stack plan for the header block using `ZtLocalArray` and write once. Avoid heap-backed scratch except when field size exceeds the local array capacity.

## Emitting Encoder Instructions

Dynamic table insertion is not part of the HEADERS frame; it is sent on the unidirectional QPACK encoder stream.

Add a transmit-side QPACK encoder stream object/state to `H3::CxnState`:

- open local encoder stream before first dynamic insert;
- write encoder instructions before HEADERS frames that reference those inserts;
- flush ordering so QUIC sends encoder stream bytes in time for the request stream field section.

If `peerQPackCapacityBytes == 0`, skip dynamic insertion and keep current static/literal encoding.

If `peerBlockedStreams == 0`, only emit dynamic references when the referenced insert is guaranteed to be available before the field section can be decoded. The simplest initial policy is:

- send insertion instructions;
- do not reference newly inserted entries in the same request/response field section;
- use dynamic references only for entries inserted before this field section.

That policy still allows later field sections to benefit without implementing blocked stream tracking.

## Index Math

Implement QPACK dynamic index helpers in one place:

- absolute insert count starts at 1;
- dynamic table insertion order is newest/oldest as defined by QPACK;
- convert field-line relative index to absolute index using Base;
- convert post-base index to absolute index;
- validate the result against inserted and evicted ranges.

Keep static table helpers (`QPackIndex`, `QPackKV`, `QPackKeyIndex`) separate from dynamic helpers.

## API Shape

Do not introduce a combined `QPackTables` object. The receive and transmit
dynamic tables are independent protocol/data-flow objects:

- `QPackRxTable` belongs with connection/request parsing. It is updated by the
  peer QPACK encoder stream and read by request/response `Parser` instances
  while decoding field sections.
- `QPackTxTable` belongs with response/request building. It is updated by
  `Builder` and by peer decoder-stream acknowledgements, and it is coupled to
  the local QPACK encoder stream writer.

These are likely to live on different thread-affine objects and have different
auxiliary state. Keep them separately owned and separately injected; do not
colocate them just because both are “QPACK”.

Prefer explicit constructor/member injection:

```c++
Parser(QPackRxTable *);
Builder(QPackTxTable *, EncoderTx *);
CxnParser(QPackRxTable *);
DecoderStreamParser(QPackTxTable *);
```

`Parser` only needs decode lookup. `Builder` only needs encode lookup and an
encoder-stream writer. `CxnParser` only needs the receive table while parsing
the peer encoder stream. A decoder-stream parser, if split out, only needs the
transmit table/ack state.

Keep defaults that preserve static-only operation for existing tests.

## Tests

Add focused unit tests before broader interop:

- encoder instruction parser inserts literal entries;
- dynamic indexed field decodes to CRTP callbacks;
- dynamic literal-name reference decodes;
- capacity overflow is rejected;
- evicted dynamic reference is rejected;
- peer capacity zero prevents dynamic tx insertion;
- repeated headers use dynamic references after the first insertion;
- no duplicate dynamic insertion on builder count/write pass.

Then extend interop:

- `curl` -> `Zhttp` H3 server with dynamic table capacity advertised;
- `zhttpclient` -> Caddy with dynamic capacity advertised;
- a synthetic peer that actually sends dynamic QPACK, because Caddy may not exercise it.

Keep existing zero-dynamic tests as a mode: capacity zero must still reject peer dynamic references and encoder stream insertions.

## Implementation Order

1. Add `QPackEntry`, `QPackRxTable`, `QPackTxTable`, capacity accounting, and index math with unit tests.
2. Add SETTINGS plumbing for local/peer QPACK capacity and blocked streams.
3. Parse peer encoder stream instructions into `QPackRxTable`; keep decoder stream validation minimal.
4. Extend field section decoding to dynamic references.
5. Add transmit dynamic table insertion without emitting dynamic references.
6. Emit encoder stream insert instructions.
7. Enable dynamic references for entries inserted before the current field section.
8. Parse decoder stream acknowledgements and use them for safe eviction.
9. Consider non-zero blocked streams only after the zero-blocked path is correct.

## Non-Goals For First Pass

- Do not implement blocked field-section buffering.
- Do not add a third hash table unless profiling or correctness requires it.
- Do not use polymorphic table entries.
- Do not use `ZuObject` ref-counting unless table entries must safely survive hash resize/eviction across asynchronous callbacks.
- Do not make dynamic QPACK mandatory; capacity zero must remain supported.
