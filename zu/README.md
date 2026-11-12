# Zu: universal foundation

Zu provides the vocabulary and compile-time machinery used throughout Z:
traits, spans, structural strings, matching, reflection, numeric types,
formatting, time, encoding and ownership. Include individual headers as
`<zlib/ZuSpan.hh>` and link `libZu` for out-of-line facilities. Zu does not
provide the threaded runtime or dynamically growing containers; those live in
[Zm](../zm/README.md) and [Zt](../zt/README.md).

See [Z, STL and Boost equivalents](../docs/Z_STL_BOOST.md) for comparisons of Zu
facilities with their closest STL and Boost counterparts.

## Strings, spans and arrays

Choose the representation according to ownership and when its size is known:

| Facility | Use |
| --- | --- |
| `ZuSpan<T>`, `ZuCSpan`, `ZuBSpan` | Borrow a contiguous sequence, text, or bytes without allocating |
| `ZuArray<T, N>` | Own fixed-capacity storage with a separate logical length |
| `ZuString` / `ZuStringT` | Carry a structural string value / string-tag type at compile time |
| `ZuMArray`, `ZuVArray` | Adapt array storage through the on-demand element transformation / type-erased interfaces |
| `ZuStdString` | Opt into interoperability with standard strings |

```cpp
#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

template <ZuString Name> struct Field {
  static constexpr auto name = Name;
};
using Price = Field<"price">;
ZuCSpan text{"price"};
```

Spans do not extend the lifetime of their storage. Keep the owner alive and
avoid resizing it while views are in use. `ZuArray` capacity is not its logical
length; use `push()` to append initialized elements. See
[span tests](test/ZuSpanTest.cc) and [array tests](test/ZuArrayTest.cc).

## Compile-time matching and tokenization

`ZuMatcher` builds an Aho–Corasick automaton from a type's `Keys` list. It maps
fixed strings to their list indices, with `-1` for no match:

```cpp
#include <zlib/ZuMatcher.hh>

struct Fields { using Keys = ZuStringTL<"price", "size">; };
constexpr auto fields = ZuMatcher<Fields>();
static_assert(fields.exact("price") == 0);
static_assert(fields.exact("price!") == -1);
static_assert(fields.match("price!") == 0);
```

Use `exact()` for field names, `match()` for prefix recognition and `find()`
for searching (returning offset and key index); consult [matching tests](test/ZuMatchTest.cc) for overlap and binary-key cases. Large header instantiations increase compile
cost, so keep substantial matcher definitions in implementation files.
`ZuTokenizer` supplies a separate tokenization facility; its
[tests](test/ZuTokenizerTest.cc) demonstrate token rules and scanning.

## Traits, tuples and reflection

`ZuTraits`, `ZuCmp` and `ZuHash` describe values and their comparison/hash
behavior. ADL customization lets framework containers and formatting discover
user types without a central registry. `ZuTL` and `ZuSeq` manipulate type and
integer sequences; `ZuIfT` expresses SFINAE constraints. `ZuPP`, `ZuDerive`,
`ZuUnroll`, `ZuSwitch` and `ZuInvoke` support generated declarations and
compile-time dispatch.

`ZuTuple` provides heterogeneous values with `p<N>()` access. `ZuUnion` holds
one selected alternative; check `has<T>()` before using `data<T>()` when the
active type is not otherwise known. See [tuple tests](test/ZuTupleTest.cc)
and the declarations in [ZuUnion.hh](src/ZuUnion.hh).

`ZuStruct` attaches field/accessor metadata to an existing C++ type and
`ZuStructFacet` defines alternate field views. Start with
[ZuStructTest](test/ZuStructTest.cc) for member, method and lambda accessors.
For serialization, use [ZfStruct](../zf/README.md), which adds wire types,
validation properties and format bindings to this foundation.

## Ownership

`ZuPtr<T>` is a move-only, single-owner pointer to a heap object. Move it to
transfer ownership; pass a raw pointer when borrowing. It has no configurable
deleter policy. `ZuRef<T>` uses the non-atomic reference count in `ZuObject`;
keep its ownership operations confined appropriately. Use `ZmRef` with the
atomic-counted object model for cross-thread ownership. Neither intrusive
reference type provides weak-pointer cycle collection.

## Numbers, formatting and time

| Facility | Usage |
| --- | --- |
| `ZuBox`, `ZuFmt`, `ZuPrint` | Parse/format scalars, select width/base/precision, and expose an object's `print(stream)` via ADL |
| `ZuFixed`, `ZuDecimal`, `ZuPercent` | Fixed-point, decimal and percentage vocabulary; use their parsing/arithmetic contracts rather than binary floating point conversions |
| `ZuTime`, `ZuDateTime` | Duration/timestamp and calendar representations; use format/scan adapters for external text |
| `ZuID`, `ZuSemVer` | Compact identifiers and semantic-version values |
| `ZuStream`, `ZuVStream` | Direct formatted output into a concrete or type-erased stream |

For example, `ZuBox<unsigned>{"42"}` parses an unsigned value and
`ZuBoxed(value).hex()` produces a hexadecimal formatting adapter. See
[numeric conversion](test/ZuNumConvTest.cc), [decimal](test/ZuDecimalTest.cc),
[time](test/ZuTimeTest.cc) and [stream](test/ZuStreamTest.cc) tests for null
sentinels, accepted syntax and output policies. Runtime clocks are in Zm;
time-zone lookup is in Zt.

## Bits, encodings and C interfaces

`ZuBitmap`, `ZuBitfield` and `ZuBitStream` operate on packed state;
`ZuByteSwap` and `ZuIntrin` expose endian and machine operations. `ZuUTF`,
`ZuHex`, `ZuBase32`, `ZuBase64` and `ZuBase64URL` convert into caller-supplied
storage. Size the destination using the selected codec's length API and check
the returned result; encoding does not itself allocate an owning string.
Use the corresponding [codec tests](test/) for round-trip and invalid-input
examples.

The headers [zu_bitmap.h](src/zu_bitmap.h), [zu_decimal.h](src/zu_decimal.h)
and [zu_time.h](src/zu_time.h) expose C interfaces. `ZuTest` and `ZuTestUtil`
provide the TAP test framework used across the repository.

## Examples and tests

Run `make -C zu test` from a configured build tree. The [test directory](test/)
contains executable API examples and boundary cases. See the top-level
[testing guide](docs/TESTING.md) for environment, diagnostics and port allocations,
and [dependencies](docs/DEPENDENCIES.md) for build prerequisites.
