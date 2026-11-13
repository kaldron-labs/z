# Zpb

## Overview

Protobuf wire support integrated with native framework metadata, explicit verification and materialization, descriptor reflection, and native schema compilation.

## Subdirectories & Boundaries

- [Library and compiler](./src/EGM.md): encoding, verification, schema reflection/export and `zprotoc`.
- [Example](./example/EGM.md): handwritten/generated `example.Note` interoperability.

## Interface

Exports `libZpb.la`, `ZpbLib.hh`, `Zpb.hh`, `ZpbReflect.hh` and installed `zprotoc`. The example build invokes the compiler to generate its native schema files. [Build manifest](./src/Makefile.am), [example manifest](./example/Makefile.am).

## Invariants

Verification and loading are separate; borrowed views require surviving input. Builder errors remain sticky, and message handlers preserve protobuf numbering and merge semantics. Reflection schema handles borrow their owning `Schema`; compiler emission supports a restricted schema subset. [Zpb.hh](./src/Zpb.hh), [ZpbReflect.hh](./src/ZpbReflect.hh), [zprotoc.cc](./src/zprotoc.cc).

## Side Effects

Serialization/materialization mutates or allocates caller/native storage. Reflection builds indexes and emits schemas; the compiler reads source/descriptor files and stages generated header/source replacement. See child contracts.

## Verification

From a configured repository root, run `make -j8 && make -C zpb/test test`. Module `make test` also invokes integration tests. Test/interoperability sources are excluded from this index. No builds, tests or compiler runs were performed for this documentation change.
