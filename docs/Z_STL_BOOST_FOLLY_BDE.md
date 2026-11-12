# Z, STL, Boost, Folly, BDE and NTF equivalents

# Zu

## Abbreviated forwarding and move operations

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuFwd` | `std::forward` | `boost::forward` (Move) | N/A | [`bslmf::Util::forward`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h) |
| `ZuMv` | `std::move` | `boost::move` | N/A | [`bslmf::MovableRefUtil::move`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_movableref.h) |
| `ZuMvPtr`<br>➕ nulls a moved raw pointer as well as moving smart pointers | Partial: `std::exchange(p, nullptr)` for raw pointers, `std::move` for smart pointers | Partial: `boost::exchange`, `boost::move` | N/A | Partial: [`bsl::exchange`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_utility.h)<br>➖ raw-pointer nulling only; smart-pointer move needs separate handling |
| `ZuFwdLike`<br>➕ optional target-type conversion | `std::forward_like` (C++23) | Partial: Boost.Move forwarding + TypeTraits qualifier propagation | [`folly::forward_like`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➖ no separate target-type conversion overload | [`bslmf::Util::forward_like`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h)<br>➖ no separate target-type conversion overload |

## Compile-time constants

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuConstant<T, V>` — typed compile-time value, including non-integral and structural class values such as `ZuString`<br>➕ conversion and call return const references | `std::integral_constant<T, V>` — also accepts permitted non-integral template values<br>➖ conversion and call return by value | `boost::integral_constant` | Partial: [`folly::vtag_t<V>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ value-list tag rather than a typed reference-returning constant wrapper | [`bsl::integral_constant<T, V>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h)<br>➖ conversion/call returns by value |
| `ZuInt` | `std::integral_constant<int, I>` | `boost::mp11::mp_int<I>` | N/A | [`bsl::integral_constant<int, I>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h) |
| `ZuUnsigned` | `std::integral_constant<unsigned, I>` | `boost::integral_constant<unsigned, I>` | Partial: [`folly::index_constant<I>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ size_t-valued rather than unsigned-valued | [`bsl::integral_constant<unsigned, I>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h) |
| `ZuBool` | `std::bool_constant` | `boost::mp11::mp_bool` | N/A | [`bsl::integral_constant<bool, B>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h) |
| `ZuTrue` | `std::true_type` | `boost::true_type` | N/A | [`bsl::true_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h) |
| `ZuFalse` | `std::false_type` | `boost::false_type` | N/A | [`bsl::false_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h) |
| `ZuAlwaysTrue` | Partial: dependent `std::true_type` wrapper | Partial: `boost::mp11::mp_true` wrapper | N/A | Partial: [`bsl::true_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuAlwaysFalse` | Partial: dependent `std::false_type` wrapper | Partial: `boost::mp11::mp_false` wrapper | N/A | Partial: [`bsl::false_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuInt128` | Partial: `std::integral_constant` over compiler 128-bit integer | Partial: constant wrapper over a supported 128-bit type | N/A | Partial: [`bsl::integral_constant with compiler 128-bit type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h)<br>➖ compiler-extension support required |
| `ZuUInt128` | Partial: `std::integral_constant` over compiler unsigned 128-bit integer | Partial: constant wrapper over a supported 128-bit type | N/A | Partial: [`bsl::integral_constant with compiler 128-bit type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integralconstant.h)<br>➖ compiler-extension support required |

## Abbreviated type transformations and normalizations

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuDeref` | `std::remove_reference_t` | `boost::remove_reference_t` | N/A | [`bsl::remove_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_removereference.h) |
| `ZuStrip` | `std::remove_cv_t` | `boost::remove_cv_t` | N/A | [`bsl::remove_cv`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_removecv.h) |
| `ZuDecay`<br>➖ does not decay arrays/functions to pointers | `std::remove_cvref_t`<br>➕ matches the actual operation; `std::decay_t` has different semantics | `boost::remove_cv_ref_t` | N/A | [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_removecvref.h) |
| `ZuAsIs` | `std::type_identity_t` | `boost::type_identity_t` | N/A | [`bsl::type_identity`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_typeidentity.h) |
| `ZuMkConst` | Partial: `std::add_const_t`<br>➕ handles reference inputs through trait rules | `boost::add_const_t` | N/A | [`bsl::add_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_addconst.h) |
| `ZuMkVolatile` | Partial: `std::add_volatile_t` | `boost::add_volatile_t` | N/A | [`bsl::add_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_addvolatile.h) |
| `ZuMkRRef` | Partial: `std::add_rvalue_reference_t`<br>➕ also handles non-referenceable types | `boost::add_rvalue_reference_t` | N/A | [`bsl::add_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_addrvaluereference.h) |
| `ZuMkLRef` | Partial: `std::add_lvalue_reference_t` | `boost::add_lvalue_reference_t` | N/A | [`bsl::add_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_addlvaluereference.h) |
| `ZuMkCRef` | Composition: `std::add_lvalue_reference_t<std::add_const_t<T>>` | Composition: `boost::add_lvalue_reference_t<boost::add_const_t<T>>` | N/A | Partial: [`bsl::add_const + add_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuRDecay`<br>➕ ADL-customizable recursive decay of composites | N/A; `std::remove_cvref_t` is shallow | Partial: recursive Boost.Mp11 / Hana transformation | N/A | Partial: [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ shallow transformation; no ADL-recursive composite decay |
| `ZuNorm`<br>➕ customizable normalization of representation-equivalent character types | Partial: `std::remove_cvref_t`<br>➖ does not normalize character representations | Partial: Boost.TypeTraits; custom mapping | N/A | Partial: [`bsl::remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ custom character-representation normalization must be added |
| `ZuEquiv` | Composition: `std::is_same` with a custom normalization | Composition: `boost::is_same` with a custom normalization | N/A | Partial: [`bsl::is_same + remove_cvref`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires Z-equivalent custom type normalization |
| `ZuAlt`<br>➕ char/wchar_t alternate-type mapping | N/A | N/A | N/A | N/A |
| `ZuUnder`<br>➕ customizable for wrappers as well as enums | Partial: `std::underlying_type_t`<br>➖ enums only | Partial: `boost::underlying_type_t` | N/A | Partial: [`bsl::underlying_type`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ enum case only; other Z unwrapping cases need adaptation |
| `ZuUnderlying`<br>➕ unwraps participating wrapper types as well as enums | Partial: `std::to_underlying` (C++23)<br>➖ enums only | Partial: `boost::underlying_cast` | Partial: [`folly::to_underlying`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➖ enums only | Partial: [`bsl::to_underlying`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_utility.h)<br>➖ enums only; availability follows standard-library support |

## Qualifier and reference constraints

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuIsConst` | `std::is_const` | `boost::is_const` | [`bsl::is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isconst.h) |
| `ZuIsVolatile` | `std::is_volatile` | `boost::is_volatile` | [`bsl::is_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isvolatile.h) |
| `ZuConst` | `std::enable_if_t<std::is_const_v<U>, R>` | `boost::enable_if<boost::is_const<U>, R>::type` | Partial: [`bsl::enable_if + is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuMutable` | `std::enable_if_t<!std::is_const_v<U>, R>` | `boost::disable_if<boost::is_const<U>, R>::type` | Partial: [`bsl::enable_if + is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuVolatile` | `std::enable_if_t<std::is_volatile_v<U>, R>` | `boost::enable_if<boost::is_volatile<U>, R>::type` | Partial: [`bsl::enable_if + is_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNonVolatile` | `std::enable_if_t<!std::is_volatile_v<U>, R>` | `boost::disable_if<boost::is_volatile<U>, R>::type` | Partial: [`bsl::enable_if + is_volatile`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuIsLRef` | `std::is_lvalue_reference` | `boost::is_lvalue_reference` | [`bsl::is_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_islvaluereference.h) |
| `ZuIsCRef` | Composition: lvalue-reference test and const-referent test | Composition of Boost.TypeTraits | Partial: [`bsl::is_lvalue_reference + is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuIsRRef` | `std::is_rvalue_reference` | `boost::is_rvalue_reference` | [`bsl::is_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isrvaluereference.h) |
| `ZuLRef` | `std::enable_if_t<std::is_lvalue_reference_v<U>, R>` | Boost.EnableIf + `boost::is_lvalue_reference` | Partial: [`bsl::enable_if + is_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotLRef` | Negated `std::is_lvalue_reference` constraint | Boost.DisableIf + `boost::is_lvalue_reference` | Partial: [`bsl::enable_if + is_lvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuCRef` | Constraint on const lvalue references | Composition of Boost.EnableIf and TypeTraits | Partial: [`bsl::enable_if + is_lvalue_reference + is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotCRef` | Negated const-lvalue-reference constraint | Composition of Boost.DisableIf and TypeTraits | Partial: [`bsl::enable_if + is_lvalue_reference + is_const`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuRRef` | `std::enable_if_t<std::is_rvalue_reference_v<U>, R>` | Boost.EnableIf + `boost::is_rvalue_reference` | Partial: [`bsl::enable_if + is_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotRRef` | Negated `std::is_rvalue_reference` constraint | Boost.DisableIf + `boost::is_rvalue_reference` | Partial: [`bsl::enable_if + is_rvalue_reference`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |

## Type identity and inheritance constraints

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuIsSame` | `std::is_same` | `boost::is_same` | [`bsl::is_same`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_issame.h) |
| `ZuSame` | `std::enable_if_t<std::is_same_v<A, B>, R>` | Boost.EnableIf + `boost::is_same` | Partial: [`bsl::enable_if + is_same`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotSame` | Negated `std::is_same` constraint | Boost.DisableIf + `boost::is_same` | Partial: [`bsl::enable_if + is_same`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuIs`<br>➕ decays types and accepts identity or accessible pointer base conversion | Partial: `std::same_as` / `std::derived_from` constraint | Partial: Boost.TypeTraits + EnableIf | Partial: [`bsl::enable_if + is_same + is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires Z decay and accessible pointer-base-conversion composition |
| `ZuIsNot` | Negation of the identity/base-conversion constraint | Partial: Boost.TypeTraits + DisableIf | Partial: [`bsl::enable_if + is_same + is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires Z decay and accessible pointer-base-conversion composition |
| `ZuIsBase`<br>➖ argument order and identity/access rules differ from `is_base_of` | Partial: `std::is_base_of` / `std::derived_from` | Partial: `boost::is_base_of` | [`bsl::is_base_of`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| `ZuBase` | Constraint on strict accessible base conversion | Partial: Boost.EnableIf + TypeTraits | Partial: [`bsl::enable_if + is_base_of`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotBase` | Negated strict accessible base-conversion constraint | Partial: Boost.DisableIf + TypeTraits | Partial: [`bsl::enable_if + is_base_of`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |

## Conversion, construction and exception traits

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuIsConvertible`<br>➖ uses Z's reference-normalized probe semantics | Partial: `std::is_convertible` | Partial: `boost::is_convertible` | [`bsl::is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isconvertible.h) |
| `ZuConvertible` | Partial: `std::enable_if_t<std::is_convertible_v<From, To>, R>` | Partial: Boost.EnableIf + `boost::is_convertible` | Partial: [`bsl::enable_if + is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotConvertible` | Partial: negated `std::is_convertible` constraint | Partial: Boost.DisableIf + `boost::is_convertible` | Partial: [`bsl::enable_if + is_convertible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuIsConstructible`<br>➖ source/destination API and cast probing differ | Partial: `std::is_constructible`<br>➕ variadic constructor-argument API | Partial: `boost::is_constructible` | [`bsl::is_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| `ZuConstructible` | Partial: `std::enable_if_t<std::is_constructible_v<To, From>, R>` | Partial: Boost.EnableIf + `boost::is_constructible` | Partial: [`bsl::enable_if + is_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNotConstructible` | Partial: negated constructibility constraint | Partial: Boost.DisableIf + `boost::is_constructible` | Partial: [`bsl::enable_if + is_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h)<br>➖ requires the corresponding Z predicate/adaptation composition |
| `ZuNXConstruct` | Partial: `std::is_nothrow_default_constructible` | Partial: `boost::is_nothrow_default_constructible` | [`bsl::is_nothrow_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| `ZuNXMove` | Partial: `std::is_nothrow_move_constructible` | Partial: `boost::is_nothrow_move_constructible` | [`bsl::is_nothrow_move_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isnothrowmoveconstructible.h) |
| `ZuNXCopy` | Partial: `std::is_nothrow_copy_constructible` | Partial: `boost::is_nothrow_copy_constructible` | [`bsl::is_nothrow_copy_constructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| `ZuNXDestroy` | Partial: `std::is_nothrow_destructible` | Partial: `boost::is_nothrow_destructible` | [`bsl::is_nothrow_destructible`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |

## Conditional types and compile-time queries

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuIf` | `std::conditional_t` | `boost::mp11::mp_if_c` | [`folly::conditional_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | [`bsl::conditional`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_conditional.h) |
| `ZuIfT` | `std::enable_if_t` | `boost::enable_if_c<...>::type` | N/A | [`bsl::enable_if`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_enableif.h) |
| `ZuDeclVal` | `std::declval` | `boost::declval` | Partial: [`FOLLY_DECLVAL`](https://github.com/facebook/folly/blob/main/folly/Utility.h)<br>➕ avoids function-template instantiation<br>➖ value-category/completeness rules differ | Partial: [`bslmf::Util::declval`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_util.h)<br>➖ value-category rules differ |
| `ZuConstEval` | `std::is_constant_evaluated` | N/A; use the standard facility or compiler intrinsic | N/A | [`bsl::is_constant_evaluated`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_type_traits.h) |
| `ZuSize`<br>➕ zero logical size for void/empty types; ordinary sizeof otherwise | Partial: `std::is_empty` with custom logical-size calculation | Partial: Boost.TypeTraits + `sizeof` | N/A | Partial: [`bsl::is_empty`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isempty.h)<br>➖ logical size calculation needs additional handling |
| `ZuLargest`<br>➕ selects a type from a pack or ZuTypeList; first wins ties; handles void | N/A; size measurement alone does not select a type | Partial: Boost.Mp11 reduction by `sizeof` | N/A | N/A |
| `ZuMostAligned`<br>➕ selects the most-aligned type from a pack or ZuTypeList; first wins ties | N/A; alignment measurement alone does not select a type | Partial: Boost.Mp11 reduction by `alignof` | N/A | N/A |
| `ZuTraits`<br>➕ common ADL vocabulary for strings, containers and numerics | Partial: `<type_traits>`, `std::ranges` concepts, `std::numeric_limits` | Partial: Boost.TypeTraits, Range, NumericConversion | Partial: [`folly::IsRelocatable / detection traits`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ no unified Z-style string/container/numeric trait vocabulary | Partial: [`bslmf::IsBitwiseMoveable + BSL type traits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_isbitwisemoveable.h)<br>➖ no unified Z string/container/numeric trait vocabulary |

## Compiler hints

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuLikely`<br>➕ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | `BOOST_LIKELY` | [`FOLLY_LIKELY`](https://github.com/facebook/folly/blob/main/folly/Likely.h) | [`BSLS_PERFORMANCEHINT_PREDICT_LIKELY`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h) |
| `ZuUnlikely`<br>➕ expression-level branch hint, usable inside compound conditions | N/A; C++23 likelihood attributes apply to statements/labels | `BOOST_UNLIKELY` | [`FOLLY_UNLIKELY`](https://github.com/facebook/folly/blob/main/folly/Likely.h) | [`BSLS_PERFORMANCEHINT_PREDICT_UNLIKELY`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_performancehint.h) |
| `ZuInline`<br>➕ requests always_inline on supported compilers | N/A; the inline keyword does not request forced optimizer inlining | `BOOST_FORCEINLINE` | [`FOLLY_ALWAYS_INLINE`](https://github.com/facebook/folly/blob/main/folly/CPortability.h) | N/A |
| `ZuNoInline` | N/A; compiler attributes | `BOOST_NOINLINE` | [`FOLLY_NOINLINE`](https://github.com/facebook/folly/blob/main/folly/CPortability.h) | N/A |
| `ZuUnreachable`<br>➕ debug build aborts at the failed assumption | `std::unreachable` (C++23) | `BOOST_UNREACHABLE_RETURN` (partial return-site idiom) | [`FOLLY_ASSUME_UNREACHABLE`](https://github.com/facebook/folly/blob/main/folly/lang/Assume.h) | N/A |

## Assertions, declarations and preprocessor utilities

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuAssert`<br>➕ deferred ZuPP expansion and optional expression-text diagnostics | N/A; wraps core static_assert with macro-expansion support | `BOOST_STATIC_ASSERT`, `BOOST_STATIC_ASSERT_MSG` | N/A | Partial: [`BSLMF_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h)<br>➖ assertion/deferred-expansion interfaces differ |
| `ZuCAssert`<br>➕ shorter spelling for a nonzero compile-time assertion<br>➖ older switch/case idiom; no semantic advantage over static_assert | N/A; core static_assert supplies the assertion itself | `BOOST_STATIC_ASSERT` | N/A | Partial: [`BSLMF_ASSERT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_assert.h)<br>➖ assertion/deferred-expansion interfaces differ |
| `ZuOpBool`<br>➕ emits a truth conversion from the existing operator! without repeating its logic<br>➖ pointer conversion permits more conversions than explicit bool | N/A; a conversion operator still needs a user-provided body | Partial: `BOOST_EXPLICIT_OPERATOR_BOOL` | N/A | Partial: [`bsls::UnspecifiedBool`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_unspecifiedbool.h)<br>➖ safe-bool result helper rather than generated conversion member |
| `ZuDerive`<br>➕ short distinct type ID reduces complex template names in symbols<br>➕ generates constrained forwarding construction and derived-returning assignment | N/A; inheritance alone does not generate this forwarding interface | Partial: Boost.StrongTypedef<br>➖ different wrapper/conversion semantics | N/A | N/A |
| `ZuPP` macro family<br>➕ deferred recursive expansion, tuple manipulation and argument mapping | N/A; the preprocessor supplies primitives, not these macro algorithms | Boost.Preprocessor<br>➕ broader documented macro algorithms | Partial: [`FOLLY_PP_*`](https://github.com/facebook/folly/blob/main/folly/Preprocessor.h)<br>➖ different macro vocabulary | Partial: [`BSLS_MACROREPEAT`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_macrorepeat.h)<br>➖ repetition subset, not the complete Z preprocessor vocabulary |
| `ZuIfReserved`<br>➕ branches on C++23 keyword tokens | N/A | Partial: Boost.Preprocessor token dispatch; custom keyword table | N/A | N/A |
| `ZuFnName` | `std::source_location::function_name()` | `BOOST_CURRENT_FUNCTION` | N/A | N/A |

## Object storage and lifetime operations

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuLaunder` | `std::launder` | `boost::core::launder` | N/A |
| `ZuAddr` | `std::addressof` | `boost::addressof` | [`bsl::addressof`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_memory.h) |
| `ZuNew` | `std::construct_at` | Partial: placement `new`; Boost.Container allocator construction | Partial: [`bslma::ConstructionUtil::construct`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_constructionutil.h)<br>➕ allocator-aware construction<br>➖ different explicit-storage interface |
| `ZuCast` | `std::bit_cast` | `boost::core::bit_cast` | N/A |
| `ZuPun`<br>➕ reusable union with forwarding input construction and suppressed member destruction<br>➖ caller manages active-member lifetime and aliasing | Partial: `std::bit_cast`<br>➕ defined value conversion for eligible types<br>➖ does not provide shared storage or lifetime control | Partial: `boost::core::bit_cast` | N/A |
| `ZuAlloca`<br>➕ concise aligned stack allocation with runtime size<br>➖ no stack-capacity fallback | N/A; runtime-sized stack arrays are not a core C++23 facility | N/A; platform `alloca` remains nonstandard | N/A |
| `ZuClear`<br>➕ OS-backed clearing resistant to dead-store elimination | N/A in C++23; `std::memset` does not guarantee secure erasure | N/A; ordinary fill algorithms do not guarantee secure erasure | N/A |
| `ZuCanOverlap`<br>➕ shorter framework spelling of the standard attribute<br>➖ naming alias only; identical attribute semantics | N/A; exact alias of core [[no_unique_address]] | `BOOST_ATTRIBUTE_NO_UNIQUE_ADDRESS` | N/A |

## Array element storage and access

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuElem`<br>➕ aligned structural union with no initial active element and explicit lifetime control<br>➕ short underlying type ID limits symbol/debug-name expansion | Partial: `std::construct_at`<br>➖ caller must supply suitable storage and lifetime management | Partial: Boost.Optional storage machinery; no matching public wrapper | Partial: [`bsls::ObjectBuffer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_objectbuffer.h)<br>➖ aligned raw storage, not Z structural union/type-ID representation |
| `ZuIsElem` | N/A; custom-wrapper type trait | N/A; custom-wrapper type trait | N/A |
| `ZuElemType` | N/A; custom-wrapper unwrapping trait | N/A; custom-wrapper unwrapping trait | N/A |
| `ZuElemVal` | N/A; conditional wrapper-value access | N/A; custom wrapper accessor | N/A |

## Primitive numeric types and traits

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `int128_t`, `uint128_t` (`ZuInt.hh`)<br>➕ native compiler integer operations on supported targets<br>➖ compiler-extension types | N/A; no required standard 128-bit integer | `boost::multiprecision::int128_t`, `uint128_t`<br>➕ portable fixed-width backends | [`folly::int128_t / uint128_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | N/A |
| `ZuFP`<br>➕ IEEE representation and precision helpers | `std::numeric_limits`, `<cmath>` | Boost.Math floating-point utilities | N/A | Partial: [`bdlb::Float`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h)<br>➖ floating classification rather than the full Z representation/precision vocabulary |

## Checked arithmetic and floating-point intrinsics

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Zu_add` / `ZuIntrin::add` | N/A in C++23; checked-addition composition | Partial: Boost.SafeNumerics<br>➕ broader checked-number policies | Partial: [`folly::checked_add`](https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h)<br>➖ reports success rather than Z overflow-flag semantics | N/A |
| `Zu_sub` / `ZuIntrin::sub` | N/A in C++23; checked-subtraction composition | Partial: Boost.SafeNumerics | N/A | N/A |
| `Zu_mul` / `ZuIntrin::mul` | N/A in C++23; checked-multiplication composition | Partial: Boost.SafeNumerics | Partial: [`folly::checked_mul`](https://github.com/facebook/folly/blob/main/folly/lang/CheckedMath.h)<br>➖ reports success rather than Z overflow-flag semantics | N/A |
| `Zu_nan*` / `ZuIntrin::nan` | `std::numeric_limits<T>::quiet_NaN`, `std::nan` | Boost.Math floating-point utilities | N/A | N/A |
| `Zu_isnan` / `ZuIntrin::isnan` | `std::isnan` | `boost::math::isnan` | N/A | [`bdlb::Float::isNan`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_float.h) |

## Byte order and byte swapping

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuByteSwap`<br>➕ swapped-storage wrapper | Partial: `std::byteswap` (C++23)<br>➖ operation rather than storage wrapper | Boost.Endian arithmetic/buffer types | Partial: [`folly::Endian::swap`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | [`bsls::ByteOrderUtil::swapBytes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h) |
| `ZuBigEndian` | Partial: `std::endian` + `std::byteswap` | Boost.Endian big-endian types | Partial: [`folly::Endian::big`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | Partial: [`bdlb::BigEndianInt16 / Int32 / Int64`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bigendian.h)<br>➖ specific integer wrappers rather than the full Z template vocabulary |
| `ZuLittleEndian` | Partial: `std::endian` + `std::byteswap` | Boost.Endian little-endian types | Partial: [`folly::Endian::little`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ conversion operation, not endian-storage wrapper | Partial: [`BSLS_BYTEORDER_LE_* conversion macros`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorder.h)<br>➖ conversion only, not little-endian storage wrappers |
| `Zu_bswap*` (`ZuIntrin.hh`) | `std::byteswap` (C++23) | `boost::endian::endian_reverse` | [`folly::Endian::swap`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h) | [`bsls::ByteOrderUtil::swapBytes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_byteorderutil.h) |

## I/O status and demangling

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Zu::IOResult`<br>➕ small normalized I/O status vocabulary | Partial: `std::error_code`, stream state flags | Partial: Boost.System / Asio error codes | N/A | N/A |
| `ZuDemangle` | N/A; `typeid(T).name()` does not guarantee readable text | `boost::core::demangle`, Boost.TypeIndex | [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h) | Partial: [`bsls::NameOf / nameOfType`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_nameof.h)<br>➖ compile-time type input, not arbitrary mangled-symbol decoding |

## C interfaces

| Z facility | STL | Boost |
| --- | --- | --- |
| `zu_bitmap.h` C API | N/A; `std::bitset` is C++ only | N/A; Boost bitsets are C++ only |
| `zu_decimal.h` C API | N/A | N/A; Boost.Multiprecision is C++ only |
| `zu_time.h` C API | Partial: C `<time.h>` | N/A; Boost time libraries are C++ only |

## Callable construction and invocation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuDefaultAxor` | `std::identity` | `boost::hana::id` | N/A | N/A |
| `ZuLambda`<br>➕ recursive callable wrapper with cv/ref-sensitive SFINAE overloads<br>➕ documented workaround for GCC explicit-object-parameter issue 113563 | N/A; explicit-object lambda syntax supplies recursion, not this wrapper interface | `boost::hof::fix` | N/A | N/A |
| `ZuDeduce`<br>➕ extracts object, return type and argument typelist | Partial: `std::invoke_result_t`<br>➖ requires supplied argument types; no full signature decomposition | Boost.CallableTraits<br>➕ broader callable-type transformations | N/A | N/A |
| `ZuInvoke`<br>➕ accepts member, bound and unbound function shapes with optional context | Partial: `std::invoke`<br>➖ does not discard an extra context argument automatically | Partial: `boost::compat::invoke` | Partial: [`folly::invoke`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ does not automatically discard an extra context argument | Partial: [`bsl::invoke`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ Z callable/context adaptation needs additional code |
| `ZuInvokeFnT` | N/A; construct a function-pointer type from a callable signature | Partial: Boost.CallableTraits function-type transformation | N/A | Partial: [`bsl::invoke`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ Z callable/context adaptation needs additional code |
| `ZuInvokeFn`<br>➕ concise named function-pointer conversion with signature deduction from lambda traits<br>➖ conversion semantics are already supplied by captureless lambdas | N/A; core lambda conversion supplies the underlying operation | Partial: Boost.TypeTraits-selected function-pointer conversion | N/A | Partial: [`bsl::invoke`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ Z callable/context adaptation needs additional code |
| `ZuInvokeLambda`<br>➕ type-only invocation with an explicit statelessness check and argument-list traits<br>➖ core construction/call already covers captureless lambdas<br>➖ placeholder-this implementation relies on supported compiler behavior | N/A; core captureless-lambda construction/call lacks the explicit statelessness check | Partial: `boost::hof::static_` | N/A | Partial: [`bsl::invoke`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_functional.h)<br>➖ Z callable/context adaptation needs additional code |

## Lambda traits and constraints

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuLambdaTraits`<br>➕ generic-lambda invocation and capture-oriented traits | Partial: `std::is_invocable`, `std::invoke_result`, type traits | Partial: Boost.CallableTraits, TypeTraits | Partial: [`folly::invoke_traits`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ invocation traits rather than full lambda argument/capture decomposition | Partial: [`bslmf::MemberFunctionPointerTraits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h)<br>➖ inspect operator() explicitly; overloaded/generic callables need adaptation |
| `ZuArgList` | N/A; signature decomposition requires custom traits | `boost::callable_traits::args_t` (partial for generic lambdas) | N/A | Partial: [`bslmf::MemberFunctionPointerTraits`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_memberfunctionpointertraits.h)<br>➖ inspect operator() explicitly; overloaded/generic callables need adaptation |
| `ZuLambdaReturn` | `std::invoke_result_t` with supplied argument types | `boost::callable_traits::return_type_t` (partial for generic lambdas) | Partial: [`folly::invoke_result_t`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h) | [`bsl::invoke_result`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_invokeresult.h) |
| `ZuIsMutableLambda` | Partial: test invocability through a const callable reference | Partial: Boost.TypeTraits / CallableTraits composition | N/A | N/A |
| `ZuIsVoidRetLambda` | `std::is_void<std::invoke_result_t<...>>` | Partial: Boost.TypeTraits + CallableTraits | Partial: [`folly::invoke_result_t`](https://github.com/facebook/folly/blob/main/folly/functional/Invoke.h)<br>➖ needs a separate void-type test | Partial: [`bsl::invoke_result + is_void`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_invokeresult.h)<br>➖ separate result-type predicate required |
| `ZuIsStatelessLambda`<br>➕ tests function-pointer conversion for the selected signature | Partial: `std::is_convertible` to the selected function pointer<br>➖ `std::is_empty` alone is not an equivalent test | Partial: `boost::is_convertible` to the selected function pointer | N/A | N/A |
| `ZuMutableLambda` | Constraint on const-callability test | Partial: Boost.EnableIf + callable traits | N/A | N/A |
| `ZuNotMutableLambda` | Negated const-callability constraint | Partial: Boost.DisableIf + callable traits | N/A | N/A |
| `ZuVoidRetLambda` | Constraint on void invocation result | Partial: Boost.EnableIf + result-type traits | N/A | N/A |
| `ZuNotVoidRetLambda` | Constraint on non-void invocation result | Partial: Boost.DisableIf + result-type traits | N/A | N/A |
| `ZuStatelessLambda` | Constraint on function-pointer convertibility | Partial: Boost.EnableIf + conversion traits | N/A | N/A |
| `ZuNotStatelessLambda` | Negated function-pointer-conversion constraint | Partial: Boost.DisableIf + conversion traits | N/A | N/A |

## Compile-time switch and unroll

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSwitch`<br>➕ generates dispatch cases from a count or sequence, passing a compile-time index<br>➕ supports result collection and a caller-supplied default value | N/A; a switch statement does not generate its own cases | `boost::mp11::mp_with_index` |
| `ZuUnroll`<br>➕ generates ordered calls from a count, sequence or type list<br>➕ shared API supports result collection and seeded reduction | N/A; pack expansion requires pack extraction/generation and call adaptation | `boost::mp11::mp_for_each`, `boost::hana::for_each` |

## Constant sequences and dispatch

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuSeq` | `std::integer_sequence<unsigned, ...>` | `boost::mp11::mp_list_c<unsigned, ...>` | Partial: [`folly::vtag_t<I...>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ value-list tag with different operations | [`bslmf::IntegerSequence<unsigned, ...>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_integersequence.h) |
| `ZuMkSeq` | `std::make_integer_sequence<unsigned, N>` | `boost::mp11::mp_iota_c` with value-type adapter | N/A | [`bslmf::MakeIntegerSequence<unsigned, N>`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_makeintegersequence.h) |
| `ZuMin` | Partial: constexpr `std::min` over constant values | `boost::mp11::mp_min` | Partial: [`folly::constexpr_min`](https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h)<br>➖ value reduction rather than ZuSeq metafunction | Partial: [`bsl::min`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)<br>➖ value reduction rather than ZuSeq metafunction |
| `ZuMax` | Partial: constexpr `std::max` over constant values | `boost::mp11::mp_max` | Partial: [`folly::constexpr_max`](https://github.com/facebook/folly/blob/main/folly/ConstexprMath.h)<br>➖ value reduction rather than ZuSeq metafunction | Partial: [`bsl::max`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)<br>➖ value reduction rather than ZuSeq metafunction |
| `ZuSeqCall` | Partial: `std::apply` with index/element adaptation | Partial: `boost::hana::unpack` | Partial: [`folly::apply`](https://github.com/facebook/folly/blob/main/folly/functional/ApplyTuple.h)<br>➖ no Z accessor/index adaptation | Partial: [`bsl::apply`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h)<br>➖ requires index/element adaptation |

## Constant-sequence conversions

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZuSeqBitmap`<br>➕ extracts sequence indices and combines their bits into a constexpr uint64_t mask | N/A; a fold expression requires a separately unpacked index pack | Partial: Boost.Mp11 fold |
| `ZuBitmapSeq`<br>➕ generates an index-sequence type from the set bits of a constant mask | N/A; integer bit operations do not generate a sequence type | Partial: Boost.Mp11 filtering of bit indices |
| `ZuSeqTL` | N/A; convert an integer sequence into constant types | `boost::mp11::mp_from_sequence` |
| `ZuTLSeq` | N/A; unpack constant types into an integer sequence | Partial: Boost.Mp11 pack adapter |
| `ZuSeqString`<br>➕ extracts sequence values and builds a structural ZuString with a trailing terminator | Partial: `std::array<char, N>` with a custom sequence-to-string adapter | Partial: `boost::hana::string` |
| `ZuStringSeq` | N/A; string-to-integer-pack adapter | Partial: `boost::hana::unpack` on a Hana string |
| `ZuSeqArray`<br>➕ extracts sequence values into a structural array of the selected element type | Partial: `std::array` with a custom sequence-conversion adapter | Partial: `boost::hana::unpack` into an array |
| `ZuArraySeq` | N/A; structural-array-to-integer-pack adapter | Partial: Boost.Hana sequence conversion |

## Type-list construction, access and slicing

Sources: [ZuTL.hh](../zu/src/ZuTL.hh), [ZuSeq.hh](../zu/src/ZuSeq.hh),
[ZuString.hh](../zu/src/ZuString.hh).
Boost counterparts: [Mp11 reference](https://www.boost.org/doc/libs/latest/libs/mp11/doc/html/mp11.html).
Composed operations below require a small adapter where the list/value representation differs.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuTypeList`<br>➕ type-only list with nested operations | Partial: `std::tuple` as a type carrier | `boost::mp11::mp_list` | Partial: [`folly::tag_t<T...>`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ lacks ZuTypeList nested push/unshift interface | Partial: [`bslmf::TypeList`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_typelist.h)<br>➖ up to 20 types; no matching nested push/unshift vocabulary |
| `ZuTypeRepeat`<br>➕ generates an N-element type list from one type, including an empty result | N/A; parameter-pack syntax does not synthesize repeated types by itself | `boost::mp11::mp_repeat_c` on a singleton list | N/A | N/A |
| `ZuType` | `std::tuple_element_t` on a tuple | `boost::mp11::mp_at_c` | [`folly::type_pack_element_t / type_list_element_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) | Partial: [`bslmf::TypeListTypeOf`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmf/bslmf_typelist.h)<br>➖ accepts the BDE typelist representation |
| `ZuTypeIndex` | N/A; compile-time type search | `boost::mp11::mp_find` | N/A | N/A |
| `ZuTypeIn`<br>➕ membership test accepts a packed list or raw type arguments and handles empty input | Composition: `std::is_same` with custom list unpacking and reduction | `boost::mp11::mp_contains` | Partial: [`folly::is_one_of_v`](https://github.com/facebook/folly/blob/main/folly/Traits.h)<br>➖ packed-list input needs adaptation | N/A |
| `ZuTypeHead` | N/A; prefix index-sequence adapter | `boost::mp11::mp_take_c` | N/A | N/A |
| `ZuTypeTail` | N/A; suffix index-sequence adapter | `boost::mp11::mp_drop_c` | N/A | N/A |
| `ZuTypeSlice`<br>➕ stride/offset selection | N/A; index-sequence selection | Partial: `boost::mp11::mp_transform` over selected indices | N/A | N/A |
| `ZuTypeApply`<br>➕ applies an existing packed type list or raw type arguments to a target template | N/A; pack expansion does not unpack a type-list object/type by itself | `boost::mp11::mp_rename` / `mp_apply` | N/A | N/A |

## Type-list algorithms

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| `ZuTypeRev` | N/A; reversed index-sequence adapter | `boost::mp11::mp_reverse` | N/A |
| `ZuTypeMap`<br>➕ transforms a packed type list or raw type arguments and rebuilds the resulting list | N/A; requires a custom list-unpacking and transformation adapter | `boost::mp11::mp_transform` | N/A |
| `ZuTypeGrep` | N/A; compile-time type filtering | `boost::mp11::mp_copy_if` | N/A |
| `ZuTypeReduce`<br>➕ recursive pairwise type reduction with empty/singleton cases and packed-list input | N/A; ordinary fold expressions do not directly reduce type metafunctions | Partial: `boost::mp11::mp_fold` | N/A |
| `ZuTypeMerge` | N/A; compile-time merge | Partial: `boost::mpl::merge` | N/A |
| `ZuTypeSort` | N/A; compile-time type sorting | `boost::mp11::mp_sort` with comparator adapter | N/A |
| `ZuTypeConcat` | Partial: result type of `std::tuple_cat` | `boost::mp11::mp_append` | [`folly::type_list_concat_t`](https://github.com/facebook/folly/blob/main/folly/Traits.h) |
| `ZuTypeUnique` | N/A; compile-time type deduplication | `boost::mp11::mp_unique` | N/A |
| `ZuTLConverts` | Composition: zipped `std::is_convertible` tests | Composition: `boost::mp11::mp_transform` + `mp_all` | N/A |
| `ZuTLConstructs` | Composition: zipped `std::is_constructible` tests | Composition: `boost::mp11::mp_transform` + `mp_all` | N/A |

## Compile-time strings

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| `ZuString`<br>➕ structural string for non-type template arguments | Partial: `std::array<char, N>`<br>➖ no dedicated structural string class | Partial: `boost::hana::string`<br>➖ different template representation | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h)<br>➖ different template-argument/structural-type contract |
| `ZuStringT` | Partial: `std::integral_constant` of a structural string | Partial: `boost::hana::string` as a type-level string | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h)<br>➖ value container rather than ZuStringT type-level wrapper |
| `ZuStringTL` | N/A; type list of structural-string constants | Partial: Boost.Mp11 list of Hana strings | N/A |

## Ownership and scope cleanup

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuObject`<br>➕ embedded non-atomic count<br>➖ participating base class required | Partial: `std::enable_shared_from_this` + shared ownership<br>➕ supports weak observers | `boost::intrusive_ref_counter<T, boost::thread_unsafe_counter>` | N/A | N/A |
| `ZuObjectTraits`<br>➕ detects Z intrusive-object participation | N/A; custom type traits | Partial: detection of intrusive pointer hooks | N/A | N/A |
| `ZuRef`<br>➕ intrusive ownership without separate control block<br>➖ non-atomic; no weak observers | `std::shared_ptr`<br>➕ arbitrary pointees and weak observers | `boost::intrusive_ptr` with non-atomic hooks | N/A | N/A |
| `ZuPolymorph` | Partial: polymorphic base with `std::shared_ptr` | Partial: polymorphic base with Boost intrusive hooks | N/A | N/A |
| `ZuPtr`<br>➖ no configurable deleter | `std::unique_ptr`<br>➕ custom deleters and array specialization | `boost::movelib::unique_ptr` | Partial: [`folly::static_function_deleter`](https://github.com/facebook/folly/blob/main/folly/Memory.h)<br>➖ a deleter policy used with unique_ptr, not an owning pointer by itself | Partial: [`bslma::ManagedPtr`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_managedptr.h)<br>➕ type-erased deleter and allocator-aware factories<br>➖ different transfer/ownership interface |
| `ZuGuard`<br>➕ cancellable lambda cleanup | N/A in C++23; `std::experimental::scope_exit` (Library Fundamentals TS) | `boost::scope::scope_exit`<br>➕ related success/failure guards | [`folly::ScopeGuard / makeGuard`](https://github.com/facebook/folly/blob/main/folly/ScopeGuard.h)<br>➕ also supplies success/failure scope guards | [`bdlb::ScopeExit / ScopeExitUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_scopeexit.h) |

## Comparison, hashing and null sentinels

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuCmp`<br>➕ comparison, null sentinel and numeric bounds in one customization | Partial: `std::compare_three_way`, `std::numeric_limits`, `std::optional` | Partial: Boost.Operators, NumericConversion, Optional | N/A | N/A |
| `ZuCmp0`<br>➕ zero-sentinel comparison policy | Partial: comparison functions + zero test | Partial: Boost.Operators + custom sentinel policy | N/A | N/A |
| `ZuCmp_1`<br>➕ negative-value null policy | N/A; custom negative-sentinel comparison policy | N/A; custom sentinel policy | N/A | N/A |
| `ZuCmpN`<br>➕ compile-time chosen null sentinel | N/A; custom sentinel comparison policy | N/A; custom sentinel policy | N/A | N/A |
| `ZuCompare` | Partial: `std::compare_three_way`<br>➖ returns an ordering category rather than an integer | Partial: Boost comparison utilities with integer-result adapter | N/A | Partial: [`bsl::compare_three_way`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_compare.h)<br>➖ ordering category rather than integer result |
| `ZuEquals` | `std::equal_to` | `boost::equal_to` (Functional) | N/A | [`bsl::equal_to`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_equalto.h) |
| `ZuNull`<br>➕ tests a type's configured sentinel | Partial: `std::optional::has_value`<br>➕ does not consume a payload value as a sentinel | Partial: `boost::optional` engagement test | N/A | N/A |
| `ZuNullRef`<br>➕ reusable const-reference sentinel accessor honoring the type-specific comparator | N/A; a static variable alone does not select the configured sentinel | N/A; application static sentinel | N/A | N/A |
| `ZuICmp` | Partial: character folding + comparison algorithms | `boost::algorithm::iequals` / `ilexicographical_compare` | Partial: [`folly::AsciiCaseInsensitive`](https://github.com/facebook/folly/blob/main/folly/Range.h)<br>➖ comparator used with range search; not the full ZuCmp interface | Partial: [`bdlb::String::areEqualCaseless / lowerCaseCmp`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h)<br>➖ text operations rather than the complete Z comparator interface |
| `ZuHash`<br>➕ consistent text hashing across supported representations | `std::hash` | `boost::hash` (ContainerHash) | [`folly::hasher / folly::hash`](https://github.com/facebook/folly/blob/main/folly/hash/Hash.h)<br>➕ multiple hash families and combination helpers | [`bslh::Hash / hashAppend`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslh/bslh_hash.h)<br>➕ extensible hashing protocol and selectable hash algorithms<br>➖ cross-representation text normalization needs adaptation |

## Bitmaps, bitfields and bit streams

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuBitmap` | `std::bitset` | Partial: `boost::dynamic_bitset`<br>➕ runtime size<br>➖ dynamic allocation | N/A | Partial: [`bdlc::BitArray`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h)<br>➖ dynamic bit array rather than fixed Z bitmap/layout |
| `ZuBitfield`<br>➕ runtime-indexed packed fields with explicit word/mask operations and assignment proxies | N/A; native named bitfields do not supply indexed packed-array access | Partial: Boost.Endian buffers with masks | N/A | N/A |
| `ZuBitStream`<br>➕ successive non-byte-aligned symbols | N/A; custom bit packing | Partial: Boost.DynamicBitset with custom cursor | N/A | N/A |
| `Zu_clz*` (`ZuIntrin.hh`) | `std::countl_zero` | Partial: Boost.Core bit utilities | Partial: [`folly::findLastSet`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ convert its one-based result to a leading-zero count | [`bdlb::BitUtil::numLeadingUnsetBits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h) |
| `Zu_ctz*` (`ZuIntrin.hh`) | `std::countr_zero` | Partial: Boost.Core bit utilities | Partial: [`folly::findFirstSet`](https://github.com/facebook/folly/blob/main/folly/lang/Bits.h)<br>➖ one-based position; zero-input convention differs | [`bdlb::BitUtil::numTrailingUnsetBits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_bitutil.h) |

## Boxed scalars and formatting adapters

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuBox`<br>➕ scalar null policy with integrated scanning/formatting | Partial: scalar + `std::optional` + character conversion | Partial: Boost.Optional + Charconv / LexicalCast | Partial: [`folly::Optional`](https://github.com/facebook/folly/blob/main/folly/Optional.h)<br>➖ separate engagement state; no integrated numeric formatting policy | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h)<br>➖ separate null state; no matching sentinel/numeric formatting policy |
| `ZuBoxed`<br>➕ scalar-reference formatting adapter | Partial: `std::format` argument / stream insertion | Partial: Boost.Format argument | N/A | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h)<br>➖ separate null state; no matching sentinel/numeric formatting policy |
| `ZuNBox`<br>➕ boxed scalar with null-string formatting policy | Partial: `std::optional` + custom formatter | Partial: Boost.Optional + custom formatter | Partial: [`folly::Optional`](https://github.com/facebook/folly/blob/main/folly/Optional.h)<br>➖ separate engagement state; no integrated numeric formatting policy | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h)<br>➖ separate null state; no matching sentinel/numeric formatting policy |
| `ZuNBoxed` | Partial: scalar-reference adapter with custom null formatting | Partial: Boost.Format argument adapter | N/A | Partial: [`bdlb::NullableValue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_nullablevalue.h)<br>➖ separate null state; no matching sentinel/numeric formatting policy |

## Arrays, spans and string interoperability

Sources: [public headers](../zu/src/), [module guide](../zu/README.md).
Relevant counterparts: [standard containers](https://eel.is/c++draft/containers),
[Boost.SmartPtr](https://www.boost.org/doc/libs/latest/libs/smart_ptr/doc/html/smart_ptr.html),
[Boost.Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuSpan`<br>➕ integrated compare/hash, slicing and text searching | `std::span`<br>➕ standard ranges interoperability and static extent | `boost::span` | [`folly::Range`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_span.h) |
| `ZuCSpan` | `std::string_view` | `boost::string_view` | [`folly::StringPiece`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::string_view`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_stringview.h) |
| `ZuWSpan` | `std::wstring_view` | `boost::wstring_view` | [`folly::Range<const wchar_t*>`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::wstring_view`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_stringview.h) |
| `ZuBSpan` | `std::span<const uint8_t>` / `std::span<const std::byte>` | `boost::span<const unsigned char>` | [`folly::ByteRange`](https://github.com/facebook/folly/blob/main/folly/Range.h) | [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_span.h) |
| `ZuArray`<br>➕ inline capacity with logical length; structural representation | Partial: `std::array`<br>➖ no logical length; `std::inplace_vector` is C++26 | `boost::container::static_vector`<br>➖ different structural/NTTP contract | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ different capacity/growth and structural-type contract | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_array.h)<br>➖ fixed extent rather than Z capacity plus logical length |
| `ZuCArray` | Partial: `std::array<char, N>` with length | `boost::static_string` | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_array.h)<br>➖ no character-string length/terminator interface |
| `ZuWArray` | Partial: `std::array<wchar_t, N>` with length | `boost::static_strings::basic_static_string<N, wchar_t>` | Partial: [`folly::BasicFixedString<wchar_t, N>`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | Partial: [`bsl::array`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_array.h)<br>➖ no character-string length/terminator interface |
| `ZuMArray`<br>➕ writable transformed-element proxies | Partial: `std::views::transform`<br>➖ inverse assignment needs custom proxy support | Partial: Boost.Iterator transform iterator | N/A | Partial: [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_span.h)<br>➖ view only; Z ownership/length policies need adaptation |
| `ZuVArray`<br>➕ type-erased array interface across compiled boundaries | N/A; `std::span` only covers contiguous homogeneous storage | Partial: Boost.Range any-range | N/A | Partial: [`bsl::span`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_span.h)<br>➖ view only; Z ownership/length policies need adaptation |
| `ZuStdString` integration | `std::basic_string` used directly | Partial: Boost string/range interoperability | N/A | Partial: [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_string.h)<br>➕ bslma allocator integration<br>➖ Z trait/printing adapters are separate |

## Tuples, unions and empty values

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuTuple`<br>➕ size-ordered flat storage and positional mutation<br>➖ incomplete `std::tuple` algorithm interoperability | `std::tuple`, `std::pair` | `boost::tuple`, `boost::hana::tuple` | N/A | Partial: [`bsl::tuple`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_tuple.h)<br>➖ does not supply Z structural/ADL metadata |
| `ZuUnion`<br>➕ void alternatives and explicit nonthrowing dispatch<br>➖ callers must respect active-alternative access contract | `std::variant`<br>➕ checked `get`/`get_if` and standard visitation | `boost::variant2::variant`<br>➕ never-valueless value semantics | Partial: [`folly::Expected<T, E>`](https://github.com/facebook/folly/blob/main/folly/Expected.h)<br>➖ value/error pair, not an arbitrary multi-alternative union | Partial: [`bdlb::Variant`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_variant.h)<br>➖ different alternative limits, storage and visitation contract |
| `ZuVoid` | `std::monostate` | `boost::blank` | N/A | Partial: [`bsl::monostate`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_monostate.h)<br>➖ no matching generic absorbing operations |

## Array algorithms, iterators, sorting and searching

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuArrayFn`<br>➕ trait-selected bulk initialization, movement and comparison | `<algorithm>`, uninitialized-memory algorithms | Partial: Boost.Container / Move algorithms | N/A |
| `ZuStringFn` | `std::char_traits`, C string functions | Partial: Boost.StringAlgo | N/A |
| `ZuIter`<br>➕ reusable random-access iterator boilerplate | Partial: iterator concepts and custom iterator implementation | `boost::iterator_facade` | N/A |
| `ZuSort`<br>➕ three-way comparison and Z bulk-element operations<br>➖ custom quicksort lacks `std::sort`'s mandated worst-case bound | `std::sort` | Boost.Sort | [`bsl::sort`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h) |
| `ZuSearch`<br>➕ packs found flag and insertion position into one result | `std::lower_bound` plus equality test | Boost.Range `lower_bound` plus equality test | Partial: [`bsl::lower_bound / binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)<br>➖ Z result and accessor conventions need adaptation |
| `ZuSearchFound` | Partial: equality test after `std::lower_bound` | Partial: equality test after Boost.Range `lower_bound` | Partial: [`bsl::lower_bound / binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)<br>➖ Z result and accessor conventions need adaptation |
| `ZuSearchPos` | Partial: iterator distance from `std::lower_bound` | Partial: iterator distance from Boost.Range `lower_bound` | Partial: [`bsl::lower_bound / binary_search`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_algorithm.h)<br>➖ Z result and accessor conventions need adaptation |
| `ZuInterSearch`<br>➕ interpolation search for suitably distributed keys<br>➖ behavior depends on key distribution | Partial: `std::lower_bound`<br>➕ logarithmic comparisons independent of distribution | Partial: Boost.Range `lower_bound` | N/A |

## Text tokenization

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuTokenizer::Delimited`<br>➕ borrowed tokens without allocation | `std::views::split` | `boost::tokenizer`, `boost::split_iterator` | [`folly::splitStep / splitTo`](https://github.com/facebook/folly/blob/main/folly/String.h) | Partial: [`bdlb::Tokenizer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h)<br>➖ delimiter/soft-delimiter token semantics differ |
| `ZuTokenizer::WhiteSpace`<br>➕ simple ASCII whitespace scanning | Partial: `std::views::split` / custom delimiter predicate | `boost::tokenizer` with character separator | Partial: [`folly::splitTo`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ whitespace-token policy needs adaptation | Partial: [`bdlb::Tokenizer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_tokenizer.h)<br>➖ delimiter/soft-delimiter token semantics differ |

## Numeric input

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Zu_nscan` / numeric input (`Zu_aton.hh`)<br>➕ numeric scanning with Z-format policies | `std::from_chars` | Boost.Charconv `from_chars` | Partial: [`folly::to / tryTo`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h)<br>➖ different remainder, error and formatting-policy contract |
| `Zu_atou` | `std::from_chars` for unsigned integers | Boost.Charconv `from_chars` | Partial: [`folly::tryTo<unsigned>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h)<br>➖ different remainder, error and formatting-policy contract |
| `Zu_atoi` | `std::from_chars` for signed integers | Boost.Charconv `from_chars` | Partial: [`folly::tryTo<signed integer>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h)<br>➖ different remainder, error and formatting-policy contract |
| `Zu_atof` | `std::from_chars` for floating point | Boost.Charconv `from_chars` | Partial: [`folly::tryTo<floating point>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ different formatting/error contract | Partial: [`bdlb::NumericParseUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_numericparseutil.h)<br>➖ different remainder, error and formatting-policy contract |

## Numeric output and buffer sizing

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Zu_nprint` / numeric output (`Zu_ntoa.hh`)<br>➕ compile-time numeric-format policies | `std::to_chars` | Boost.Charconv `to_chars` | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_utoa` | `std::to_chars` for unsigned integers | Boost.Charconv `to_chars` | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_itoa` | `std::to_chars` for signed integers | Boost.Charconv `to_chars` | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_ftoa`<br>➖ no automatic scientific-notation fallback | `std::to_chars` for floating point<br>➕ general, fixed, scientific and hexadecimal modes | Boost.Charconv `to_chars` | Partial: [`folly::toAppend`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-appending API rather than Z numeric buffer/format policy | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_ulen`<br>➕ constexpr unsigned-output buffer sizing | Partial: `std::numeric_limits` and digit-count calculation | Partial: Boost.Integer / Charconv sizing composition | N/A | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_ilen`<br>➕ constexpr signed-output buffer sizing | Partial: `std::numeric_limits` and digit-count calculation | Partial: Boost.Integer / Charconv sizing composition | N/A | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |
| `Zu_flen`<br>➕ format-aware floating-output buffer sizing | N/A; size according to selected conversion format | Partial: Boost.Charconv with caller-selected capacity | N/A | Partial: [`bsl::to_chars`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_charconv.h)<br>➖ formatting policy and length-query interfaces differ |

## Formatting, streams and joining

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuFmt`<br>➕ formatting policies encoded in types | `std::format`, stream manipulators | Boost.Format / Convert | N/A | Partial: [`bsl::format`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslfmt/bslfmt_format.h)<br>➖ format-string vocabulary rather than Z compile-time format policy |
| `ZuPrint`<br>➕ ADL print customization shared by Z streams | Partial: `std::formatter`, `operator<<` | Partial: Boost.Format streamable types | Partial: [`folly::toAppend customization`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ string-conversion customization rather than Z stream ADL | Partial: [`bslim::Printer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_printer.h)<br>➖ structured ostream printing rather than Z stream ADL |
| `ZuStream`<br>➕ direct output into caller-supplied bounded text storage | `std::ospanstream` (C++23) | Boost.Iostreams array sink | Partial: [`folly::io::Appender`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h)<br>➖ IOBuf-chain output, not a bounded character-span stream | Partial: [`bdlsb::FixedMemOutput`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlsb/bdlsb_fixedmemoutput.h)<br>➖ streambuf contract rather than Z span/ADL stream |
| `ZuVStream`<br>➕ type-erased stream interface with function pointers | Partial: `std::ostream` / `std::streambuf` | Partial: Boost.Iostreams `stream` / filtering stream | N/A | Partial: [`bsl::ostream`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsl+bslhdrs/bsl_ostream.h)<br>➖ runtime stream interface differs |
| `ZuJoin`<br>➕ streams elements directly with a delimiter | Partial: `std::views::join_with` (C++23)<br>➖ range joining is not a formatting adapter | `boost::algorithm::join`<br>➖ normally builds a result string | Partial: [`folly::join`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ normally appends to an owning string rather than an arbitrary Z stream | N/A |

## Time, identifiers and versions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuTime`<br>➕ compact seconds/nanoseconds with null sentinel<br>➖ same type represents instants and intervals | `std::chrono::duration`, `std::chrono::time_point`<br>➕ distinct clock and duration types | Boost.Chrono / DateTime | Partial: [`folly::to<chrono duration> / timespec conversions`](https://github.com/facebook/folly/blob/main/folly/chrono/Conv.h)<br>➖ no unified Z instant/interval/sentinel type | Partial: [`bsls::TimeInterval`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_timeinterval.h)<br>➖ no matching Z null-sentinel/instant conventions |
| `ZuDateTime`<br>➕ wide Julian-calendar date range | `std::chrono` calendar types and formatting (C++20) | Boost.DateTime | N/A | Partial: [`bdlt::Datetime`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlt/bdlt_datetime.h)<br>➕ broad calendar/date utilities<br>➖ representation, range and formatting contracts differ |
| `ZuID`<br>➕ by-value bounded name with ZuArray length, comparison, hashing and printing<br>➖ fixed framework capacity | Partial: `std::string`<br>➖ different storage/capacity contract | `boost::static_string` | Partial: [`folly::FixedString`](https://github.com/facebook/folly/blob/main/folly/FixedString.h) | N/A |
| `ZuSemVer`<br>➕ packed compact version value<br>➖ minor ≤ 99, patch ≤ 999; no prerelease/build metadata | N/A | N/A; `BOOST_VERSION` is a library version macro, not a SemVer value type | N/A | N/A |

## Fixed-point and decimal arithmetic

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuFixed`<br>➕ mantissa plus runtime decimal scale, null/reset sentinels and scan/format conversions<br>➖ constrained precision/range | N/A; integer arithmetic alone supplies none of these decimal-value policies | Partial: Boost.Multiprecision scaled integer composition | Partial: [`bdldfp::Decimal64 / Decimal128`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h)<br>➕ IEEE decimal floating-point arithmetic<br>➖ floating precision/exponent rather than Z fixed-scale arithmetic |
| `ZuDecimal`<br>➕ decimal arithmetic with 18 integer and 18 fractional digits<br>➖ fixed precision/range | N/A in C++23 | Partial: `boost::multiprecision::cpp_dec_float`<br>➕ configurable precision<br>➖ decimal floating point has different rounding/storage semantics | Partial: [`bdldfp::Decimal128`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimal.h)<br>➕ IEEE decimal arithmetic and rounding<br>➖ decimal floating-point rather than Z fixed-point representation |
| `ZuDecimalFn` | N/A; decimal representation helpers | Partial: Boost.Multiprecision arithmetic and conversions | Partial: [`bdldfp::DecimalUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdldfp/bdldfp_decimalutil.h)<br>➖ operations on IEEE decimal types rather than ZuDecimal |

## Unicode conversion and measurement

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuUTF`<br>➕ conversion into caller-owned storage | Partial: locale/codecvt facilities<br>➖ legacy Unicode conversion APIs are deprecated in C++23 | Boost.Locale / Nowide | Partial: [`folly::utf8ToCodePoint / codePointToUtf8`](https://github.com/facebook/folly/blob/main/folly/Unicode.h)<br>➖ lower-level conversion subset rather than the full UTF-8/16/32 adapter | Partial: [`bdlde::CharConvertUtf16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h)<br>➖ buffer/status conventions and conversion surface differ |
| `ZuUTF8`<br>➕ code-point decoding/encoding and input/output length operations for caller buffers | Partial: legacy `std::codecvt_utf8`<br>➖ different stateful facet interface; deprecated in C++23 | Partial: Boost.Locale UTF conversion primitives | [`folly::utf8ToCodePoint / codePointToUtf8`](https://github.com/facebook/folly/blob/main/folly/Unicode.h) | Partial: [`bdlde::Utf8Util`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_utf8util.h)<br>➖ validation/code-point helpers rather than the complete Z codec interface |
| `ZuUTF16`<br>➕ surrogate-aware code-point decoding/encoding with caller-buffer length checks | Partial: legacy `std::codecvt_utf16`<br>➖ different stateful facet interface; deprecated in C++23 | Partial: Boost.Locale UTF conversion primitives | Partial: [`folly::unicode_code_point_from_utf16_surrogate_pair`](https://github.com/facebook/folly/blob/main/folly/Unicode.h)<br>➖ code-point helper, not complete buffer codec | Partial: [`bdlde::CharConvertUtf16`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h)<br>➖ buffer/status conventions and conversion surface differ |
| `ZuUTF32`<br>➕ bounded code-point input/output and Unicode display-width lookup | N/A; char32_t stores a code unit but supplies no width lookup or buffer operations | Partial: Boost.Locale UTF conversion primitives | N/A | [`bdlde::CharConvertUtf32`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf32.h) |
| `ZuUTFSpan`<br>➕ packs input length, output length and display width | N/A; custom encoding measurement record | Partial: Boost.Locale conversion + custom measurement record | N/A | N/A |

## Binary-text and percent encoding

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZuHex`<br>➕ caller-buffer output and in-place decoding | N/A; custom digit conversion | `boost::algorithm::hex` / `unhex` | Partial: [`folly::hexlify / unhexlify`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ different destination/policy interfaces | [`bdlde::HexEncoder / HexDecoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_hexencoder.h)<br>➕ incremental conversion state |
| `ZuBase32`<br>➕ caller-buffer codec | N/A | Partial: Boost.Archive iterator composition | N/A | N/A |
| `ZuBase64`<br>➕ caller-buffer codec | N/A | Partial: Boost.Archive `base64_from_binary` / `binary_from_base64` | [`folly::base64Encode / base64Decode`](https://github.com/facebook/folly/blob/main/folly/base64.h) | [`bdlde::Base64Encoder / Base64Decoder`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h)<br>➕ incremental codec with configurable options |
| `ZuBase64URL`<br>➕ URL-safe alphabet | N/A | Partial: Base64 iterator composition with alphabet adaptation | [`folly::base64URLEncode / base64URLDecode`](https://github.com/facebook/folly/blob/main/folly/base64.h) | Partial: [`bdlde::Base64EncoderOptions::urlSafe + Base64DecoderOptions`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoderoptions.h)<br>➖ distinct codec/options interface |
| `ZuPercent`<br>➕ policy-driven percent encoding, plus/space handling and in-place decoding | N/A | Boost.URL encoding/decoding<br>➕ URL component and grammar vocabulary | Partial: [`folly::uriEscape / uriUnescape`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ different destination/policy interfaces | N/A |

## Testing

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuTest`<br>➕ TAP output and static nested test counts | N/A | Boost.Test<br>➕ broader test framework and reporting options | Partial: [`bslim::TestUtil / BSLIM_TESTUTIL_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)<br>➖ BDE test-driver conventions rather than ZuTest TAP runner |
| `ZuTestUtil` | N/A | Partial: Boost.Test helpers and execution utilities | Partial: [`bslim::TestUtil / BSLIM_TESTUTIL_*`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslim/bslim_testutil.h)<br>➖ BDE test-driver conventions rather than ZuTest TAP runner |

## Structure reflection and facets

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuStruct`<br>➕ field/accessor metadata, facets and composite keys in C++23<br>➕ supports members, methods and lambda-derived fields | N/A in C++23; core C++23 has no structure reflection | Boost.Describe / Hana.Struct<br>➕ Describe also describes bases and member functions | Partial: [`bdlat_SequenceFunctions / bdlat type traits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h)<br>➖ user/generated adaptation instead of Z structural-string field declarations |
| `ZuStructFacet`<br>➕ alternate reflected field views of one type | N/A in C++23 | Partial: separate Hana/Fusion adaptations or metadata types | Partial: [`bdlat_SequenceFunctions / bdlat type traits`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h)<br>➖ user/generated adaptation instead of Z structural-string field declarations |

## ZuMatcher — compile-time multi-string matching

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZuMatcher`<br>➕ compile-time Aho–Corasick for fixed multiple keys<br>➖ large instantiations increase compile cost | Partial: `std::search`, Boyer–Moore searchers<br>➖ single-pattern facilities | Partial: Boost.Algorithm searching / Boost.Spirit symbols | Partial: [`bsl::boyer_moore_horspool_searcher`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_boyermoorehorspoolsearcher.h)<br>➖ runtime single-pattern search rather than compile-time multi-key automaton |

# Zm

## Platform and cache alignment

Sources: [public headers](../zm/src/), [module guide](../zm/README.md).
Standard counterparts: [concurrency library](https://eel.is/c++draft/thread).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmPlatform` primitives<br>➕ OS IDs, aligned allocation and runtime platform helpers | Partial: `std::thread::id`, aligned `operator new` | Partial: Boost.Thread, Align, WinAPI | N/A | Partial: [`BSLS_PLATFORM_* / bsls::Platform`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_platform.h)<br>➖ compile-time platform traits; Z runtime helpers need separate facilities |
| `ZmCacheLineSize` / `ZmCacheAlign`<br>➕ shared target-specific cache-separation policy and matching alignment annotation | Partial: `std::hardware_destructive_interference_size`<br>➖ does not supply Z's selected 128-byte ARM64 / 64-byte other-target policy | Partial: Boost.Align / alignment macros | Partial: [`folly::hardware_destructive_interference_size`](https://github.com/facebook/folly/blob/main/folly/lang/Align.h)<br>➖ uses Folly's own separation policy | Partial: [`bslmt::Platform::e_CACHE_LINE_SIZE`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_platform.h)<br>➖ BDE target policy differs; no matching Z alignment annotation |

## Atomic operations and locks

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmAtomic` | `std::atomic`, atomic fences | Boost.Atomic | Partial: [`folly::relaxed_atomic`](https://github.com/facebook/folly/blob/main/folly/synchronization/RelaxedAtomic.h)<br>➖ relaxed default ordering; not the same complete operation contract | Partial: [`bsls::AtomicInt / AtomicInt64 / AtomicPointer`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_atomic.h)<br>➖ type-specific wrappers rather than the Z template/operation contract |
| `ZmNoLock`<br>➕ compile-time no-op locking policy | N/A; custom null mutex | Partial: Boost.Interprocess `null_mutex` | N/A | N/A |
| `ZmLockTraits` | Partial: Lockable concepts/requirements | Partial: Boost.Thread lockable traits | N/A | N/A |
| `ZmSpinLock`<br>➕ wraps Concurrency Kit FAS spinlock | Partial: `std::atomic_flag` spin loop | Partial: Boost.Atomic spinlock composition | [`folly::MicroSpinLock`](https://github.com/facebook/folly/blob/main/folly/synchronization/MicroSpinLock.h) | [`bsls::SpinLock`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_spinlock.h) |
| `ZmPLock`<br>➕ platform-tuned primitive mutex<br>➖ cannot be used with `ZmCondition` | Partial: `std::mutex` | Partial: `boost::mutex` | Partial: [`folly::DistributedMutex`](https://github.com/facebook/folly/blob/main/folly/synchronization/DistributedMutex.h)<br>➖ different contention/locking implementation | Partial: [`bslmt::QLock`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_qlock.h)<br>➖ queue-lock algorithm rather than Z adaptive contention policy |
| `ZmLock` | `std::recursive_mutex` | `boost::recursive_mutex` | N/A | [`bslmt::Mutex`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_mutex.h) |
| `ZmRWLock` | `std::shared_mutex` | `boost::shared_mutex` | [`folly::SharedMutex / SharedMutexReadPriority`](https://github.com/facebook/folly/blob/main/folly/SharedMutex.h) | [`bslmt::ReaderWriterMutex`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_readerwritermutex.h) |
| `ZmGuard` / `ZmReadGuard` | `std::unique_lock`, `std::shared_lock` | `boost::unique_lock`, `boost::shared_lock` | Partial: [`folly::Synchronized locking proxies`](https://github.com/facebook/folly/blob/main/folly/Synchronized.h)<br>➕ ties protected data to its lock<br>➖ different guard/data ownership interface | Partial: [`bslmt::LockGuard + ReadLockGuard`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_lockguard.h)<br>➖ lock-selection and upgrade semantics differ |

## Conditions, semaphores and blocking adapters

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmCondition` | `std::condition_variable_any` | `boost::condition_variable_any` | N/A | [`bslmt::Condition`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_condition.h) |
| `ZmSemaphore` | `std::counting_semaphore` (C++20) | Boost.Interprocess `interprocess_semaphore` (partial sharing model) | Partial: [`folly::LifoSem / NativeSemaphore`](https://github.com/facebook/folly/blob/main/folly/synchronization/LifoSem.h) | [`bslmt::Semaphore`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_semaphore.h) |
| `ZmBlock`<br>➕ continuation-to-blocking bridge reusing a thread-local semaphore | Partial: `std::promise` / `std::future`<br>➖ shared-state machinery differs | Partial: Boost.Thread futures / Asio `use_future` | Partial: [`folly::Baton`](https://github.com/facebook/folly/blob/main/folly/synchronization/Baton.h)<br>➖ one-shot handoff primitive; async-to-blocking wrapper is additional work | Partial: [`bslmt::Latch`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_latch.h)<br>➖ countdown synchronization rather than async-result blocking adapter |

## Random number generation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmRandom`<br>➕ framework Mersenne Twister wrapper<br>➖ not cryptographic | `std::mt19937`<br>➕ standard distribution family | Boost.Random Mersenne Twister and distributions | Partial: [`folly::Random / ThreadLocalPRNG`](https://github.com/facebook/folly/blob/main/folly/Random.h)<br>➖ current default generator is not Mersenne Twister | Partial: [`bdlb::PcgRandomGenerator / RandomDevice`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_pcgrandomgenerator.h)<br>➖ PCG rather than Z Mersenne Twister state/sequence |

## Clocks, timing statistics and retry policies

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmTime` | `std::chrono` clocks | Boost.Chrono | Partial: [`folly::chrono::coarse_steady_clock / coarse_system_clock`](https://github.com/facebook/folly/blob/main/folly/Chrono.h) | [`bsls::SystemTime`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_systemtime.h) |
| `ZmTimeInterval`<br>➕ locked min/max/total/mean/count aggregation | Partial: chrono values + custom aggregation | Boost.Accumulators with timing values | Partial: [`folly::Histogram`](https://github.com/facebook/folly/blob/main/folly/stats/Histogram.h)<br>➕ distribution buckets in addition to aggregate statistics<br>➖ different time-value/locking contract | Partial: [`bdlsta::Moment`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlsta/bdlsta_moment.h)<br>➖ statistical accumulator; time typing and locking need composition |
| `ZmBackoff`<br>➕ configurable exponential backoff with jitter | Partial: chrono + `<random>` composition | Partial: Boost.Random + Chrono composition | Partial: [`folly::futures::retryingPolicyCappedJitteredExponentialBackoff`](https://github.com/facebook/folly/blob/main/folly/futures/Retrying.h)<br>➖ future retry policy rather than standalone backoff state | N/A |
| `ZmTimeout`<br>➕ timeout state with exponential backoff | Partial: chrono deadlines + custom retry policy | Partial: Boost.Asio timer + retry policy | Partial: [`folly::HHWheelTimer`](https://github.com/facebook/folly/blob/main/folly/io/async/HHWheelTimer.h)<br>➖ timer scheduling does not include Z exponential-backoff state | Partial: [`bdlmt::EventScheduler`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_eventscheduler.h)<br>➖ scheduling rather than timeout/backoff state |

## Stack and scratch allocation

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmStackAvail`<br>➕ queries remaining stack for scratch policies | N/A | N/A | N/A | N/A |
| `ZmAlloc`<br>➕ checks remaining stack space and selects aligned stack storage or RAII heap fallback<br>➖ macro evaluates size more than once | N/A; automatic storage does not provide runtime stack-capacity checks or heap fallback | Partial: Boost.Container `small_vector`<br>➖ fixed inline capacity rather than remaining-stack check | N/A | N/A |
| `ZmLocal`<br>➕ selects stack/heap storage from available stack space and runs construction/destruction | Partial: `std::unique_ptr`<br>➖ does not automatically select stack storage | Partial: scoped/unique pointer with explicit storage selection | N/A | Partial: [`bdlma::LocalSequentialAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ fixed local buffer plus upstream allocation rather than available-stack selection |
| `ZmScratch`<br>➕ runtime-sized scratch array with stack-space checks, heap fallback and element cleanup | Partial: `std::vector`<br>➖ no automatic stack-backed allocation path | Partial: `boost::container::small_vector` | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ fixed inline capacity rather than remaining-stack-space selection | Partial: [`bdlma::LocalSequentialAllocator`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ fixed local buffer plus upstream allocation rather than available-stack selection |

## Heaps and allocator integration

Sources: [public headers](../zm/src/), [module guide](../zm/README.md).
Relevant counterparts: [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html),
[Boost.Intrusive](https://www.boost.org/doc/libs/latest/doc/html/intrusive.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmHeap` / `ZmNHeap`<br>➕ named fixed-block arenas, partitions and telemetry<br>➕ CPU/NUMA-aware reserves<br>➖ reserves can fall back to ordinary allocation | Partial: `std::allocator`, `std::pmr::synchronized_pool_resource`<br>➕ standard allocator/resource interfaces<br>➖ no named tuning/telemetry layer | Boost.Pool<br>➕ reusable pool allocator vocabulary<br>➖ no equivalent integrated naming/NUMA/telemetry layer | Partial: [`folly::Arena / ThreadCachedArena`](https://github.com/facebook/folly/blob/main/folly/memory/Arena.h)<br>➖ bulk arena lifetime rather than named individually recycled block heaps | Partial: [`bdlma::ConcurrentPool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentpool.h)<br>➕ thread-safe configurable block pooling<br>➖ no matching named/sharded heap registry and tuning files |
| `ZmVHeap`<br>➕ named variable-size recycling classes and instrumentation | `std::pmr` pool resources | Boost.Pool / Container PMR | Partial: [`folly::Arena`](https://github.com/facebook/folly/blob/main/folly/memory/Arena.h)<br>➖ bulk allocation/release model rather than per-block recycling classes | Partial: [`bdlma::ConcurrentMultipool`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_concurrentmultipool.h)<br>➖ different size-class and heap-lifetime policies |
| `ZmAllocator`<br>➕ connects Z heaps to standard allocator-aware containers | `std::allocator`, `std::pmr::polymorphic_allocator` | `boost::pool_allocator` / Container PMR allocator | Partial: [`folly::reentrant_allocator`](https://github.com/facebook/folly/blob/main/folly/memory/ReentrantAllocator.h)<br>➖ different allocator policy; no Z named-heap integration | Partial: [`bslma::Allocator / bsl::allocator`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslma/bslma_allocator.h)<br>➕ pervasive allocator propagation in BSL containers<br>➖ no direct Z named-heap policy |
| `ZmHeapMgr`<br>➕ global named arena configuration and statistics | N/A; custom PMR resource registry | Partial: pool management with application registry | N/A | N/A |

## Scratch-buffer codecs

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZmCodec` (`ZmBase64`, `ZmBase32`, `ZmHex`)<br>➕ scratch-buffer codecs delivered through callbacks | N/A | Partial: Boost encoding iterators + scratch storage | Partial: [`bdlde::Base64Encoder / HexEncoder + bsl::string`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_base64encoder.h)<br>➖ codec/destination composition rather than Z pooled codec wrapper |

## Intrusive ownership and callbacks

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmObject`<br>➕ embedded atomic reference count | Partial: `std::enable_shared_from_this` | `boost::intrusive_ref_counter<T, boost::thread_safe_counter>` | N/A | N/A |
| `ZmRef`<br>➕ intrusive shared ownership<br>➖ no weak observers | `std::shared_ptr`<br>➕ arbitrary pointees and `std::weak_ptr` | `boost::intrusive_ptr` with atomic hooks | N/A | N/A |
| `ZmPolymorph` | Partial: shared ownership of a virtual-destructor base | Partial: intrusive ownership of a virtual-destructor base | N/A | N/A |
| `ZmContext`<br>➕ compact raw-or-owning erased context<br>➖ pointer-tag representation assumes supported architectures | Partial: `std::variant<void*, std::shared_ptr<Base>>` | Partial: `boost::variant2::variant` + `boost::intrusive_ptr` | Partial: [`folly::DiscriminatedPtr`](https://github.com/facebook/folly/blob/main/folly/DiscriminatedPtr.h)<br>➖ tagged pointers do not retain pointees | N/A |
| `ZmFn`<br>➕ optimized function/context representation<br>➖ larger captures can allocate | `std::function`, `std::move_only_function` (C++23) | `boost::function` | [`folly::Function`](https://github.com/facebook/folly/blob/main/folly/Function.h)<br>➕ move-only captures and cv-qualified signatures<br>➖ move-only wrapper rather than ZmFn's copyable delegate | [`bsl::function`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_function.h)<br>➕ allocator-aware type erasure<br>➖ no Z delegate representation/heap naming |

## Thread-local state and cleanup

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmSpecific`<br>➕ explicit scope, cleanup levels and enumeration of all thread instances<br>➕ consolidates per-type TLS across Windows modules | N/A; thread_local does not provide this registry and cleanup policy | `boost::thread_specific_ptr`<br>➖ no equivalent global instance-enumeration contract | [`folly::ThreadLocal / ThreadLocalPtr`](https://github.com/facebook/folly/blob/main/folly/ThreadLocal.h)<br>➕ supports cross-thread instance access<br>➖ different teardown/order and Windows module contracts | Partial: [`bslmt::ThreadUtil TLS keys`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h)<br>➖ construction, enumeration and cleanup-level policy need composition |
| `ZmSingleton`<br>➕ configurable lazy construction, optional final() hook and cleanup-level registration | Partial: `std::call_once`<br>➖ no singleton ownership or cleanup sequencing | Partial: Boost.Serialization singleton | [`folly::Singleton / SingletonVault`](https://github.com/facebook/folly/blob/main/folly/Singleton.h)<br>➕ managed singleton lifecycle and dependency handling | N/A |
| `ZmCleanup` / `ZmGlobal`<br>➕ ordered cleanup levels and a shared registry for singleton/TLS teardown | N/A; static destruction order alone does not implement this registry/policy | Partial: Boost singleton/TLS primitives with custom coordination | Partial: [`folly::SingletonVault`](https://github.com/facebook/folly/blob/main/folly/Singleton.h)<br>➖ does not implement Z's shared singleton/TLS cleanup-level registry | N/A |

## Assertions and diagnostic utilities

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmAssert` / `ZmAssert_`<br>➕ debug, always-on and release-fallback forms | `assert`, custom always-on check | Boost.Assert<br>➕ configurable assertion handlers | Partial: [`FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK`](https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h)<br>➖ no matching release-fallback expression form | Partial: [`BSLS_ASSERT / BSLS_ASSERT_SAFE`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h)<br>➕ configurable assertion handling and build levels<br>➖ no matching Z release-fallback expression |
| `ZmObjectDebug`<br>➕ intrusive object lifetime diagnostics | N/A | Partial: Boost.SmartPtr debugging hooks | N/A | N/A |
| `ZmDemangle`<br>➕ binutils demangling for difficult compiler names<br>➖ additional binutils dependency | N/A; `type_info::name()` is implementation-defined | `boost::core::demangle`, Boost.TypeIndex | Partial: [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h)<br>➖ different demangler/backend contract | N/A |
| `ZmBackTrace` | `std::stacktrace` (C++23) | Boost.Stacktrace | [`folly::symbolizer stack traces`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h) | [`balst::StackTraceUtil / StackTrace`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktraceutil.h) |
| `ZmBackTracer`<br>➕ keeps a bounded ring of captured traces | Partial: `std::stacktrace` + bounded storage | Partial: Boost.Stacktrace + CircularBuffer | Partial: [`folly::symbolizer stack traces`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/Symbolizer.h)<br>➖ bounded trace-history ring is application work | Partial: [`balst::StackTrace`](https://github.com/bloomberg/bde/blob/main/groups/bal/balst/balst_stacktrace.h)<br>➖ bounded history capture/ring is application work |
| `ZmTrap`<br>➕ last-ditch diagnostics and signal trapping | Partial: `std::signal`, `std::set_terminate` | Partial: Boost.Stacktrace signal-handler integration | Partial: [`folly symbolizer fatal-signal handlers`](https://github.com/facebook/folly/blob/main/folly/debugging/symbolizer/SignalHandler.h) | N/A |

## Sequential containers and nodes

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmNode` / `ZmNodeFn`<br>➕ explicit intrusive node ownership policies | Partial: container node handles<br>➖ no general intrusive-node framework | Boost.Intrusive hooks and value traits | Partial: [`folly::IntrusiveListHook`](https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h)<br>➖ list linkage rather than Z's generic node ownership policy | N/A |
| `ZmList`<br>➕ intrusive ownership, heap and locking policies | `std::list`<br>➕ standard iterators and algorithms | `boost::intrusive::list` | [`folly::IntrusiveList / CountedIntrusiveList`](https://github.com/facebook/folly/blob/main/folly/container/IntrusiveList.h)<br>➖ different locking/allocation/ownership policies | Partial: [`bsl::list`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_list.h)<br>➖ allocator-aware owning list rather than Z intrusive policy nodes |
| `ZmStack`<br>➕ deletion inside stack for queued cancellation<br>➖ requires sentinel-null value semantics | `std::stack` / `std::vector` composition | `boost::container::vector` with stack operations | Partial: [`folly::AtomicLinkedList`](https://github.com/facebook/folly/blob/main/folly/AtomicLinkedList.h)<br>➖ bulk-sweep atomic stack rather than Z's indexed/removable stack | Partial: [`bdlcc::Deque`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_deque.h)<br>➖ deque composition, not indexed/removable Z stack contract |
| `ZmQueue`<br>➕ FIFO/LIFO operations without shifting remaining items<br>➖ sentinel-null value contract | `std::deque`, `std::queue` | Boost.CircularBuffer / Container deque | Partial: [`folly::MPMCQueue`](https://github.com/facebook/folly/blob/main/folly/MPMCQueue.h)<br>➕ bounded concurrent producer/consumer queue<br>➖ different sizing and FIFO/LIFO contract | Partial: [`bdlcc::Queue / BoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_queue.h)<br>➖ different FIFO/LIFO, blocking and node policy |

## Ordered and hash indices

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmRBTree`<br>➕ intrusive nodes and configurable locking/ownership | `std::map`, `std::set`, multi variants | `boost::intrusive::rbtree` / set | Partial: [`folly::ConcurrentSkipList`](https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h)<br>➖ ordered skip-list set rather than intrusive red/black tree | Partial: [`bdlcc::SkipList`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_skiplist.h)<br>➖ concurrent ordered skip list rather than intrusive red/black tree |
| `ZmLHash`<br>➕ by-value linear probing with Z policies | `std::unordered_map` / `std::unordered_set`<br>➖ standard node-oriented contracts differ | `boost::unordered_flat_map` / `unordered_flat_set` | Partial: [`folly::F14ValueMap / F14FastMap`](https://github.com/facebook/folly/blob/main/folly/container/F14Map.h)<br>➕ optimized value/node/vector storage choices<br>➖ different probing, locking and ownership policies | Partial: [`bdlc::FlatHashMap / FlatHashSet`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h)<br>➕ allocator-aware flat storage<br>➖ different probing, locking and node policies |
| `ZmHash`<br>➕ intrusive chaining, lock striping and named tuning | `std::unordered_map` / `std::unordered_set`<br>➖ caller supplies synchronization | `boost::intrusive::unordered_set`; partial: Boost.Unordered concurrent containers | Partial: [`folly::ConcurrentHashMap`](https://github.com/facebook/folly/blob/main/folly/concurrency/ConcurrentHashMap.h)<br>➕ concurrent lookup/update and reclamation<br>➖ not Z intrusive nodes or named hash/heap tuning | Partial: [`bdlcc::StripedUnorderedMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_stripedunorderedmap.h)<br>➕ concurrent operations with striped locking<br>➖ no matching Z intrusive nodes and heap/hash registry |
| `ZmHashMgr`<br>➕ named sizing, striping and telemetry | N/A; per-container load-factor/rehash controls only | Partial: Boost.Unordered controls with custom registry | N/A | N/A |
| `ZmPolyHash`<br>➕ reflected multiple intrusive hash indices in one allocation | N/A; multiple containers with manual index maintenance | Boost.MultiIndex<br>➕ also supports ordered, sequenced and other index kinds | N/A | N/A |

## Caches and eviction

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmCacheStats` | N/A; application counters | Partial: Boost.Accumulators or custom counters | N/A | N/A |
| `ZmCache`<br>➕ integrated hash lookup and LRU eviction | Partial: `std::list` + `std::unordered_map` | Partial: Boost.MultiIndex hashed + sequenced indices | [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ different locking/node/heap policy | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)<br>➕ LRU/FIFO eviction and allocator support<br>➖ different node, locking and lifecycle contracts |
| `ZmPolyCache`<br>➕ reflected multiple keys with LRU eviction | N/A; custom multi-index cache | Partial: Boost.MultiIndex + eviction policy | Partial: [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ no reflected multiple-key indices | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)<br>➖ no reflected multi-key indices |

## Sequence reassembly

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| `ZmPQueue`, `ZmPQRx`, `ZmPQTx`<br>➕ deterministic skip-list sequence reassembly; gaps and overlaps | Partial: `std::map` plus interval/reassembly logic<br>➖ `std::priority_queue` does not implement this contract | Partial: Boost.ICL + ordered index<br>➖ Boost.Heap is an ordinary priority heap, not sequence reassembly | Partial: [`folly::ConcurrentSkipList`](https://github.com/facebook/folly/blob/main/folly/ConcurrentSkipList.h)<br>➖ no sequence-gap/overlap reassembly or retransmission contract |

## Ring storage and callable dispatch

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmRingMirror`<br>➕ double-mapped storage simplifies wraparound records | N/A | Partial: Boost.Interprocess mapping primitives<br>➖ mirror arrangement is application work | N/A | N/A |
| `ZmRing`<br>➕ fixed/variable records, producer policies and broadcast readers<br>➖ multi-reader mode broadcasts rather than distributes work | N/A; custom bounded queue | Partial: Boost.Lockfree queues<br>➖ different record/broadcast contracts | Partial: [`folly::LockFreeRingBuffer`](https://github.com/facebook/folly/blob/main/folly/concurrency/container/LockFreeRingBuffer.h)<br>➖ fixed-record overwrite ring; delivery/retirement differs | Partial: [`bdlcc::SingleProducerSingleConsumerBoundedQueue`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_singleproducersingleconsumerboundedqueue.h)<br>➖ fixed typed slots, not variable-record broadcast/retirement semantics |
| `ZmRingFn`<br>➕ captured callable payloads stored in variable-record rings | Partial: callable wrapper queued in a container | Partial: Boost.Function + Lockfree queue composition | Partial: [`folly::Function with a Folly queue`](https://github.com/facebook/folly/blob/main/folly/Function.h)<br>➖ no callable-in-variable-record-ring representation | Partial: [`bdlcc::BoundedQueue<bsl::function<...>>`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_boundedqueue.h)<br>➖ no callable-in-ring storage or broadcast contract |

## Topology and threads

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmBitmap`<br>➕ hwloc CPU/resource bitmap operations | Partial: `std::vector<bool>`<br>➖ no CPU-set semantics | `boost::dynamic_bitset`<br>➖ no topology integration | N/A | Partial: [`bdlc::BitArray`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h)<br>➖ synchronization and Z fixed bitmap layout need adaptation |
| `ZmTopology`<br>➕ hwloc CPU/NUMA discovery and binding | Partial: `std::thread::hardware_concurrency`<br>➖ only a concurrency hint | N/A; platform/hwloc integration required | Partial: [`folly::CacheLocality / AccessSpreader`](https://github.com/facebook/folly/blob/main/folly/concurrency/CacheLocality.h)<br>➖ not the full hwloc topology/binding surface | N/A |
| `ZmThread` / `ZmThreadParams`<br>➕ named affinity, stack, priority and partition configuration | `std::thread`, `std::jthread`<br>➕ `jthread` joins automatically and supplies stop tokens<br>➖ native tuning needs platform code | `boost::thread` / thread attributes | Partial: [`folly::NamedThreadFactory / InitThreadFactory`](https://github.com/facebook/folly/blob/main/folly/executors/thread_factory/NamedThreadFactory.h)<br>➖ full stack/priority/partition setup needs composition | Partial: [`bslmt::ThreadUtil + ThreadAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadutil.h)<br>➖ Z partition/topology and per-thread policy need composition |

## Scheduling and engine lifecycle

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZmScheduler` / `ZmSchedParams`<br>➕ named destinations, dedicated threads, timers and ring dispatch<br>➕ affinity/partition/queue configuration | N/A in C++23; C++26 `std::execution` is a partial asynchronous-execution counterpart | Boost.Asio `io_context`, thread pool, timers and strands<br>➕ executor/completion-token ecosystem<br>➖ matching Z destination/affinity policies needs composition | Partial: [`folly::IOThreadPoolExecutor / CPUThreadPoolExecutor`](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h)<br>➕ EventBase/future/coroutine executor ecosystem<br>➖ matching named destinations, timers and affinity needs composition | Partial: [`bdlmt::MultiQueueThreadPool + EventScheduler`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_multiqueuethreadpool.h)<br>➕ serialized job queues and recurring/one-shot scheduling<br>➖ queues are not fixed Z shard destinations |
| `ZmEngine`<br>➕ reusable asynchronous start/stop lifecycle and pending operations | N/A | Partial: Boost.Statechart / MSM plus Asio | N/A | Partial: [`bdlmt::ThreadPool lifecycle`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlmt/bdlmt_threadpool.h)<br>➖ worker lifecycle rather than general async startup/drain state machine |

## Runtime telemetry

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZtcHeap`, `ZtcHash`, `ZtcQueue`, `ZtcThread`, `ZtcTypes` (in `zm`)<br>➕ reflected runtime telemetry vocabulary | N/A | Partial: application-defined Boost.Describe / Serialization records | Partial: [`balm::MetricsManager / bdlm::MetricsRegistry`](https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h)<br>➖ metrics ecosystem rather than Z reflected heap/hash/queue records |

# Zt

## Platform and environment

Sources: [public headers](../zt/src/), [module guide](../zt/README.md).
Container comparisons use [Boost.Container](https://www.boost.org/doc/libs/latest/doc/html/container.html).

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZtPlatform` environment helpers | Partial: `std::getenv`<br>➖ no standard environment setter | Boost.Process environment facilities | Partial: [`bdls::OsUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_osutil.h)<br>➖ OS information subset rather than all Z environment helpers |

## Boolean and enum vocabulary

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| `ZtScanBool`<br>➕ accepts case-insensitive yes/no, y/n, true/false and 1/0 | Partial: `std::boolalpha` input | Partial: Boost.Convert / LexicalCast | Partial: [`folly::to<bool> / tryTo<bool>`](https://github.com/facebook/folly/blob/main/folly/Conv.h)<br>➖ accepted boolean spellings/error behavior differ |
| `ZtBytesFmt`<br>➕ shared binary-text encoding vocabulary | N/A | N/A; codec-specific options | N/A |
| `ZtcRAGMap` (in `zt`)<br>➕ common telemetry vocabulary with generated name lookup, conversion and iteration | N/A; an enum declaration alone does not generate the name map | Partial: enum + Boost.Describe | N/A |
| `ZtEnum` macro family<br>➕ integer enum vocabulary with generated name/ordinal matching and iteration | N/A; native enums have no generated runtime name mapping in C++23 | Boost.Describe enum descriptors<br>➕ metadata can support other generic algorithms | N/A |

## Text formatting

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZtQuote`<br>➕ quoting and binary-text formatting adapters | Partial: `std::quoted` | Partial: Boost.IO quoted manipulators | N/A | Partial: [`bdljsn::StringUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdljsn/bdljsn_stringutil.h)<br>➖ JSON escaping rather than Z generic quote policy |
| `ZtHexDump` | N/A; composed formatting loop | Partial: Boost.Log `dump` manipulator | [`folly::hexDump`](https://github.com/facebook/folly/blob/main/folly/String.h) | [`bdlb::Print::hexDump`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_print.h) |
| `ZtFmt`<br>➕ reflected-field printing/scanning policies | Partial: `std::format`, `std::from_chars` | Partial: Boost.Format / Convert | N/A | Partial: [`bsl::format`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslfmt/bslfmt_format.h)<br>➖ format strings rather than Z formatting policy vocabulary |

## Character case and encoding conversion

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZtCase` | Partial: `<cctype>`, `<cwctype>`, locale facets | Boost.StringAlgo / Locale case conversion | Partial: [`folly::toLowerAscii / toUpperAscii`](https://github.com/facebook/folly/blob/main/folly/String.h)<br>➖ ASCII transformations rather than general locale/encoding handling | Partial: [`bdlb::String::toLower / toUpper`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_string.h)<br>➖ byte-character rather than general encoding conversion |
| `ZtIconv`<br>➕ iconv conversions into adapted output buffers | Partial: legacy locale conversion facilities | Boost.Locale encoding conversion | N/A | Partial: [`bdlde::CharConvertUtf16 / CharConvertUtf32`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_charconvertutf16.h)<br>➖ Unicode conversion subset, not arbitrary iconv encodings |

## Owning arrays, strings and scratch buffers

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZtArray`<br>➕ owning or shadowing storage and direct capacity control<br>➖ ownership mode and initialization require care | `std::vector`<br>➕ uniform owning semantics and standard iterators | `boost::container::vector` | [`folly::fbvector`](https://github.com/facebook/folly/blob/main/folly/container/FBVector.h)<br>➖ owning storage; no Z shadowing mode | [`bsl::vector`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_vector.h)<br>➕ allocator propagation<br>➖ no Z shadowing mode |
| `ZtString` / `ZtWString`<br>➕ inline storage, named heaps and stream-style appending | `std::string`, `std::wstring`<br>➕ standard string/range integration | `boost::container::basic_string` | Partial: [`folly::fbstring`](https://github.com/facebook/folly/blob/main/folly/FBString.h)<br>➖ different allocator/shadowing and wide-string interface | [`bsl::basic_string`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_string.h)<br>➕ allocator-aware strings<br>➖ no Z shadowing mode |
| `ZtBuiltin`<br>➕ adds an initial inline buffer to array storage | N/A; `std::inplace_vector` (C++26) cannot spill to heap | `boost::container::small_vector` | [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h) | Partial: [`bdlma::LocalSequentialAllocator + BSL containers`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ buffer-backed allocation composition rather than Z storage classes |
| `ZtScratch`<br>➕ ZtArray/ZtString operations over runtime stack backing with heap fallback and cleanup | Partial: `std::vector`<br>➖ no automatic stack-backed allocation path | Partial: `boost::container::small_vector`<br>➖ inline capacity is compile-time fixed | Partial: [`folly::small_vector`](https://github.com/facebook/folly/blob/main/folly/small_vector.h)<br>➖ fixed inline size rather than stack-availability-based selection | Partial: [`bdlma::LocalSequentialAllocator + BSL containers`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlma/bdlma_localsequentialallocator.h)<br>➖ buffer-backed allocation composition rather than Z storage classes |

## Bitmaps and sliding windows

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZtBitmap` | Partial: `std::vector<bool>` | `boost::dynamic_bitset` | [`bdlc::BitArray`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_bitarray.h) |
| `ZtWindow`<br>➕ indexed sliding window with assignment-driven extension | Partial: `std::deque` with sequence-index bookkeeping | Partial: Boost.CircularBuffer | N/A |
| `ZtBitWindow`<br>➕ sliding packed values in selected bit widths | N/A; custom packed circular storage | Partial: Boost.DynamicBitset + CircularBuffer concepts | N/A |

## Timezone conversion

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `Zt::tzOffset` (`ZtTimeZone.hh`)<br>➖ explicit zones temporarily change `TZ` under a global lock | `std::chrono::time_zone`, `zoned_time` (C++20)<br>➕ independent zone objects and timezone database | Boost.DateTime timezone facilities | [`baltzo::TimeZoneUtil`](https://github.com/bloomberg/bde/blob/main/groups/bal/baltzo/baltzo_timezoneutil.h)<br>➕ explicit zone-info loading/cache and DST handling |

## Demangling

| Z facility | STL | Boost | Folly |
| --- | --- | --- | --- |
| `ZtDemangle`<br>➕ rewrites Z structural-string/template names for readability | N/A | Partial: Boost.Core demangle + custom postprocessing | Partial: [`folly::demangle`](https://github.com/facebook/folly/blob/main/folly/Demangle.h)<br>➖ no Z-specific template-name rewriting |

## Regular expressions

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZtRegex` / `ZtREGEX`<br>➕ PCRE syntax, captures and substitutions<br>➖ external PCRE dependency | `std::regex`<br>➖ different syntax/feature contract | Boost.Regex | [`bdlpcre::RegEx`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlpcre/bdlpcre_regex.h)<br>➕ compiled PCRE expression wrapper<br>➖ matching/capture/replacement interface differs |

# Ze

## Native errors

Sources: [ZePlatform.hh](../ze/src/ZePlatform.hh), [module guide](../ze/README.md).
Ze constructs diagnostics; the asynchronous sink runtime is `ZiLog` below.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZeOK` | Default-constructed success `std::error_code` | Default-constructed success Boost.System `error_code` | N/A | N/A |
| `ZeError`<br>➕ native OS-error wrapper and message formatting | `std::error_code`<br>➕ extensible error categories and conditions | Boost.System `error_code` | Partial: [`folly::makeSystemErrorExplicit`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ creates system_error rather than Z's numeric wrapper | Partial: [`bsl::error_code / system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_systemerror.h)<br>➖ platform error capture needs separate code |
| `ZeLastError`<br>➕ captures native last-error state at the call site | Partial: capture `errno` / native error into `std::error_code` | Partial: native error capture + Boost.System | Partial: [`folly::makeSystemError`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ errno-based helper rather than Z's separate Windows OS/socket capture | Partial: [`bsl::error_code / system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_systemerror.h)<br>➖ platform error capture needs separate code |
| `ZeLastSockError`<br>➕ distinguishes Windows socket errors from ordinary OS errors | N/A; native call + `std::error_code` | Partial: Boost.Asio socket operation error reporting | N/A | Partial: [`bsl::error_code / system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_systemerror.h)<br>➖ platform error capture needs separate code |

## Diagnostic metadata and events

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Ze::Debug` / `Info` / `Warning` / `Error` / `Fatal`<br>➕ shared severity vocabulary with generated name lookup and event/log integration | N/A; a plain enum alone does not provide the generated mapping or integration | Boost.Log `trivial::severity_level` | [`folly::LogLevel`](https://github.com/facebook/folly/blob/main/folly/logging/LogLevel.h) | [`ball::Severity`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_severity.h) |
| `ZeEventInfo`<br>➕ timestamp, source, severity and component identity | Partial: `std::source_location` + custom event record | Boost.Log attributes / record metadata | Partial: [`folly::LogMessage metadata`](https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h) | [`ball::RecordAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_recordattributes.h) |
| `ZeEvent` / `ZeAnyEvent` / `ZeMsgFn`<br>➕ structured events with deferred or type-erased formatting | N/A; custom event type and callable | Partial: Boost.Log record and message attributes | Partial: [`folly::LogMessage`](https://github.com/facebook/folly/blob/main/folly/logging/LogMessage.h)<br>➖ different message ownership/formatting contract | Partial: [`ball::Record`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_record.h)<br>➖ ownership/formatting differ |
| `ZeEVENT` / `ZeMkEvent`<br>➕ call-site metadata or explicit metadata construction | Partial: `std::source_location::current()` + custom factory | Partial: Boost.Log source macros and attributes | Partial: [`XLOG / XLOGF`](https://github.com/facebook/folly/blob/main/folly/logging/xlog.h)<br>➖ constructs and emits a log record, not a standalone Ze event | Partial: [`BALL_LOG_*`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_log.h)<br>➖ logging operation, not a standalone Ze event constructor |

## Exceptions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZeException`<br>➕ same diagnostic vocabulary as logging events<br>➖ not the `std::exception::what()` interface | `std::exception`, `std::system_error`<br>➕ standard catch and message interface | Boost.Exception<br>➕ extensible attached diagnostic information | Partial: [`folly::exception_wrapper`](https://github.com/facebook/folly/blob/main/folly/ExceptionWrapper.h)<br>➖ type-erased existing exception, not a structured Ze diagnostic event | Partial: [`bsl::system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_systemerror.h)<br>➖ error-code exception rather than general Ze diagnostic event |
| `ZeEXCEPT` / `ZeMkException` | Partial: construct exception with `std::source_location` metadata | Partial: `BOOST_THROW_EXCEPTION` / `boost::enable_error_info`<br>➖ throw macro throws; `ZeEXCEPT` only constructs | Partial: [`folly::makeSystemErrorExplicit`](https://github.com/facebook/folly/blob/main/folly/Exception.h)<br>➖ system-error construction, not arbitrary severity/component event construction | Partial: [`bsl::system_error`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_systemerror.h)<br>➖ error-code exception rather than general Ze diagnostic event |

# Zf

## Configuration trees and typed loading

Sources: [public headers](../zf/src/), [module guide](../zf/README.md).
Closest reflection counterparts: [Hana](https://www.boost.org/doc/libs/latest/libs/hana/doc/html/index.html),
[Describe](https://www.boost.org/doc/libs/latest/libs/describe/doc/html/describe.html).
JSON counterpart: [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html).
Format parsers assembled with Spirit are **partial** equivalents, not supplied codecs.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZfTree`<br>➕ owned typed configuration nodes and diagnostic paths | Partial: recursive `std::variant` / containers | Partial: Boost.PropertyTree<br>➖ scalar typing and structural contracts differ | Partial: [`folly::dynamic`](https://github.com/facebook/folly/blob/main/folly/json/dynamic.h)<br>➖ JSON value model, not the shared Z configuration tree contract | Partial: [`bdld::Datum / ManagedDatum`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdld/bdld_datum.h)<br>➖ dynamic value tree rather than Z configuration ownership/update contract |
| `ZfTreeLoad`<br>➕ shared construction/load/update and validation policies | N/A | Partial: Boost.Describe + custom tree conversion | Partial: [`folly::DynamicParser / DynamicConverter`](https://github.com/facebook/folly/blob/main/folly/json/DynamicParser.h)<br>➖ typed conversion requires application bindings; no Z field/update policies | Partial: [`bdlat_SymbolicConverter`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_symbolicconverter.h)<br>➖ adapted-type conversion rather than Z configuration tree binding |

## Structure reflection and serialization metadata

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZfStruct`<br>➕ typed/runtime field metadata, facets and serialization properties in C++23<br>➕ constructor/load/update/key policies shared across formats<br>➖ substantial template/macro machinery | N/A in C++23; core C++23 has no matching reflection/serialization framework | Partial: Boost.Hana / Describe + Serialization<br>➕ Hana supplies a broader heterogeneous metaprogramming library<br>➖ matching Z's format/facet/update policies requires additional code | Partial: [`bdlat adaptation protocols + baljsn/balxml/balber codecs`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlat/bdlat_sequencefunctions.h)<br>➕ shared model for sequence/choice/enum/array/nullable types<br>➖ generated/user adapters rather than the Z field/facet macro interface |

## Text and web formats

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| `ZfCSV`<br>➕ streaming rows with reflected header/column binding | N/A | Partial: Boost.Tokenizer or Spirit<br>➖ row construction, quoting and field binding require composition | N/A | N/A |
| `ZfURI`<br>➕ typed path/query/form binding and direct path loading | N/A | Boost.URL<br>➕ URI parsing, components and normalization<br>➖ no matching reflected field/facet loader | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ URI component model rather than reflected path/query/form binding | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h)<br>➖ URI value model rather than reflected path/query/form bindings |
| `ZfJSON`<br>➕ reflected load/save, fixed-field matching and in-place decoding<br>➖ borrowed text requires mutable input and retained backing | N/A | Boost.JSON<br>➕ general JSON values, streaming and type conversion<br>➖ different metadata/update contract | Partial: [`folly::parseJson / toJson + dynamic conversion`](https://github.com/facebook/folly/blob/main/folly/json/json.h)<br>➖ no Z reflected facets, constructor/update policy or fixed-field automaton | Partial: [`baljsn::Encoder / Decoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_encoder.h)<br>➕ codecs over bdlat-adapted types<br>➖ Z update/facet/constructor policies need adaptation |

## Configuration formats

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZfCf`<br>➕ native configuration syntax, substitutions and typed updates | N/A | Partial: Boost.ProgramOptions / PropertyTree<br>➖ different syntax and binding model |
| `ZfTOML`<br>➕ owned typed tree, field binding, formatting and parser limits | N/A | N/A; Boost.PropertyTree has no built-in TOML parser |
| `ZfYAML`<br>➕ owned typed tree, field binding and bounded parsing<br>➖ restricted single-document subset; rejects tags and cyclic/shared graphs | N/A | N/A; Boost.PropertyTree has no built-in YAML parser |

## Command-line binding

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZfCLI`<br>➕ metadata-bound options/positionals, command text and argv output | N/A | Boost.ProgramOptions<br>➕ established option descriptions, parsing and notification<br>➖ reflected facets and reverse serialization need custom work | Partial: [`folly::NestedCommandLineApp`](https://github.com/facebook/folly/blob/main/folly/cli/NestedCommandLineApp.h)<br>➕ nested command structure<br>➖ no matching reflected argv/command reverse serialization | Partial: [`balcl::CommandLine`](https://github.com/bloomberg/bde/blob/main/groups/bal/balcl/balcl_commandline.h)<br>➕ typed option specifications and validation<br>➖ no Z reflected reverse command serialization |

## Binary wire formats

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZfCBOR`<br>➕ reflected binary object/array mapping | N/A | Partial: Boost.JSON CBOR example<br>➖ example-level codec, not matching reflected API | N/A |
| `ZfASN1`<br>➕ schema properties bind C++ fields to tags and wire encodings | N/A | N/A; Spirit-based ASN.1 implementation would be custom work | Partial: [`balber::BerEncoder / BerDecoder`](https://github.com/bloomberg/bde/blob/main/groups/bal/balber/balber_berencoder.h)<br>➖ BER codec over bdlat types rather than Z ASN.1 facets |
| `ZfDBUS`<br>➕ reflected signatures, alignment and message-body serialization | N/A | N/A; no dedicated Boost D-Bus structural codec | N/A |

# Zi

## Native paths and scatter/gather descriptors

Sources: [public headers](../zi/src/), [module guide](../zi/README.md).
Networking counterparts are in [Boost.Asio](https://www.boost.org/doc/libs/latest/doc/html/boost_asio.html).
The STL has no C++23 socket/event-loop API.

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| `ZiPlatform` / `Zi::Path` and native I/O types | Partial: `std::filesystem::path`<br>➖ no standard socket/native-handle abstraction | Partial: Boost.Filesystem, Asio, WinAPI | N/A | Partial: [`bdls::PathUtil / FilesystemUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_pathutil.h)<br>➖ native handle/type vocabulary differs |
| `ZiVec` / `ZiVec_ptr` / `ZiVec_len`<br>➕ portable native scatter/gather descriptors | Partial: `std::span`<br>➖ no native scatter/gather syscall adapter | Boost.Asio mutable/const buffer sequences | Partial: [`folly::IOBuf iovec conversion`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ chained-buffer adapter rather than portable native descriptor aliases | Partial: [`ntsa::ConstBuffer / MutableBuffer`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_buffer.h)<br>➖ descriptor objects rather than native portable aliases |

## Process and module utilities

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZiProgram`<br>➕ current executable/process information | N/A | Partial: Boost.DLL program location + Process | Partial: [`bdls::ProcessUtil`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_processutil.h)<br>➖ process identity/path helpers rather than complete program lifecycle |
| `ZiModule` | N/A; OS loader calls | Boost.DLL | N/A |
| `ZiPIDFile`<br>➕ PID file ownership and cleanup | N/A | Partial: Boost.Interprocess file lock + custom PID file | N/A |
| `ZiDaemon` | N/A | Partial: Boost.Process plus platform daemon setup | N/A |

## Directories and path completion

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZiDir` | `std::filesystem::directory_iterator` | Boost.Filesystem directory iterator | Partial: [`bdls::FilesystemUtil path matching/visitation`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)<br>➖ directory/glob interface differs |
| `ZiGlob`<br>➕ directory/path completion for interactive CLIs<br>➖ not a full glob-expression engine | Partial: `std::filesystem` iteration + prefix matching | Partial: Boost.Filesystem + StringAlgo | Partial: [`bdls::FilesystemUtil path matching/visitation`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)<br>➖ directory/glob interface differs |

## File operations and structured output

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZiFile`<br>➕ direct native unbuffered file operations | Partial: `std::fstream`, `std::filesystem`<br>➖ stream buffering and native-operation APIs differ | Partial: Boost.Filesystem / Asio file APIs / Iostreams file descriptors | [`folly::File + FileUtil operations`](https://github.com/facebook/folly/blob/main/folly/File.h)<br>➕ RAII descriptor ownership and file locking | Partial: [`bdls::FilesystemUtil + FdStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_filesystemutil.h)<br>➕ allocator-aware stream buffering and filesystem utilities<br>➖ different descriptor/async contract |
| `ZiFileTxStream`<br>➕ file-backed buffered Z transmit stream | `std::ofstream` | Boost.Iostreams file sink / stream | Partial: [`folly::writeFull`](https://github.com/facebook/folly/blob/main/folly/FileUtil.h)<br>➖ full-write helper, not a buffered Z transmit stream | Partial: [`bdls::FdStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdls/bdls_fdstreambuf.h)<br>➖ buffered stream I/O rather than Z owner-directed Tx stream |
| `ZiCSV`<br>➕ file/stream integration for reflected CSV records | N/A; fstreams + custom CSV binding | Partial: Boost.Iostreams + Tokenizer/Spirit | N/A | N/A |

## Addresses and name resolution

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| `ZiIP` | N/A | Boost.Asio `ip::address` / endpoint types | [`folly::SocketAddress / IPAddress`](https://github.com/facebook/folly/blob/main/folly/SocketAddress.h) | [`ntsa::IpAddress / Endpoint`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_ipaddress.h) |
| `ZiResolver`<br>➕ c-ares-backed asynchronous DNS with controlled runtime lifecycle | N/A | Boost.Asio `ip::tcp::resolver` / `ip::udp::resolver`<br>➖ DNS configuration/lifecycle surface differs | N/A | [`ntci::Resolver`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_resolver.h)<br>➕ asynchronous name/service resolution and cache configuration |

## I/O buffers, allocation and contexts

| Z facility | STL | Boost | Folly | BDE / NTF |
| --- | --- | --- | --- | --- |
| `ZiIOBuf`<br>➕ reference-owned packet backing and allocator integration | Partial: `std::span` + owning storage<br>➖ span alone carries no ownership | Partial: Asio buffers + application-owned storage<br>➖ buffer descriptors alone carry no ownership | [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➕ chainable buffers with shared storage, headroom and tailroom | Partial: [`bdlbb::Blob / BlobBuffer`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blob.h)<br>➕ shared segmented backing<br>➖ no Z role/provider geometry contract |
| `Zi::IOBufAllocator`, builtin/plugin allocators and allocation roles<br>➕ explicit App/Rx/Tx storage provenance and transport integration | Partial: `std::pmr::memory_resource`<br>➖ no packet-backing/provenance contract | Partial: Asio associated allocators + custom packet pool | Partial: [`folly::memory::makeIOBufArenaFactory`](https://github.com/facebook/folly/blob/main/folly/memory/IOBufArenaFactory.h)<br>➖ no Z App/Rx/Tx role/provenance contract | Partial: [`bdlbb::PooledBlobBufferFactory`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_pooledblobbufferfactory.h)<br>➖ Blob buffers rather than Z App/Rx/Tx provider roles |
| `ZiRxContext` / `ZiTxContext` (`ZiIOContext.hh`)<br>➕ buffer, progress and continuation state for transport I/O | N/A | Partial: Asio composed-operation state / completion handlers | Partial: [`folly::AsyncTransport ReadCallback / WriteCallback`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncTransport.h)<br>➖ different callback state and lifetime interface | Partial: [`ntca::ReceiveContext / SendContext`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_receivecontext.h)<br>➖ result metadata rather than Z I/O continuation state |

## Receive and transmit streams

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZiRxStream`<br>➕ consumable view over queued receive buffers | Partial: `std::istream` over a custom stream buffer | Partial: Asio DynamicBuffer / Beast `multi_buffer` | Partial: [`folly::io::Cursor + IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h) | Partial: [`bdlbb::InBlobStreamBuf / OutBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)<br>➖ segmented streambuf rather than Z framing/credit/layer contract |
| `ZiTxStream`<br>➕ streamed output into packet/frame-sized buffer sequences | Partial: `std::ostream` over custom segmented storage | Partial: Asio buffer sequences / Beast buffers | Partial: [`folly::io::QueueAppender + IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/Cursor.h)<br>➖ frame-size/flush policy needs adaptation | Partial: [`bdlbb::InBlobStreamBuf / OutBlobStreamBuf`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlbb/bdlbb_blobstreambuf.h)<br>➖ segmented streambuf rather than Z framing/credit/layer contract |

## Message framing and transmission

| Z facility | STL | Boost | NTF |
| --- | --- | --- | --- |
| `ZiRx`<br>➕ CRTP header/body framing and fragmented-message accumulation | N/A | Partial: Asio `async_read` / `async_read_until` + framing state | Partial: [`ntci::StreamSocket send/receive queues`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➕ asynchronous operations with flow-control watermarks<br>➖ Z frame/layer/owner contracts need adaptation |
| `ZiTx`<br>➕ CRTP sender and queued completion handling | N/A | Partial: Asio `async_write` + queued write state | Partial: [`ntci::StreamSocket send/receive queues`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➕ asynchronous operations with flow-control watermarks<br>➖ Z frame/layer/owner contracts need adaptation |

## Event loops and connection multiplexing

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| `ZiEventLoop`<br>➕ external socket/handle readiness integration | N/A | Boost.Asio descriptors / handles and async waits | [`folly::EventBase / EventHandler`](https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h) | [`ntci::Reactor / Proactor`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_reactor.h)<br>➕ readiness and completion driver interfaces |
| `ZiConnection` / `ZiCxnInfo` / `ZiCxnOptions`<br>➕ transport callbacks, connection state and explicit I/O allocation roles | N/A | Partial: Asio socket, endpoint and socket options | Partial: [`folly::AsyncSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ different connection callbacks and allocation contract | Partial: [`ntci::StreamSocket / StreamSocketSession`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➖ different session/callback and allocation contracts |
| `ZiMxPlugin`<br>➕ transport-plugin lifecycle integrated with the multiplexer | N/A | Partial: custom Asio service/executor/stream implementation | N/A | N/A |
| `ZiMultiplex` / `ZiMxParams`<br>➕ shard-directed I/O with scheduler and allocator integration | N/A | Boost.Asio sockets, `io_context`, executors and strands<br>➕ completion tokens and coroutine integration<br>➖ Z shard/buffer ownership contracts need application composition | Partial: [`folly::IOThreadPoolExecutor + EventBase`](https://github.com/facebook/folly/blob/main/folly/executors/IOThreadPoolExecutor.h)<br>➖ matching Z Rx/Tx destinations and buffer ownership needs composition | Partial: [`ntci::Interface`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h)<br>➕ socket/timer factories, thread management and executor/strand integration<br>➖ no matching Z shard and buffer-role vocabulary |

## Interprocess broadcast rings

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiRing`<br>➕ named cross-process variable-record broadcast ring<br>➖ processes must agree on layout and payload protocol | N/A | Partial: Boost.Interprocess shared memory / message queue<br>➖ message queue consumption is not broadcast |

## Logging and assertions

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZiAssert`<br>➕ release fallback with logging/backtrace | Partial: `assert` + explicit release handling | Partial: Boost.Assert + Log + Stacktrace | Partial: [`FOLLY_SAFE_CHECK / FOLLY_SAFE_DCHECK`](https://github.com/facebook/folly/blob/main/folly/lang/SafeAssert.h)<br>➖ no matching release-fallback logging contract | Partial: [`BSLS_ASSERT + balst stack traces`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bsls/bsls_assert.h)<br>➖ explicit composition needed for release fallback/logging |
| `ZiLog` / `ZiLOG` / `ZiLogEvent`<br>➕ asynchronous Ze-event fanout to multiple sinks | Partial: `std::clog`, `std::osyncstream`<br>➖ no severity, sink routing or async log runtime | Boost.Log<br>➕ configurable attributes, filters and sinks | [`Folly logging: XLOG, handlers and AsyncFileWriter`](https://github.com/facebook/folly/blob/main/folly/logging/xlog.h)<br>➕ category hierarchy and configurable handler factories | [`ball::LoggerManager / AsyncFileObserver`](https://github.com/bloomberg/bde/blob/main/groups/bal/ball/ball_asyncfileobserver.h)<br>➕ categories, rules, observers and asynchronous file output |

## Resource tuning

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZiHeapTune`<br>➕ file-based named heap tuning and measurements | N/A | N/A; custom configuration around Boost.Pool |
| `ZiHashTune`<br>➕ file-based hash configuration and measurements | N/A; per-container controls only | N/A; custom configuration around Boost.Unordered |

## I/O telemetry

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZtcMx`, `ZtcLink`, `ZtcPool`, `ZtcHub` (in `zi`)<br>➕ reflected I/O telemetry records | N/A | Partial: application-defined Boost.Describe / Serialization records | Partial: [`balm::MetricsManager`](https://github.com/bloomberg/bde/blob/main/groups/bal/balm/balm_metricsmanager.h)<br>➖ metrics aggregation rather than Z reflected I/O records |

# Zxdp

## Registered packet storage

Sources: [module guide](../zxdp/README.md), [public headers](../zxdp/src/).
Linux-only; excluded from Windows builds.

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zxdp::PoolParams`, `BufPool`<br>➕ caller-owned intrusive pools over registered Rx/Tx arenas<br>➕ exhaustion returns null without replacement allocation<br>➕ allocation and kernel leases independently protect backing | N/A | Partial: Boost.Pool<br>➖ no registered UMEM or kernel submission lifetime |

## Raw packet I/O and service

| Z facility | STL | Boost |
| --- | --- | --- |
| `Zxdp::Params`, `Instance`<br>➕ direct AF_XDP packet submission and receive over one interface<br>➕ optional destination-mask steering with host-stack coexistence<br>➖ Linux AF_XDP and suitable kernel/driver support required | N/A | Partial: Boost.Asio generic raw sockets<br>➖ kernel socket path rather than AF_XDP rings and registered backing |
| `Zxdp::Instance::rx`, `wake`, `pollTx`, `close`<br>➕ configurable spin-before-epoll receive service and explicit wake<br>➕ bounded Tx retirement with retryable kernel-safe close | N/A | Partial: Boost.Asio event/cancellation machinery<br>➖ no matching raw-ring ownership, lease retirement or close contract |

# Zutcp

## Multiplexer backend and packet providers

Sources: [module guide](../zutcp/README.md), [execution plan](../zutcp_plan.md).
These comparisons describe the planned TCP goal-state; implementation is in
progress. Linux-only; excluded from Windows builds.

| Z facility | STL | Boost | Folly | NTF |
| --- | --- | --- | --- | --- |
| `Zutcp::Stack`, `StackParams`<br>➕ user-mode TCP as a ZiMxPlugin over Zxdp, retaining ordinary Zi application interfaces<br>➕ protocol Rx/Tx work runs on the mx owners<br>➖ requires a configured interface and raw packet privileges | N/A | Partial: Boost.Asio TCP sockets/executors<br>➖ kernel TCP rather than a user-mode TCP implementation | Partial: [`folly::AsyncSocket / EventBase`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ kernel TCP rather than an AF_XDP transport backend | Partial: [`ntci::Interface + StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_interface.h)<br>➖ kernel sockets rather than user-mode TCP over AF_XDP |
| `Zutcp::Allocator` and packet geometry<br>➕ Zi providers over caller-owned Zxdp pools, with in-place wire construction<br>➕ separate control capacity and bounded ingress retention | N/A | Partial: Boost.Asio buffer sequences + custom allocation<br>➖ no registered-packet provider or retention/control reserve contract | Partial: [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ buffer ownership only; no Zxdp registration or protocol admission policy | Partial: [`ntci::DataPool`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h)<br>➖ pooled Blob storage rather than registered Zxdp packet geometry |

## TCP connection and recovery

| Z facility | STL | Boost | NTF |
| --- | --- | --- | --- |
| `Zutcp::Connection`, `CliConnection`, `SrvConnection`<br>➕ one connection owns ordered bytes, reassembly, credit, retained Tx data and close<br>➕ bounded pending/live admission and closed-tuple history | N/A | Partial: Boost.Asio TCP sockets<br>➖ corresponding TCP state and recovery live in the OS | Partial: [`ntci::StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➖ TCP recovery and sequence state reside in the OS |
| TCP ACK/window/recovery facilities<br>➕ cumulative ACKs, optional SACK/DSACK, scaling and timestamps/PAWS<br>➕ RACK-TLP, RTO/persist and ECN/NewReno on explicit owners | N/A | N/A; Asio delegates these mechanisms to kernel TCP | N/A |
| TCP initial sequences and Fast Open<br>➕ secure keyed ISNs and bounded cookie state independent of TLS<br>➕ default-off, application-authorized early data with ordinary fallback | N/A | Partial: platform-specific socket options around kernel TCP<br>➖ no portable user-mode ISN/cookie or early-delivery policy | N/A |

# Zv

## Thread, stack and ring parameter binding

Sources: [public headers](../zv/src/), [module guide](../zv/README.md).
PropertyTree and ProgramOptions comparisons refer to their configuration
storage/binding roles, not syntax-compatible parsers. Boost's
[library index](https://www.boost.org/doc/libs/latest/libs/libraries.htm)
lists those facilities and the other Boost components referenced above.

| Z facility | STL | Boost | BDE |
| --- | --- | --- | --- |
| `ZvThreadParams`<br>➕ validated tree-to-thread settings with default preservation | N/A | Partial: Boost.ProgramOptions + Thread attributes | Partial: [`bslmt::ThreadAttributes`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslmt/bslmt_threadattributes.h)<br>➖ attribute object without Z configuration-tree binding |
| `ZvStackParams`<br>➕ binds stack sizing/configuration while retaining defaults | N/A | Partial: Boost.ProgramOptions + container configuration | N/A |
| `ZvRingParams`<br>➕ binds configuration to shared-memory ring settings | N/A | Partial: Boost.ProgramOptions + Interprocess setup | N/A |

## Connection and multiplexer parameter binding

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZvCxnOptions`<br>➕ connection flags, TTL and multicast binding | N/A | Partial: Boost.ProgramOptions + Asio socket options |
| `ZvMxParams`<br>➕ binds scheduler threads, destinations and multiplexer settings | N/A | Partial: Boost.ProgramOptions + custom Asio runtime settings |

## Configuration file loading and saving

| Z facility | STL | Boost |
| --- | --- | --- |
| `ZvCf`<br>➕ native configuration files with includes, environment expansion and typed binding<br>➖ save/update/delete outputs are not transactional in-place file edits | N/A; fstreams + custom parser/binder | Partial: Boost.ProgramOptions / PropertyTree + Filesystem<br>➖ different syntax; includes/expansion require custom policy |
| `ZvTOML`<br>➕ file I/O for reflected TOML and its owned tree | N/A | N/A; no built-in Boost TOML loader |
| `ZvYAML`<br>➕ file I/O for Zf's reflected YAML subset | N/A | N/A; no built-in Boost YAML loader |

# Zpb

## Protobuf wire format and typed messages

[Public API](../zpb/src/Zpb.hh) · [Module guide](../zpb/README.md)

| Z facility | Boost |
| --- | --- |
| `Zpb` / `PB` facet<br>➕ protobuf wire mapping from ZfStruct metadata without canonical generated C++ headers<br>➖ unknown fields are skipped rather than retained | Partial: Boost.Serialization<br>➖ its archives are not protobuf wire format |
| `Zpb::Builder`, `Zpb::IOBuilder`<br>➕ bounded output into caller storage or pooled I/O buffers | Partial: Boost.Serialization archive over an Iostreams device<br>➖ custom protobuf encoding still required |
| `Zpb::verify`, `handler`, `load`<br>➕ separate wire verification, borrowed access and typed construction<br>➖ borrowed results require retained backing | N/A; no dedicated Boost protobuf verifier/view API |

## Schema reflection and compiler

| Z facility | Boost |
| --- | --- |
| `Zpb::Reflect` (`ZpbReflect.hh`)<br>➕ protobuf schema/descriptor representation integrated with Z metadata | Partial: Boost.Describe / Hana<br>➖ no protobuf descriptor model |
| `zprotoc`<br>➕ compiles supported protobuf declarations into Z metadata<br>➖ supported declaration profile is narrower than the complete protobuf ecosystem | N/A |

# Zfb

## FlatBuffers construction and access

[Public API](../zfb/src/Zfb.hh) · [Module guide](../zfb/README.md)

| Z facility | Boost |
| --- | --- |
| `Zfb` / `FB` facet<br>➕ canonical FlatBuffers mapping from reflected C++ types without canonical generated headers | Partial: Boost.Serialization<br>➖ archive formats do not supply FlatBuffers layout |
| `Zfb::Builder`, `Zfb::IOBuilder`<br>➕ downward arena construction and direct pooled I/O output | Partial: Boost.Container storage + custom builder |
| `Zfb::view`, `handler`, `load`, `verify`<br>➕ borrowed table/vector access, typed construction and structural verification<br>➖ views depend on wire-buffer lifetime | N/A; no dedicated Boost FlatBuffers access/verifier API |

## Schema reflection and compiler

| Z facility | Boost |
| --- | --- |
| `Zfb::Reflect` (`ZfbReflect.hh`)<br>➕ emits and consumes BFBS/FBS schema metadata from reflected types | Partial: Boost.Describe / Hana<br>➖ no FlatBuffers schema emission |
| `zflatc`<br>➕ FlatBuffers schema tooling for Z types | N/A |

# Zdbus

## Addresses, authentication and framing

[Public headers](../zdbus/src/) · [Module guide](../zdbus/README.md).
Used by the Linux Ztls credential Vault; native message bodies use `ZfDBUS`.

| Z facility | Boost |
| --- | --- |
| `ZdbusAddress`<br>➕ decodes D-Bus unix addresses into filesystem/abstract socket endpoints | Partial: Asio local stream endpoints<br>➖ D-Bus address parsing is additional work |
| D-Bus EXTERNAL authentication (`ZdbusAuth.hh`)<br>➕ incremental client authentication integrated with connection setup | N/A |
| D-Bus envelopes and pooled messages (`ZdbusEnvelope.hh`, `ZdbusMessage.hh`)<br>➕ bounded framing, header validation and direct pooled construction | Partial: Asio buffers + custom D-Bus codec |

## Typed message adapters and runtime

| Z facility | Boost |
| --- | --- |
| Request/return/error/signal adapters and catalogs<br>➕ routing headers and typed ZfDBUS bodies share application declarations | Partial: Boost.Describe + custom dispatch |
| `ZdbusClient`<br>➕ asynchronous calls, serial correlation, named errors and signal subscriptions<br>➖ Linux-only module | Partial: Asio local sockets, timers and pending-call map<br>➖ no complete Boost D-Bus client |
| `ZdbusServer`<br>➕ service-side method routes and typed returns/errors over the shared bus client<br>➖ Linux-only module | N/A; service routing would be application code over Asio |

# Ztcp

## Buffer policies and byte streams

[Public API](../ztcp/src/Ztcp.hh) · [Module guide](../ztcp/README.md)

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Ztcp::RxBufAlloc`, `TxBufAlloc`, `RxStream`, `TxStream`<br>➕ pooled, reference-owned input/output with explicit allocator roles<br>➖ applications supply message framing | Partial: Asio buffer sequences and DynamicBuffer<br>➖ matching pool ownership and framing require composition | Partial: [`folly::IOBuf / IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no matching transport allocator-role or Z stream interface | Partial: [`ntci::DataPool + StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_datapool.h)<br>➖ Blob pools and socket queues rather than Z stream/provider policies |
| `Ztcp::HubParams`, `ClientParams`, `ServerParams`<br>➕ named Rx/Tx destinations and allocator configuration shared by links | Partial: Asio executors, socket options and application configuration | Partial: [`folly::EventBase + AsyncSocket options`](https://github.com/facebook/folly/blob/main/folly/io/async/EventBase.h)<br>➖ hub/allocator configuration requires application composition | Partial: [`ntca::StreamSocketOptions / ListenerSocketOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_streamsocketoptions.h)<br>➖ options rather than complete Z hub/allocator configuration |

## TCP links and endpoints

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Ztcp::CliLink`, `SrvLink`<br>➕ CRTP connection callbacks, queued writes and reconnect integration | Asio `ip::tcp::socket` with composed connection/read/write operations | Partial: [`folly::AsyncSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSocket.h)<br>➖ reconnect/hub behavior remains application policy | Partial: [`ntci::StreamSocket / StreamSocketSession`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➖ different callbacks, ownership and reconnect policy |
| `Ztcp::Client`, `Server`<br>➕ hub-managed link/listener lifecycle and restart after drain | Partial: Asio resolver, sockets and acceptor<br>➖ hub lifecycle and reconnect policy remain application work | Partial: [`folly::AsyncSocket / AsyncServerSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncServerSocket.h)<br>➖ endpoint startup/drain/restart orchestration is additional work | Partial: [`ntci::ListenerSocket + StreamSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_listenersocket.h)<br>➖ endpoint orchestration needs application composition |

# Ztls

## Cryptographic primitives

[Public headers](../ztls/src/) · [Module guide](../ztls/README.md).
TLS and crypto use zpicotls/OpenSSL; backend requirements are in
[dependencies](DEPENDENCIES.md). Hash/MAC comparisons use
[Boost.Hash2](https://www.boost.org/doc/libs/latest/libs/hash2/doc/html/hash2.html).

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `Ztls::Random`<br>➕ crypto-provider random bytes directly into a caller span | Partial: `std::random_device`<br>➖ implementation may use a pseudorandom engine; no universal cryptographic guarantee | Partial: Boost.Random `random_device`<br>➖ different device/word-generation interface | [`folly::Random::secureRandom`](https://github.com/facebook/folly/blob/main/folly/Random.h) | Partial: [`bdlb::RandomDevice`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlb/bdlb_randomdevice.h)<br>➖ raw entropy API rather than Z TLS random wrapper |
| `Ztls::ctEqual`<br>➕ security-oriented constant-time byte comparison | N/A; ordinary equality algorithms provide no constant-time guarantee | N/A; ordinary range equality provides no constant-time guarantee | N/A | N/A |
| `Ztls::secretHash`, `secretVerify`<br>➕ versioned salted secret-hash representation and verification | N/A | Partial: Boost.Hash2 + custom salt/format/verification policy | N/A | N/A |
| `Ztls::MD`<br>➕ incremental SHA digests through the configured crypto backend | N/A; `std::hash` is not a cryptographic digest API | Boost.Hash2 SHA algorithms<br>➕ also offers other hash families | [`folly::ssl::OpenSSLHash::Digest`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h) | Partial: [`bdlde::Sha256 / Sha512 / Sha1 / Md5`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlde/bdlde_sha2.h)<br>➖ concrete digest classes rather than selectable Z digest wrapper |
| `Ztls::HMAC`<br>➕ backend MAC state with span input/output and reset | N/A | Boost.Hash2 `hmac` | [`folly::ssl::OpenSSLHash::Hmac`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLHash.h) | N/A |
| `Ztls::TOTP`<br>➕ time-based code calculation and bounded-window verification | N/A | Partial: Boost.Hash2 HMAC + custom TOTP construction | N/A | N/A |

## Keys, encapsulation and encrypted files

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Ztls::PK`<br>➕ RSA/EC/Ed25519/X25519 key loading, generation and applicable signing/verification operations | N/A; Asio SSL's native handle is not a public-key wrapper library | Partial: [`folly::ssl::OpenSSLKeyUtils`](https://github.com/facebook/folly/blob/main/folly/ssl/OpenSSLKeyUtils.h)<br>➖ private-key parsing helpers, not the full typed key/signature API | Partial: [`ntci::EncryptionKey + EncryptionDriver`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptionkey.h)<br>➖ key/resource facilities rather than Z typed signing/key-agreement API |
| `Ztls::PK` ML-KEM-768 and ML-KEM-768–X25519 keys<br>➕ encapsulation/decapsulation and key serialization integrated with Z buffers | N/A | N/A | N/A |
| `Ztls::COSE`<br>➕ COSE key representations bound to native public/private key types | N/A | N/A | N/A |
| `Ztls::HPKE`<br>➕ base-mode hybrid ML-KEM-768–X25519 encryption with caller buffers | N/A | N/A | N/A |
| `ZtlsAge`<br>➕ age-format file encryption/decryption and recipient/identity parsing | N/A; file streams alone supply no age encryption | N/A | N/A |

## Credential storage

| Z facility | Boost |
| --- | --- |
| `Ztls::Vault`, `VaultConfig`, `VaultStore`<br>➕ scoped credential names with ephemeral/file/keyring/module stores<br>➕ Linux Secret Service and Windows platform integration | N/A; no matching Boost credential vault |

## TLS byte streams and endpoints

TLS counterpart: [Boost.Asio SSL](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/overview/ssl.html).

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Ztls::RxBufAlloc`, `TxBufAlloc` and TLS packet allocation<br>➕ separate plaintext/ciphertext ownership and serial record encryption into packet spans | Partial: Asio SSL buffers and associated allocators<br>➖ different packet-backing integration | Partial: [`folly::AsyncSSLSocket with IOBuf output`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h)<br>➖ no Z plaintext/ciphertext packet-allocation contract | Partial: [`ntci::Encryption + DataPool`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryption.h)<br>➖ no Z plaintext/ciphertext allocator-role contract |
| `Ztls::HubParams`, `ClientParams`, `ServerParams`<br>➕ trust, keys, ALPN, allocator roles and Rx/Tx destinations in one setup contract | Partial: Asio `ssl::context` + executors and application parameters | Partial: [`folly::SSLContext + EventBase`](https://github.com/facebook/folly/blob/main/folly/io/async/SSLContext.h)<br>➖ different allocator and endpoint configuration model | Partial: [`ntca::EncryptionClientOptions / EncryptionServerOptions`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntca/ntca_encryptionclientoptions.h)<br>➖ TLS options rather than Z transport hub configuration |
| `Ztls::CliLink`, `SrvLink`<br>➕ handshake/session callbacks integrated with pooled transport and shard ownership | Asio `ssl::stream<ip::tcp::socket>`<br>➕ standard Asio asynchronous-stream composition | [`folly::AsyncSSLSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h) | Partial: [`ntci::StreamSocket::upgrade / downgrade`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_streamsocket.h)<br>➕ TLS upgrade/downgrade of an existing stream socket<br>➖ different session/callback and buffer ownership interface |
| `Ztls::Client`, `Server`<br>➕ managed TLS link/listener startup, drain and restart | Partial: Asio SSL streams, acceptors and application lifecycle | Partial: [`folly::AsyncSSLSocket / AsyncServerSocket`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncSSLSocket.h)<br>➖ listener acceptance and lifecycle require composition | Partial: [`ntci::EncryptionDriver + StreamSocket / ListenerSocket`](https://github.com/bloomberg/ntf-core/blob/main/groups/ntc/ntci/ntci_encryptiondriver.h)<br>➖ TLS sessions and socket factories rather than Z endpoint lifecycle |

# Zquic

## Packet, frame and crypto codecs

[Public headers](../zquic/src/) · [Module guide](../zquic/README.md).
QUIC v1 is the mainstream deployment baseline; quic-go still prefers it as the
[most commonly deployed version](https://quic-go.net/docs/quic/transport/).
V2 adds no application capabilities and does not deprecate v1
([RFC 9369](https://www.rfc-editor.org/rfc/rfc9369.html)).

| Z facility | Boost |
| --- | --- |
| QUIC packet/frame/transport-parameter codecs<br>➕ native wire parsing and output integrated with Z buffer ownership | N/A; Asio UDP does not implement QUIC wire formats |
| `Zquic::AddressToken`, `StatelessReset`, connection-ID/bootstrap facilities<br>➕ Retry/address-validation and connection routing machinery | N/A |
| QUIC crypto (`ZquicCrypto.hh`)<br>➕ zpicotls handshake state, packet protection and key retirement | N/A; Asio SSL streams are TLS byte streams, not QUIC packet protection |

## Recovery, paths and stream ownership

| Z facility | Boost |
| --- | --- |
| ACK/loss recovery, `RttEstimator`, `NewReno`, PTO and retransmission queues<br>➕ packet-number-space recovery with retained frame ownership | N/A; Asio timers provide scheduling primitives only |
| `Zquic::Path` and link migration<br>➕ path validation, anti-amplification, PMTU/ECN and single-active-path migration | N/A |
| QUIC buffer/sequence-queue policies<br>➕ separate packet, CRYPTO and stream retention/reassembly contracts | Partial: Boost.Intrusive / ICL containers<br>➖ custom QUIC ownership/recovery logic required |
| `Zquic::CliStream`, `SrvStream`<br>➕ multiplexed streams with independent directional retirement, FIN/reset/stop and flow control | N/A; no complete Boost QUIC stream facility |

## Transport endpoints and diagnostics

| Z facility | Boost |
| --- | --- |
| `Zquic::HubParams`<br>➕ unified TLS, ALPN, transport, migration and instrumentation configuration | N/A |
| `Zquic::CliLink`, `SrvLink`<br>➕ shared connection runtime, stream scheduling and migration callbacks | N/A |
| `Zquic::Client`, `Server`<br>➕ QUIC connection bootstrap and UDP listener lifecycle over Zi | Partial: Asio UDP sockets and timers<br>➖ no QUIC transport implementation |
| `ZquicLogger`, `ZquicLogSink`, transport diagnostics<br>➕ qlog JSON-SEQ events with bounded queues and controlled drain/restart | Partial: Boost.Log + JSON<br>➖ QUIC/qlog event model and instrumentation are custom work |

# Zhttp

## URLs, fields and protocol configuration

[Public headers](../zhttp/src/) · [Module guide](../zhttp/README.md).
[Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/introduction.html)
supplies HTTP/1 and WebSocket facilities; Zhttp also owns HTTP/2 and HTTP/3.

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Zhttp::URLView`, `URL`, `Target`<br>➕ explicit borrowed/owning HTTP URL and request-target representations<br>➖ borrowed parsing normalizes mutable input | Boost.URL `url_view` / `url`<br>➕ general URI component model | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ no matching mutable borrowed HTTP-target view | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h)<br>➖ owning URI model rather than mutable borrowed HTTP target view |
| `ZhttpHeaders`, field catalogs and matchers<br>➕ compile-time name/value dispatch and shared header semantics across wire versions | Partial: Beast HTTP fields<br>➖ matching typed catalogs require application machinery | N/A | N/A |
| `Zhttp::Profiles`, `H1TCP`, `H1TLS`, `H2TLS`, `H3QUIC`<br>➕ explicit compile-time selection of supported transport/protocol combinations | Partial: Beast stream types with Asio SSL<br>➖ no corresponding H2/H3 profiles | N/A | N/A |
| `Zhttp::HubConfig`, `Config`, `ServerConfig` and transport configs<br>➕ shared connection, body, queue, protocol and allocator policy | Partial: Beast/Asio stream options + application configuration | N/A | N/A |

## Discovery and event framing

| Z facility | Boost |
| --- | --- |
| `Zhttp::DiscoveryRequest`, endpoint selection and `AltSvcCache`<br>➕ HTTPS/SVCB and Alt-Svc routing integrated with client protocol selection | Partial: Asio resolver + custom discovery/cache logic |
| `Zhttp::SSEDecoder`, `SSEEvent`<br>➕ incremental server-sent event framing and bounded event storage | Partial: Beast streaming HTTP bodies + custom SSE parser |

## Messages, body streams and response routes

| Z facility | Boost |
| --- | --- |
| `Zhttp::Parser`, request/response Builders and header catalogs<br>➕ one typed application interface for H1/H2/H3 parsing and production | Beast HTTP parsers, serializers and Body models<br>➖ HTTP/1 vocabulary rather than a shared H1/H2/H3 interface |
| `Zhttp::BodyEmitter` and body policies<br>➕ bounded fixed/streaming production, cancellation and H2/H3 trailers<br>➖ application owns body decompression | Partial: Beast serializers and custom Body implementations |
| `Zhttp::Channel`, `Route`, response queues<br>➕ owning deferred replies with H1 ordering and logical-stream cancellation | Partial: Beast stream plus application response-ordering/lifetime state |

## HTTP wire protocols and endpoints

| Z facility | Boost |
| --- | --- |
| HTTP/1.1 framing (`Zhttp`)<br>➕ integrated request/response streaming and pooled transport | Boost.Beast HTTP/1 |
| HTTP/2 and HPACK (`Zhttp`)<br>➕ multiplexed streams, flow control and header compression over TLS | N/A; Beast does not provide HTTP/2 |
| HTTP/3 and QPACK (`Zhttp`)<br>➕ HTTP messages, control streams and compression over Zquic<br>➖ nonzero blocked-stream buffering is outside the current QPACK profile | N/A; Beast does not provide HTTP/3 |
| `Zhttp::Pool`, `Client`<br>➕ pooled destinations with independent link/request concurrency, retry and protocol fallback | Partial: Beast client examples + custom pooling/request coordinator |
| `Zhttp::Server`<br>➕ typed admission, listeners, sessions and graceful drain across selected HTTP profiles | Partial: Beast server examples + custom runtime<br>➖ no native H2/H3 endpoints |

# Zws

## URI, handshake and frame codecs

[Public headers](../zws/src/) · [Module guide](../zws/README.md).
WebSocket counterpart: [Boost.Beast](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/using_websocket.html).
Zws supplies the WebSocket dependency of Zjrpc and Zmcp.

| Z facility | Boost | Folly | NTF |
| --- | --- | --- | --- |
| `Zws::URI`<br>➕ ws/wss URI interpretation for native profile setup | Partial: Boost.URL URI parsing + WebSocket-specific checks | Partial: [`folly::Uri`](https://github.com/facebook/folly/blob/main/folly/Uri.h)<br>➖ WebSocket scheme/profile validation is additional work | Partial: [`ntsa::Uri`](https://github.com/bloomberg/ntf-core/blob/main/groups/nts/ntsa/ntsa_uri.h)<br>➖ WebSocket scheme/profile validation needs application code |
| Opening handshake, H1 Upgrade and H2/H3 Extended CONNECT<br>➕ binds the same WebSocket application to concrete HTTP profiles | Beast WebSocket handshake<br>➖ H2/H3 Extended CONNECT requires another HTTP stack | N/A | N/A |
| `Zws::Codec`, protocol frame/header utilities<br>➕ transport-independent masking, fragmentation and control-frame processing | Beast WebSocket frame/message operations | N/A | N/A |
| `Zws::TxLayer`<br>➕ reference-buffer output and explicit fragment/message boundaries | Beast WebSocket write/partial-write operations | N/A | N/A |

## WebSocket endpoints

| Z facility | Boost |
| --- | --- |
| `Zws::Config` and link state<br>➕ bounded messages with handshake/close/heartbeat policy and per-link state | Partial: Beast WebSocket options + application state |
| `Zws::Client`, `Server` and native links<br>➕ CRTP message callbacks and streaming output over H1/H2/H3 bindings | Beast `websocket::stream` + application accept/connect lifecycle |

# Zrest

## Typed request/response contracts

[Public headers](../zrest/src/) · [Module guide](../zrest/README.md)

| Z facility | Boost | Folly |
| --- | --- | --- |
| `Zrest::Request`, `Response`, `Reply` and catalog macros<br>➕ compile-time methods, paths, body policies and allowed typed replies | Partial: Beast + Describe/JSON<br>➖ typed REST contracts and routing require application code | N/A |
| `Zrest::Input`, `BorrowedData`, `RequestContext`<br>➕ bounded entity assembly with explicit owned/borrowed decoding policy | Partial: Beast Body storage + Boost.JSON conversion | Partial: [`folly::IOBufQueue + dynamic JSON conversion`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no REST request-context or reflected ownership/binding policy |
| REST query/body signing hooks and `REST::Signature`<br>➕ filters the signature field and exposes the serialized bytes to the signer<br>➖ algorithm, verification and wire-coverage policy are application-owned | Partial: Beast/JSON serialization + application signing logic | N/A |

## Adapters, completion and server-sent events

| Z facility | Boost | Folly |
| --- | --- | --- |
| `MReqBuilder`, `MResParser`, `MReqParser`, `MResBuilder`<br>➕ shared catalog adapters for typed operations over Zhttp profiles | Partial: Beast HTTP parser/serializer + Describe/JSON binding | N/A |
| `Zrest::Completion`<br>➕ one-shot asynchronous typed completion with route invalidation | Partial: Asio completion handlers + application token/lifetime state | Partial: [`folly::Promise / Future`](https://github.com/facebook/folly/blob/main/folly/futures/Future.h)<br>➖ no REST route invalidation or allowed-reply catalog |
| `Zrest::SSEProducer`, `SSEBuilder`, SSE reply metadata<br>➕ bounded producer queues and incremental typed SSE delivery<br>➖ no automatic replay or reconnection | Partial: Beast streaming bodies + custom SSE/lifecycle state | N/A |

## REST endpoints

| Z facility | Boost |
| --- | --- |
| `Zrest::Client`, `Server`, endpoint configs and limits<br>➕ typed calls/routes, admission limits and full REST/HTTP drain boundary | Partial: Beast + JSON/Describe + Asio<br>➖ no complete matching Boost REST endpoint framework |

# Zgrpc

## Method, metadata and status vocabulary

[Public headers](../zgrpc/src/) · [Module guide](../zgrpc/README.md).
Zpb supplies native protobuf payloads; Zhttp supplies H2/H3 transport.

| Z facility | Boost |
| --- | --- |
| `Zgrpc::Method`, `Message`, `Reply` and catalog macros<br>➕ typed method paths, RPC cardinality and response contracts | Partial: Boost.Describe / Hana<br>➖ no gRPC method/catalog runtime |
| `Zgrpc::Metadata`, `Status`, timeout and binary-value codecs<br>➕ bounded initial/trailing metadata and gRPC status/deadline interpretation | N/A; generic HTTP fields do not implement these gRPC rules |

## Envelope, protobuf and HTTP adapters

| Z facility | Boost | Folly |
| --- | --- | --- |
| `Zgrpc::FrameRx` and pooled frame output<br>➕ incremental five-byte envelopes with byte budgets and retained payload backing | Partial: Asio buffers + custom envelope codec | N/A |
| `Zgrpc::PBMessage` and typed payload adapters<br>➕ Zpb verification before typed construction and explicit borrowed-data policy | N/A; no native Boost protobuf/gRPC payload binding | N/A |
| `Zgrpc::RxInput`, `TxFrames`, `TxBody`<br>➕ bounded receive turns and retained frame queues for HTTP production | Partial: Asio queues/composed operations + custom stream state | Partial: [`folly::IOBufQueue`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h)<br>➖ no protobuf verification, gRPC envelope or bounded RPC turn policy |
| Request/response HTTP and multi-method adapters<br>➕ method-specific metadata, streaming bodies and terminal trailers over H2/H3 | N/A; Beast lacks H2/H3 and gRPC adapters | N/A |

## Call ownership and endpoints

| Z facility | Boost |
| --- | --- |
| `Zgrpc::ClientCall`, `ServerCall`, call tokens and directional state<br>➕ invalidatable Rx/Tx routes, cardinality and one-shot finish/cancel admission | Partial: Asio cancellation and completion primitives<br>➖ gRPC call semantics remain custom work |
| `Zgrpc::Client`, `Server`, endpoint configs and limits<br>➕ unary/client-streaming/server-streaming/duplex RPCs, deadlines and bounded shutdown | N/A; no complete Boost gRPC endpoint library |

# Zjrpc

## JSON-RPC envelopes and declarations

[Public headers](../zjrpc/src/) · [Module guide](../zjrpc/README.md).
JSON counterparts use [Boost.JSON](https://www.boost.org/doc/libs/latest/libs/json/doc/html/index.html);
JSON parsing alone does not supply JSON-RPC dispatch or correlation.

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| `Zjrpc::Request`, `Response`, IDs, `Envelope`, `Parsed` and wire views<br>➕ typed parameters/results and distinct missing/null IDs with retained JSON views<br>➖ numeric IDs use signed 64-bit integers; no fractional/exponent forms | Partial: Boost.JSON values/conversion + custom JSON-RPC schema | Partial: [`folly::dynamic / parseJson / toJson`](https://github.com/facebook/folly/blob/main/folly/json/json.h)<br>➖ no JSON-RPC envelope declarations or typed RPC dispatch | N/A |
| `Zjrpc::Input`, `Output`, `BufOutput`<br>➕ bounded pooled JSON assembly and serialization | Partial: Boost.JSON parser/serializer with custom pooled storage | Partial: [`folly::IOBufQueue + JSON parser/serializer`](https://github.com/facebook/folly/blob/main/folly/io/IOBufQueue.h) | Partial: [`baljsn::DatumUtil + bdlbb::Blob`](https://github.com/bloomberg/bde/blob/main/groups/bal/baljsn/baljsn_datumutil.h)<br>➖ JSON buffering/serialization only; no JSON-RPC framing or ownership contract |

## Correlation, completion and batches

| Z facility | Boost |
| --- | --- |
| `Zjrpc::PendingCalls`, `Caller`<br>➕ outbound ID correlation removed before typed terminal callbacks | Partial: Boost.Unordered + Asio completion state |
| `Zjrpc::InboundCalls`<br>➕ separate live requests and bounded recent-ID history<br>➖ history contains IDs, not replayable replies | Partial: Boost.MultiIndex + custom admission/history policy |
| `Zjrpc::Completion` and completion slots<br>➕ one-shot claims before reply posting and invalidation before cancellation callbacks | Partial: Asio completion tokens + custom one-shot ownership |
| `Zjrpc::Dispatcher`, `Batch`, `BatchReply`<br>➕ shared typed dispatch, notifications and bounded batch aggregation | Partial: Boost.JSON + custom RPC dispatcher |

## Transport bindings and endpoints

| Z facility | Boost | Folly |
| --- | --- | --- |
| `Zjrpc::StdioConfig`, `StdioFramer`, `IOEndpoint`<br>➕ line framing and isolated blocking workers over owned per-run handle duplicates | Partial: Asio descriptors/handles or Iostreams + custom line/RPC runtime | Partial: [`folly::AsyncPipeReader / AsyncPipeWriter`](https://github.com/facebook/folly/blob/main/folly/io/async/AsyncPipe.h)<br>➖ no newline JSON-RPC framing or complete stdio endpoint lifecycle |
| JSON-RPC HTTP/SSE and WebSocket bindings<br>➕ shared RPC work/correlation over Zhttp/Zws channels | Partial: Beast HTTP/WebSocket + JSON<br>➖ RPC/SSE work and lifecycle remain application code | N/A |
| `Zjrpc::IOClient`, `IOServer`, `HTTPClient`, `HTTPServer`, `WSClient`, `WSServer`<br>➕ typed calls/notifications/batches and coordinated restart after drain | N/A; no complete matching Boost JSON-RPC endpoint library | N/A |

# Zmcp

## Typed tools and protocol vocabulary

[Public headers](../zmcp/src/) · [Module guide](../zmcp/README.md).
Uses Zjrpc, Zws and Zhttp; it has no dependency on Zrest despite shared metadata conventions.

| Z facility | Boost |
| --- | --- |
| `Zmcp::Request`, `Response`, tool catalogs and generated schemas<br>➕ reflected tools/list/tools/call with typed input/output schemas<br>➖ current scope is tool providers/callers; prompts/resources and other MCP subsystems are deferred | Partial: Boost.Describe + JSON<br>➖ MCP schema/catalog policy is additional work |
| `Zmcp::ToolAnnotations`, `MCP::Header`<br>➕ compile-time tool semantics drive schema/header output and trusted retry policy | N/A |
| MCP peer negotiation and catalog cache<br>➕ discovery/legacy handshake and explicit catalog invalidation | N/A |
| `Zmcp::Context` and extended completion policy<br>➕ transport/session/stream context with cancellation, progress and logging<br>➖ context pointers are callback-scoped | Partial: Asio handlers/cancellation + custom MCP policy |

## MCP endpoints

| Z facility | Boost |
| --- | --- |
| `Zmcp::Batch` and shared JSON-RPC aggregation<br>➕ typed aggregate tool calls reuse the ordinary pending-call table | Partial: Boost.JSON + custom MCP/RPC aggregation |
| `Zmcp::IOClient`, `IOServer`<br>➕ newline-delimited MCP over inherited stdio with bounded work and lifecycle<br>➖ peer process creation/supervision is application-owned | Partial: Boost.Process + custom MCP protocol/runtime |
| `Zmcp::HTTPClient`, `HTTPServer`, `WSClient`, `WSServer`<br>➕ shared typed tool handlers over Streamable HTTP and WebSockets with full drain<br>➖ authentication/authorization remain application-owned | Partial: Beast/Asio + JSON<br>➖ no complete Boost MCP endpoint library |

# Zdb

## Schema, rows and indices

[Public headers](../zdb/src/) · [Module guide](../zdb/README.md).
Zfb supplies wire/schema reflection and Zv supplies runtime configuration.

| Z facility | STL | Boost | Folly | BDE |
| --- | --- | --- | --- | --- |
| `ZdbTable<T>`, `ZdbTableDerive` and reflected key/schema metadata<br>➕ typed primary/secondary indices, grouped queries and generated BFBS schema<br>➖ immutable application schema is selected before startup | N/A | Partial: Boost.MultiIndex + Describe<br>➖ no persistence/schema-store integration | N/A | N/A |
| `ZdbRow`, `ZdbRowRef`, `ZdbTblRef`<br>➕ intrusive row/table lifetime and shard-owned mutation callbacks | Partial: `std::shared_ptr`<br>➖ no database row state or shard ownership contract | Partial: Boost.Intrusive / SmartPtr | N/A | Partial: [`bsl::shared_ptr`](https://github.com/bloomberg/bde/blob/main/groups/bsl/bslstl/bslstl_sharedptr.h)<br>➕ allocator-aware shared ownership<br>➖ no Zdb row/schema lifecycle |
| Zdb buffer and message formats (`ZdbBuf.hh`, `ZdbMsg.hh`)<br>➕ reflected replication/store records share reference-owned I/O backing | N/A | Partial: Boost.Serialization<br>➖ different wire layout and ownership model | Partial: [`folly::IOBuf`](https://github.com/facebook/folly/blob/main/folly/io/IOBuf.h)<br>➖ backing ownership only; no Zdb wire/schema codec | N/A |

## Database operations and lifecycle

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| Zdb find/insert/update/delete and row caches<br>➕ typed asynchronous operations with write-through caching and index maintenance<br>➖ local commit acceptance is not synchronous persistence/replication confirmation | Partial: Boost.MultiIndex cache + custom store/transaction runtime | Partial: [`folly::EvictingCacheMap`](https://github.com/facebook/folly/blob/main/folly/container/EvictingCacheMap.h)<br>➖ cache only; no typed database writes, persistence or index coordination | Partial: [`bdlcc::Cache`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlcc/bdlcc_cache.h)<br>➖ in-memory cache only; no database writes, replication or persistence |
| Zdb select/count and queue barriers<br>➕ index-based backing-store queries ordered with writes | Partial: database client + custom cache/store queue coordination | N/A | N/A |
| `Zdb`, `ZdbCf`, `ZdbHandler`<br>➕ DB-wide shards, configured workers, activation callbacks and orderly drain | N/A; Asio/Statechart primitives do not form a database runtime | N/A | N/A |

## Replication and recoverable workflows

| Z facility | Boost |
| --- | --- |
| `ZdbHost` and replication/activation<br>➕ leader election, asynchronous chain replication and failover recovery<br>➖ asynchronous replication is not a synchronous consensus commit guarantee | N/A |
| `ZdbSaga`, `ZdbSagaDB`, `ZdbSagaPhase`, `ZdbSagaRepeatPhase`<br>➕ journaled workflow steps, recovery and application compensation<br>➖ arbitrary external effects are not automatically journaled or exactly-once | Partial: Boost.Statechart / MSM<br>➖ no durable saga journal, database mutation protocol or recovery |
| `Ztc::DB`, `DBHost`, `DBTable` telemetry<br>➕ reflected database, host and table statistics | Partial: application records + Boost.Describe / Serialization |

## Pluggable backing stores

[Store contract](../zdb/src/ZdbStore.hh).
Boost.MultiIndex supplies in-memory indices; [Boost.MySQL](https://www.boost.org/doc/libs/latest/libs/mysql/doc/html/index.html)
is a database client, not an equivalent of Zdb's database/store framework.

| Z facility | Boost | Folly | BDE |
| --- | --- | --- | --- |
| `Zdb_::Store`, `StoreTbl` and store-module loading<br>➕ asynchronous store contract with reflected schemas and query/write callbacks | Partial: Boost.DLL + custom database adapter<br>➖ no matching Boost backing-store interface | N/A | N/A |
| `ZdbMem::Store` / `libZdbMem`<br>➕ in-memory implementation of the same store contract<br>➖ process loss loses locally stored data | Partial: Boost.MultiIndex<br>➖ persistence/replication/store contracts require custom code | Partial: [`folly::F14FastMap / ConcurrentHashMap`](https://github.com/facebook/folly/blob/main/folly/container/F14Map.h)<br>➖ in-memory indices only; no Zdb store/query contract | Partial: [`bdlc::FlatHashMap / bdlcc::StripedUnorderedMap`](https://github.com/bloomberg/bde/blob/main/groups/bdl/bdlc/bdlc_flathashmap.h)<br>➖ indices only; no Zdb store/query contract |

# ZdbPQ

## PostgreSQL backing store

[Public API](../zdb_pq/src/ZdbPQ.hh) · [Module guide](../zdb_pq/README.md)

| Z facility | Boost |
| --- | --- |
| `ZdbPQ` / `libZdbPQ`<br>➕ asynchronous libpq store with reflected prepared SQL and binary I/O<br>➖ requires PostgreSQL/libpq and configured server-side types | N/A; Boost.MySQL speaks a different database protocol |
| PostgreSQL `libz` type extension<br>➕ native database representations for Z decimal, time and bitmap values | N/A |

# ZdbSL

## SQLite backing store and inspection

[Public headers](../zdb_sqlite/src/) · [Module guide](../zdb_sqlite/README.md)

| Z facility | Boost |
| --- | --- |
| `ZdbSL::Store` / `libZdbSL`<br>➕ reflected persistent store with WAL and one isolated worker per connection<br>➖ local filesystem and one writer connection per store | N/A; no matching Boost SQLite backing-store implementation |
| `ZdbSL` storage codecs and inspection extension<br>➕ canonical sortable Z scalar encodings and readable SQLite inspection helpers | Partial: Boost.Endian / Serialization<br>➖ Z logical encodings and SQLite extension require custom work |
