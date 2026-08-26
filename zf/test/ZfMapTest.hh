//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// heap-identified map containers for Zf unit tests

#ifndef ZfMapTest_HH
#define ZfMapTest_HH

#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>

template <typename Base_, typename Heap = ZuVoid>
struct ZfMapTest_ : public Heap, public Base_ {
  using Base = Base_;
  using Base::Base;
  using Base::operator =;

  class CIter {
    using Iter = decltype(ZuDeclVal<const Base &>().citer());
    using NodePtr = decltype(ZuDeclVal<Iter &>()());
    struct CNode {
      NodePtr node = nullptr;

      template <typename P>
      static auto key_(P node, int) -> decltype(node->key()) {
	return node->key();
      }
      template <typename P>
      static auto key_(P node, ...) -> decltype(node->template p<0>()) {
	return node->template p<0>();
      }
      template <typename P>
      static auto val_(P node, int) -> decltype(node->val()) {
	return node->val();
      }
      template <typename P>
      static auto val_(P node, ...) -> decltype(node->template p<1>()) {
	return node->template p<1>();
      }
      decltype(auto) key() const { return key_(node, 0); }
      decltype(auto) val() const { return val_(node, 0); }
    };
  public:
    CIter(const Base &base) : m_i{base.citer()} { }
    CNode *operator ()() {
      if (auto node = m_i()) { m_node.node = node; return &m_node; }
      return nullptr;
    }
  private:
    Iter	m_i;
    CNode	m_node;
  };
  CIter citer() const { return CIter{*this}; }
};

template <ZuString ID, typename Base>
using ZfMapTest =
  ZfMapTest_<Base, ZmHeap<ID, ZfMapTest_<Base>>>;

template <typename Base_>
struct ZfRefMapTest : public ZmObject, public Base_ {
  using Base = Base_;
  using Base::Base;
  using Base::operator =;

  ZfRefMapTest() = default;
  ZfRefMapTest(ZfRefMapTest &&o) : Base{ZuMv(o)} { }
  ZfRefMapTest &operator =(ZfRefMapTest &&o) {
    Base::operator =(ZuMv(o));
    return *this;
  }
};

#endif /* ZfMapTest_HH */
