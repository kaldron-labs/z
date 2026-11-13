# Zpb handwritten/generated example

## Overview

Bidirectional protobuf serialization between handwritten PB metadata and compiler-generated native types.

## Interface

- [zpbexample.proto](./zpbexample.proto), `example.Note`: proto3 fields `uint32 id = 1`, `string text = 2`, and `repeated sint32 deltas = 3`.
- [zpbexample.cc](./zpbexample.cc), `HandNote` and `Zpb_Fmt`: matching handwritten PB-facet metadata, including ZigZag encoding for deltas and the schema name `example.Note`.
- [Makefile.am](./Makefile.am), `noinst_PROGRAMS`: builds non-installed `zpbexample` with handwritten source and generated `zpb-gen/note.hh`/`note.cc`, linking `libZpb` and framework dependencies.

## Invariants

- `main` verifies and loads handwritten output into generated `example::Note`, then verifies and loads generated output back into `HandNote`, checking all values. [zpbexample.cc](./zpbexample.cc)
- The grouped generation rule invokes the built `zprotoc` on the source schema; the handwritten object explicitly depends on the generated header. [Makefile.am](./Makefile.am), `zpb-gen/note.hh`.

## Side Effects

- `main` allocates serialization arrays, prints `Zpb example OK` on success, and returns one on encoding, verification, comparison or output failure. [zpbexample.cc](./zpbexample.cc)
- The build creates the `zpb-gen` directory and generated files; `CLEANFILES` removes those outputs. The source schema is distributed. [Makefile.am](./Makefile.am)

## Verification

From a configured repository root, run `make -j8 && make -C zpb/test test`. The default build includes this example; it was not executed for this documentation change.
