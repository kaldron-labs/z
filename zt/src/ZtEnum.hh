//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// good ole' plain enum wrapper
// - intentionally NOT the path taken by `enum class`
// - plain integer ordinals
// - most use cases are satisfied by ZtEnumNS or ZtEnumStruct
// - the enum is wrapped in a containing namespace or
//   struct and accompanied by compile-time machinery to
//   efficiently match names and convert between ordinals
//   and names. The implementation should live in a `.cc`
//   file and is defined using ZtEnumImpl(ID[, Map])
//
// - ZtEnumValues(Type, Names...)
//   - Type is most often int8_t (T must be signed)
//   - expands to:
//     using T = Type;
//     enum { Names..., N };
//
// - ZtEnumMap(API, ID, Map, Names...)
//   - requires a preceding ZtEnumValues
//   - Names... can optionally include one or two trailing
//     sentinel names ..., Unknown, Null
//     - these specify names for ordinals that are
//       unknown (>= N) and/or null (< 0)
//   - expands to:
//     struct API Map_ {
//       const auto &id() { return #ID; } // compile-time ID
//       // compile-time name matcher using ZuMatcher
//       T s2v(ZuCSpan s); // map name to ordinal
//       ZuCSpan v2s(int i); // map ordinal to name
//       // iterates over all ordinals calling l(name, ordinal)
//       template <typename L> void all(L &&l);
//     };
//
// - ZtEnumNames(API, ID, Names...)
//   - declares a default ZtEnumMap with name(int) and lookup(ZuCSpan)
//   - expands to:
//     // see above; "Map" is the name of the default map
//     ZtEnumMap(API, ID, Map, Names...);
//     ZuCSpan name(int i); // map ordinal to name with Map
//     T lookup(ZuCSpan s); // map name to ordinal with Map
//
// - ZtEnum(API, ID, Type, Names...)
//   - expands to:
//     ZtEnumValues(Type, Names...);
//     ZtEnumNames(API, ID, Names...);
//
// - ZtEnumNS(API, ID, Type, Names...)
//   - expands to:
//     namespace ID { ZtEnum(API, ID, Type, Names...); }
//
// - ZtEnumStruct(API, ID, Type, Names...)
//   - expands to:
//     struct ID { ZtEnum(API, ID, Type, Names...); }

#ifndef ZtEnum_HH
#define ZtEnum_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

#include <zlib/ZuBox.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuAssert.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZtFmt.hh>

// ZtEnum class declaration macros
//   Note: use in this order: Values; Map; Flags;...

#define ZtEnumMap_(API, ID, Map, ...) \
  struct API Map##_ { \
    using Names = ZuStringTL<__VA_ARGS__>; \
    static constexpr const auto &id() { return #ID; } \
    static T s2v(ZuCSpan); \
    static T match(ZuCSpan); \
    static ZuCSpan v2s(int); \
    template <typename L> \
    static void all(L &&l) { \
      ZuUnroll::all<Names>([&l]<typename Name>() { \
	l(ZuCSpan(Name{}), unsigned(ZuTypeIndex<Name, Names>{})); \
      }); \
    } \
  }

#define ZtEnumNames(API, ID, ...) \
  ZtEnumMap_(API, ID, Map, ZuPP_Eval(ZuPP_MapComma(ZuPP_Q, __VA_ARGS__))); \
  ZuInline ZuCSpan name(int i) { return Map_::v2s(i); } \
  ZuInline T lookup(ZuCSpan s) { return Map_::s2v(s); }

#define ZtEnumValues(Type, ...) \
  using T = Type; \
  enum { __VA_ARGS__, N }

#define ZtEnum(API, ID, Type, ...) \
  ZtEnumValues(Type, __VA_ARGS__); \
  ZtEnumNames(API, ID, __VA_ARGS__) \
  struct Map : public Map_ { }

#define ZtEnumNS(API, ID, Type, ...) \
  namespace ID { ZtEnum(API, ID, Type, __VA_ARGS__); }

#define ZtEnumStruct(API, ID, Type, ...) \
  struct ID { ZtEnum(API, ID, Type, __VA_ARGS__); }

#define ZtEnumMap(API, ID, Map, ...) \
  ZtEnumMap_(API, ID, Map, __VA_ARGS__); \
  struct Map : public Map##_ { }

#define ZtFlagsMap_(ID, Map) \
  struct Map : public Map_ { \
    struct Print { \
      T		v; \
      ZuCSpan	delim = "|"; \
      template <typename S> \
      friend inline S &operator <<(S &s, const Print &self) { \
	if (!self.v) return s; \
	bool first = true; \
	T mask = 1; \
	for (unsigned i = 0; i < N; i++, (mask <<= 1)) { \
	  if (self.v & mask) { \
	    if (ZuCSpan s_ = v2s(i)) { \
	      if (!first) s << self.delim; \
	      s << s_; \
	      first = false; \
	    } \
	  } \
	} \
	return s; \
      } \
    }; \
    struct Scan { \
      using B = ZuBox<T>; \
      B v = 0; \
      operator T() const { return v; } \
      Scan() = default; \
      Scan(ZuCSpan s, ZuCSpan delim = "|") { scan(s, delim); } \
      int scan(ZuCSpan s, ZuCSpan delim = "|") { \
	auto r = eov(s, delim); \
	v = r.template p<1>(); \
	return r.template p<0>(); \
      } \
      static ZuTuple<int, B> eov( \
          ZuCSpan s, ZuCSpan delim = "|") { \
	auto begin = s.data(); \
	auto length = s.length(); \
	auto n = delim.length(); \
	B out = 0; \
	s.trim(); \
	if (!s) return {int(length), out}; \
	bool matched = false; \
	while (s) { \
	  auto i = match(s); \
	  if (i >= N) { \
	    if (!matched) return {-1, B{}}; \
	    break; \
	  } \
	  matched = true; \
	  out |= (T(1)<<i); \
	  s.offset(v2s(i).length()); \
	  if (!s) return {int(length), out}; \
	  s.trim(); \
	  if (!s) return {int(length), out}; \
	  auto consumed = int(s.data() - begin); \
	  if (!n) return {consumed, out}; \
	  if (s.length() < n) break; \
	  if (ZuCSpan(&s[0], n) != delim) break; \
	  auto next = s; \
	  next.offset(n); \
	  next.trim(); \
	  if (!next || match(next) >= N) return {consumed, out}; \
	  s = next; \
	} \
	return {int(s.data() - begin), out}; \
      } \
    }; \
  }

#define ZtFlagsMap(API, ID, Map, ...) \
  ZtEnumMap_(API, ID, Map, __VA_ARGS__); \
  ZtFlagsMap_(ID, Map);

#define ZtFlag_(V) V##_
#define ZtFlagValue_(Type, V) \
  constexpr Type V() { return (Type(1)<<V##_); }

#define ZtFlags_(ID, Type, ...) \
  ZtEnumValues(Type, ZuPP_Eval(ZuPP_MapComma(ZtFlag_, __VA_ARGS__))); \
  ZuPP_Eval(ZuPP_MapArg(ZtFlagValue_, Type, __VA_ARGS__))

#define ZtFlags(API, ID, Type, ...) \
  ZtFlags_(ID, Type, __VA_ARGS__) \
  ZtEnumNames(API, ID, __VA_ARGS__) \
  ZtFlagsMap_(ID, Map);

#define ZtFlagsNS(API, ID, Type, ...) \
  namespace ID { ZtFlags(API, ID, Type, __VA_ARGS__); }

#define ZtFlagsStruct(API, ID, Type, ...) \
  struct ID { ZtFlags(API, ID, Type, __VA_ARGS__); }

// --- enum implementation code (lives in `.cc` / `.cpp`)

// ZtEnumImpl(ID[, Map])
// - defines `s2v`, `match` and `v2s` for enum ID
// - use in .cc
#define ZtEnumImpl_v2s() \
  if (i >= N) { \
    return []<unsigned N_ = N>() -> ZuCSpan { \
      if constexpr (Names::N > N_) \
	return ZuType<N_, Names>{}(); \
      else \
	return "Unknown"; \
    }(); \
  } \
  if (i < 0) { \
    return []<unsigned N_ = N>() -> ZuCSpan { \
      if constexpr (Names::N > N_ + 1) \
	return ZuType<N_ + 1, Names>{}(); \
      else \
	return ""; \
    }(); \
  } \
  return ZuSwitch::dispatch<Names::N>(i, [](auto I) { \
    return ZuCSpan(ZuType<I, Names>{}); \
  })
#define ZtEnumImpl_2(ID, Map) \
  static auto Map##_matcher = ZuMatcher<Map##_::Names>(); \
  T Map##_::s2v(ZuCSpan s) { return Map##_matcher.exact(s); } \
  T Map##_::match(ZuCSpan s) { return Map##_matcher.match(s); } \
  ZuCSpan Map##_::v2s(int i) { ZtEnumImpl_v2s(); }
#define ZtEnumImpl_1(ID) ZtEnumImpl_2(ID, Map)
#define ZtEnumImpl_N(_0, _1, Fn, ...) Fn
#define ZtEnumImpl(...) \
  ZtEnumImpl_N(__VA_ARGS__, ZtEnumImpl_2, ZtEnumImpl_1)(__VA_ARGS__)

// ZtEnumImplNS(ID[, Map])
// - shorthand for namespace-wrapped enums
#define ZtEnumImplNS(ID, ...) \
  namespace ID { ZtEnumImpl(ID __VA_OPT__(,) __VA_ARGS__); }

// ZtEnumImplStruct(ID[, Map]);
// - shorthand for struct-wrapped enums
#define ZtEnumImplStruct_2(ID, Map) \
  static auto ID##_##Map##_matcher = ZuMatcher<ID::Map##_::Names>(); \
  ID::T ID::Map##_::s2v(ZuCSpan s) { \
    return ID##_##Map##_matcher.exact(s); \
  } \
  ID::T ID::Map##_::match(ZuCSpan s) { \
    return ID##_##Map##_matcher.match(s); \
  } \
  ZuCSpan ID::Map##_::v2s(int i) { ZtEnumImpl_v2s(); }
#define ZtEnumImplStruct_1(ID) ZtEnumImplStruct_2(ID, Map)
#define ZtEnumImplStruct(...) \
  ZtEnumImpl_N(__VA_ARGS__, \
    ZtEnumImplStruct_2, ZtEnumImplStruct_1)(__VA_ARGS__)

#endif /* ZtEnum_HH */
