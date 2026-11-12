# Z, STL and Boost equivalents

# Zu

## Abbreviated forwarding and move operations

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuFwd` | `std::forward` | `boost::forward` (Move) |
| `ZuMv` | `std::move` | `boost::move` |
| `ZuMvPtr`<br>\+ nulls a moved raw pointer as well as moving smart pointers | Partial: `std::exchange(p, nullptr)` for raw pointers, `std::move` for smart pointers | Partial: `boost::exchange`, `boost::move` |
| `ZuFwdLike`<br>\+ optional target-type conversion | `std::forward_like` (C++23) | Partial: Boost.Move forwarding + TypeTraits qualifier propagation |

## Compile-time constants

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuConstant<T, V>` — typed compile-time value, including non-integral and structural class values such as `ZuString`<br>\+ conversion and call return const references | `std::integral_constant<T, V>` — also accepts permitted non-integral template values<br>\- conversion and call return by value | `boost::integral_constant` |
| `ZuInt` | `std::integral_constant<int, I>` | `boost::mp11::mp_int<I>` |
| `ZuUnsigned` | `std::integral_constant<unsigned, I>` | `boost::integral_constant<unsigned, I>` |
| `ZuBool` | `std::bool_constant` | `boost::mp11::mp_bool` |
| `ZuTrue` | `std::true_type` | `boost::true_type` |
| `ZuFalse` | `std::false_type` | `boost::false_type` |
| `ZuAlwaysTrue` | Partial: dependent `std::true_type` wrapper | Partial: `boost::mp11::mp_true` wrapper |
| `ZuAlwaysFalse` | Partial: dependent `std::false_type` wrapper | Partial: `boost::mp11::mp_false` wrapper |
| `ZuInt128` | Partial: `std::integral_constant` over compiler 128-bit integer | Partial: constant wrapper over a supported 128-bit type |
| `ZuUInt128` | Partial: `std::integral_constant` over compiler unsigned 128-bit integer | Partial: constant wrapper over a supported 128-bit type |

## Abbreviated type transformations and normalizations

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuDeref` | `std::remove_reference_t` | `boost::remove_reference_t` |
| `ZuStrip` | `std::remove_cv_t` | `boost::remove_cv_t` |
| `ZuDecay`<br>\- does not decay arrays/functions to pointers | `std::remove_cvref_t`<br>\+ matches the actual operation; `std::decay_t` has different semantics | `boost::remove_cv_ref_t` |
| `ZuAsIs` | `std::type_identity_t` | `boost::type_identity_t` |
| `ZuMkConst` | Partial: `std::add_const_t`<br>\+ handles reference inputs through trait rules | `boost::add_const_t` |
| `ZuMkVolatile` | Partial: `std::add_volatile_t` | `boost::add_volatile_t` |
| `ZuMkRRef` | Partial: `std::add_rvalue_reference_t`<br>\+ also handles non-referenceable types | `boost::add_rvalue_reference_t` |
| `ZuMkLRef` | Partial: `std::add_lvalue_reference_t` | `boost::add_lvalue_reference_t` |
| `ZuMkCRef` | Composition: `std::add_lvalue_reference_t<std::add_const_t<T>>` | Composition: `boost::add_lvalue_reference_t<boost::add_const_t<T>>` |
| `ZuRDecay`<br>\+ ADL-customizable recursive decay of composites | N/A; `std::remove_cvref_t` is shallow | Partial: recursive Boost.Mp11 / Hana transformation |
| `ZuNorm`<br>\+ customizable normalization of representation-equivalent character types | Partial: `std::remove_cvref_t`<br>\- does not normalize character representations | Partial: Boost.TypeTraits; custom mapping |
| `ZuEquiv` | Composition: `std::is_same` with a custom normalization | Composition: `boost::is_same` with a custom normalization |
| `ZuAlt`<br>\+ char/wchar_t alternate-type mapping | N/A | N/A |
| `ZuUnder`<br>\+ customizable for wrappers as well as enums | Partial: `std::underlying_type_t`<br>\- enums only | Partial: `boost::underlying_type_t` |
| `ZuUnderlying`<br>\+ unwraps participating wrapper types as well as enums | Partial: `std::to_underlying` (C++23)<br>\- enums only | Partial: `boost::underlying_cast` |

## Qualifier and reference constraints

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuIsConst` | `std::is_const` | `boost::is_const` |
| `ZuIsVolatile` | `std::is_volatile` | `boost::is_volatile` |
| `ZuConst` | `std::enable_if_t<std::is_const_v<U>, R>` | `boost::enable_if<boost::is_const<U>, R>::type` |
| `ZuMutable` | `std::enable_if_t<!std::is_const_v<U>, R>` | `boost::disable_if<boost::is_const<U>, R>::type` |
| `ZuVolatile` | `std::enable_if_t<std::is_volatile_v<U>, R>` | `boost::enable_if<boost::is_volatile<U>, R>::type` |
| `ZuNonVolatile` | `std::enable_if_t<!std::is_volatile_v<U>, R>` | `boost::disable_if<boost::is_volatile<U>, R>::type` |
| `ZuIsLRef` | `std::is_lvalue_reference` | `boost::is_lvalue_reference` |
| `ZuIsCRef` | Composition: lvalue-reference test and const-referent test | Composition of Boost.TypeTraits |
| `ZuIsRRef` | `std::is_rvalue_reference` | `boost::is_rvalue_reference` |
| `ZuLRef` | `std::enable_if_t<std::is_lvalue_reference_v<U>, R>` | Boost.EnableIf + `boost::is_lvalue_reference` |
| `ZuNotLRef` | Negated `std::is_lvalue_reference` constraint | Boost.DisableIf + `boost::is_lvalue_reference` |
| `ZuCRef` | Constraint on const lvalue references | Composition of Boost.EnableIf and TypeTraits |
| `ZuNotCRef` | Negated const-lvalue-reference constraint | Composition of Boost.DisableIf and TypeTraits |
| `ZuRRef` | `std::enable_if_t<std::is_rvalue_reference_v<U>, R>` | Boost.EnableIf + `boost::is_rvalue_reference` |
| `ZuNotRRef` | Negated `std::is_rvalue_reference` constraint | Boost.DisableIf + `boost::is_rvalue_reference` |

## Type identity and inheritance constraints

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuIsSame` | `std::is_same` | `boost::is_same` |
| `ZuSame` | `std::enable_if_t<std::is_same_v<A, B>, R>` | Boost.EnableIf + `boost::is_same` |
| `ZuNotSame` | Negated `std::is_same` constraint | Boost.DisableIf + `boost::is_same` |
| `ZuIs`<br>\+ decays types and accepts identity or accessible pointer base conversion | Partial: `std::same_as` / `std::derived_from` constraint | Partial: Boost.TypeTraits + EnableIf |
| `ZuIsNot` | Negation of the identity/base-conversion constraint | Partial: Boost.TypeTraits + DisableIf |
| `ZuIsBase`<br>\- argument order and identity/access rules differ from `is_base_of` | Partial: `std::is_base_of` / `std::derived_from` | Partial: `boost::is_base_of` |
| `ZuBase` | Constraint on strict accessible base conversion | Partial: Boost.EnableIf + TypeTraits |
| `ZuNotBase` | Negated strict accessible base-conversion constraint | Partial: Boost.DisableIf + TypeTraits |

## Conversion, construction and exception traits

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuIsConvertible`<br>\- uses Z's reference-normalized probe semantics | Partial: `std::is_convertible` | Partial: `boost::is_convertible` |
| `ZuConvertible` | Partial: `std::enable_if_t<std::is_convertible_v<From, To>, R>` | Partial: Boost.EnableIf + `boost::is_convertible` |
| `ZuNotConvertible` | Partial: negated `std::is_convertible` constraint | Partial: Boost.DisableIf + `boost::is_convertible` |
| `ZuIsConstructible`<br>\- source/destination API and cast probing differ | Partial: `std::is_constructible`<br>\+ variadic constructor-argument API | Partial: `boost::is_constructible` |
| `ZuConstructible` | Partial: `std::enable_if_t<std::is_constructible_v<To, From>, R>` | Partial: Boost.EnableIf + `boost::is_constructible` |
| `ZuNotConstructible` | Partial: negated constructibility constraint | Partial: Boost.DisableIf + `boost::is_constructible` |
| `ZuNXConstruct` | Partial: `std::is_nothrow_default_constructible` | Partial: `boost::is_nothrow_default_constructible` |
| `ZuNXMove` | Partial: `std::is_nothrow_move_constructible` | Partial: `boost::is_nothrow_move_constructible` |
| `ZuNXCopy` | Partial: `std::is_nothrow_copy_constructible` | Partial: `boost::is_nothrow_copy_constructible` |
| `ZuNXDestroy` | Partial: `std::is_nothrow_destructible` | Partial: `boost::is_nothrow_destructible` |

## Conditional types and compile-time queries

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuIf` | `std::conditional_t` | `boost::mp11::mp_if_c` |
| `ZuIfT` | `std::enable_if_t` | `boost::enable_if_c<...>::type` |
| `ZuDeclVal` | `std::declval` | `boost::declval` |
| `ZuConstEval` | `std::is_constant_evaluated` | N/A; use the standard facility or compiler intrinsic |
| `ZuSize`<br>\+ zero logical size for void/empty types; ordinary sizeof otherwise | Partial: `std::is_empty` with custom logical-size calculation | Partial: Boost.TypeTraits + `sizeof` |
| `ZuLargest`<br>\+ selects a type from a pack or ZuTypeList; first wins ties; handles void | N/A; size measurement alone does not select a type | Partial: Boost.Mp11 reduction by `sizeof` |
| `ZuMostAligned`<br>\+ selects the most-aligned type from a pack or ZuTypeList; first wins ties | N/A; alignment measurement alone does not select a type | Partial: Boost.Mp11 reduction by `alignof` |
| `ZuTraits`<br>\+ common ADL vocabulary for strings, containers and numerics | Partial: `<type_traits>`, `std::ranges` concepts, `std::numeric_limits` | Partial: Boost.TypeTraits, Range, NumericConversion |

## Compiler hints

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuLikely`<br>\+ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | `BOOST_LIKELY` |
| `ZuUnlikely`<br>\+ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | `BOOST_UNLIKELY` |
| `ZuInline`<br>\+ requests always_inline on supported compilers | N/A; the inline keyword does not request forced optimizer inlining | `BOOST_FORCEINLINE` |
| `ZuNoInline` | N/A; compiler attributes | `BOOST_NOINLINE` |
| `ZuUnreachable`<br>\+ debug build aborts at the failed assumption | `std::unreachable` (C++23) | `BOOST_UNREACHABLE_RETURN` (partial return-site idiom) |

## Assertions, declarations and preprocessor utilities

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuAssert`<br>\+ deferred ZuPP expansion and optional expression-text diagnostics | N/A; wraps core static_assert with macro-expansion support | `BOOST_STATIC_ASSERT`, `BOOST_STATIC_ASSERT_MSG` |
| `ZuCAssert`<br>\+ shorter spelling for a nonzero compile-time assertion<br>\- older switch/case idiom; no semantic advantage over static_assert | N/A; core static_assert supplies the assertion itself | `BOOST_STATIC_ASSERT` |
| `ZuOpBool`<br>\+ emits a truth conversion from the existing operator! without repeating its logic<br>\- pointer conversion permits more conversions than explicit bool | N/A; a conversion operator still needs a user-provided body | Partial: `BOOST_EXPLICIT_OPERATOR_BOOL` |
| `ZuDerive`<br>\+ short distinct type ID reduces complex template names in symbols<br>\+ generates constrained forwarding construction and derived-returning assignment | N/A; inheritance alone does not generate this forwarding interface | Partial: Boost.StrongTypedef<br>\- different wrapper/conversion semantics |
| `ZuPP` macro family<br>\+ deferred recursive expansion, tuple manipulation and argument mapping | N/A; the preprocessor supplies primitives, not these macro algorithms | Boost.Preprocessor<br>\+ broader documented macro algorithms |
| `ZuIfReserved`<br>\+ branches on C++23 keyword tokens | N/A | Partial: Boost.Preprocessor token dispatch; custom keyword table |
| `ZuFnName` | `std::source_location::function_name()` | `BOOST_CURRENT_FUNCTION` |

## Object storage and lifetime operations

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuLaunder` | `std::launder` | `boost::core::launder` |
| `ZuAddr` | `std::addressof` | `boost::addressof` |
| `ZuNew` | `std::construct_at` | Partial: placement `new`; Boost.Container allocator construction |
| `ZuCast` | `std::bit_cast` | `boost::core::bit_cast` |
| `ZuPun`<br>\+ reusable union with forwarding input construction and suppressed member destruction<br>\- caller manages active-member lifetime and aliasing | Partial: `std::bit_cast`<br>\+ defined value conversion for eligible types<br>\- does not provide shared storage or lifetime control | Partial: `boost::core::bit_cast` |
| `ZuAlloca`<br>\+ concise aligned stack allocation with runtime size<br>\- no stack-capacity fallback | N/A; runtime-sized stack arrays are not a core C++23 facility | N/A; platform `alloca` remains nonstandard |
| `ZuClear`<br>\+ OS-backed clearing resistant to dead-store elimination | N/A in C++23; `std::memset` does not guarantee secure erasure | N/A; ordinary fill algorithms do not guarantee secure erasure |
| `ZuCanOverlap`<br>\+ shorter framework spelling of the standard attribute<br>\- naming alias only; identical attribute semantics | N/A; exact alias of core [[no_unique_address]] | `BOOST_ATTRIBUTE_NO_UNIQUE_ADDRESS` |

## Array element storage and access

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuElem`<br>\+ aligned structural union with no initial active element and explicit lifetime control<br>\+ short underlying type ID limits symbol/debug-name expansion | Partial: `std::construct_at`<br>\- caller must supply suitable storage and lifetime management | Partial: Boost.Optional storage machinery; no matching public wrapper |
| `ZuIsElem` | N/A; custom-wrapper type trait | N/A; custom-wrapper type trait |
| `ZuElemType` | N/A; custom-wrapper unwrapping trait | N/A; custom-wrapper unwrapping trait |
| `ZuElemVal` | N/A; conditional wrapper-value access | N/A; custom wrapper accessor |

## Primitive numeric types and traits

| Z facility | STL | Boost |
| --- | --- | --- |
| `int128_t`, `uint128_t` (`ZuInt.hh`)<br>\+ native compiler integer operations on supported targets<br>\- compiler-extension types | N/A; no required standard 128-bit integer | `boost::multiprecision::int128_t`, `uint128_t`<br>\+ portable fixed-width backends |
| `ZuFP`<br>\+ IEEE representation and precision helpers | `std::numeric_limits`, `<cmath>` | Boost.Math floating-point utilities |

## Checked arithmetic and floating-point intrinsics

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zu_add` / `ZuIntrin::add` | N/A in C++23; checked-addition composition | Partial: Boost.SafeNumerics<br>\+ broader checked-number policies |
| `Zu_sub` / `ZuIntrin::sub` | N/A in C++23; checked-subtraction composition | Partial: Boost.SafeNumerics |
| `Zu_mul` / `ZuIntrin::mul` | N/A in C++23; checked-multiplication composition | Partial: Boost.SafeNumerics |
| `Zu_nan*` / `ZuIntrin::nan` | `std::numeric_limits<T>::quiet_NaN`, `std::nan` | Boost.Math floating-point utilities |
| `Zu_isnan` / `ZuIntrin::isnan` | `std::isnan` | `boost::math::isnan` |

## Byte order and byte swapping

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuByteSwap`<br>\+ swapped-storage wrapper | Partial: `std::byteswap` (C++23)<br>\- operation rather than storage wrapper | Boost.Endian arithmetic/buffer types |
| `ZuBigEndian` | Partial: `std::endian` + `std::byteswap` | Boost.Endian big-endian types |
| `ZuLittleEndian` | Partial: `std::endian` + `std::byteswap` | Boost.Endian little-endian types |
| `Zu_bswap*` (`ZuIntrin.hh`) | `std::byteswap` (C++23) | `boost::endian::endian_reverse` |

## I/O status and demangling

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zu::IOResult`<br>\+ small normalized I/O status vocabulary | Partial: `std::error_code`, stream state flags | Partial: Boost.System / Asio error codes |
| `ZuDemangle` | N/A; `typeid(T).name()` does not guarantee readable text | `boost::core::demangle`, Boost.TypeIndex |

## C interfaces

| Z facility | STL | Boost |
| --- | --- | --- |
| `zu_bitmap.h` C API | N/A; `std::bitset` is C++ only | N/A; Boost bitsets are C++ only |
| `zu_decimal.h` C API | N/A | N/A; Boost.Multiprecision is C++ only |
| `zu_time.h` C API | Partial: C `<time.h>` | N/A; Boost time libraries are C++ only |

## Callable construction and invocation

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuDefaultAxor` | `std::identity` | `boost::hana::id` |
| `ZuLambda`<br>\+ recursive callable wrapper with cv/ref-sensitive SFINAE overloads<br>\+ documented workaround for GCC explicit-object-parameter issue 113563 | N/A; explicit-object lambda syntax supplies recursion, not this wrapper interface | `boost::hof::fix` |
| `ZuDeduce`<br>\+ extracts object, return type and argument typelist | Partial: `std::invoke_result_t`<br>\- requires supplied argument types; no full signature decomposition | Boost.CallableTraits<br>\+ broader callable-type transformations |
| `ZuInvoke`<br>\+ accepts member, bound and unbound function shapes with optional context | Partial: `std::invoke`<br>\- does not discard an extra context argument automatically | Partial: `boost::compat::invoke` |
| `ZuInvokeFnT` | N/A; construct a function-pointer type from a callable signature | Partial: Boost.CallableTraits function-type transformation |
| `ZuInvokeFn`<br>\+ concise named function-pointer conversion with signature deduction from lambda traits<br>\- conversion semantics are already supplied by captureless lambdas | N/A; core lambda conversion supplies the underlying operation | Partial: Boost.TypeTraits-selected function-pointer conversion |
| `ZuInvokeLambda`<br>\+ type-only invocation with an explicit statelessness check and argument-list traits<br>\- core construction/call already covers captureless lambdas<br>\- placeholder-this implementation relies on supported compiler behavior | N/A; core captureless-lambda construction/call lacks the explicit statelessness check | Partial: `boost::hof::static_` |

## Lambda traits and constraints

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuLambdaTraits`<br>\+ generic-lambda invocation and capture-oriented traits | Partial: `std::is_invocable`, `std::invoke_result`, type traits | Partial: Boost.CallableTraits, TypeTraits |
| `ZuArgList` | N/A; signature decomposition requires custom traits | `boost::callable_traits::args_t` (partial for generic lambdas) |
| `ZuLambdaReturn` | `std::invoke_result_t` with supplied argument types | `boost::callable_traits::return_type_t` (partial for generic lambdas) |
| `ZuIsMutableLambda` | Partial: test invocability through a const callable reference | Partial: Boost.TypeTraits / CallableTraits composition |
| `ZuIsVoidRetLambda` | `std::is_void<std::invoke_result_t<...>>` | Partial: Boost.TypeTraits + CallableTraits |
| `ZuIsStatelessLambda`<br>\+ tests function-pointer conversion for the selected signature | Partial: `std::is_convertible` to the selected function pointer<br>\- `std::is_empty` alone is not an equivalent test | Partial: `boost::is_convertible` to the selected function pointer |
| `ZuMutableLambda` | Constraint on const-callability test | Partial: Boost.EnableIf + callable traits |
| `ZuNotMutableLambda` | Negated const-callability constraint | Partial: Boost.DisableIf + callable traits |
| `ZuVoidRetLambda` | Constraint on void invocation result | Partial: Boost.EnableIf + result-type traits |
| `ZuNotVoidRetLambda` | Constraint on non-void invocation result | Partial: Boost.DisableIf + result-type traits |
| `ZuStatelessLambda` | Constraint on function-pointer convertibility | Partial: Boost.EnableIf + conversion traits |
| `ZuNotStatelessLambda` | Negated function-pointer-conversion constraint | Partial: Boost.DisableIf + conversion traits |

## Compile-time switch and unroll

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSwitch`<br>\+ generates dispatch cases from a count or sequence, passing a compile-time index<br>\+ supports result collection and a caller-supplied default value | N/A; a switch statement does not generate its own cases | `boost::mp11::mp_with_index` |
| `ZuUnroll`<br>\+ generates ordered calls from a count, sequence or type list<br>\+ shared API supports result collection and seeded reduction | N/A; pack expansion requires pack extraction/generation and call adaptation | `boost::mp11::mp_for_each`, `boost::hana::for_each` |

## Constant sequences and dispatch

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSeq` | `std::integer_sequence<unsigned, ...>` | `boost::mp11::mp_list_c<unsigned, ...>` |
| `ZuMkSeq` | `std::make_integer_sequence<unsigned, N>` | `boost::mp11::mp_iota_c` with value-type adapter |
| `ZuMin` | Partial: constexpr `std::min` over constant values | `boost::mp11::mp_min` |
| `ZuMax` | Partial: constexpr `std::max` over constant values | `boost::mp11::mp_max` |
| `ZuSeqCall` | Partial: `std::apply` with index/element adaptation | Partial: `boost::hana::unpack` |

## Constant-sequence conversions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSeqBitmap`<br>\+ extracts sequence indices and combines their bits into a constexpr uint64_t mask | N/A; a fold expression requires a separately unpacked index pack | Partial: Boost.Mp11 fold |
| `ZuBitmapSeq`<br>\+ generates an index-sequence type from the set bits of a constant mask | N/A; integer bit operations do not generate a sequence type | Partial: Boost.Mp11 filtering of bit indices |
| `ZuSeqTL` | N/A; convert an integer sequence into constant types | `boost::mp11::mp_from_sequence` |
| `ZuTLSeq` | N/A; unpack constant types into an integer sequence | Partial: Boost.Mp11 pack adapter |
| `ZuSeqString`<br>\+ extracts sequence values and builds a structural ZuString with a trailing terminator | Partial: `std::array<char, N>` with a custom sequence-to-string adapter | Partial: `boost::hana::string` |
| `ZuStringSeq` | N/A; string-to-integer-pack adapter | Partial: `boost::hana::unpack` on a Hana string |
| `ZuSeqArray`<br>\+ extracts sequence values into a structural array of the selected element type | Partial: `std::array` with a custom sequence-conversion adapter | Partial: `boost::hana::unpack` into an array |
| `ZuArraySeq` | N/A; structural-array-to-integer-pack adapter | Partial: Boost.Hana sequence conversion |

## Type-list construction, access and slicing

Sources: [ZuTL.hh](../zu/src/ZuTL.hh), [ZuSeq.hh](../zu/src/ZuSeq.hh),
[ZuString.hh](../zu/src/ZuString.hh).
Boost counterparts: [Mp11 reference](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html).
Composed operations below require a small adapter where the list/value representation differs.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTypeList`<br>\+ type-only list with nested operations | Partial: `std::tuple` as a type carrier | `boost::mp11::mp_list` |
| `ZuTypeRepeat`<br>\+ generates an N-element type list from one type, including an empty result | N/A; parameter-pack syntax does not synthesize repeated types by itself | `boost::mp11::mp_repeat_c` on a singleton list |
| `ZuType` | `std::tuple_element_t` on a tuple | `boost::mp11::mp_at_c` |
| `ZuTypeIndex` | N/A; compile-time type search | `boost::mp11::mp_find` |
| `ZuTypeIn`<br>\+ membership test accepts a packed list or raw type arguments and handles empty input | Composition: `std::is_same` with custom list unpacking and reduction | `boost::mp11::mp_contains` |
| `ZuTypeHead` | N/A; prefix index-sequence adapter | `boost::mp11::mp_take_c` |
| `ZuTypeTail` | N/A; suffix index-sequence adapter | `boost::mp11::mp_drop_c` |
| `ZuTypeSlice`<br>\+ stride/offset selection | N/A; index-sequence selection | Partial: `boost::mp11::mp_transform` over selected indices |
| `ZuTypeApply`<br>\+ applies an existing packed type list or raw type arguments to a target template | N/A; pack expansion does not unpack a type-list object/type by itself | `boost::mp11::mp_rename` / `mp_apply` |

## Type-list algorithms

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTypeRev` | N/A; reversed index-sequence adapter | `boost::mp11::mp_reverse` |
| `ZuTypeMap`<br>\+ transforms a packed type list or raw type arguments and rebuilds the resulting list | N/A; requires a custom list-unpacking and transformation adapter | `boost::mp11::mp_transform` |
| `ZuTypeGrep` | N/A; compile-time type filtering | `boost::mp11::mp_copy_if` |
| `ZuTypeReduce`<br>\+ recursive pairwise type reduction with empty/singleton cases and packed-list input | N/A; ordinary fold expressions do not directly reduce type metafunctions | Partial: `boost::mp11::mp_fold` |
| `ZuTypeMerge` | N/A; compile-time merge | Partial: `boost::mpl::merge` |
| `ZuTypeSort` | N/A; compile-time type sorting | `boost::mp11::mp_sort` with comparator adapter |
| `ZuTypeConcat` | Partial: result type of `std::tuple_cat` | `boost::mp11::mp_append` |
| `ZuTypeUnique` | N/A; compile-time type deduplication | `boost::mp11::mp_unique` |
| `ZuTLConverts` | Composition: zipped `std::is_convertible` tests | Composition: `boost::mp11::mp_transform` + `mp_all` |
| `ZuTLConstructs` | Composition: zipped `std::is_constructible` tests | Composition: `boost::mp11::mp_transform` + `mp_all` |

## Compile-time strings

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuString`<br>\+ structural string for non-type template arguments | Partial: `std::array<char, N>`<br>\- no dedicated structural string class | Partial: `boost::hana::string`<br>\- different template representation |
| `ZuStringT` | Partial: `std::integral_constant` of a structural string | Partial: `boost::hana::string` as a type-level string |
| `ZuStringTL` | N/A; type list of structural-string constants | Partial: Boost.Mp11 list of Hana strings |

## Ownership and scope cleanup

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuObject`<br>\+ embedded non-atomic count<br>\- participating base class required | Partial: `std::enable_shared_from_this` + shared ownership<br>\+ supports weak observers | `boost::intrusive_ref_counter<T, boost::thread_unsafe_counter>` |
| `ZuObjectTraits`<br>\+ detects Z intrusive-object participation | N/A; custom type traits | Partial: detection of intrusive pointer hooks |
| `ZuRef`<br>\+ intrusive ownership without separate control block<br>\- non-atomic; no weak observers | `std::shared_ptr`<br>\+ arbitrary pointees and weak observers | `boost::intrusive_ptr` with non-atomic hooks |
| `ZuPolymorph` | Partial: polymorphic base with `std::shared_ptr` | Partial: polymorphic base with Boost intrusive hooks |
| `ZuPtr`<br>\- no configurable deleter | `std::unique_ptr`<br>\+ custom deleters and array specialization | `boost::movelib::unique_ptr` |
| `ZuGuard`<br>\+ cancellable lambda cleanup | N/A in C++23; `std::experimental::scope_exit` (Library Fundamentals TS) | `boost::scope::scope_exit`<br>\+ related success/failure guards |

## Comparison, hashing and null sentinels

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuCmp`<br>\+ comparison, null sentinel and numeric bounds in one customization | Partial: `std::compare_three_way`, `std::numeric_limits`, `std::optional` | Partial: Boost.Operators, NumericConversion, Optional |
| `ZuCmp0`<br>\+ zero-sentinel comparison policy | Partial: comparison functions + zero test | Partial: Boost.Operators + custom sentinel policy |
| `ZuCmp_1`<br>\+ negative-value null policy | N/A; custom negative-sentinel comparison policy | N/A; custom sentinel policy |
| `ZuCmpN`<br>\+ compile-time chosen null sentinel | N/A; custom sentinel comparison policy | N/A; custom sentinel policy |
| `ZuCompare` | Partial: `std::compare_three_way`<br>\- returns an ordering category rather than an integer | Partial: Boost comparison utilities with integer-result adapter |
| `ZuEquals` | `std::equal_to` | `boost::equal_to` (Functional) |
| `ZuNull`<br>\+ tests a type's configured sentinel | Partial: `std::optional::has_value`<br>\+ does not consume a payload value as a sentinel | Partial: `boost::optional` engagement test |
| `ZuNullRef`<br>\+ reusable const-reference sentinel accessor honoring the type-specific comparator | N/A; a static variable alone does not select the configured sentinel | N/A; application static sentinel |
| `ZuICmp` | Partial: character folding + comparison algorithms | `boost::algorithm::iequals` / `ilexicographical_compare` |
| `ZuHash`<br>\+ consistent text hashing across supported representations | `std::hash` | `boost::hash` (ContainerHash) |

## Bitmaps, bitfields and bit streams

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuBitmap` | `std::bitset` | Partial: `boost::dynamic_bitset`<br>\+ runtime size<br>\- dynamic allocation |
| `ZuBitfield`<br>\+ runtime-indexed packed fields with explicit word/mask operations and assignment proxies | N/A; native named bitfields do not supply indexed packed-array access | Partial: Boost.Endian buffers with masks |
| `ZuBitStream`<br>\+ successive non-byte-aligned symbols | N/A; custom bit packing | Partial: Boost.DynamicBitset with custom cursor |
| `Zu_clz*` (`ZuIntrin.hh`) | `std::countl_zero` | Partial: Boost.Core bit utilities |
| `Zu_ctz*` (`ZuIntrin.hh`) | `std::countr_zero` | Partial: Boost.Core bit utilities |

## Boxed scalars and formatting adapters

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuBox`<br>\+ scalar null policy with integrated scanning/formatting | Partial: scalar + `std::optional` + character conversion | Partial: Boost.Optional + Charconv / LexicalCast |
| `ZuBoxed`<br>\+ scalar-reference formatting adapter | Partial: `std::format` argument / stream insertion | Partial: Boost.Format argument |
| `ZuNBox`<br>\+ boxed scalar with null-string formatting policy | Partial: `std::optional` + custom formatter | Partial: Boost.Optional + custom formatter |
| `ZuNBoxed` | Partial: scalar-reference adapter with custom null formatting | Partial: Boost.Format argument adapter |

## Arrays, spans and string interoperability

Sources: [public headers](../zu/src/), [module guide](../zu/README.md).
Relevant counterparts: [standard containers](https://eel.is/c++draft/containers),
[Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html),
[Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html).

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSpan`<br>\+ integrated compare/hash, slicing and text searching | `std::span`<br>\+ standard ranges interoperability and static extent | `boost::span` |
| `ZuCSpan` | `std::string_view` | `boost::string_view` |
| `ZuWSpan` | `std::wstring_view` | `boost::wstring_view` |
| `ZuBSpan` | `std::span<const uint8_t>` / `std::span<const std::byte>` | `boost::span<const unsigned char>` |
| `ZuArray`<br>\+ inline capacity with logical length; structural representation | Partial: `std::array`<br>\- no logical length; `std::inplace_vector` is C++26 | `boost::container::static_vector`<br>\- different structural/NTTP contract |
| `ZuCArray` | Partial: `std::array<char, N>` with length | `boost::static_string` |
| `ZuWArray` | Partial: `std::array<wchar_t, N>` with length | `boost::static_strings::basic_static_string<N, wchar_t>` |
| `ZuMArray`<br>\+ writable transformed-element proxies | Partial: `std::views::transform`<br>\- inverse assignment needs custom proxy support | Partial: Boost.Iterator transform iterator |
| `ZuVArray`<br>\+ type-erased array interface across compiled boundaries | N/A; `std::span` only covers contiguous homogeneous storage | Partial: Boost.Range any-range |
| `ZuStdString` integration | `std::basic_string` used directly | Partial: Boost string/range interoperability |

## Tuples, unions and empty values

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTuple`<br>\+ size-ordered flat storage and positional mutation<br>\- incomplete `std::tuple` algorithm interoperability | `std::tuple`, `std::pair` | `boost::tuple`, `boost::hana::tuple` |
| `ZuUnion`<br>\+ void alternatives and explicit nonthrowing dispatch<br>\- callers must respect active-alternative access contract | `std::variant`<br>\+ checked `get`/`get_if` and standard visitation | `boost::variant2::variant`<br>\+ never-valueless value semantics |
| `ZuVoid` | `std::monostate` | `boost::blank` |

## Array algorithms, iterators, sorting and searching

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuArrayFn`<br>\+ trait-selected bulk initialization, movement and comparison | `<algorithm>`, uninitialized-memory algorithms | Partial: Boost.Container / Move algorithms |
| `ZuStringFn` | `std::char_traits`, C string functions | Partial: Boost.StringAlgo |
| `ZuIter`<br>\+ reusable random-access iterator boilerplate | Partial: iterator concepts and custom iterator implementation | `boost::iterator_facade` |
| `ZuSort`<br>\+ three-way comparison and Z bulk-element operations<br>\- custom quicksort lacks `std::sort`'s mandated worst-case bound | `std::sort` | Boost.Sort |
| `ZuSearch`<br>\+ packs found flag and insertion position into one result | `std::lower_bound` plus equality test | Boost.Range `lower_bound` plus equality test |
| `ZuSearchFound` | Partial: equality test after `std::lower_bound` | Partial: equality test after Boost.Range `lower_bound` |
| `ZuSearchPos` | Partial: iterator distance from `std::lower_bound` | Partial: iterator distance from Boost.Range `lower_bound` |
| `ZuInterSearch`<br>\+ interpolation search for suitably distributed keys<br>\- behavior depends on key distribution | Partial: `std::lower_bound`<br>\+ logarithmic comparisons independent of distribution | Partial: Boost.Range `lower_bound` |

## Text tokenization

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTokenizer::Delimited`<br>\+ borrowed tokens without allocation | `std::views::split` | `boost::tokenizer`, `boost::split_iterator` |
| `ZuTokenizer::WhiteSpace`<br>\+ simple ASCII whitespace scanning | Partial: `std::views::split` / custom delimiter predicate | `boost::tokenizer` with character separator |

## Numeric input

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zu_nscan` / numeric input (`Zu_aton.hh`)<br>\+ numeric scanning with Z-format policies | `std::from_chars` | Boost.Charconv `from_chars` |
| `Zu_atou` | `std::from_chars` for unsigned integers | Boost.Charconv `from_chars` |
| `Zu_atoi` | `std::from_chars` for signed integers | Boost.Charconv `from_chars` |
| `Zu_atof` | `std::from_chars` for floating point | Boost.Charconv `from_chars` |

## Numeric output and buffer sizing

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zu_nprint` / numeric output (`Zu_ntoa.hh`)<br>\+ compile-time numeric-format policies | `std::to_chars` | Boost.Charconv `to_chars` |
| `Zu_utoa` | `std::to_chars` for unsigned integers | Boost.Charconv `to_chars` |
| `Zu_itoa` | `std::to_chars` for signed integers | Boost.Charconv `to_chars` |
| `Zu_ftoa`<br>\- no automatic scientific-notation fallback | `std::to_chars` for floating point<br>\+ general, fixed, scientific and hexadecimal modes | Boost.Charconv `to_chars` |
| `Zu_ulen`<br>\+ constexpr unsigned-output buffer sizing | Partial: `std::numeric_limits` and digit-count calculation | Partial: Boost.Integer / Charconv sizing composition |
| `Zu_ilen`<br>\+ constexpr signed-output buffer sizing | Partial: `std::numeric_limits` and digit-count calculation | Partial: Boost.Integer / Charconv sizing composition |
| `Zu_flen`<br>\+ format-aware floating-output buffer sizing | N/A; size according to selected conversion format | Partial: Boost.Charconv with caller-selected capacity |

## Formatting, streams and joining

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuFmt`<br>\+ formatting policies encoded in types | `std::format`, stream manipulators | Boost.Format / Convert |
| `ZuPrint`<br>\+ ADL print customization shared by Z streams | Partial: `std::formatter`, `operator<<` | Partial: Boost.Format streamable types |
| `ZuStream`<br>\+ direct output into caller-supplied bounded text storage | `std::ospanstream` (C++23) | Boost.Iostreams array sink |
| `ZuVStream`<br>\+ type-erased stream interface with function pointers | Partial: `std::ostream` / `std::streambuf` | Partial: Boost.Iostreams `stream` / filtering stream |
| `ZuJoin`<br>\+ streams elements directly with a delimiter | Partial: `std::views::join_with` (C++23)<br>\- range joining is not a formatting adapter | `boost::algorithm::join`<br>\- normally builds a result string |

## Time, identifiers and versions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTime`<br>\+ compact seconds/nanoseconds with null sentinel<br>\- same type represents instants and intervals | `std::chrono::duration`, `std::chrono::time_point`<br>\+ distinct clock and duration types | Boost.Chrono / DateTime |
| `ZuDateTime`<br>\+ wide Julian-calendar date range | `std::chrono` calendar types and formatting (C++20) | Boost.DateTime |
| `ZuID`<br>\+ by-value bounded name with ZuArray length, comparison, hashing and printing<br>\- fixed framework capacity | Partial: `std::string`<br>\- different storage/capacity contract | `boost::static_string` |
| `ZuSemVer`<br>\+ packed compact version value<br>\- minor ≤ 99, patch ≤ 999; no prerelease/build metadata | N/A | N/A; `BOOST_VERSION` is a library version macro, not a SemVer value type |

## Fixed-point and decimal arithmetic

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuFixed`<br>\+ mantissa plus runtime decimal scale, null/reset sentinels and scan/format conversions<br>\- constrained precision/range | N/A; integer arithmetic alone supplies none of these decimal-value policies | Partial: Boost.Multiprecision scaled integer composition |
| `ZuDecimal`<br>\+ decimal arithmetic with 18 integer and 18 fractional digits<br>\- fixed precision/range | N/A in C++23 | Partial: `boost::multiprecision::cpp_dec_float`<br>\+ configurable precision<br>\- decimal floating point has different rounding/storage semantics |
| `ZuDecimalFn` | N/A; decimal representation helpers | Partial: Boost.Multiprecision arithmetic and conversions |

## Unicode conversion and measurement

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuUTF`<br>\+ conversion into caller-owned storage | Partial: locale/codecvt facilities<br>\- legacy Unicode conversion APIs are deprecated in C++23 | Boost.Locale / Nowide |
| `ZuUTF8`<br>\+ code-point decoding/encoding and input/output length operations for caller buffers | Partial: legacy `std::codecvt_utf8`<br>\- different stateful facet interface; deprecated in C++23 | Partial: Boost.Locale UTF conversion primitives |
| `ZuUTF16`<br>\+ surrogate-aware code-point decoding/encoding with caller-buffer length checks | Partial: legacy `std::codecvt_utf16`<br>\- different stateful facet interface; deprecated in C++23 | Partial: Boost.Locale UTF conversion primitives |
| `ZuUTF32`<br>\+ bounded code-point input/output and Unicode display-width lookup | N/A; char32_t stores a code unit but supplies no width lookup or buffer operations | Partial: Boost.Locale UTF conversion primitives |
| `ZuUTFSpan`<br>\+ packs input length, output length and display width | N/A; custom encoding measurement record | Partial: Boost.Locale conversion + custom measurement record |

## Binary-text and percent encoding

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuHex`<br>\+ caller-buffer output and in-place decoding | N/A; custom digit conversion | `boost::algorithm::hex` / `unhex` |
| `ZuBase32`<br>\+ caller-buffer codec | N/A | Partial: Boost.Archive iterator composition |
| `ZuBase64`<br>\+ caller-buffer codec | N/A | Partial: Boost.Archive `base64_from_binary` / `binary_from_base64` |
| `ZuBase64URL`<br>\+ URL-safe alphabet | N/A | Partial: Base64 iterator composition with alphabet adaptation |
| `ZuPercent`<br>\+ policy-driven percent encoding, plus/space handling and in-place decoding | N/A | Boost.URL encoding/decoding<br>\+ URL component and grammar vocabulary |

## Testing

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuTest`<br>\+ TAP output and static nested test counts | N/A | Boost.Test<br>\+ broader test framework and reporting options |
| `ZuTestUtil` | N/A | Partial: Boost.Test helpers and execution utilities |

## Structure reflection and facets

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuStruct`<br>\+ field/accessor metadata, facets and composite keys in C++23<br>\+ supports members, methods and lambda-derived fields | N/A in C++23; core C++23 has no structure reflection | Boost.Describe / Hana.Struct<br>\+ Describe also describes bases and member functions |
| `ZuStructFacet`<br>\+ alternate reflected field views of one type | N/A in C++23 | Partial: separate Hana/Fusion adaptations or metadata types |

## ZuMatcher — compile-time multi-string matching

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuMatcher`<br>\+ compile-time Aho–Corasick for fixed multiple keys<br>\- large instantiations increase compile cost | Partial: `std::search`, Boyer–Moore searchers<br>\- single-pattern facilities | Partial: Boost.Algorithm searching / Boost.Spirit symbols |

# Zm

## Platform and cache alignment

Sources: [public headers](../zm/src/), [module guide](../zm/README.md).
Standard counterparts: [concurrency library](https://eel.is/c++draft/thread).

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmPlatform` primitives<br>\+ OS IDs, aligned allocation and runtime platform helpers | Partial: `std::thread::id`, aligned `operator new` | Partial: Boost.Thread, Align, WinAPI |
| `ZmCacheLineSize` / `ZmCacheAlign`<br>\+ shared target-specific cache-separation policy and matching alignment annotation | Partial: `std::hardware_destructive_interference_size`<br>\- does not supply Z's selected 128-byte ARM64 / 64-byte other-target policy | Partial: Boost.Align / alignment macros |

## Atomic operations and locks

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmAtomic` | `std::atomic`, atomic fences | Boost.Atomic |
| `ZmNoLock`<br>\+ compile-time no-op locking policy | N/A; custom null mutex | Partial: Boost.Interprocess `null_mutex` |
| `ZmLockTraits` | Partial: Lockable concepts/requirements | Partial: Boost.Thread lockable traits |
| `ZmSpinLock`<br>\+ wraps Concurrency Kit FAS spinlock | Partial: `std::atomic_flag` spin loop | Partial: Boost.Atomic spinlock composition |
| `ZmPLock`<br>\+ platform-tuned primitive mutex<br>\- cannot be used with `ZmCondition` | Partial: `std::mutex` | Partial: `boost::mutex` |
| `ZmLock` | `std::recursive_mutex` | `boost::recursive_mutex` |
| `ZmRWLock` | `std::shared_mutex` | `boost::shared_mutex` |
| `ZmGuard` / `ZmReadGuard` | `std::unique_lock`, `std::shared_lock` | `boost::unique_lock`, `boost::shared_lock` |

## Conditions, semaphores and blocking adapters

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmCondition` | `std::condition_variable_any` | `boost::condition_variable_any` |
| `ZmSemaphore` | `std::counting_semaphore` (C++20) | Boost.Interprocess `interprocess_semaphore` (partial sharing model) |
| `ZmBlock`<br>\+ continuation-to-blocking bridge reusing a thread-local semaphore | Partial: `std::promise` / `std::future`<br>\- shared-state machinery differs | Partial: Boost.Thread futures / Asio `use_future` |

## Random number generation

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmRandom`<br>\+ framework Mersenne Twister wrapper<br>\- not cryptographic | `std::mt19937`<br>\+ standard distribution family | Boost.Random Mersenne Twister and distributions |

## Clocks, timing statistics and retry policies

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmTime` | `std::chrono` clocks | Boost.Chrono |
| `ZmTimeInterval`<br>\+ locked min/max/total/mean/count aggregation | Partial: chrono values + custom aggregation | Boost.Accumulators with timing values |
| `ZmBackoff`<br>\+ configurable exponential backoff with jitter | Partial: chrono + `<random>` composition | Partial: Boost.Random + Chrono composition |
| `ZmTimeout`<br>\+ timeout state with exponential backoff | Partial: chrono deadlines + custom retry policy | Partial: Boost.Asio timer + retry policy |

## Stack and scratch allocation

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmStackAvail`<br>\+ queries remaining stack for scratch policies | N/A | N/A |
| `ZmAlloc`<br>\+ checks remaining stack space and selects aligned stack storage or RAII heap fallback<br>\- macro evaluates size more than once | N/A; automatic storage does not provide runtime stack-capacity checks or heap fallback | Partial: Boost.Container `small_vector`<br>\- fixed inline capacity rather than remaining-stack check |
| `ZmLocal`<br>\+ selects stack/heap storage from available stack space and runs construction/destruction | Partial: `std::unique_ptr`<br>\- does not automatically select stack storage | Partial: scoped/unique pointer with explicit storage selection |
| `ZmScratch`<br>\+ runtime-sized scratch array with stack-space checks, heap fallback and element cleanup | Partial: `std::vector`<br>\- no automatic stack-backed allocation path | Partial: `boost::container::small_vector` |

## Heaps and allocator integration

Sources: [public headers](../zm/src/), [module guide](../zm/README.md).
Relevant counterparts: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html),
[Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html).

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmHeap` / `ZmNHeap`<br>\+ named fixed-block arenas, partitions and telemetry<br>\+ CPU/NUMA-aware reserves<br>\- reserves can fall back to ordinary allocation | Partial: `std::allocator`, `std::pmr::synchronized_pool_resource`<br>\+ standard allocator/resource interfaces<br>\- no named tuning/telemetry layer | Boost.Pool<br>\+ reusable pool allocator vocabulary<br>\- no equivalent integrated naming/NUMA/telemetry layer |
| `ZmVHeap`<br>\+ named variable-size recycling classes and instrumentation | `std::pmr` pool resources | Boost.Pool / Container PMR |
| `ZmAllocator`<br>\+ connects Z heaps to standard allocator-aware containers | `std::allocator`, `std::pmr::polymorphic_allocator` | `boost::pool_allocator` / Container PMR allocator |
| `ZmHeapMgr`<br>\+ global named arena configuration and statistics | N/A; custom PMR resource registry | Partial: pool management with application registry |

## Scratch-buffer codecs

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmCodec` (`ZmBase64`, `ZmBase32`, `ZmHex`)<br>\+ scratch-buffer codecs delivered through callbacks | N/A | Partial: Boost encoding iterators + scratch storage |

## Intrusive ownership and callbacks

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmObject`<br>\+ embedded atomic reference count | Partial: `std::enable_shared_from_this` | `boost::intrusive_ref_counter<T, boost::thread_safe_counter>` |
| `ZmRef`<br>\+ intrusive shared ownership<br>\- no weak observers | `std::shared_ptr`<br>\+ arbitrary pointees and `std::weak_ptr` | `boost::intrusive_ptr` with atomic hooks |
| `ZmPolymorph` | Partial: shared ownership of a virtual-destructor base | Partial: intrusive ownership of a virtual-destructor base |
| `ZmContext`<br>\+ compact raw-or-owning erased context<br>\- pointer-tag representation assumes supported architectures | Partial: `std::variant<void*, std::shared_ptr<Base>>` | Partial: `boost::variant2::variant` + `boost::intrusive_ptr` |
| `ZmFn`<br>\+ optimized function/context representation<br>\- larger captures can allocate | `std::function`, `std::move_only_function` (C++23) | `boost::function` |

## Thread-local state and cleanup

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmSpecific`<br>\+ explicit scope, cleanup levels and enumeration of all thread instances<br>\+ consolidates per-type TLS across Windows modules | N/A; thread_local does not provide this registry and cleanup policy | `boost::thread_specific_ptr`<br>\- no equivalent global instance-enumeration contract |
| `ZmSingleton`<br>\+ configurable lazy construction, optional final() hook and cleanup-level registration | Partial: `std::call_once`<br>\- no singleton ownership or cleanup sequencing | Partial: Boost.Serialization singleton |
| `ZmCleanup` / `ZmGlobal`<br>\+ ordered cleanup levels and a shared registry for singleton/TLS teardown | N/A; static destruction order alone does not implement this registry/policy | Partial: Boost singleton/TLS primitives with custom coordination |

## Assertions and diagnostic utilities

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmAssert` / `ZmAssert_`<br>\+ debug, always-on and release-fallback forms | `assert`, custom always-on check | Boost.Assert<br>\+ configurable assertion handlers |
| `ZmObjectDebug`<br>\+ intrusive object lifetime diagnostics | N/A | Partial: Boost.SmartPtr debugging hooks |
| `ZmDemangle`<br>\+ binutils demangling for difficult compiler names<br>\- additional binutils dependency | N/A; `type_info::name()` is implementation-defined | `boost::core::demangle`, Boost.TypeIndex |
| `ZmBackTrace` | `std::stacktrace` (C++23) | Boost.Stacktrace |
| `ZmBackTracer`<br>\+ keeps a bounded ring of captured traces | Partial: `std::stacktrace` + bounded storage | Partial: Boost.Stacktrace + CircularBuffer |
| `ZmTrap`<br>\+ last-ditch diagnostics and signal trapping | Partial: `std::signal`, `std::set_terminate` | Partial: Boost.Stacktrace signal-handler integration |

## Sequential containers and nodes

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmNode` / `ZmNodeFn`<br>\+ explicit intrusive node ownership policies | Partial: container node handles<br>\- no general intrusive-node framework | Boost.Intrusive hooks and value traits |
| `ZmList`<br>\+ intrusive ownership, heap and locking policies | `std::list`<br>\+ standard iterators and algorithms | `boost::intrusive::list` |
| `ZmStack`<br>\+ deletion inside stack for queued cancellation<br>\- requires sentinel-null value semantics | `std::stack` / `std::vector` composition | `boost::container::vector` with stack operations |
| `ZmQueue`<br>\+ FIFO/LIFO operations without shifting remaining items<br>\- sentinel-null value contract | `std::deque`, `std::queue` | Boost.CircularBuffer / Container deque |

## Ordered and hash indices

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmRBTree`<br>\+ intrusive nodes and configurable locking/ownership | `std::map`, `std::set`, multi variants | `boost::intrusive::rbtree` / set |
| `ZmLHash`<br>\+ by-value linear probing with Z policies | `std::unordered_map` / `std::unordered_set`<br>\- standard node-oriented contracts differ | `boost::unordered_flat_map` / `unordered_flat_set` |
| `ZmHash`<br>\+ intrusive chaining, lock striping and named tuning | `std::unordered_map` / `std::unordered_set`<br>\- caller supplies synchronization | `boost::intrusive::unordered_set`; partial: Boost.Unordered concurrent containers |
| `ZmHashMgr`<br>\+ named sizing, striping and telemetry | N/A; per-container load-factor/rehash controls only | Partial: Boost.Unordered controls with custom registry |
| `ZmPolyHash`<br>\+ reflected multiple intrusive hash indices in one allocation | N/A; multiple containers with manual index maintenance | Boost.MultiIndex<br>\+ also supports ordered, sequenced and other index kinds |

## Caches and eviction

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmCacheStats` | N/A; application counters | Partial: Boost.Accumulators or custom counters |
| `ZmCache`<br>\+ integrated hash lookup and LRU eviction | Partial: `std::list` + `std::unordered_map` | Partial: Boost.MultiIndex hashed + sequenced indices |
| `ZmPolyCache`<br>\+ reflected multiple keys with LRU eviction | N/A; custom multi-index cache | Partial: Boost.MultiIndex + eviction policy |

## Sequence reassembly

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmPQueue`, `ZmPQRx`, `ZmPQTx`<br>\+ deterministic skip-list sequence reassembly; gaps and overlaps | Partial: `std::map` plus interval/reassembly logic<br>\- `std::priority_queue` does not implement this contract | Partial: Boost.ICL + ordered index<br>\- Boost.Heap is an ordinary priority heap, not sequence reassembly |

## Ring storage and callable dispatch

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmRingMirror`<br>\+ double-mapped storage simplifies wraparound records | N/A | Partial: Boost.Interprocess mapping primitives<br>\- mirror arrangement is application work |
| `ZmRing`<br>\+ fixed/variable records, producer policies and broadcast readers<br>\- multi-reader mode broadcasts rather than distributes work | N/A; custom bounded queue | Partial: Boost.Lockfree queues<br>\- different record/broadcast contracts |
| `ZmRingFn`<br>\+ captured callable payloads stored in variable-record rings | Partial: callable wrapper queued in a container | Partial: Boost.Function + Lockfree queue composition |

## Topology and threads

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmBitmap`<br>\+ hwloc CPU/resource bitmap operations | Partial: `std::vector<bool>`<br>\- no CPU-set semantics | `boost::dynamic_bitset`<br>\- no topology integration |
| `ZmTopology`<br>\+ hwloc CPU/NUMA discovery and binding | Partial: `std::thread::hardware_concurrency`<br>\- only a concurrency hint | N/A; platform/hwloc integration required |
| `ZmThread` / `ZmThreadParams`<br>\+ named affinity, stack, priority and partition configuration | `std::thread`, `std::jthread`<br>\+ `jthread` joins automatically and supplies stop tokens<br>\- native tuning needs platform code | `boost::thread` / thread attributes |

## Scheduling and engine lifecycle

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZmScheduler` / `ZmSchedParams`<br>\+ named destinations, dedicated threads, timers and ring dispatch<br>\+ affinity/partition/queue configuration | N/A in C++23; C++26 `std::execution` is a partial asynchronous-execution counterpart | Boost.Asio `io_context`, thread pool, timers and strands<br>\+ executor/completion-token ecosystem<br>\- matching Z destination/affinity policies needs composition |
| `ZmEngine`<br>\+ reusable asynchronous start/stop lifecycle and pending operations | N/A | Partial: Boost.Statechart / MSM plus Asio |

## Runtime telemetry

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtcHeap`, `ZtcHash`, `ZtcQueue`, `ZtcThread`, `ZtcTypes` (in `zm`)<br>\+ reflected runtime telemetry vocabulary | N/A | Partial: application-defined Boost.Describe / Serialization records |

# Zt

## Platform and environment

Sources: [public headers](../zt/src/), [module guide](../zt/README.md).
Container comparisons use [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html).

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtPlatform` environment helpers | Partial: `std::getenv`<br>\- no standard environment setter | Boost.Process environment facilities |

## Boolean and enum vocabulary

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtScanBool`<br>\+ accepts case-insensitive yes/no, y/n, true/false and 1/0 | Partial: `std::boolalpha` input | Partial: Boost.Convert / LexicalCast |
| `ZtBytesFmt`<br>\+ shared binary-text encoding vocabulary | N/A | N/A; codec-specific options |
| `ZtcRAGMap` (in `zt`)<br>\+ common telemetry vocabulary with generated name lookup, conversion and iteration | N/A; an enum declaration alone does not generate the name map | Partial: enum + Boost.Describe |
| `ZtEnum` macro family<br>\+ integer enum vocabulary with generated name/ordinal matching and iteration | N/A; native enums have no generated runtime name mapping in C++23 | Boost.Describe enum descriptors<br>\+ metadata can support other generic algorithms |

## Text formatting

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtQuote`<br>\+ quoting and binary-text formatting adapters | Partial: `std::quoted` | Partial: Boost.IO quoted manipulators |
| `ZtHexDump` | N/A; composed formatting loop | Partial: Boost.Log `dump` manipulator |
| `ZtFmt`<br>\+ reflected-field printing/scanning policies | Partial: `std::format`, `std::from_chars` | Partial: Boost.Format / Convert |

## Character case and encoding conversion

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtCase` | Partial: `<cctype>`, `<cwctype>`, locale facets | Boost.StringAlgo / Locale case conversion |
| `ZtIconv`<br>\+ iconv conversions into adapted output buffers | Partial: legacy locale conversion facilities | Boost.Locale encoding conversion |

## Owning arrays, strings and scratch buffers

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtArray`<br>\+ owning or shadowing storage and direct capacity control<br>\- ownership mode and initialization require care | `std::vector`<br>\+ uniform owning semantics and standard iterators | `boost::container::vector` |
| `ZtString` / `ZtWString`<br>\+ inline storage, named heaps and stream-style appending | `std::string`, `std::wstring`<br>\+ standard string/range integration | `boost::container::basic_string` |
| `ZtBuiltin`<br>\+ adds an initial inline buffer to array storage | N/A; `std::inplace_vector` (C++26) cannot spill to heap | `boost::container::small_vector` |
| `ZtScratch`<br>\+ ZtArray/ZtString operations over runtime stack backing with heap fallback and cleanup | Partial: `std::vector`<br>\- no automatic stack-backed allocation path | Partial: `boost::container::small_vector`<br>\- inline capacity is compile-time fixed |

## Bitmaps and sliding windows

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtBitmap` | Partial: `std::vector<bool>` | `boost::dynamic_bitset` |
| `ZtWindow`<br>\+ indexed sliding window with assignment-driven extension | Partial: `std::deque` with sequence-index bookkeeping | Partial: Boost.CircularBuffer |
| `ZtBitWindow`<br>\+ sliding packed values in selected bit widths | N/A; custom packed circular storage | Partial: Boost.DynamicBitset + CircularBuffer concepts |

## Timezone conversion

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zt::tzOffset` (`ZtTimeZone.hh`)<br>\- explicit zones temporarily change `TZ` under a global lock | `std::chrono::time_zone`, `zoned_time` (C++20)<br>\+ independent zone objects and timezone database | Boost.DateTime timezone facilities |

## Demangling

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtDemangle`<br>\+ rewrites Z structural-string/template names for readability | N/A | Partial: Boost.Core demangle + custom postprocessing |

## Regular expressions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtRegex` / `ZtREGEX`<br>\+ PCRE syntax, captures and substitutions<br>\- external PCRE dependency | `std::regex`<br>\- different syntax/feature contract | Boost.Regex |

# Ze

## Native errors

Sources: [ZePlatform.hh](../ze/src/ZePlatform.hh), [module guide](../ze/README.md).
Ze constructs diagnostics; the asynchronous sink runtime is `ZiLog` below.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZeOK` | Default-constructed success `std::error_code` | Default-constructed success Boost.System `error_code` |
| `ZeError`<br>\+ native OS-error wrapper and message formatting | `std::error_code`<br>\+ extensible error categories and conditions | Boost.System `error_code` |
| `ZeLastError`<br>\+ captures native last-error state at the call site | Partial: capture `errno` / native error into `std::error_code` | Partial: native error capture + Boost.System |
| `ZeLastSockError`<br>\+ distinguishes Windows socket errors from ordinary OS errors | N/A; native call + `std::error_code` | Partial: Boost.Asio socket operation error reporting |

## Diagnostic metadata and events

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ze::Debug` / `Info` / `Warning` / `Error` / `Fatal`<br>\+ shared severity vocabulary with generated name lookup and event/log integration | N/A; a plain enum alone does not provide the generated mapping or integration | Boost.Log `trivial::severity_level` |
| `ZeEventInfo`<br>\+ timestamp, source, severity and component identity | Partial: `std::source_location` + custom event record | Boost.Log attributes / record metadata |
| `ZeEvent` / `ZeAnyEvent` / `ZeMsgFn`<br>\+ structured events with deferred or type-erased formatting | N/A; custom event type and callable | Partial: Boost.Log record and message attributes |
| `ZeEVENT` / `ZeMkEvent`<br>\+ call-site metadata or explicit metadata construction | Partial: `std::source_location::current()` + custom factory | Partial: Boost.Log source macros and attributes |

## Exceptions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZeException`<br>\+ same diagnostic vocabulary as logging events<br>\- not the `std::exception::what()` interface | `std::exception`, `std::system_error`<br>\+ standard catch and message interface | Boost.Exception<br>\+ extensible attached diagnostic information |
| `ZeEXCEPT` / `ZeMkException` | Partial: construct exception with `std::source_location` metadata | Partial: `BOOST_THROW_EXCEPTION` / `boost::enable_error_info`<br>\- throw macro throws; `ZeEXCEPT` only constructs |

# Zf

## Configuration trees and typed loading

Sources: [public headers](../zf/src/), [module guide](../zf/README.md).
Closest reflection counterparts: [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html),
[Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html).
JSON counterpart: [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html).
Format parsers assembled with Spirit are **partial** equivalents, not supplied codecs.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfTree`<br>\+ owned typed configuration nodes and diagnostic paths | Partial: recursive `std::variant` / containers | Partial: Boost.PropertyTree<br>\- scalar typing and structural contracts differ |
| `ZfTreeLoad`<br>\+ shared construction/load/update and validation policies | N/A | Partial: Boost.Describe + custom tree conversion |

## Structure reflection and serialization metadata

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfStruct`<br>\+ typed/runtime field metadata, facets and serialization properties in C++23<br>\+ constructor/load/update/key policies shared across formats<br>\- substantial template/macro machinery | N/A in C++23; core C++23 has no matching reflection/serialization framework | Partial: Boost.Hana / Describe + Serialization<br>\+ Hana supplies a broader heterogeneous metaprogramming library<br>\- matching Z's format/facet/update policies requires additional code |

## Text and web formats

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfCSV`<br>\+ streaming rows with reflected header/column binding | N/A | Partial: Boost.Tokenizer or Spirit<br>\- row construction, quoting and field binding require composition |
| `ZfURI`<br>\+ typed path/query/form binding and direct path loading | N/A | Boost.URL<br>\+ URI parsing, components and normalization<br>\- no matching reflected field/facet loader |
| `ZfJSON`<br>\+ reflected load/save, fixed-field matching and in-place decoding<br>\- borrowed text requires mutable input and retained backing | N/A | Boost.JSON<br>\+ general JSON values, streaming and type conversion<br>\- different metadata/update contract |

## Configuration formats

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfCf`<br>\+ native configuration syntax, substitutions and typed updates | N/A | Partial: Boost.ProgramOptions / PropertyTree<br>\- different syntax and binding model |
| `ZfTOML`<br>\+ owned typed tree, field binding, formatting and parser limits | N/A | N/A; Boost.PropertyTree has no built-in TOML parser |
| `ZfYAML`<br>\+ owned typed tree, field binding and bounded parsing<br>\- restricted single-document subset; rejects tags and cyclic/shared graphs | N/A | N/A; Boost.PropertyTree has no built-in YAML parser |

## Command-line binding

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfCLI`<br>\+ metadata-bound options/positionals, command text and argv output | N/A | Boost.ProgramOptions<br>\+ established option descriptions, parsing and notification<br>\- reflected facets and reverse serialization need custom work |

## Binary wire formats

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfCBOR`<br>\+ reflected binary object/array mapping | N/A | Partial: Boost.JSON CBOR example<br>\- example-level codec, not matching reflected API |
| `ZfASN1`<br>\+ schema properties bind C++ fields to tags and wire encodings | N/A | N/A; Spirit-based ASN.1 implementation would be custom work |
| `ZfDBUS`<br>\+ reflected signatures, alignment and message-body serialization | N/A | N/A; no dedicated Boost D-Bus structural codec |

# Zi

## Native paths and scatter/gather descriptors

Sources: [public headers](../zi/src/), [module guide](../zi/README.md).
Networking counterparts are in [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html).
The STL has no C++23 socket/event-loop API.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiPlatform` / `Zi::Path` and native I/O types | Partial: `std::filesystem::path`<br>\- no standard socket/native-handle abstraction | Partial: Boost.Filesystem, Asio, WinAPI |
| `ZiVec` / `ZiVec_ptr` / `ZiVec_len`<br>\+ portable native scatter/gather descriptors | Partial: `std::span`<br>\- no native scatter/gather syscall adapter | Boost.Asio mutable/const buffer sequences |

## Process and module utilities

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiProgram`<br>\+ current executable/process information | N/A | Partial: Boost.DLL program location + Process |
| `ZiModule` | N/A; OS loader calls | Boost.DLL |
| `ZiPIDFile`<br>\+ PID file ownership and cleanup | N/A | Partial: Boost.Interprocess file lock + custom PID file |
| `ZiDaemon` | N/A | Partial: Boost.Process plus platform daemon setup |

## Directories and path completion

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiDir` | `std::filesystem::directory_iterator` | Boost.Filesystem directory iterator |
| `ZiGlob`<br>\+ directory/path completion for interactive CLIs<br>\- not a full glob-expression engine | Partial: `std::filesystem` iteration + prefix matching | Partial: Boost.Filesystem + StringAlgo |

## File operations and structured output

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiFile`<br>\+ direct native unbuffered file operations | Partial: `std::fstream`, `std::filesystem`<br>\- stream buffering and native-operation APIs differ | Partial: Boost.Filesystem / Asio file APIs / Iostreams file descriptors |
| `ZiFileTxStream`<br>\+ file-backed buffered Z transmit stream | `std::ofstream` | Boost.Iostreams file sink / stream |
| `ZiCSV`<br>\+ file/stream integration for reflected CSV records | N/A; fstreams + custom CSV binding | Partial: Boost.Iostreams + Tokenizer/Spirit |

## Addresses and name resolution

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiIP` | N/A | Boost.Asio `ip::address` / endpoint types |
| `ZiResolver`<br>\+ c-ares-backed asynchronous DNS with controlled runtime lifecycle | N/A | Boost.Asio `ip::tcp::resolver` / `ip::udp::resolver`<br>\- DNS configuration/lifecycle surface differs |

## I/O buffers, allocation and contexts

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiIOBuf`<br>\+ reference-owned packet backing and allocator integration | Partial: `std::span` + owning storage<br>\- span alone carries no ownership | Partial: Asio buffers + application-owned storage<br>\- buffer descriptors alone carry no ownership |
| `Zi::IOBufAllocator`, builtin/plugin allocators and allocation roles<br>\+ explicit App/Rx/Tx storage provenance and transport integration | Partial: `std::pmr::memory_resource`<br>\- no packet-backing/provenance contract | Partial: Asio associated allocators + custom packet pool |
| `ZiRxContext` / `ZiTxContext` (`ZiIOContext.hh`)<br>\+ buffer, progress and continuation state for transport I/O | N/A | Partial: Asio composed-operation state / completion handlers |

## Receive and transmit streams

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiRxStream`<br>\+ consumable view over queued receive buffers | Partial: `std::istream` over a custom stream buffer | Partial: Asio DynamicBuffer / Beast `multi_buffer` |
| `ZiTxStream`<br>\+ streamed output into packet/frame-sized buffer sequences | Partial: `std::ostream` over custom segmented storage | Partial: Asio buffer sequences / Beast buffers |

## Message framing and transmission

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiRx`<br>\+ CRTP header/body framing and fragmented-message accumulation | N/A | Partial: Asio `async_read` / `async_read_until` + framing state |
| `ZiTx`<br>\+ CRTP sender and queued completion handling | N/A | Partial: Asio `async_write` + queued write state |

## Event loops and connection multiplexing

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiEventLoop`<br>\+ external socket/handle readiness integration | N/A | Boost.Asio descriptors / handles and async waits |
| `ZiConnection` / `ZiCxnInfo` / `ZiCxnOptions`<br>\+ transport callbacks, connection state and explicit I/O allocation roles | N/A | Partial: Asio socket, endpoint and socket options |
| `ZiMxPlugin`<br>\+ transport-plugin lifecycle integrated with the multiplexer | N/A | Partial: custom Asio service/executor/stream implementation |
| `ZiMultiplex` / `ZiMxParams`<br>\+ shard-directed I/O with scheduler and allocator integration | N/A | Boost.Asio sockets, `io_context`, executors and strands<br>\+ completion tokens and coroutine integration<br>\- Z shard/buffer ownership contracts need application composition |

## Interprocess broadcast rings

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiRing`<br>\+ named cross-process variable-record broadcast ring<br>\- processes must agree on layout and payload protocol | N/A | Partial: Boost.Interprocess shared memory / message queue<br>\- message queue consumption is not broadcast |

## Logging and assertions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiAssert`<br>\+ release fallback with logging/backtrace | Partial: `assert` + explicit release handling | Partial: Boost.Assert + Log + Stacktrace |
| `ZiLog` / `ZiLOG` / `ZiLogEvent`<br>\+ asynchronous Ze-event fanout to multiple sinks | Partial: `std::clog`, `std::osyncstream`<br>\- no severity, sink routing or async log runtime | Boost.Log<br>\+ configurable attributes, filters and sinks |

## Resource tuning

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiHeapTune`<br>\+ file-based named heap tuning and measurements | N/A | N/A; custom configuration around Boost.Pool |
| `ZiHashTune`<br>\+ file-based hash configuration and measurements | N/A; per-container controls only | N/A; custom configuration around Boost.Unordered |

## I/O telemetry

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZtcMx`, `ZtcLink`, `ZtcPool`, `ZtcHub` (in `zi`)<br>\+ reflected I/O telemetry records | N/A | Partial: application-defined Boost.Describe / Serialization records |

# Zv

## Thread, stack and ring parameter binding

Sources: [public headers](../zv/src/), [module guide](../zv/README.md).
PropertyTree and ProgramOptions comparisons refer to their configuration
storage/binding roles, not syntax-compatible parsers. Boost's
[library index](https://www.boost.org/doc/libs/latest/libs/libraries.htm)
lists those facilities and the other Boost components referenced above.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZvThreadParams`<br>\+ validated tree-to-thread settings with default preservation | N/A | Partial: Boost.ProgramOptions + Thread attributes |
| `ZvStackParams`<br>\+ binds stack sizing/configuration while retaining defaults | N/A | Partial: Boost.ProgramOptions + container configuration |
| `ZvRingParams`<br>\+ binds configuration to shared-memory ring settings | N/A | Partial: Boost.ProgramOptions + Interprocess setup |

## Connection and multiplexer parameter binding

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZvCxnOptions`<br>\+ connection flags, TTL and multicast binding | N/A | Partial: Boost.ProgramOptions + Asio socket options |
| `ZvMxParams`<br>\+ binds scheduler threads, destinations and multiplexer settings | N/A | Partial: Boost.ProgramOptions + custom Asio runtime settings |

## Configuration file loading and saving

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZvCf`<br>\+ native configuration files with includes, environment expansion and typed binding<br>\- save/update/delete outputs are not transactional in-place file edits | N/A; fstreams + custom parser/binder | Partial: Boost.ProgramOptions / PropertyTree + Filesystem<br>\- different syntax; includes/expansion require custom policy |
| `ZvTOML`<br>\+ file I/O for reflected TOML and its owned tree | N/A | N/A; no built-in Boost TOML loader |
| `ZvYAML`<br>\+ file I/O for Zf's reflected YAML subset | N/A | N/A; no built-in Boost YAML loader |

# Zpb

## Protobuf wire format and typed messages

[Public API](../zpb/src/Zpb.hh) · [Module guide](../zpb/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zpb` / `PB` facet<br>\+ protobuf wire mapping from ZfStruct metadata without canonical generated C++ headers<br>\- unknown fields are skipped rather than retained | N/A | Partial: Boost.Serialization<br>\- its archives are not protobuf wire format |
| `Zpb::Builder`, `Zpb::IOBuilder`<br>\+ bounded output into caller storage or pooled I/O buffers | N/A | Partial: Boost.Serialization archive over an Iostreams device<br>\- custom protobuf encoding still required |
| `Zpb::verify`, `handler`, `load`<br>\+ separate wire verification, borrowed access and typed construction<br>\- borrowed results require retained backing | N/A | N/A; no dedicated Boost protobuf verifier/view API |

## Schema reflection and compiler

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zpb::Reflect` (`ZpbReflect.hh`)<br>\+ protobuf schema/descriptor representation integrated with Z metadata | N/A | Partial: Boost.Describe / Hana<br>\- no protobuf descriptor model |
| `zprotoc`<br>\+ compiles supported protobuf declarations into Z metadata<br>\- supported declaration profile is narrower than the complete protobuf ecosystem | N/A | N/A |

# Zfb

## FlatBuffers construction and access

[Public API](../zfb/src/Zfb.hh) · [Module guide](../zfb/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zfb` / `FB` facet<br>\+ canonical FlatBuffers mapping from reflected C++ types without canonical generated headers | N/A | Partial: Boost.Serialization<br>\- archive formats do not supply FlatBuffers layout |
| `Zfb::Builder`, `Zfb::IOBuilder`<br>\+ downward arena construction and direct pooled I/O output | N/A | Partial: Boost.Container storage + custom builder |
| `Zfb::view`, `handler`, `load`, `verify`<br>\+ borrowed table/vector access, typed construction and structural verification<br>\- views depend on wire-buffer lifetime | N/A | N/A; no dedicated Boost FlatBuffers access/verifier API |

## Schema reflection and compiler

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zfb::Reflect` (`ZfbReflect.hh`)<br>\+ emits and consumes BFBS/FBS schema metadata from reflected types | N/A | Partial: Boost.Describe / Hana<br>\- no FlatBuffers schema emission |
| `zflatc`<br>\+ FlatBuffers schema tooling for Z types | N/A | N/A |

# Zdbus

## Addresses, authentication and framing

[Public headers](../zdbus/src/) · [Module guide](../zdbus/README.md).
Used by the Linux Ztls credential Vault; native message bodies use `ZfDBUS`.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZdbusAddress`<br>\+ decodes D-Bus unix addresses into filesystem/abstract socket endpoints | N/A | Partial: Asio local stream endpoints<br>\- D-Bus address parsing is additional work |
| D-Bus EXTERNAL authentication (`ZdbusAuth.hh`)<br>\+ incremental client authentication integrated with connection setup | N/A | N/A |
| D-Bus envelopes and pooled messages (`ZdbusEnvelope.hh`, `ZdbusMessage.hh`)<br>\+ bounded framing, header validation and direct pooled construction | N/A | Partial: Asio buffers + custom D-Bus codec |

## Typed message adapters and runtime

| Z facility | STL | Boost |
| --- | --- | --- |
| Request/return/error/signal adapters and catalogs<br>\+ routing headers and typed ZfDBUS bodies share application declarations | N/A | Partial: Boost.Describe + custom dispatch |
| `ZdbusClient`<br>\+ asynchronous calls, serial correlation, named errors and signal subscriptions<br>\- Linux-only module | N/A | Partial: Asio local sockets, timers and pending-call map<br>\- no complete Boost D-Bus client |
| `ZdbusServer`<br>\+ service-side method routes and typed returns/errors over the shared bus client<br>\- Linux-only module | N/A | N/A; service routing would be application code over Asio |

# Ztcp

## Buffer policies and byte streams

[Public API](../ztcp/src/Ztcp.hh) · [Module guide](../ztcp/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztcp::RxBufAlloc`, `TxBufAlloc`, `RxStream`, `TxStream`<br>\+ pooled, reference-owned input/output with explicit allocator roles<br>\- applications supply message framing | N/A | Partial: Asio buffer sequences and DynamicBuffer<br>\- matching pool ownership and framing require composition |
| `Ztcp::HubParams`, `ClientParams`, `ServerParams`<br>\+ named Rx/Tx destinations and allocator configuration shared by links | N/A | Partial: Asio executors, socket options and application configuration |

## TCP links and endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztcp::CliLink`, `SrvLink`<br>\+ CRTP connection callbacks, queued writes and reconnect integration | N/A | Asio `ip::tcp::socket` with composed connection/read/write operations |
| `Ztcp::Client`, `Server`<br>\+ hub-managed link/listener lifecycle and restart after drain | N/A | Partial: Asio resolver, sockets and acceptor<br>\- hub lifecycle and reconnect policy remain application work |

# Ztls

## Cryptographic primitives

[Public headers](../ztls/src/) · [Module guide](../ztls/README.md).
TLS and crypto use zpicotls/OpenSSL; backend requirements are in
[dependencies](DEPENDENCIES.md). Hash/MAC comparisons use
[Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html).

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztls::Random`<br>\+ crypto-provider random bytes directly into a caller span | Partial: `std::random_device`<br>\- implementation may use a pseudorandom engine; no universal cryptographic guarantee | Partial: Boost.Random `random_device`<br>\- different device/word-generation interface |
| `Ztls::ctEqual`<br>\+ security-oriented constant-time byte comparison | N/A; ordinary equality algorithms provide no constant-time guarantee | N/A; ordinary range equality provides no constant-time guarantee |
| `Ztls::secretHash`, `secretVerify`<br>\+ versioned salted secret-hash representation and verification | N/A | Partial: Boost.Hash2 + custom salt/format/verification policy |
| `Ztls::MD`<br>\+ incremental SHA digests through the configured crypto backend | N/A; `std::hash` is not a cryptographic digest API | Boost.Hash2 SHA algorithms<br>\+ also offers other hash families |
| `Ztls::HMAC`<br>\+ backend MAC state with span input/output and reset | N/A | Boost.Hash2 `hmac` |
| `Ztls::TOTP`<br>\+ time-based code calculation and bounded-window verification | N/A | Partial: Boost.Hash2 HMAC + custom TOTP construction |

## Keys, encapsulation and encrypted files

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztls::PK`<br>\+ RSA/EC/Ed25519/X25519 key loading, generation and applicable signing/verification operations | N/A | N/A; Asio SSL's native handle is not a public-key wrapper library |
| `Ztls::PK` ML-KEM-768 and ML-KEM-768–X25519 keys<br>\+ encapsulation/decapsulation and key serialization integrated with Z buffers | N/A | N/A |
| `Ztls::COSE`<br>\+ COSE key representations bound to native public/private key types | N/A | N/A |
| `Ztls::HPKE`<br>\+ base-mode hybrid ML-KEM-768–X25519 encryption with caller buffers | N/A | N/A |
| `ZtlsAge`<br>\+ age-format file encryption/decryption and recipient/identity parsing | N/A | N/A; file streams alone supply no age encryption |

## Credential storage

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztls::Vault`, `VaultConfig`, `VaultStore`<br>\+ scoped credential names with ephemeral/file/keyring/module stores<br>\+ Linux Secret Service and Windows platform integration | N/A | N/A; no matching Boost credential vault |

## TLS byte streams and endpoints

TLS counterpart: [Boost.Asio SSL](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/ssl.html).

| Z facility | STL | Boost |
| --- | --- | --- |
| `Ztls::RxBufAlloc`, `TxBufAlloc` and TLS packet allocation<br>\+ separate plaintext/ciphertext ownership and serial record encryption into packet spans | N/A | Partial: Asio SSL buffers and associated allocators<br>\- different packet-backing integration |
| `Ztls::HubParams`, `ClientParams`, `ServerParams`<br>\+ trust, keys, ALPN, allocator roles and Rx/Tx destinations in one setup contract | N/A | Partial: Asio `ssl::context` + executors and application parameters |
| `Ztls::CliLink`, `SrvLink`<br>\+ handshake/session callbacks integrated with pooled transport and shard ownership | N/A | Asio `ssl::stream<ip::tcp::socket>`<br>\+ standard Asio asynchronous-stream composition |
| `Ztls::Client`, `Server`<br>\+ managed TLS link/listener startup, drain and restart | N/A | Partial: Asio SSL streams, acceptors and application lifecycle |

# Zquic

## Packet, frame and crypto codecs

[Public headers](../zquic/src/) · [Module guide](../zquic/README.md).
QUIC v1 is the mainstream deployment baseline; quic-go still prefers it as the
[most commonly deployed version](https://quic-go.net/docs/quic/transport/).
V2 adds no application capabilities and does not deprecate v1
([RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html)).

| Z facility | STL | Boost |
| --- | --- | --- |
| QUIC packet/frame/transport-parameter codecs<br>\+ native wire parsing and output integrated with Z buffer ownership | N/A | N/A; Asio UDP does not implement QUIC wire formats |
| `Zquic::AddressToken`, `StatelessReset`, connection-ID/bootstrap facilities<br>\+ Retry/address-validation and connection routing machinery | N/A | N/A |
| QUIC crypto (`ZquicCrypto.hh`)<br>\+ zpicotls handshake state, packet protection and key retirement | N/A | N/A; Asio SSL streams are TLS byte streams, not QUIC packet protection |

## Recovery, paths and stream ownership

| Z facility | STL | Boost |
| --- | --- | --- |
| ACK/loss recovery, `RttEstimator`, `NewReno`, PTO and retransmission queues<br>\+ packet-number-space recovery with retained frame ownership | N/A | N/A; Asio timers provide scheduling primitives only |
| `Zquic::Path` and link migration<br>\+ path validation, anti-amplification, PMTU/ECN and single-active-path migration | N/A | N/A |
| QUIC buffer/sequence-queue policies<br>\+ separate packet, CRYPTO and stream retention/reassembly contracts | N/A | Partial: Boost.Intrusive / ICL containers<br>\- custom QUIC ownership/recovery logic required |
| `Zquic::CliStream`, `SrvStream`<br>\+ multiplexed streams with independent directional retirement, FIN/reset/stop and flow control | N/A | N/A; no complete Boost QUIC stream facility |

## Transport endpoints and diagnostics

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zquic::HubParams`<br>\+ unified TLS, ALPN, transport, migration and instrumentation configuration | N/A | N/A |
| `Zquic::CliLink`, `SrvLink`<br>\+ shared connection runtime, stream scheduling and migration callbacks | N/A | N/A |
| `Zquic::Client`, `Server`<br>\+ QUIC connection bootstrap and UDP listener lifecycle over Zi | N/A | Partial: Asio UDP sockets and timers<br>\- no QUIC transport implementation |
| `ZquicLogger`, `ZquicLogSink`, transport diagnostics<br>\+ qlog JSON-SEQ events with bounded queues and controlled drain/restart | N/A | Partial: Boost.Log + JSON<br>\- QUIC/qlog event model and instrumentation are custom work |

# Zhttp

## URLs, fields and protocol configuration

[Public headers](../zhttp/src/) · [Module guide](../zhttp/README.md).
[Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)
supplies HTTP/1 and WebSocket facilities; Zhttp also owns HTTP/2 and HTTP/3.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zhttp::URLView`, `URL`, `Target`<br>\+ explicit borrowed/owning HTTP URL and request-target representations<br>\- borrowed parsing normalizes mutable input | N/A | Boost.URL `url_view` / `url`<br>\+ general URI component model |
| `ZhttpHeaders`, field catalogs and matchers<br>\+ compile-time name/value dispatch and shared header semantics across wire versions | N/A | Partial: Beast HTTP fields<br>\- matching typed catalogs require application machinery |
| `Zhttp::Profiles`, `H1TCP`, `H1TLS`, `H2TLS`, `H3QUIC`<br>\+ explicit compile-time selection of supported transport/protocol combinations | N/A | Partial: Beast stream types with Asio SSL<br>\- no corresponding H2/H3 profiles |
| `Zhttp::HubConfig`, `Config`, `ServerConfig` and transport configs<br>\+ shared connection, body, queue, protocol and allocator policy | N/A | Partial: Beast/Asio stream options + application configuration |

## Discovery and event framing

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zhttp::DiscoveryRequest`, endpoint selection and `AltSvcCache`<br>\+ HTTPS/SVCB and Alt-Svc routing integrated with client protocol selection | N/A | Partial: Asio resolver + custom discovery/cache logic |
| `Zhttp::SSEDecoder`, `SSEEvent`<br>\+ incremental server-sent event framing and bounded event storage | N/A | Partial: Beast streaming HTTP bodies + custom SSE parser |

## Messages, body streams and response routes

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zhttp::Parser`, request/response Builders and header catalogs<br>\+ one typed application interface for H1/H2/H3 parsing and production | N/A | Beast HTTP parsers, serializers and Body models<br>\- HTTP/1 vocabulary rather than a shared H1/H2/H3 interface |
| `Zhttp::BodyEmitter` and body policies<br>\+ bounded fixed/streaming production, cancellation and H2/H3 trailers<br>\- application owns body decompression | N/A | Partial: Beast serializers and custom Body implementations |
| `Zhttp::Channel`, `Route`, response queues<br>\+ owning deferred replies with H1 ordering and logical-stream cancellation | N/A | Partial: Beast stream plus application response-ordering/lifetime state |

## HTTP wire protocols and endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| HTTP/1.1 framing (`Zhttp`)<br>\+ integrated request/response streaming and pooled transport | N/A | Boost.Beast HTTP/1 |
| HTTP/2 and HPACK (`Zhttp`)<br>\+ multiplexed streams, flow control and header compression over TLS | N/A | N/A; Beast does not provide HTTP/2 |
| HTTP/3 and QPACK (`Zhttp`)<br>\+ HTTP messages, control streams and compression over Zquic<br>\- nonzero blocked-stream buffering is outside the current QPACK profile | N/A | N/A; Beast does not provide HTTP/3 |
| `Zhttp::Pool`, `Client`<br>\+ pooled destinations with independent link/request concurrency, retry and protocol fallback | N/A | Partial: Beast client examples + custom pooling/request coordinator |
| `Zhttp::Server`<br>\+ typed admission, listeners, sessions and graceful drain across selected HTTP profiles | N/A | Partial: Beast server examples + custom runtime<br>\- no native H2/H3 endpoints |

# Zws

## URI, handshake and frame codecs

[Public headers](../zws/src/) · [Module guide](../zws/README.md).
WebSocket counterpart: [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket.html).
Zws supplies the WebSocket dependency of Zjrpc and Zmcp.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zws::URI`<br>\+ ws/wss URI interpretation for native profile setup | N/A | Partial: Boost.URL URI parsing + WebSocket-specific checks |
| Opening handshake, H1 Upgrade and H2/H3 Extended CONNECT<br>\+ binds the same WebSocket application to concrete HTTP profiles | N/A | Beast WebSocket handshake<br>\- H2/H3 Extended CONNECT requires another HTTP stack |
| `Zws::Codec`, protocol frame/header utilities<br>\+ transport-independent masking, fragmentation and control-frame processing | N/A | Beast WebSocket frame/message operations |
| `Zws::TxLayer`<br>\+ reference-buffer output and explicit fragment/message boundaries | N/A | Beast WebSocket write/partial-write operations |

## WebSocket endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zws::Config` and link state<br>\+ bounded messages with handshake/close/heartbeat policy and per-link state | N/A | Partial: Beast WebSocket options + application state |
| `Zws::Client`, `Server` and native links<br>\+ CRTP message callbacks and streaming output over H1/H2/H3 bindings | N/A | Beast `websocket::stream` + application accept/connect lifecycle |

# Zrest

## Typed request/response contracts

[Public headers](../zrest/src/) · [Module guide](../zrest/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zrest::Request`, `Response`, `Reply` and catalog macros<br>\+ compile-time methods, paths, body policies and allowed typed replies | N/A | Partial: Beast + Describe/JSON<br>\- typed REST contracts and routing require application code |
| `Zrest::Input`, `BorrowedData`, `RequestContext`<br>\+ bounded entity assembly with explicit owned/borrowed decoding policy | N/A | Partial: Beast Body storage + Boost.JSON conversion |
| REST query/body signing hooks and `REST::Signature`<br>\+ filters the signature field and exposes the serialized bytes to the signer<br>\- algorithm, verification and wire-coverage policy are application-owned | N/A | Partial: Beast/JSON serialization + application signing logic |

## Adapters, completion and server-sent events

| Z facility | STL | Boost |
| --- | --- | --- |
| `MReqBuilder`, `MResParser`, `MReqParser`, `MResBuilder`<br>\+ shared catalog adapters for typed operations over Zhttp profiles | N/A | Partial: Beast HTTP parser/serializer + Describe/JSON binding |
| `Zrest::Completion`<br>\+ one-shot asynchronous typed completion with route invalidation | N/A | Partial: Asio completion handlers + application token/lifetime state |
| `Zrest::SSEProducer`, `SSEBuilder`, SSE reply metadata<br>\+ bounded producer queues and incremental typed SSE delivery<br>\- no automatic replay or reconnection | N/A | Partial: Beast streaming bodies + custom SSE/lifecycle state |

## REST endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zrest::Client`, `Server`, endpoint configs and limits<br>\+ typed calls/routes, admission limits and full REST/HTTP drain boundary | N/A | Partial: Beast + JSON/Describe + Asio<br>\- no complete matching Boost REST endpoint framework |

# Zgrpc

## Method, metadata and status vocabulary

[Public headers](../zgrpc/src/) · [Module guide](../zgrpc/README.md).
Zpb supplies native protobuf payloads; Zhttp supplies H2/H3 transport.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zgrpc::Method`, `Message`, `Reply` and catalog macros<br>\+ typed method paths, RPC cardinality and response contracts | N/A | Partial: Boost.Describe / Hana<br>\- no gRPC method/catalog runtime |
| `Zgrpc::Metadata`, `Status`, timeout and binary-value codecs<br>\+ bounded initial/trailing metadata and gRPC status/deadline interpretation | N/A | N/A; generic HTTP fields do not implement these gRPC rules |

## Envelope, protobuf and HTTP adapters

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zgrpc::FrameRx` and pooled frame output<br>\+ incremental five-byte envelopes with byte budgets and retained payload backing | N/A | Partial: Asio buffers + custom envelope codec |
| `Zgrpc::PBMessage` and typed payload adapters<br>\+ Zpb verification before typed construction and explicit borrowed-data policy | N/A | N/A; no native Boost protobuf/gRPC payload binding |
| `Zgrpc::RxInput`, `TxFrames`, `TxBody`<br>\+ bounded receive turns and retained frame queues for HTTP production | N/A | Partial: Asio queues/composed operations + custom stream state |
| Request/response HTTP and multi-method adapters<br>\+ method-specific metadata, streaming bodies and terminal trailers over H2/H3 | N/A | N/A; Beast lacks H2/H3 and gRPC adapters |

## Call ownership and endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zgrpc::ClientCall`, `ServerCall`, call tokens and directional state<br>\+ invalidatable Rx/Tx routes, cardinality and one-shot finish/cancel admission | N/A | Partial: Asio cancellation and completion primitives<br>\- gRPC call semantics remain custom work |
| `Zgrpc::Client`, `Server`, endpoint configs and limits<br>\+ unary/client-streaming/server-streaming/duplex RPCs, deadlines and bounded shutdown | N/A | N/A; no complete Boost gRPC endpoint library |

# Zjrpc

## JSON-RPC envelopes and declarations

[Public headers](../zjrpc/src/) · [Module guide](../zjrpc/README.md).
JSON counterparts use [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html);
JSON parsing alone does not supply JSON-RPC dispatch or correlation.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zjrpc::Request`, `Response`, IDs, `Envelope`, `Parsed` and wire views<br>\+ typed parameters/results and distinct missing/null IDs with retained JSON views<br>\- numeric IDs use signed 64-bit integers; no fractional/exponent forms | N/A | Partial: Boost.JSON values/conversion + custom JSON-RPC schema |
| `Zjrpc::Input`, `Output`, `BufOutput`<br>\+ bounded pooled JSON assembly and serialization | N/A | Partial: Boost.JSON parser/serializer with custom pooled storage |

## Correlation, completion and batches

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zjrpc::PendingCalls`, `Caller`<br>\+ outbound ID correlation removed before typed terminal callbacks | N/A | Partial: Boost.Unordered + Asio completion state |
| `Zjrpc::InboundCalls`<br>\+ separate live requests and bounded recent-ID history<br>\- history contains IDs, not replayable replies | N/A | Partial: Boost.MultiIndex + custom admission/history policy |
| `Zjrpc::Completion` and completion slots<br>\+ one-shot claims before reply posting and invalidation before cancellation callbacks | N/A | Partial: Asio completion tokens + custom one-shot ownership |
| `Zjrpc::Dispatcher`, `Batch`, `BatchReply`<br>\+ shared typed dispatch, notifications and bounded batch aggregation | N/A | Partial: Boost.JSON + custom RPC dispatcher |

## Transport bindings and endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zjrpc::StdioConfig`, `StdioFramer`, `IOEndpoint`<br>\+ line framing and isolated blocking workers over owned per-run handle duplicates | N/A | Partial: Asio descriptors/handles or Iostreams + custom line/RPC runtime |
| JSON-RPC HTTP/SSE and WebSocket bindings<br>\+ shared RPC work/correlation over Zhttp/Zws channels | N/A | Partial: Beast HTTP/WebSocket + JSON<br>\- RPC/SSE work and lifecycle remain application code |
| `Zjrpc::IOClient`, `IOServer`, `HTTPClient`, `HTTPServer`, `WSClient`, `WSServer`<br>\+ typed calls/notifications/batches and coordinated restart after drain | N/A | N/A; no complete matching Boost JSON-RPC endpoint library |

# Zmcp

## Typed tools and protocol vocabulary

[Public headers](../zmcp/src/) · [Module guide](../zmcp/README.md).
Uses Zjrpc, Zws and Zhttp; it has no dependency on Zrest despite shared metadata conventions.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zmcp::Request`, `Response`, tool catalogs and generated schemas<br>\+ reflected tools/list/tools/call with typed input/output schemas<br>\- current scope is tool providers/callers; prompts/resources and other MCP subsystems are deferred | N/A | Partial: Boost.Describe + JSON<br>\- MCP schema/catalog policy is additional work |
| `Zmcp::ToolAnnotations`, `MCP::Header`<br>\+ compile-time tool semantics drive schema/header output and trusted retry policy | N/A | N/A |
| MCP peer negotiation and catalog cache<br>\+ discovery/legacy handshake and explicit catalog invalidation | N/A | N/A |
| `Zmcp::Context` and extended completion policy<br>\+ transport/session/stream context with cancellation, progress and logging<br>\- context pointers are callback-scoped | N/A | Partial: Asio handlers/cancellation + custom MCP policy |

## MCP endpoints

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zmcp::Batch` and shared JSON-RPC aggregation<br>\+ typed aggregate tool calls reuse the ordinary pending-call table | N/A | Partial: Boost.JSON + custom MCP/RPC aggregation |
| `Zmcp::IOClient`, `IOServer`<br>\+ newline-delimited MCP over inherited stdio with bounded work and lifecycle<br>\- peer process creation/supervision is application-owned | N/A | Partial: Boost.Process + custom MCP protocol/runtime |
| `Zmcp::HTTPClient`, `HTTPServer`, `WSClient`, `WSServer`<br>\+ shared typed tool handlers over Streamable HTTP and WebSockets with full drain<br>\- authentication/authorization remain application-owned | N/A | Partial: Beast/Asio + JSON<br>\- no complete Boost MCP endpoint library |

# Zdb

## Schema, rows and indices

[Public headers](../zdb/src/) · [Module guide](../zdb/README.md).
Zfb supplies wire/schema reflection and Zv supplies runtime configuration.

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZdbTable<T>`, `ZdbTableDerive` and reflected key/schema metadata<br>\+ typed primary/secondary indices, grouped queries and generated BFBS schema<br>\- immutable application schema is selected before startup | N/A | Partial: Boost.MultiIndex + Describe<br>\- no persistence/schema-store integration |
| `ZdbRow`, `ZdbRowRef`, `ZdbTblRef`<br>\+ intrusive row/table lifetime and shard-owned mutation callbacks | Partial: `std::shared_ptr`<br>\- no database row state or shard ownership contract | Partial: Boost.Intrusive / SmartPtr |
| Zdb buffer and message formats (`ZdbBuf.hh`, `ZdbMsg.hh`)<br>\+ reflected replication/store records share reference-owned I/O backing | N/A | Partial: Boost.Serialization<br>\- different wire layout and ownership model |

## Database operations and lifecycle

| Z facility | STL | Boost |
| --- | --- | --- |
| Zdb find/insert/update/delete and row caches<br>\+ typed asynchronous operations with write-through caching and index maintenance<br>\- local commit acceptance is not synchronous persistence/replication confirmation | N/A | Partial: Boost.MultiIndex cache + custom store/transaction runtime |
| Zdb select/count and queue barriers<br>\+ index-based backing-store queries ordered with writes | N/A | Partial: database client + custom cache/store queue coordination |
| `Zdb`, `ZdbCf`, `ZdbHandler`<br>\+ DB-wide shards, configured workers, activation callbacks and orderly drain | N/A | N/A; Asio/Statechart primitives do not form a database runtime |

## Replication and recoverable workflows

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZdbHost` and replication/activation<br>\+ leader election, asynchronous chain replication and failover recovery<br>\- asynchronous replication is not a synchronous consensus commit guarantee | N/A | N/A |
| `ZdbSaga`, `ZdbSagaDB`, `ZdbSagaPhase`, `ZdbSagaRepeatPhase`<br>\+ journaled workflow steps, recovery and application compensation<br>\- arbitrary external effects are not automatically journaled or exactly-once | N/A | Partial: Boost.Statechart / MSM<br>\- no durable saga journal, database mutation protocol or recovery |
| `Ztc::DB`, `DBHost`, `DBTable` telemetry<br>\+ reflected database, host and table statistics | N/A | Partial: application records + Boost.Describe / Serialization |

## Pluggable backing stores

[Store contract](../zdb/src/ZdbStore.hh).
Boost.MultiIndex supplies in-memory indices; [Boost.MySQL](https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/index.html)
is a database client, not an equivalent of Zdb's database/store framework.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zdb_::Store`, `StoreTbl` and store-module loading<br>\+ asynchronous store contract with reflected schemas and query/write callbacks | N/A | Partial: Boost.DLL + custom database adapter<br>\- no matching Boost backing-store interface |
| `ZdbMem::Store` / `libZdbMem`<br>\+ in-memory implementation of the same store contract<br>\- process loss loses locally stored data | N/A | Partial: Boost.MultiIndex<br>\- persistence/replication/store contracts require custom code |

# ZdbPQ

## PostgreSQL backing store

[Public API](../zdb_pq/src/ZdbPQ.hh) · [Module guide](../zdb_pq/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZdbPQ` / `libZdbPQ`<br>\+ asynchronous libpq store with reflected prepared SQL and binary I/O<br>\- requires PostgreSQL/libpq and configured server-side types | N/A | N/A; Boost.MySQL speaks a different database protocol |
| PostgreSQL `libz` type extension<br>\+ native database representations for Z decimal, time and bitmap values | N/A | N/A |

# ZdbSL

## SQLite backing store and inspection

[Public headers](../zdb_sqlite/src/) · [Module guide](../zdb_sqlite/README.md)

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZdbSL::Store` / `libZdbSL`<br>\+ reflected persistent store with WAL and one isolated worker per connection<br>\- local filesystem and one writer connection per store | N/A | N/A; no matching Boost SQLite backing-store implementation |
| `ZdbSL` storage codecs and inspection extension<br>\+ canonical sortable Z scalar encodings and readable SQLite inspection helpers | N/A | Partial: Boost.Endian / Serialization<br>\- Z logical encodings and SQLite extension require custom work |
