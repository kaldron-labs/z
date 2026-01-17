//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// extends ZtArray<> with a built-in initial buffer
// - growth beyond the built-in size will use heap allocation
// - example:
//   ZuDerive(Buf, (ZtBuiltin<ZtArray<char, ZtArrayHeapID<"Buf">>, 32>));

#ifndef ZtBuiltin_HH
#define ZtBuiltin_HH

#ifndef ZtLib_HH
#include <zlib/ZtLib.hh>
#endif

template <typename Array, unsigned BuiltinSize_>
class ZtBuiltin : public Array {
public:
  static constexpr unsigned BuiltinSize = BuiltinSize_;
  using T = typename Array::T;

protected:
  using Array::copy___;
  using Array::move___;
  using Array::shadow__;
  using Array::own__;
  using Array::assign;

public:
  ZtBuiltin() noexcept(ZuNXConstruct<Array>{}) :
    Array(reinterpret_cast<T *>(&m_data_[0]), 0, BuiltinSize, false) { }

  ZtBuiltin(const ZtBuiltin &a) noexcept(ZuNXCopy<T>{}) :
    Array(reinterpret_cast<T *>(&m_data_[0]), 0, BuiltinSize, false)
  {
    auto length = a.length();
    if (length) copy___(a.data(), length);
  }
  ZtBuiltin(ZtBuiltin &&a) noexcept(ZuNXMove<T>{}) :
    Array(reinterpret_cast<T *>(&m_data_[0]), 0, BuiltinSize, false)
  {
    auto length = a.length();
    if (!length) return;
    if (length < BuiltinSize) {
      move___(a.data(), length);
      a.null();
    } else {
      if (!a.owned())
	shadow__(a.data(), length);
      else {
	own__(a.data(), length, a.size(), a.vallocd());
	a.owned(false);
      }
    }
  }
  ZtBuiltin &operator =(const ZtBuiltin &a) noexcept(ZuNXCopy<T>{}) {
    assign(a);
    return *this;
  }
  ZtBuiltin &operator =(ZtBuiltin &&a) noexcept(ZuNXMove<T>{}) {
    this->~ZtBuiltin();
    new (this) ZtBuiltin(ZuMv(a));
    return *this;
  }

  template <typename ...Args,
    decltype(Array(ZuDeclVal<Args &&>()...), int()) = 0>
  ZtBuiltin(Args &&...args) : Array(ZuFwd<Args>(args)...) { }

  template <typename Arg>
  ZtBuiltin &operator =(Arg &&arg) {
    assign(ZuFwd<Arg>(arg));
    return *this;
  }

private:
  alignas(T) uint8_t	m_data_[BuiltinSize * sizeof(T)];
};

#endif /* ZtBuiltin_HH */
