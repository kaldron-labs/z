//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZpktTestCf_HH
#define ZpktTestCf_HH

#include <zlib/ZfCf.hh>

namespace ZpktTest {

using Node = ZfCf::AnyNode;

inline auto object(Node *parent = nullptr) { return ZfCf::newNode<Node::Object>(parent); }

inline Node *object(Node *parent, ZuCSpan key)
{
  auto child = object(parent);
  auto ptr = child.ptr();
  parent->data<Node::Object>().push(Node::Field{key, ZuMv(child)});
  return ptr;
}

template <typename T> void value(Node *parent, ZuCSpan key, const T &value)
{
  Node::String text;
  text << value;
  parent->data<Node::Object>().push(Node::Field{
    key, ZfCf::newNode<Node::String>(parent, ZuMv(text))});
}

inline auto config(ZuCSpan module)
{
  auto root = object();
  value(root.ptr(), "module", module);
  object(root.ptr(), "interfaces");
  return root;
}

inline auto config(
  ZuCSpan module, ZuCSpan name, unsigned sender, int64_t rxSpin = -1, int64_t txSpin = -1)
{
  auto root = object();
  value(root.ptr(), "module", module);
  auto nif = object(object(root.ptr(), "interfaces"), name);
  value(nif, "txSid", sender);
  if (rxSpin >= 0) value(nif, "rxSpin", rxSpin);
  if (txSpin >= 0) value(nif, "txSpin", txSpin);
  return root;
}

} // ZpktTest

#endif /* ZpktTestCf_HH */
