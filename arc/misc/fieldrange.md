# `ZtStruct` field-range migration plan

## Target contract

Replace the optional `minimum` and `maximum` scalar field arguments with one
compile-time field property:

```c++
ZuFieldProp::Range<Minimum, Maximum>
```

Both endpoints are mandatory template arguments and are inclusive.  As with
other comma-containing field properties, `Range` is parenthesized in the
macro DSL.  The field type argument list retains only the default value, so a
declaration such as:

```c++
(((limit), (Ctor<0>)), (UInt16, 10, 1, 100))
```

becomes:

```c++
(((limit), (Ctor<0>, (Range<1, 100>))), (UInt16, 10))
```

An omitted `Range` continues to mean the scalar type's full existing range;
`Field::minimum()`, `Field::maximum()`, and the type-erased minimum/maximum
constants therefore remain available and return the same intrinsic bounds as
today.  `ZtVFieldProp::Range` distinguishes an explicitly constrained field
from one using those intrinsic bounds.  This is a metadata/API migration, not
an instruction to make every serializer enforce ranges; existing consumers
that validate bounds retain their inclusive `minimum <= value <= maximum`
rule.

## 1. Add the compile-time and run-time properties

1. In `zt/src/ZtStruct.hh`, add
   `template <auto Minimum, auto Maximum> struct Range` to `ZuFieldProp`.
   Give it adjacent `minimum()` and `maximum()` accessors (or equivalently
   named constants) so callers do not need to decompose template arguments.
   Template arity makes a half-specified range ill-formed.
2. Add `HasRange<Props>` and `GetRange<Props>` helpers beside the existing
   `HasEnum`/`GetEnum`, `HasFlags`/`GetFlags`, and `HasNDP`/`GetNDP` helpers.
   Implement a two-NTTP property matcher local to `Range`; do not distort the
   generic one-value/type/sequence extractors in `ZuStruct.hh` to accommodate
   this one pair property.
3. Add `Range` to the `ZtVFieldProp` flags and add the corresponding
   `Value_<ZuFieldProp::Range<Minimum, Maximum>>` mapping.  Keep it a field
   property: do not add it to `ZtFieldType_Props`, because two fields of the
   same printed/scanned type may have different ranges and the range does not
   change type formatting or enum/flags metadata.
4. Extend `ZtVField::print_()` to render the `Range` bit normally with the
   other simple properties.  The actual endpoint values remain available
   through the existing type-erased `constant` dispatcher rather than adding
   duplicate endpoint storage to every `ZtVField`.

## 2. Refactor scalar field adapters

Apply the same shape to the integer macro and the `Float`, `Fixed`, and
`Decimal` adapters:

1. Remove `Min` and `Max` from `ZtField_*` template parameter lists and from
   their read-only/read-write specializations.  Keep the existing `Def`
   function argument, since the default remains a field type argument.
2. Retain the existing intrinsic-bound helpers (`ZuCmp<T>::minimum()` and
   `maximum()`, floating infinities, `ZuFixedMin`/`ZuFixedMax`, and the
   `ZuDecimal` bounds), but use them only when `HasRange<Props>` is false.
3. Implement `Field::minimum()` and `Field::maximum()` by selecting the two
   values from `GetRange<Props>` when present and otherwise selecting the
   intrinsic bounds.  Convert the selected endpoints through the field's
   canonical scalar type at the same boundary as the current function-valued
   arguments, preserving integer-width and floating-point behavior.
4. Update each `constantFn()` `Minimum` and `Maximum` arm to call those field
   accessors.  This preserves the compile-time and type-erased APIs while
   removing the old endpoint function template parameters.
5. Keep `Bool` unchanged: it has no configurable minimum/maximum field
   arguments, and its fixed `false`/`true` endpoints are not part of this
   migration.
6. Do not add implicit default-in-range validation or change incremental
   scalar scanning.  Those are separate contracts.  If an ordering assertion
   is added, apply it only after converting both endpoints to the field scalar
   type so the comparison matches the values exposed to consumers.

Update the header's DSL documentation from `[, default, min, max]` to
`[, default]` for integer, float, fixed, and decimal fields, and document
`Range<Minimum, Maximum>` as the way to attach inclusive bounds.

## 3. Migrate declarations and dependants

Change every four-argument scalar declaration atomically; do not retain a
compatibility overload for the old form.  The current tree contains these
explicit ranges:

- `zt/test/ZtStructTest.cc`, `zt/test/ZtCLITest.cc`, and
  `zt/test/ZtURITest.cc`: move `0, 100` and `0.0, 1` into the property lists,
  leaving defaults `42` and `0.42` in the type argument lists.
- `zcmd/src/zcmd.cc`: migrate the three query-limit fields to
  `Range<1, Zum::MaxQueryLimit>` and the interval field to
  `Range<100, 1000000>`.
- `zum/src/zuserdb.cc`: migrate the password length to `Range<6, 60>`.

After the edits, search multiline declarations for all integer, float, fixed,
and decimal field types with more than one post-default argument.  The
completion gate is that no declaration still encodes bounds positionally.

The only current non-test dependent that directly calls
`Field::minimum()`/`Field::maximum()` is the legacy typed-loading path in
`zv/src/ZvCf.hh`.  Its calls should continue to compile unchanged and should
receive the new property endpoints.  Coordinate this with the separate
`ZvCf` replacement: if that code is deleted first, preserve the same endpoint
use in whichever checked typed configuration loader replaces its range
validation.  `ZtCLI`, `ZtURI`, `ZtJSON`, and `ZvNewCf` currently do not enforce
field ranges merely by loading a node, so this migration must not claim to add
validation to those adapters.

## 4. Test the contract

Extend `ZtStructTest` to cover both representations:

1. At compile time, verify `HasRange` is true for an explicitly ranged field
   and false for an ordinary scalar field, and verify both values exposed by
   `GetRange`.
2. Verify `Field::minimum()` and `Field::maximum()` return the explicit
   endpoints for integer and floating fields, while an unranged field retains
   its existing intrinsic endpoints.
3. Through `ZtVFields`, verify the `Range` bit is set only on explicit ranges
   and that `ZtVFieldConstant::Minimum`/`Maximum` return the same values as the
   compile-time API.
4. In the checked range consumer, verify values exactly equal to both
   endpoints are accepted and values immediately outside an integral range
   are rejected.  This pins the inclusive semantics without attributing range
   enforcement to adapters that do not provide it.
5. Keep CLI/URI serialization and loading tests for the migrated declarations
   to prove moving metadata out of type arguments does not alter their default
   values or wire/text representation.

## 5. Verification

1. Build and run `ZtStructTest`, `ZtCLITest`, and `ZtURITest` first.
2. Build the affected `zcmd`, `zum`, and current `zv` targets and run their
   relevant tests where those optional modules are configured.
3. Run a top-level `make -j8`, followed by the configured test suite.
4. Finish with searches for positional scalar ranges, direct assumptions
   about the old `ZtField_*<Base, Def, Min, Max, ...>` arity, and all
   `Field::minimum()`/`Field::maximum()` consumers.  No shim or deprecated old
   declaration form should remain.
