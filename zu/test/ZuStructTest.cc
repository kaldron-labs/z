//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuStruct.hh>
#include <zlib/ZuUnroll.hh>
#include <zlib/ZuDemangle.hh>

using namespace ZuTestUtil;

ZuStructFacet(JSON);
ZuStructFacet(Foo);
ZuStructFacet(Bar);

namespace ZtJSON {
  enum { Base64 = 0, Base32, Hex, String };
  enum { ISO = 0, FIX, CSV, Unix };
  template <unsigned Fmt_, unsigned Scale_, int NDP_>
  struct TimeFmt {
    static constexpr unsigned Fmt = Fmt_;	// ISO | FIX | CSV | Unix
    static constexpr unsigned Scale = Scale_;	// decimal exponent
    static constexpr int NDP = NDP_;		// aka precision
  };

  template <typename Field>
  using Props = typename ZuFields_JSON<Field>::Props;
}

namespace ZuFieldProp {
  namespace JSON {
    template <ZuString ID_> struct ID { }; // defaults to field ID
    template <uint8_t I> struct BytesFmt { };
    template <typename> struct NumberFmt { };
    template <typename> struct TimeFmt { };

    // shorthand
    using Base64 = BytesFmt<ZtJSON::Base64>;
    using Base32 = BytesFmt<ZtJSON::Base32>;
    using Hex = BytesFmt<ZtJSON::Hex>;
    using String = BytesFmt<ZtJSON::String>;

    template <uint8_t Scale, int8_t NDP>
    using ISO = TimeFmt<ZtJSON::TimeFmt<ZtJSON::ISO, Scale, NDP>>;
    template <uint8_t Scale, int8_t NDP>
    using FIX = TimeFmt<ZtJSON::TimeFmt<ZtJSON::FIX, Scale, NDP>>;
    template <uint8_t Scale, int8_t NDP>
    using CSV = TimeFmt<ZtJSON::TimeFmt<ZtJSON::CSV, Scale, NDP>>;
    template <uint8_t Scale, int8_t NDP>
    using Unix = TimeFmt<ZtJSON::TimeFmt<ZtJSON::Unix, Scale, NDP>>;

    // GetID<Field> - ZuStringT
    // - gets the JSON-specific ID for the field
    // - the Field is passed because the value defaults to Field::id()
    template <
      typename Field,
      bool = HasValue<typename Field::Props, ID>{}>
    struct GetID_ {
      using T = ZuStringT<Field::id()>;
    };
    template <typename Field>
    struct GetID_<Field, true> {
      using T = GetValue<typename Field::Props, ID>;
    };
    template <typename Field>
    using GetID = typename GetID_<Field>::T;

    // GetBytesFmt - ZuUnsigned
    template <typename Props, bool = HasValue<Props, BytesFmt>{}>
    struct GetBytesFmt_ { using T = ZuUnsigned<ZtJSON::Base64>; };
    template <typename Props>
    struct GetBytesFmt_<Props, true> {
      using T = GetValue<Props, BytesFmt>;
    };
    template <typename Props>
    using GetBytesFmt = typename GetBytesFmt_<Props>::T;

    // GetNumberFmt - ZuFmt
    template <typename Props, bool = HasType<Props, NumberFmt>{}>
    struct GetNumberFmt_ { using T = ZuFmt::Default; };
    template <typename Props>
    struct GetNumberFmt_<Props, true> { using T = GetType<Props, NumberFmt>; };
    template <typename Props>
    using GetNumberFmt = typename GetNumberFmt_<Props>::T;

    // GetTimeFmt - ZtJSON::TimeFmt::{Fmt,Scale,NDP}
    template <typename Props, bool = HasType<Props, TimeFmt>{}>
    struct GetTimeFmt_ { using T = ZtJSON::TimeFmt<ZtJSON::ISO, 0, 3>; };
    template <typename Props>
    struct GetTimeFmt_<Props, true> { using T = GetType<Props, TimeFmt>; };
    template <typename Props>
    using GetTimeFmt = typename GetTimeFmt_<Props>::T;
  }
}

namespace Foo {
  struct A {
    int i = 42;
    const char *j_ = "hello";
    const char *j() const { return j_; }
    void j(const char *s) { j_ = s; }
    double k = 42.0;
  };

  ZuStruct(A,
      ((i), (Keys<0>)),
      ((j, Fn), (Keys<1>)),
      ((k, Lambda,
	([](const A &a) { return a.k; }),
	([](A &a, double v) { a.k = v; })), (Keys<1>)));

  struct B {
    int i = 42;
    const char *j_ = "hello";
    const char *j() const { return j_; }
    double k = 42.0;
  };

  ZuStruct(B,
      ((i, Rd), (Keys<0>)),
      ((j, RdFn), (Keys<0>)),
      ((k, LambdaRd, ([](const B &b) { return b.k; }))));

  ZuStructRender(B, JSON,
    (i, JSON::ID<"i-JSON">),
    (j, JSON::ID<"j-JSON">, JSON::Base64),
    k);
  ZuStructRender(B, Foo,
    (i, JSON::ID<"i-Foo">),
    (j, JSON::Base32, (Keys<1>)),
    k);
}

int main(int argc, char **argv)
{
  parse(argc, argv);

  ZuTestMain();

  using A = Foo::A;
  using B = Foo::B;
  A a;
  ZuType<1, ZuFields<A>>::set(a, "bye");
  ZuType<2, ZuFields<A>>::set(a, 43.0);
  ZuUnroll::all<ZuFields<A>>([&a]<typename T>() mutable {
    log(T::id(), '=', T::get(a));
  });
  B b;
  ZuUnroll::all<ZuFields<B>>([&b]<typename T>() mutable {
    log(T::id(), '=', T::get(b));
  });
  log(ZuFieldAxor<A>()(a));
  log(ZuFieldAxor<A, 1>()(a));

  ZuUnroll::all<ZuFields_JSON<B>>([&b]<typename T>() mutable {
    log(T::id(), '=', T::get(b));
  });
  ZuUnroll::all<ZuFields_Foo<B>>([&b]<typename T>() mutable {
    log(T::id(), '=', T::get(b));
  });
  ZuUnroll::all<ZuFields_Bar<B>>([&b]<typename T>() mutable {
    log(T::id(), '=', T::get(b));
  });

  using T1 = ZuTuple<int, const char *>;
  using T2 = ZuStructKeyT<B, 0>;
  log("T1 = ", ZuDemangle<T1>{});
  log("T2 = ", ZuDemangle<T2>{});
  ZuCHECK((ZuIs_<T2, T1>{}));
  {
    using namespace ZuFieldProp::JSON;
    ZuCHECK((GetBytesFmt<Foo::ZuField(B, j)>{} == ZtJSON::Base64));
  }

  return 0;
}
