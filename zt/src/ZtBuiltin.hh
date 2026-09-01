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

private:
  template <typename ...Args> struct IsSelf : public ZuFalse { };
  template <typename Arg>
  struct IsSelf<Arg> : public ZuIsBase<ZuDecay<Arg>, ZtBuiltin> { };

protected:
  using Array::copy___;
  using Array::move___;
  using Array::shadow__;
  using Array::own__;
  using Array::assign;
  using Array::length_;

  T *builtinData_() {
    if constexpr (BuiltinSize)
      return reinterpret_cast<T *>(&m_data_[0]);
    else
      return nullptr;
  }
  const T *builtinData_() const {
    if constexpr (BuiltinSize)
      return reinterpret_cast<const T *>(&m_data_[0]);
    else
      return nullptr;
  }

public:
  using Array::length;

  ZtBuiltin() noexcept(ZuNXConstruct<Array>{}) :
    Array(builtinData_(), 0, BuiltinSize, false) { }

  ZtBuiltin(const ZtBuiltin &a) noexcept(ZuNXCopy<T>{}) :
    Array(builtinData_(), 0, BuiltinSize, false)
  {
    auto length = a.length();
    if (!length) return;
    if (length <= BuiltinSize) {
      copy___(a.data(), length);
      own__(builtinData_(), length, BuiltinSize, false);
    } else {
      this->size(length);
      copy___(a.data(), length);
    }
  }
  ZtBuiltin(ZtBuiltin &&a) noexcept(ZuNXMove<T>{}) :
    Array(builtinData_(), 0, BuiltinSize, false)
  {
    auto length = a.length();
    if (!length) return;
    if (length <= BuiltinSize) {
      move___(a.data(), length);
      own__(builtinData_(), length, BuiltinSize, false);
      a.null();
    } else {
      if (!a.mutable_())
	shadow__(a.data(), length);
      else {
	own__(a.data(), length, a.size(), a.vallocd());
	a.mutable_(false);
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

  template <typename ...Args, ZuIfT<
    !IsSelf<Args...>{} &&
    ZuIsConstructible<ZuTypeList<Args...>, Array>{}, int> = 0>
  ZtBuiltin(Args &&...args) : Array(ZuFwd<Args>(args)...) { }

  template <typename Arg>
  ZtBuiltin &operator =(Arg &&arg) {
    assign(ZuFwd<Arg>(arg));
    return *this;
  }

  void length(uint64_t length) {
    this->length(length, !ZuTraits<T>::IsPrimitive);
  }
  void length(uint64_t length, bool initElems_) {
    if constexpr (BuiltinSize) {
      auto builtinData = builtinData_();
      if (ZuLikely(length <= BuiltinSize && this->data() == builtinData)) {
	if (initElems_) {
	  auto n = this->length();
	  if (length > n)
	    this->initElems(builtinData + n, length - n);
	  else if (length < n)
	    this->destroyElems(builtinData + length, n - length);
	}
	length_(length);
	return;
      }
    }
    Array::length(length, initElems_);
  }

private:
  alignas(T) uint8_t	m_data_[BuiltinSize * sizeof(T)];
};

#endif /* ZtBuiltin_HH */
