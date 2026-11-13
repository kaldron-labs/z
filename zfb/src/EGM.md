# Zfb FlatBuffers codec and compiler

## Overview

Native metadata-driven FlatBuffers serialization, borrowed views, explicit verification, canonical BFBS reflection and schema export, plus a compiler for a restricted FlatBuffers schema subset.

## Interface

- [Zfb.hh](./Zfb.hh): `ZfbConfig` selects facets; `AsTable`, `AsStruct`, `AsMap`, `AsEnum`, and `AsUnion` describe representations through ADL and `ZfStruct` fields. `View`, `VectorValue`, `ArrayValue`, `UnionView`, and `MapView` expose borrowed wire access.
- `Verifier` and `verify` validate input explicitly. `Arena`, `Builder`, `ArrayStorage`, `IOStorage`, and `IOBuilder` construct output in caller or pooled storage. `save` supports builders and streams; `load`/handlers materialize, allocate, place, update or load native objects. [Zfb.hh](./Zfb.hh)
- [ZfbReflect.hh](./ZfbReflect.hh): `Reflect::verifySchema` validates BFBS structure and semantics; reflection helpers inspect trusted metadata and verify/write payloads. `saveSchema` emits canonical BFBS; `saveFBS` emits textual schema. `Service` and `RPCCall` are schema descriptions, not executable RPC services.
- [zflatc.cc](./zflatc.cc), `Options`: `zflatc INPUT.fbs [-o STEM] [-I DIR] [--ref BASE] [--export MACRO] [--alt] [-h]`; include paths repeat, default stem removes `.fbs`, and `--alt` changes `.hh/.cc` to `.hpp/.cpp`. The compiler emits one native header/source pair for the requested file, referencing included schemas rather than publishing them recursively.
- [Makefile.am](./Makefile.am): exports `libZfb.la`, three public headers and installed `zflatc`, linked with Zi/Zf/Ze/Zt/Zm/Zu and platform dependencies.

## Invariants

- `TableView`/typed views and `load` trust input; verify ingress explicitly and retain backing for borrowed pointers/spans. Ordinary offsets are 32-bit, vtable entries 16-bit, and wire size is bounded by `INT32_MAX`. `Verifier` enforces traversal budgets; missing fields are structurally valid unless schema requiredness rejects them. [Zfb.hh](./Zfb.hh)
- `AsTable::IDs` requires wholly explicit or implicit contiguous IDs, with unions consuming two slots. Inline structs have fixed active fields and cannot be empty. Field/facet projection preserves complete-schema slots; selected nested values retain their required representation. [Zfb.hh](./Zfb.hh), `FieldWire` and `ObjectHandler`.
- `Arena` retains the first error. Children precede containing tables, reserved-region callbacks cannot grow storage, and finished spans borrow builder storage. Required offsets and union tag/payload presence must agree. Map binary lookup requires sorted input; map materialization accepts unsorted entries with later duplicates replacing earlier values. [Zfb.hh](./Zfb.hh), `Builder`, `fieldUnion`, and `MapView::find`.
- `Reflect::SchemaView` borrows BFBS; `verifySchema` checks canonical ordering/references/layout, rejects unsupported forms such as offset64 and union vectors, and requires any root to identify a listed table. Payload verification assumes trusted schema. `SchemaOut::Graph` rejects conflicting names/shapes. [ZfbReflect.hh](./ZfbReflect.hh), [ZfbReflect.cc](./ZfbReflect.cc)
- Compiler `Schema::resolve` validates its supported declaration/attribute subset, contiguous enum/field IDs, union alternatives, key/default rules and inline layout. Include/inline dependency cycles fail; root types must be tables. `Publish::stage` requires existing output directories. [zflatc.cc](./zflatc.cc)

## Side Effects

- Storage growth allocates/copies and invalidates prior addresses. Serialization mutates buffers/scratch; materialization invokes constructors/setters and may allocate native objects. Map loading can release owned raw pointees. `IOBuilder::buf` transfers buffer ownership and may compact payload; stream save flushes output, while destination failures remain stream state. [Zfb.hh](./Zfb.hh)
- Reflection writers mutate caller builders; BFBS/FBS export writes caller streams, with scratch storage for alignment traversal and formatting. [ZfbReflect.hh](./ZfbReflect.hh), [ZfbReflect.cc](./ZfbReflect.cc)
- Compiler `Schema::read` canonicalizes/reads files and recursively resolves includes. `Emit` renders owning native containers and metadata in memory. `Publish::stage` exclusively creates sibling temporaries, writes/syncs them and reserves backups; `commit` moves old outputs aside and installs both new files, attempting rollback on replacement failure. Failed restoration retains reported backups. Pair publication is not crash-atomic; backup deletion can fail after new outputs are installed. [zflatc.cc](./zflatc.cc)
- Compiler `main` prints help or diagnostics and returns zero/one for success/failure. It creates no missing output directories. [zflatc.cc](./zflatc.cc)

## Verification

From a configured repository root, run `make -j8 && make -C zfb/test test`. Module `make test` additionally invokes integration tests. Compiler and tests were not executed for this documentation change.
