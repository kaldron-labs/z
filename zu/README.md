# Zu: Framework underlying foundation

Zu provides the vocabulary and compile-time machinery used throughout Z:
traits, spans, structural strings, matching, reflection, numeric types,
formatting, time, encoding and ownership. Include individual headers as
[`<zlib/ZuSpan.hh>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) and link `libZu` for out-of-line facilities. The threaded
runtime and dynamically growing containers live in [Zm](../zm/README.md)
and [Zt](../zt/README.md).

See [Z, STL, Boost, Folly, BDE and NTF equivalents](../docs/Z_STL_BOOST_FOLLY_BDE.md#zu---framework-underlying-foundation)
for feature comparisons, and the [container verb Rosetta stone](../docs/ROSETTA.md)
for insertion, lookup, iteration and removal idioms.

## Forwarding, constants and type transformations

The short names in [ZuLib.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) keep heavily instantiated framework
code concise. Some are aliases; others encode Z's type and lifetime policies.

| Facility | Use |
| --- | --- |
| [`ZuFwd`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L334), [`ZuMv`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L343) | Forward a value with its template argument's reference category, or obtain a move reference |
| [`ZuMvPtr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L359) | Move a smart pointer or transfer a raw pointer while nulling the source raw pointer |
| [`ZuFwdLike`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L365) | Propagate a model type's constness and reference category; an optional target type also converts the value |
| [`ZuConstant<T, V>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Carry a typed compile-time value, including permitted non-integral and structural class values; conversion and invocation return const references |
| [`ZuInt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuUnsigned`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuBool`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuTrue`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuFalse`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Concise constant wrappers for common integer and Boolean cases |
| [`ZuAlwaysTrue`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuAlwaysFalse`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Type-dependent constant predicates |
| [`ZuInt128`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh), [`ZuUInt128`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh) | Signed/unsigned 128-bit constant wrappers from ZuInt.hh |
| [`ZuDeref`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuStrip`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuDecay`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Remove references, top-level cv qualifiers, or both; ZuDecay preserves array and function types |
| [`ZuAsIs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuMkConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuMkVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Pass a type through, or add a top-level qualifier |
| [`ZuMkRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuMkLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuMkCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Form rvalue, lvalue or const-lvalue references |
| [`ZuRDecay`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Recursively decay participating composites through the ADL-selected decayer |
| [`ZuNorm`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuNorm.hh), [`ZuEquiv`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuEquiv.hh) | Normalize representation-equivalent character types and compare their normalized types |
| [`ZuAlt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAlt.hh) | Select the alternate char/wchar_t representation |
| [`ZuUnder`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuUnderlying`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L747) | Obtain an enum's or participating wrapper's underlying type/value through framework customization |

## Constraints and compile-time queries

The `ZuIs...` traits and the constraint aliases have distinct roles. For example,
[`ZuIsSame<A, B>{}`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) produces a Boolean constant, while [`ZuSame<A, B, R>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) supplies
`R` only when the condition holds. Most constraint aliases default `R` to `void`.
[`ZuIs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) and [`ZuIsNot`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) are themselves constraint aliases.

| Facility | Use |
| --- | --- |
| [`ZuIsConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuIsVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Test top-level qualifiers |
| [`ZuConst`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuMutable`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNonVolatile`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Select qualified or unqualified types in overload constraints |
| [`ZuIsLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuIsCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuIsRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Test lvalue, const-lvalue and rvalue references |
| [`ZuLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotLRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotCRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotRRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Constrain reference categories and their complements |
| [`ZuIsSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotSame`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Test or constrain exact type identity |
| [`ZuIs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuIsNot`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Constrain decayed identity or accessible derived-to-base pointer conversion, or its complement |
| [`ZuIsBase<From, To>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuBase`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotBase`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Use Z's derived-to-base argument order and identity/access rules |
| [`ZuIsConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotConvertible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Probe or constrain conversion using Z's reference-normalized source/destination rules |
| [`ZuIsConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNotConstructible`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Probe or constrain construction with source first and destination second; the probe accepts cast syntax for non-composite destinations |
| [`ZuNXConstruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNXMove`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNXCopy`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNXDestroy`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Query default construction, move, copy and destruction exception behavior under Z's class/non-class policies |
| [`ZuIf`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuIfT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Choose between two types, or supply a type only when a Boolean condition holds |
| [`ZuDeclVal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuConstEval`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L753) | Form values in unevaluated probes and detect constant evaluation |
| [`ZuSize`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Obtain zero logical size for void/empty types, sizeof otherwise |
| [`ZuLargest`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLargest.hh), [`ZuMostAligned`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMostAligned.hh) | Select a type by logical size or alignment from a pack/type list; the first type wins ties |

These predicates deliberately share framework conventions rather than requiring
call sites to compose normalization and qualification repeatedly. Their exact
probes are in [ZuLib.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh).

## Compiler hints, declarations and storage

| Facility | Use |
| --- | --- |
| [`ZuLikely`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L138), [`ZuUnlikely`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L139) | Apply expression-level branch hints, including inside compound conditions |
| [`ZuInline`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuNoInline`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Request always-inline or no-inline behavior on supported compilers |
| [`ZuUnreachable`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L145) | Mark an unreachable path, with an abort in debug builds |
| [`ZuAssert`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L28), [`ZuCAssert`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuAssert.hh#L32) | Express template/preprocessor-friendly compile-time assertions; ZuCAssert is the compact switch/case spelling |
| [`ZuOpBool`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Generate a truth conversion from an existing operator! implementation |
| [`ZuDerive`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDerive.hh#L61) | Generate a distinct derived type with forwarding constructors and derived-returning assignment; its short type ID limits expanded symbol names |
| [`ZuPP`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPP.hh) | Expand deferred recursive macros, manipulate tuples and map arguments |
| [`ZuIfReserved`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuReserved.hh#L117) | Branch on C++23 keyword tokens during macro expansion |
| [`ZuFnName`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFnName.hh) | Obtain the framework's compiler-specific function-name string |
| [`ZuAddr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L758), [`ZuLaunder`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L183), [`ZuNew`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L776) | Obtain an object's real address, launder a pointer, or construct into supplied storage with constant-evaluation support |
| [`ZuCast`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L783) | Convert eligible representations by value through bit casting |
| [`ZuPun`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Reuse union storage with explicit active-member lifetime control |
| [`ZuAlloca`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L642) | Allocate aligned stack storage of runtime size; callers bound stack use |
| [`ZuClear`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L127) | Clear a buffer or span through the platform's clearing primitive, resisting dead-store elimination |
| [`ZuCanOverlap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Concise framework spelling of the no_unique_address attribute |
| [`ZuElem`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh), [`ZuIsElem`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh), [`ZuElemType`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh), [`ZuElemVal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh) | Represent an explicitly managed structural array slot, detect/unwrap its type, and forward its value |

[`ZuElem`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuElem.hh) starts without an active element. Construction, destruction and access
must follow the owning array/storage facility's lifetime rules. [`ZuPun`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) likewise
leaves member lifetime and aliasing discipline to its caller.

## Type lists and constant sequences

[ZuTL.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) accepts type packs or type lists for the operations that
provide both forms. [ZuSeq.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) supplies unsigned constant sequences
and conversions used by dispatch, tuples, strings and reflection.

| Facility | Use |
| --- | --- |
| [`ZuTypeList`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh), [`ZuTypeRepeat`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Declare a type list or repeat a type N times |
| [`ZuType`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeIndex`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeIn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Select a type by index, locate its first occurrence, or test membership; index lookup requires the type to be present |
| [`ZuTypeHead`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeTail`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeSlice`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Take a prefix, drop a prefix, or select a strided slice |
| [`ZuTypeApply`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Instantiate a template with a list's types |
| [`ZuTypeRev`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeMap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeGrep`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Reverse, transform or filter types |
| [`ZuTypeReduce`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Recursively reduce types through a supplied template |
| [`ZuTypeMerge`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeSort`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Merge sorted lists or merge-sort a list using an index trait |
| [`ZuTypeConcat`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTypeUnique`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Concatenate lists or remove duplicates while retaining first occurrence order |
| [`ZuTLConverts`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh), [`ZuTLConstructs`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTL.hh) | Check elementwise conversion or construction between corresponding type lists |
| [`ZuSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh), [`ZuMkSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Declare unsigned indices or generate the sequence [0, N) |
| [`ZuMin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh), [`ZuMax`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Reduce a constant sequence to its minimum/maximum |
| [`ZuSeqCall`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Expand indexed accessor results into a callable's argument list |
| [`ZuSeqBitmap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L61), [`ZuBitmapSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Convert between a sequence of bit positions and a 64-bit bitmap |
| [`ZuSeqTL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh), [`ZuTLSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Convert between an unsigned sequence and a list of constant-wrapper types |
| [`ZuSeqString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L107), [`ZuStringSeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Convert between a constant sequence and a structural string |
| [`ZuSeqArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh#L128), [`ZuArraySeq`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSeq.hh) | Convert between a constant sequence and a structural array |

## Callables and compile-time dispatch

| Facility | Use |
| --- | --- |
| [`ZuDefaultAxor`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh#L652) | Construct a forwarding pass-through accessor |
| [`ZuLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Wrap a self-referential lambda, passing the wrapper as its first argument for recursion |
| [`ZuDeduce`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDeduce.hh) | Extract return, argument-list and member-object types from supported function/member-pointer signatures |
| [`ZuInvoke`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInvoke.hh#L35) | Invoke a member function, a callable taking the supplied object pointer, or an unbound callable that omits it |
| [`ZuLambdaTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuArgList`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuLambdaReturn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Inspect callable signatures and derive argument-list/return types |
| [`ZuIsMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuIsVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuIsStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Query mutability, void return and statelessness |
| [`ZuMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuNotMutableLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuNotVoidRetLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuNotStatelessLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) | Constrain callable properties and their complements |
| [`ZuInvokeFnT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh), [`ZuInvokeFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L175) | Derive a function-pointer type and convert a compatible stateless callable to it |
| [`ZuInvokeLambda`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh#L201) | Invoke a stateless callable by type with an explicit statelessness check; the runtime implementation relies on supported compiler behavior |
| [`ZuSwitch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSwitch.hh) | Dispatch a runtime index to a callable instantiated with a compile-time index |
| [`ZuUnroll`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnroll.hh) | Expand indexed/type-based calls and supported map/reduce operations at compile time |

See [ZuLambdaTraits.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLambdaTraits.hh) for explicit argument-list forms
when a callable's signature cannot be deduced directly, and the
[dispatch examples](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSwitch.hh) for default-case handling.

## Traits, comparison and ownership

[`ZuTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTraits.hh) supplies the shared ADL vocabulary for strings, containers, numeric
values and user types. Comparison, hashing and printing discover corresponding
customizations without a central type registry.

| Facility | Use |
| --- | --- |
| [`ZuCmp`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh) | Combine ordering, equality, null-sentinel and numeric-bound policies |
| [`ZuCmp0`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh), [`ZuCmp_1`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh), [`ZuCmpN`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh) | Select zero, any negative value, or a specified value as the null policy |
| [`ZuCompare`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L750), [`ZuEquals`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L755), [`ZuNull`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L760) | Apply a value's comparator to ordering, equality or null testing |
| [`ZuNullRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuCmp.hh#L747) | Obtain a reusable const-reference sentinel through the selected comparator |
| [`ZuICmp`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuICmp.hh) | Compare supported text case-insensitively |
| [`ZuHash`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHash.hh) | Hash values, including consistent hashing across supported text representations |
| [`ZuObject`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObject.hh), [`ZuObjectTraits`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuObjectTraits.hh) | Supply/detect the embedded non-atomic intrusive reference-counted object model |
| [`ZuPolymorph`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPolymorph.hh) | Supply a non-atomic intrusive-counted base with a virtual destructor |
| [`ZuRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuRef.hh) | Share intrusive ownership without a separate control block |
| [`ZuPtr`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPtr.hh) | Transfer single ownership with a move-only pointer; deletion has no configurable deleter policy |
| [`ZuGuard`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) | Run lambda cleanup on scope exit, unless cancelled |

Keep [`ZuRef`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuRef.hh) ownership operations confined appropriately; use `ZmRef` and the
atomic-counted object model for cross-thread ownership. Neither intrusive
reference type provides weak-pointer cycle collection. Borrowing a raw pointer
does not extend the object's lifetime.

## Strings, spans and arrays

Choose the representation according to ownership and when its size is known:

| Facility | Use |
| --- | --- |
| [`ZuSpan<T>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh), [`ZuCSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh), [`ZuWSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh), [`ZuBSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSpan.hh) | Borrow a contiguous sequence, narrow/wide text or bytes, with slicing, comparison, hashing and searching |
| [`ZuArray<T, N>`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh), [`ZuCArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh), [`ZuWArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) | Own fixed-capacity structural storage with a separate logical length; narrow/wide character aliases integrate with string operations |
| [`ZuString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh), [`ZuStringT`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh), [`ZuStringTL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuString.hh) | Carry a structural compile-time string value, a string-tag type or a type list of string tags |
| [`ZuMArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMArray.hh) | Transform an existing array's elements on demand through a compile-time shim, including writable element proxies |
| [`ZuVArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVArray.hh) | Expose different array implementations through one type-erased function-pointer interface |
| [`ZuStdString`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStdString.hh) | Opt into interoperability with standard strings |
| [`ZuArrayFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArrayFn.hh), [`ZuStringFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStringFn.hh) | Reuse trait-selected element initialization/movement/comparison and narrow/wide string operations |
| [`ZuIter`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIter.hh) | Build random-access iterators for array-like interfaces |
| [`ZuSort`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSort.hh#L139) | Sort through three-way comparison and Z element operations; its quicksort does not promise a worst-case n log n bound |
| [`ZuSearch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L23), [`ZuSearchFound`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L12), [`ZuSearchPos`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L13) | Binary-search sorted data and decode the packed found flag/insertion position |
| [`ZuInterSearch`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSearch.hh#L66) | Search sorted data by interpolation when the key distribution makes it useful |

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
avoid resizing it while views are in use. [`ZuArray`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuArray.hh) capacity is not its logical
length; use `push()` to append an element. See
[span tests](test/ZuSpanTest.cc) and [array tests](test/ZuArrayTest.cc).

## Tuples, unions and reflection

[`ZuTuple`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTuple.hh) stores heterogeneous values with `p<N>()` access, size-ordered flat
storage and positional mutation. [`ZuUnion`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh) holds one selected alternative,
including void alternatives. Test `is<T>()` before reading `p<T>()`, or use
`ptr<T>()` for a pointer that is null when that type is inactive. `type()` returns
the active index. [`ZuVoid`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuLib.hh) is the framework's empty value type. See
[tuple tests](test/ZuTupleTest.cc) and [ZuUnion.hh](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUnion.hh).

[`ZuStruct`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L460) attaches field/accessor metadata and composite keys to an existing
C++ type; [`ZuStructFacet`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStruct.hh#L768) defines alternate field views. Start with
[ZuStructTest](test/ZuStructTest.cc) for member, method and lambda accessors.
For serialization, use [ZfStruct](https://github.com/kaldron-labs/z/blob/main/zf/src/ZfStruct.hh), which adds wire types,
validation properties and format bindings to this foundation.

## Numeric types and machine operations

| Facility | Use |
| --- | --- |
| [`int128_t`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh), [`uint128_t`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuInt.hh) | Use native signed/unsigned 128-bit compiler integers, declared in ZuInt.hh |
| [`ZuFP`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFP.hh) | Inspect floating-point representation and precision helpers |
| [`Zu_add`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L179), [`Zu_sub`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L180), [`Zu_mul`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L181); [`ZuIntrin::add`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L242), [`ZuIntrin::sub`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L244), [`ZuIntrin::mul`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L246) | Perform checked integer arithmetic with overflow reporting |
| [`Zu_nan*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh), [`Zu_isnan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L197); [`ZuIntrin::nan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L250), [`ZuIntrin::isnan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh#L258) | Generate and test floating-point NaNs |
| [`Zu_bswap*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh), [`Zu_clz*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh), [`Zu_ctz*`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIntrin.hh) | Swap integer bytes and count leading/trailing zero bits |
| [`ZuByteSwap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh), [`ZuBigEndian`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh), [`ZuLittleEndian`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuByteSwap.hh) | Store values in swapped, big-endian or little-endian representation |
| [`ZuFixed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFixed.hh) | Represent a mantissa with a runtime decimal scale, null/reset sentinels and scanning/formatting conversions |
| [`ZuDecimal`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimal.hh), [`ZuDecimalFn`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDecimalFn.hh) | Use fixed decimal arithmetic with 18 integer and 18 fractional digits and its shared arithmetic helpers |

Fixed and decimal types have bounded precision/range; follow their parsing and
arithmetic contracts when converting from binary floating point. See
[decimal tests](test/ZuDecimalTest.cc).

## Packed state

[`ZuBitmap`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitmap.hh) manages bit sets, [`ZuBitfield`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitfield.hh) exposes runtime-indexed packed fields
with assignment proxies, and [`ZuBitStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBitStream.hh) reads/writes successive symbols that
need not be byte-aligned. These facilities operate on packed storage rather
than requiring a separate full-sized object for each logical value.

## Numeric input, formatting and streams

| Facility | Use |
| --- | --- |
| [`ZuBox`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh), [`ZuBoxed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L730) | Combine scalar null policy and scanning/formatting, or adapt an existing scalar by reference |
| [`ZuNBox`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh), [`ZuNBoxed`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L757) | Add null-string formatting policy to owned/borrowed boxed scalars |
| [`Zu_nscan`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh), [`Zu_atou`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L332), [`Zu_atoi`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L336), [`Zu_atof`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_aton.hh#L340) | Scan numeric input under Z format policies; unsigned, signed and floating entry points are in Zu_aton.hh |
| [`Zu_nprint`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh), [`Zu_utoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L915), [`Zu_itoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L922), [`Zu_ftoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L929) | Write numeric output under compile-time format policies; entry points are in Zu_ntoa.hh |
| [`Zu_ulen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L911), [`Zu_ilen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L918), [`Zu_flen`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L925) | Calculate format-aware destination buffer sizes for unsigned, signed and floating output |
| [`ZuFmt`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuFmt.hh) | Select width, base, precision and other formatting policies through types |
| [`ZuPrint`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPrint.hh) | Expose a type's printing behavior through ADL, including its print(stream) implementation |
| [`ZuStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuStream.hh), [`ZuVStream`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuVStream.hh) | Write into bounded caller-supplied text storage through a concrete or type-erased stream |
| [`ZuJoin`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuJoin.hh) | Stream elements directly with a delimiter |

For example, [`ZuBox<unsigned>{"42"}`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh) parses an unsigned value and
[`ZuBoxed(value).hex()`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBox.hh#L730) produces a hexadecimal formatting adapter. Size the output
buffer using the chosen format; [`Zu_ftoa`](https://github.com/kaldron-labs/z/blob/main/zu/src/Zu_ntoa.hh#L929) does not automatically fall back to
scientific notation. See [numeric conversion](test/ZuNumConvTest.cc) and
[stream](test/ZuStreamTest.cc) tests for null sentinels, syntax and output policy.

## Time, identifiers and versions

[`ZuTime`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTime.hh) stores seconds/nanoseconds with a null sentinel and represents both
instants and intervals. [`ZuDateTime`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDateTime.hh) supplies calendar/date-time operations;
use their format/scan adapters for external text. Runtime clocks are in Zm and
time-zone lookup is in Zt. See [time tests](test/ZuTimeTest.cc).

[`ZuID`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuID.hh) is a bounded by-value name with array length, comparison, hashing and
printing. [`ZuSemVer`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuSemVer.hh) packs a numeric major/minor/patch value; minor is bounded
by 99 and patch by 999, and prerelease/build metadata is outside its format.

## Unicode and binary-text codecs

| Facility | Use |
| --- | --- |
| [`ZuUTF`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh) | Convert supported character encodings into caller-supplied storage |
| [`ZuUTF8`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh), [`ZuUTF16`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh), [`ZuUTF32`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh) | Decode/encode code points and measure input/output lengths; UTF-16 handles surrogate pairs and UTF-32 supplies Unicode display-width lookup |
| [`ZuUTFSpan`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuUTF.hh) | Carry packed input length, output length and display width |
| [`ZuHex`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuHex.hh) | Encode hexadecimal text and decode into caller storage, including in-place decoding |
| [`ZuBase32`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase32.hh), [`ZuBase64`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64.hh), [`ZuBase64URL`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuBase64URL.hh) | Encode/decode binary-text alphabets, including URL-safe Base64 |
| [`ZuPercent`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuPercent.hh) | Apply policy-driven percent encoding, plus/space handling and in-place decoding |

Size the destination using the selected codec's length API and check the
returned result. These codecs do not allocate an owning output string.
Use the corresponding [codec tests](test/) for round-trip and invalid-input
examples.

## Text tokenization

[`ZuTokenizer::Delimited`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh) returns borrowed tokens without allocation;
[`ZuTokenizer::WhiteSpace`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTokenizer.hh) supplies ASCII whitespace scanning. The
[tokenizer tests](test/ZuTokenizerTest.cc) demonstrate token rules and scanning.
Keep the backing text alive while consuming borrowed tokens.

## ZuMatcher

[`ZuMatcher`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuMatcher.hh#L447) builds an Aho–Corasick automaton from a type's `Keys` list. It maps
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
for searching (returning offset and key index). Consult
[matching tests](test/ZuMatchTest.cc) for overlap and binary-key cases. Large
instantiations increase compile cost; keep substantial matcher definitions
in implementation files.

## I/O vocabulary, C interfaces and diagnostics

[`Zu::IOResult`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuIOResult.hh) prints normalized I/O status codes, including OK, end of file,
error and not-ready states. [`ZuDemangle`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuDemangle.hh) supplies readable C++ type names for
diagnostics.

The headers [zu_bitmap.h](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_bitmap.h), [zu_decimal.h](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_decimal.h)
and [zu_time.h](https://github.com/kaldron-labs/z/blob/main/zu/src/zu_time.h) expose bitmap, decimal and time facilities to C.

## Examples and tests

[`ZuTest`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTest.hh#L255) and [`ZuTestUtil`](https://github.com/kaldron-labs/z/blob/main/zu/src/ZuTestUtil.hh) provide TAP output, static nested test counts and
shared test-program utilities. Run `make -C zu test` from a configured build
tree. The [test directory](test/) contains executable API examples and boundary
cases. See the top-level [testing guide](../docs/TESTING.md) for environment,
diagnostics and port allocations, and [dependencies](../docs/DEPENDENCIES.md)
for build prerequisites.
