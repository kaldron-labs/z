//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ring buffer configuration

#ifndef ZiRingParams_HH
#define ZiRingParams_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <limits.h>

#include <zlib/ZmRing.hh>

#include <zlib/ZiPlatform.hh>

#include <zlib/ZfCf.hh>

namespace ZiRing_ {

// ring buffer parameters

struct ParamData : public ZmRing_::ParamData {
  Zi::Path	name;
  unsigned	initial = 0; // creation default when size is zero
  unsigned	killWait = 1;
  bool		coredump = false;

  inline const ParamData &data() { return *this; } // upcast

  using Base = ZmRing_::ParamData;

  ParamData() = default;
  ParamData(const ParamData &) = default;
  ParamData(ParamData &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0>
  ParamData(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Args>(args)...), name{ZuFwd<Arg0>(arg0)} { }
  ParamData &operator =(const ParamData &) = default;
  ParamData &operator =(ParamData &&) = default;
};

template <typename Derived, typename Data = ParamData>
class Params_ : public ZmRing_::Params_<Derived, Data> {
  using Base = ZmRing_::Params_<Derived, Data>;

  Derived &&derived() { return ZuMv(*static_cast<Derived *>(this)); }

public:
  Params_() = default;
  Params_(const Params_ &) = default;
  Params_(Params_ &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params_(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params_ &operator =(const Params_ &) = default;
  Params_ &operator =(Params_ &&) = default;

  Derived &&name(ZuCSpan s) { Data::name = s; return derived(); }
  Derived &&initial(unsigned n) { Data::initial = n; return derived(); }
  Derived &&killWait(unsigned n) { Data::killWait = n; return derived(); }
  Derived &&coredump(bool b) { Data::coredump = b; return derived(); }

  const Zi::Path &name() const { return Data::name; }
  unsigned initial() const { return Data::initial; }
  unsigned killWait() const { return Data::killWait; }
  bool coredump() const { return Data::coredump; }
};

class Params : public Params_<Params> {
  using Base = Params_<Params>;

public:
  Params() = default;
  Params(const Params &) = default;
  Params(Params &&) = default;
  template <
    typename Arg0, typename ...Args,
    typename = ZuIsNot<Arg0, ZmRing_::ParamData>,
    decltype(Base(ZuDeclVal<Arg0 &&>(), ZuDeclVal<Args &&>()...), int()) = 0>
  Params(Arg0 &&arg0, Args &&...args) :
      Base(ZuFwd<Arg0>(arg0), ZuFwd<Args>(args)...) { }
  Params &operator =(const Params &) = default;
  Params &operator =(Params &&) = default;
};

ZfStruct(ZiAPI, (Params, Cf),
  ((name, Fn),		(Mutable, Required),			String),
  ((size, Fn),		(Mutable, (Range<8192U, INT_MAX>)),	UInt32),
  ((ll, Fn),		(Mutable),				Bool),
  ((spin, Fn),		(Mutable, (Range<0, INT_MAX>)),		Int32),
  ((timeout, Fn),	(Mutable, (Range<0, 3600>)),		Int32),
  ((killWait, Fn),	(Mutable, (Range<0, 3600>)),		Int32),
  ((coredump, Fn),	(Mutable),				Bool));

} // ZiRing_

using ZiRingParams = ZiRing_::Params;

inline ZiRingParams ZiRingParams_load(
    const ZfCf::AnyNode *cf, ZiRingParams params = {})
{
  if (cf) ZfCf::handler<ZiRingParams>(cf).update(params);
  return params;
}

#endif /* ZiRingParams_HH */
