# Zpb: protobuf serialization and schema tooling

Zpb supplies a native protobuf codec driven by `ZfStruct` metadata, wire
verification and views, pooled output, reflection and the `zprotoc` compiler.
Include `<zlib/Zpb.hh>` and link `libZpb`. Ordinary use does not require the
canonical protobuf C++ library or generated canonical headers. Canonical
protobuf is an optional interoperability oracle.

## Describing messages

Declare a PB facet and associate the C++ type with its protobuf name through ADL:

```cpp
#include <zlib/Zpb.hh>

struct Note {
  unsigned id;
  ZuCSpan text;
};
ZfStruct(, (Note, PB),
  (id, (Ctor<0>), UInt32),
  (text, (Ctor<1>), String));
Zpb::AsMessage<"example.Note"> Zpb_Fmt(Note *);
```

Ordinary fields get numbers from complete `ZfStruct` order, starting at one;
`Skip` reserves its number. If one ordinary field uses `PB::FieldID<N>`, number
all ordinary fields explicitly. Preserve existing numbers when evolving wire
schemas. Facets may select/reorder fields without renumbering them. Constructor
fields are collected before construction; writable fields are then applied.

`PB::ZigZag` selects signed zigzag encoding where appropriate. Nested messages,
repeated fields, maps and unions use the mappings described in the
[implementation contract](zpb.md). `AsArray`, `AsMap` and `AsUnion` provide
explicit schema associations for those forms.

## Writing, verifying and reading

Use `Zpb::save(builder, object)` to serialize into a `Builder` or `IOBuilder`.
The ordinary builder returns a span into caller-owned storage; the pooled
builder transfers its completed buffer through `buf()`. Supply output bounds,
check failure before using raw bytes, and retain the output owner through
transmission. A limit failure is sticky and does not yield a valid partial
message.

Before materializing untrusted input, call `Zpb::verify<Type>(bytes)`.
`Zpb::load<Type>(bytes)` and `handler<Type>(bytes)` are trusted materialization
paths, not substitutes for verification. Verification checks wire structure,
native narrowing, required fields and nesting; application domain checks remain
separate. Unknown fields are skipped without retention.

The example's `ZuCSpan text` borrows from the wire buffer. Use an owning string
field when the decoded object must outlive it. `save<Facet, Filter>` chooses
an operation facet/filter; generic streams and transmit streams support the
same selection. Default wire output is unprefixed; `Zpb_SizePrefixed<>` adds
a four-byte little-endian outer length, which peers must explicitly agree on.

## Schema compiler and reflection

`zprotoc` compiles supported protobuf declarations into Z metadata and supports
descriptor-based workflows. Start with the complete
[zpbexample](example/README.md), which exchanges messages between generated and
handwritten types. The [integration guide](itest/README.md) covers imports,
proto2/proto3 cases, generated consumers and rejected language features.
This is a codec/compiler facility, not a gRPC service runtime.

[ZpbReflect.hh](src/ZpbReflect.hh) provides schema reflection facilities.
Use [zpb.md](zpb.md) as the support matrix for descriptors, language features,
framing, maps, unions and duplicate-field behavior rather than assuming parity
with every feature of canonical protobuf.

## Examples and tests

Run `make -C zpb test` from a configured build tree. See
[example/README.md](example/README.md), [itest/README.md](itest/README.md), and
[interop/README.md](interop/README.md) for focused programs and independent
wire checks. Build/test prerequisites are in [DEPENDENCIES.md](../docs/DEPENDENCIES.md)
and [TESTING.md](../docs/TESTING.md).
