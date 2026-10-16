//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZmNode - intrusive container node (used by ZmHash, ZmRBTree, ...)

#ifndef ZmNode_HH
#define ZmNode_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZmHeap.hh>

template <
  typename T,
  auto KeyAxor,
  auto ValAxor,
  typename Base,
  typename NodeExt,
  typename HeapID,
  bool Sharded,
  bool = ZuIs_<T, Base>{}>
class ZmNode_;

// node contains type
template <
  typename T_,
  auto KeyAxor_,
  auto ValAxor_,
  typename Base_,
  typename NodeExt,
  typename HeapID,
  bool Sharded>
class ZmNode_<T_, KeyAxor_, ValAxor_, Base_, NodeExt, HeapID, Sharded, false> :
    public ZmNHeap<
      ZmNode_<T_, KeyAxor_, ValAxor_, Base_, NodeExt, HeapID, Sharded>,
      !ZuIs_<HeapID, ZmNHeapDisable>{}>,
    public Base_,
    public NodeExt {
friend inline HeapID ZmNHeapID(ZmNode_ *);
friend inline ZuBool<Sharded> ZmNHeapSharded(ZmNode_ *);

public:
  using T = T_;
  static constexpr auto KeyAxor = KeyAxor_;
  static constexpr auto ValAxor = ValAxor_;
  using U = ZuDecay<T>;

  ZmNode_() = default;
  ZmNode_(const ZmNode_ &) = default;
  ZmNode_ &operator =(const ZmNode_ &) = default;
  ZmNode_(ZmNode_ &&) = default;
  ZmNode_ &operator =(ZmNode_ &&) = default;
  template <typename ...Args,
    decltype(U(ZuDeclVal<Args &&>()...), int()) = 0>
  ZmNode_(Args &&...args) : m_data{ZuFwd<Args>(args)...} { }
  template <typename Arg>
  ZmNode_ &operator =(Arg &&arg) {
    m_data = ZuFwd<Arg>(arg);
    return *this;
  }
  virtual ~ZmNode_() = default;

  ZuInline const auto &data() const & { return m_data; }
  ZuInline auto &data() & { return m_data; }
  ZuInline decltype(auto) data() && { return ZuMv(m_data); }

  ZuInline decltype(auto) key() const & { return KeyAxor(data()); }
  ZuInline decltype(auto) key() & { return KeyAxor(data()); }
  ZuInline decltype(auto) key() && { return KeyAxor(data()); }

  ZuInline decltype(auto) val() const & { return ValAxor(data()); }
  ZuInline decltype(auto) val() & { return ValAxor(data()); }
  ZuInline decltype(auto) val() && { return ValAxor(data()); }

private:
  U	m_data;
};

// node derives from type
template <
  typename T_,
  auto KeyAxor_,
  auto ValAxor_,
  typename Base_,
  typename NodeExt,
  typename HeapID,
  bool Sharded>
class ZmNode_<T_, KeyAxor_, ValAxor_, Base_, NodeExt, HeapID, Sharded, true> :
    public ZmNHeap<
      ZmNode_<T_, KeyAxor_, ValAxor_, Base_, NodeExt, HeapID, Sharded>,
      !ZuIs_<HeapID, ZmNHeapDisable>{}>,
    public ZuDecay<T_>,
    public NodeExt {
friend inline HeapID ZmNHeapID(ZmNode_ *);
friend inline ZuBool<Sharded> ZmNHeapSharded(ZmNode_ *);

public:
  using Base = ZuDecay<T_>;
  // ZuDerive_() causes gcc to choke on ZmPolyHash
  using Base::Base;
  template <typename ...Args>
  ZmNode_(Args &&...args) : Base(ZuFwd<Args>(args)...) { }

  using T = T_;
  static constexpr auto KeyAxor = KeyAxor_;
  static constexpr auto ValAxor = ValAxor_;
  using U = ZuDecay<T>;

  virtual ~ZmNode_() = default;

  ZuInline decltype(auto) data() const & {
    return static_cast<const U &>(*this);
  }
  ZuInline decltype(auto) data() & { return static_cast<U &>(*this); }
  ZuInline decltype(auto) data() && { return static_cast<U &&>(*this); }

  ZuInline decltype(auto) key() const & { return KeyAxor(data()); }
  ZuInline decltype(auto) key() & { return KeyAxor(data()); }
  ZuInline decltype(auto) key() && { return KeyAxor(data()); }

  ZuInline decltype(auto) val() const & { return ValAxor(data()); }
  ZuInline decltype(auto) val() & { return ValAxor(data()); }
  ZuInline decltype(auto) val() && { return ValAxor(data()); }
};

template <
  typename T,
  auto KeyAxor,
  auto ValAxor,
  typename Base,
  typename NodeExt,
  typename HeapID,
  bool Sharded>
using ZmNode =
  ZmNode_<T, KeyAxor, ValAxor, Base, NodeExt, HeapID, Sharded>;

#endif /* ZmNode_HH */
