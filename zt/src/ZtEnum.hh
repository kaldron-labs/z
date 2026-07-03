//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// good ole' plain enum wrapper
// - intentionally the path not taken by C++ with `enum class`
// - plain integer ordinals
// - most use cases are satisfied by ZtEnumNS or ZtEnumStruct
// - the enum is wrapped in a containing namespace or
//   struct and accompanied by compile-time machinery to
//   efficiently match names and convert between ordinals
//   and names:
//
// - ZtEnumValues([Type], [Names...])
//   - [Type] is most often int8_t (T must be signed)
//   - expands to:
//     using T = [Type];
//     enum { [Names...], N };
//
// - ZtEnumMap([ID], [Map], [Names...])
//   - requires a preceding ZtEnumValues
//   - [Names...] can optionally include one or two trailing
//     sentinel names ..., [Unknown, [Null]]
//     - these specify names for ordinals that are
//       unknown (>= N) and/or null (< 0)
//   - expands to:
//     struct [Map] {
//       const auto &id() { return #[ID]; } // compile-time ID
//       // compile-time name matcher using ZuMatcher
//       constexpr auto matcher = ZuMatcher<[Names]>();
//       constexpr T s2v(ZuCSpan s); // map name to ordinal
//       constexpr ZuCSpan v2s(int i); // map ordinal to name
//       // iterates over all ordinals calling l(name, ordinal)
//       template <typename L> constexpr void all(L &&l);
//     };
//
// - ZtEnumNames([ID], [Names...])
//   - declares a default ZtEnumMap with name(int) and lookup(ZuCSpan)
//   - expands to:
//     // see above; "Map" is the name of the default map
//     ZtEnumMap([ID], Map, [Names...]);
//     constexpr ZuCSpan name(int i); // map ordinal to name with Map
//     constexpr T lookup(ZuCSpan s); // map name to ordinal with Map
//
// - ZtEnum([ID], [Type], [Names...])
//   - expands to:
//     ZtEnumValues([Type], [Names...]);
//     ZtEnumNames([ID], [Names...]);
//
// - ZtEnumNS([ID], [Type], [Names...])
//   - expands to:
//     namespace [ID] { ZtEnum([ID], [Type], [Names...]); }
//
// - ZtEnumStruct([ID], [Type], [Names...])
//   - expands to:
//     struct [ID] { ZtEnum([ID], [Type], [Names...]); }

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

#define ZtEnumMap_(ID, Map, ...) \
  struct Map##_ { \
    using Names = ZuStringTL<__VA_ARGS__>; \
    static constexpr const auto &id() { return #ID; } \
    static constexpr auto matcher = ZuMatcher<Names>(); \
    static constexpr T s2v(ZuCSpan s) { return matcher.match(s); } \
    template <unsigned N_ = N> \
    static constexpr ZuCSpan v2s(int i) { \
      if (i >= N) { \
	if constexpr (Names::N > N_) \
	  return ZuType<N_, Names>{}(); \
	else \
	  return "Unknown"; \
      } \
      if (i < 0) { \
	if constexpr (Names::N > N_ + 1) \
	  return ZuType<N_ + 1, Names>{}(); \
	else \
	  return ""; \
      } \
      return ZuSwitch::dispatch<Names::N>(i, [](auto I) { \
	return ZuCSpan(ZuType<I, Names>{}); \
      }); \
    } \
    template <typename L> \
    static constexpr void all(L &&l) { \
      ZuUnroll::all<Names>([&l]<typename Name>() { \
	l(ZuCSpan(Name{}), unsigned(ZuTypeIndex<Name, Names>{})); \
      }); \
    } \
  }

#define ZtEnumNames(ID, ...) \
  ZtEnumMap_(ID, Map, ZuPP_Eval(ZuPP_MapComma(ZuPP_Q, __VA_ARGS__))); \
  ZuInline constexpr ZuCSpan name(int i) { return Map_::v2s(i); } \
  ZuInline constexpr T lookup(ZuCSpan s) { return Map_::s2v(s); }

#define ZtEnumValues(Type, ...) \
  using T = Type; \
  enum { __VA_ARGS__, N }

#define ZtEnum(ID, Type, ...) \
  ZtEnumValues(Type, __VA_ARGS__); \
  ZtEnumNames(ID, __VA_ARGS__) \
  struct Map : public Map_ { }

#define ZtEnumNS(ID, Type, ...) \
  namespace ID { ZtEnum(ID, Type, __VA_ARGS__); }

#define ZtEnumStruct(ID, Type, ...) \
  struct ID { ZtEnum(ID, Type, __VA_ARGS__); }

#define ZtEnumMap(ID, Map, ...) \
  ZtEnumMap_(ID, Map, __VA_ARGS__); \
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
      T		v; \
      operator T() const { return v; } \
      ZuInline static constexpr bool isspace__(char c) { \
	return ((c >= '\t' && c <= '\r') || c == ' '); \
      } \
      Scan(ZuCSpan s, ZuCSpan delim = "|") { \
	auto n = delim.length(); \
	v = 0; \
	while (s && isspace__(s[0])) s.offset(1); \
	while (s) { \
	  auto i = s2v(s); \
	  if (i < 0) break; \
	  v |= (T(1)<<i); \
	  s.offset(v2s(i).length()); \
	  while (s && isspace__(s[0])) s.offset(1); \
	  if (s.length() < n) break; \
	  if (ZuCSpan(&s[0], n) != delim) break; \
	  s.offset(n); \
	  while (s && isspace__(s[0])) s.offset(1); \
	} \
      } \
    }; \
  }

#define ZtFlagsMap(ID, Map, ...) \
  ZtEnumMap_(ID, Map, __VA_ARGS__); \
  ZtFlagsMap_(ID, Map);

#define ZtFlag_(V) V##_
#define ZtFlagValue_(Type, V) \
  constexpr Type V() { return (Type(1)<<V##_); }

#define ZtFlags_(ID, Type, ...) \
  ZtEnumValues(Type, ZuPP_Eval(ZuPP_MapComma(ZtFlag_, __VA_ARGS__))); \
  ZuPP_Eval(ZuPP_MapArg(ZtFlagValue_, Type, __VA_ARGS__))

#define ZtFlags(ID, Type, ...) \
  ZtFlags_(ID, Type, __VA_ARGS__) \
  ZtEnumNames(ID, __VA_ARGS__) \
  ZtFlagsMap_(ID, Map);

#endif /* ZtEnum_HH */
