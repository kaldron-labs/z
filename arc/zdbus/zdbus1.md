# `ZfDBUS`: direct D-Bus binary codec plan

## Scope

Add portable `ZfDBUS` support in `zf`.  It maps application structures
directly to and from D-Bus binary values.  It has no socket, scheduler,
`ZiIOBuf`, serial-allocation, or `zdbus` dependency.  D-Bus transport is
planned separately in `zdbus2.md`.

`ZfDBUS` follows `ZfASN1` and `ZfCBOR`: a standalone binary codec with
codec-specific field properties, ADL format selection, span-based readers,
and direct output to a sink.  It uses `ZfStruct` metadata rather than JSON or
CBOR as an intermediate format, and writes directly into final message
storage supplied by its caller.

## Existing framework patterns

| Precedent | `ZfDBUS` application |
| --- | --- |
| `ZfStruct` | `ZfStructRender`, `ZuFields<Object, Facet>`, field types, properties, and accessors define the schema. |
| `ZfURI` / `ZfJSON` | `save<Facet>()`, `handler<Object, Facet>()`, selected fields, construction/load filters, and multiple renderings of one type. |
| `ZfASN1` / `ZfCBOR` | Codec-specific field properties, ADL format handlers, checked `ctor`/`alloc`/`new_`/`load`, and direct sink output. |
| `Zrest` | A `Body_DBUS_Facet` can select a subobject's representation, with the same object also supplying protocol headers in `zdbus`. |

## Public design

1. Add `zf/src/ZfDBUS.hh`, `ZfDBUS.cc`, and the normal modelines, library
   exports, and direct dependencies.  Register the public header and source
   in `zf/src/Makefile.am`; add `zf/test/ZfDBUSTest.cc` to the test Makefile.
2. Declare `ZuStructFacet(DBUS)` and `ZuFieldProp::DBUS`.  `DBUS` is the
   canonical facet name: applications use `ZuFacet::DBUS`,
   `ZfStruct(..., (Object, DBUS), ...)`, or
   `ZfStructRender(..., Object, DBUS, ...)` to select fields and their D-Bus
   wire order independently of C++ layout.  Custom application facets remain
   possible through a `Body_DBUS_Facet` alias.
3. Define exactly these codec-specific `ZuFieldProp::DBUS` property templates:

   ```cpp
   template <uint8_t Code_> struct Type { };
   template <uint8_t Code_> struct ElemType { };
   ```

   The supported `ZfFieldTC` scalar type code is the D-Bus wire type: for
   example, `ZfFieldTC::UInt32` is unconditionally D-Bus `UINT32`.  It needs no
   D-Bus annotation, compatibility check, or value-range validation.  Define
   only the metadata needed to distinguish representations which share a
   `ZfFieldTC` type, notably `STRING` from object path and signature strings;
   apply the same refinement to array elements where required.  `ZfDBUS`
   publishes named type codes such as `ZfDBUS::Type::String`, `ObjectPath`,
   `Signature`, and `Variant`, so application metadata does not use raw
   character literals.  The `UnixFD` code remains internally known but
   explicitly unsupported until its transport ownership contract exists.
   Use ADL `ZfDBUS_Fmt(T *)` handlers for object, map, variant, and
   other structural mappings instead of adding overlapping field properties.
   Do not define `DBUS::Optional`: positional D-Bus bodies have no omission
   convention.  Define one `WireFields` selection from the complete selected
   facet field list and use it unchanged for signature, measure, save, skip,
   and load.  It must not use `ZfFieldFilter::Save` or `Load`, because their
   read-only behavior can differ.  `Ctor<N>` and field accessors govern object
   construction/assignment after every wire field is parsed.  Constructor
   argument order may differ from wire order; a read-only field is still
   emitted, consumed, and validated so later positional arguments cannot shift.
4. Provide a `ZfDBUS::Result` containing a stable error enum and byte offset,
   mirroring `ZfCBOR::Result`, plus a metadata-derived
   `signature<Object, Facet>()`,
   `measure<Facet>(object, context)` returning `Result`,
   `save<Facet>(sink, object, context)` returning the sink as in the other
   `ZfStruct` adapters, and
   `handler<Object, Facet>(view)`.  The handler exposes checked
   `ctor`, `alloc`, `new_`, and `load` operations in the established Zf style;
   callers must test the result before constructing or assigning.
   The input view carries bytes, signature, byte order, and message-relative
   offset; the output context carries byte order and destination offset.
   Complete typed input validation occurs before constructing or changing the
   application object.
5. A top-level application body is an argument list: fields can yield `su`,
   while a nested structured argument yields `(su)`.  An empty body has an
   empty signature; it is not an empty D-Bus struct.  Use the ADL
   `ZfDBUS_Fmt(T *)` family for object, array, map, and variant
   handlers.  Field names remain metadata; `AsMap` encodes the map's actual
   keys and values as D-Bus dictionary entries.

## Type mapping and wire rules

| Metadata/value | D-Bus representation |
| --- | --- |
| `Bool`, signed/unsigned 8/16/32/64-bit fields, `Float` | Directly and completely determined by `ZfFieldTC`: `b`, `y`, `n/q`, `i/u`, `x/t`, `d`. `Int8` and `UInt8` both map to D-Bus `BYTE` and preserve every bit pattern; no numeric field needs a D-Bus annotation or an additional range check. Booleans retain the D-Bus requirement that the encoded 32-bit value is 0 or 1. |
| `String` / `CString` | `s`; an explicit refinement selects `o` or `g`, because all three use the same Z string field type but have distinct D-Bus syntax. |
| `Bytes`, scalar vectors, typed-array UDTs | `ay`, `aT`, including nested arrays. |
| Structured UDT | Ordered nested `(…)` using its selected facet. |
| Map adapter | `a{KV}` from `Key`/`Val` type codes, with optional explicit code/property overrides; use `citer()` and `add(key, value)` as in `ZfJSON::AsMap`. |
| Variant adapter backed by `ZuUnion` | `v`, using the active alternative's signature. |
| 128-bit, decimal/fixed, time/date, other UDTs | Explicit application mapping or ADL adapter; no implicit narrowing or stringification. |

1. Do not use JSON/CBOR optional-field omission or null conventions for
   positional D-Bus bodies.  Applications represent optional values in their
   explicit D-Bus schema.
2. Keep unknown variant alternatives as a validated signature-plus-value view,
   allowing dynamic values such as `a{sv}` without a schema for every
   alternative.  Re-encoding honors the destination's byte order and
   alignment; it cannot copy source bytes at an unrelated offset.
3. Defer the `h`/UNIX_FD type until `zdbus` negotiates and owns ancillary
   descriptors.
4. Build one bounds-checked value walker for validation, skipping, dynamic
   values, and typed loading.  Enforce the 255-byte signature limit, separate
   32-array and 32-struct nesting limits, the 64 MiB array-payload limit,
   signature/object-path/name validity, non-NUL strict UTF-8 strings, overflow,
   exact body consumption, and all alignment/padding rules.  Typed traversal
   uses `ZuUnroll` and compile-time dispatch rather than an allocated generic
   tree.
5. Load owned strings/containers through normal field accessors.  Borrowed
   spans and lazy array/variant views require the caller to retain backing
   bytes; `zdbus` supplies that ownership boundary.  No dangling span may
   escape a parser callback.
6. Measure with the same metadata used for output, check limits before
   reserving final storage, then write directly to the final sink.  A caller
   needing diagnostics or a checked frame length calls `measure()`; `save()`
   returns the sink, not an I/O success result.  Keep the
   object stable across both passes.  Initialize required wire padding only;
   use framework scratch and named heaps for bounded auxiliary storage.

## Tests and completion

1. Add TAP tests for inferred signatures and golden encodings; direct
   save/handler round trips for every supported scalar/container mapping; custom
   facets and overrides; argument-list versus nested-struct layout; empty
   bodies; arrays, dictionaries, typed and unknown variants; constructor order
   versus wire order; read-only fields; accessor setters; and all handler forms.
2. Test both byte orders, every alignment boundary, exact consumption,
   malformed values, unsupported mappings, owned/borrowed results, and
   size-pass/output agreement.  Verify invalid input leaves a destination
   object unchanged.
3. Render a single structure through JSON, CBOR, and DBUS facets to demonstrate
   metadata reuse without transport-specific DTOs or hand-written marshallers.
   Round trips are necessary regression coverage, but golden D-Bus vectors and
   the independent `zdbus/interop` tests in `zdbus2.md` remain the compatibility
   check; a codec can round-trip its own incorrect encoding.
4. Retain the current build configuration.  Other compilers, release/debug
   flags, and platforms are out of scope for this implementation.
5. Batch source changes into major codec milestones and rebuild infrequently.
   Rebuild incrementally using the current build artifacts; do not clean,
   reconfigure, rebuild dependent modules, or rerun tests after each local
   edit.  At each major milestone, rebuild the changed `zf/src` target and its
   focused `zf/test` target.  Defer rebuilding dependent modules to the end of
   the complete implementation.
6. At completion, run the focused codec tests and the required dependent test
   set against the current configuration.  Use Valgrind only while diagnosing
   suspected memory corruption and as the final leak/corruption gate; do not
   put it on routine edit/test iterations.

Completion requires an application to define a `ZfStruct` model and obtain a
correct D-Bus signature plus direct binary save/load behavior without writing
any D-Bus marshalling code.
