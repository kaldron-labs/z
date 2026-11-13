# Zpb protobuf codec and compiler

## Overview

Native metadata-driven protobuf encoding, verification and materialization, descriptor-set reflection, schema export, and restricted native C++ generation from proto source or descriptor input.

## Interface

- [Zpb.hh](./Zpb.hh): `ZpbConfig` and PB field properties select facets, numbering and encoding. `Wire` supplies primitive readers/writers; `Builder` constructs backward into `SpanStorage`, `ArrayStorage`, or `IOStorage`; `IOBuilder` integrates pooled output.
- `AsMessage`, `AsArray`, `AsMap`, `AsUnion` and mapped-value descriptors define native representations. `save`, `verify`, `handler`, `load`, and handlers expose encoding, explicit verification, construction and updates. Built-in mappings cover framework numeric, time, IP and bitmap types. [Zpb.hh](./Zpb.hh)
- [ZpbReflect.hh](./ZpbReflect.hh): descriptor DTOs, owned `Reflect::Schema` with borrowed handles, `verifySchema`, message verification, `visit`, reflective `write`/`writeNested`, `exportSchema` and `emitProto`. Supported descriptor storage has no service/method collection.
- [zprotoc.cc](./zprotoc.cc), `Options`: `zprotoc [-I DIR]... [-o STEM] [--syntax proto2|proto3] [--export MACRO] INPUT.proto`; `--descriptor [--file NAME]` accepts a binary descriptor set, selecting its last file by default. Outputs are `.hh/.cc`; text-input default stems strip `.proto`, descriptor-input defaults retain the input filename.
- [Makefile.am](./Makefile.am): exports `libZpb.la`, three public headers, and installed `zprotoc`, with Zi/Zf/Ze/Zt/Zm/Zu dependencies.

## Invariants

- `Wire::readVarint` bounds encoding to ten bytes; tags require nonzero 29-bit fields and supported wire types. Group skipping matches nested tags with a depth limit; `unframe` requires exact declared size. Builder size is bounded by `INT_MAX`, preserves encoded suffix offsets during growth, and retains its first error. [Zpb.cc](./Zpb.cc), [Zpb.hh](./Zpb.hh)
- `AsMessage::Handler` validates unique field/union numbers and encoding-property combinations; verification checks required fields, narrowing, wire types, nested depth and merged message fragments. Numeric vectors accept packed or unpacked input. Loading is separate: it can partially mutate on malformed input without reporting failure, so verify before loading untrusted bytes. [Zpb.hh](./Zpb.hh)
- Singular nested occurrences merge, scalars replace, and vectors append; union changes replace the selected value. Borrowed string/byte spans require surviving input unless destination types copy them. `Schema` lookup handles require the schema lifetime; its constructor does not implicitly verify descriptor bytes. [Zpb.hh](./Zpb.hh), [ZpbReflect.hh](./ZpbReflect.hh), [ZpbReflect.cc](./ZpbReflect.cc)
- `verifySchema` validates the supported descriptor subset, names/references/import completeness, enum/field/reservation/oneof rules and direct-import visibility. Services, extensions, editions, groups and unsupported options are rejected. Recursive message verification merges singular fragments and checks repeated instances individually; `visit` reports occurrences rather than performing strict integer-narrowing verification. [ZpbReflect.cc](./ZpbReflect.cc)
- `exportSchema` builds a temporary descriptor file and leaves output unchanged on unsupported native fields/references. Reflective `write` validates field membership and required presence, but does not enforce unique occurrences or oneof exclusivity; failures need not roll back writes. [ZpbReflect.hh](./ZpbReflect.hh)
- Compiler `supportedFile`/`generatedNames` impose native-emission restrictions beyond descriptor validation, including rejecting proto3 optional output and unsupported value-dependency cycles. Both output texts are generated before publication; output directories must already exist. [zprotoc.cc](./zprotoc.cc)

## Side Effects

- Readers advance cursors, including before some failures. Array storage clears/replaces caller arrays; pooled storage can reallocate and transfer ownership. Materialization allocates objects/C strings, mutates containers and can release replaced raw pointees. Stream encoding allocates and flushes output; allocation failure can remain destination state while returning `Error::OK`. [Zpb.cc](./Zpb.cc), [Zpb.hh](./Zpb.hh)
- Reflection prepares indexes and temporary validation storage, invokes visitor callbacks with borrowed byte spans, and writes caller builders/streams. `emitProto` clears output first and can leave partial output on failure; accepted file options are not emitted. [ZpbReflect.hh](./ZpbReflect.hh), [ZpbReflect.cc](./ZpbReflect.cc)
- Compiler `main` reads input/imports, verifies the schema, generates owning native containers and metadata, then stages output. `Publish::stage` creates exclusive sibling temporary/backup files and syncs staged content; `commit` replaces destinations with rollback attempts on preservation/installation failure. Failed recovery retains reported backups; multiple renames do not provide atomic pair publication. Backup deletion failure can be reported after installation. [zprotoc.cc](./zprotoc.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zpb/test test`. Module `make test` also runs integration tests. No compiler, builds or tests were executed for this documentation change.
