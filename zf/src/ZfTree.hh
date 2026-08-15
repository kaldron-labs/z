//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// owned configuration tree

#ifndef ZfTree_HH
#define ZfTree_HH

#ifndef ZfLib_HH
#include <zlib/ZfLib.hh>
#endif

#include <limits.h>
#include <stdint.h>

#include <zlib/ZuPtr.hh>
#include <zlib/ZuDerive.hh>

#include <zlib/ZmHeap.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZtString.hh>

namespace ZfTree {

struct Node_HeapID : public ZuStringT<"ZfTree.Node"> { };

namespace ScalarTC {
  enum { None, String, Number, False, True, Null };
}

// node in a scan tree
class AnyNode {
  AnyNode() = delete;
  AnyNode(const AnyNode &) = delete;
  AnyNode &operator =(const AnyNode &) = delete;
  AnyNode(AnyNode &&) = delete;
  AnyNode &operator =(AnyNode &&) = delete;

public:
  AnyNode	*parent;
  int		type;
  int8_t	scalarType;

  AnyNode(AnyNode *parent_, int type_, int scalarType_) :
    parent{parent_}, type{type_}, scalarType{int8_t(scalarType_)} { }
  virtual ~AnyNode() = default;

  template <typename Data>
  bool has() const;

  template <typename Data>
  decltype(auto) data(this auto &&);

  // path is in "Member" format, see ZfURI.hh
  const AnyNode *resolve(ZuCSpan path) const;
  template <typename S> void path(S &s) const;

  static constexpr unsigned LNodeSize = 512;
  static constexpr unsigned SNodeSize = 48;

  static constexpr int StringSize =
    (SNodeSize - (sizeof(ZtString<>) - ZtString<>::BuiltinSize));
  ZuAssert(StringSize > 0);
  ZuDerive(String, (ZtString<
      ZtStringBuiltin<StringSize, ZtStringHeapID_<Node_HeapID>>>));

  ZuDerive(Field, (ZuTuple<String, ZuPtr<AnyNode>>));

  static constexpr int ArraySize =
    (LNodeSize - sizeof(ZtArray<ZuPtr<AnyNode>>)) / sizeof(ZuPtr<AnyNode>);
  static constexpr int ObjectSize =
    (LNodeSize - sizeof(ZtArray<Field>)) / sizeof(Field);
  ZuAssert(ArraySize > 0);
  ZuAssert(ObjectSize > 0);

  ZuDerive(Array, (ZtBuiltin<
      ZtArray<ZuPtr<AnyNode>, ZtArrayHeapID_<Node_HeapID>>, ArraySize>));
  ZuDerive(Object, (ZtBuiltin<
      ZtArray<Field, ZtArrayHeapID_<Node_HeapID>>, ObjectSize>));

  using TL = ZuTypeList<Array, Object, String>;

  template <typename T>
  using Index = ZuTypeIndex<T, TL>;
};

// value type enum (with same values as the AnyNode typelist indices)
namespace ValueTC {
  enum {
    Array = AnyNode::Index<AnyNode::Array>{},
    Object = AnyNode::Index<AnyNode::Object>{},
    String = AnyNode::Index<AnyNode::String>{}
  };
}

template <typename Data, typename Heap>
class Node_ : public Heap, public AnyNode {
  Node_(const Node_ &) = delete;
  Node_ &operator =(const Node_ &) = delete;
  Node_(Node_ &&) = delete;
  Node_ &operator =(Node_ &&) = delete;

public:
  using AnyNode::TL;

  template <typename ...Args>
  Node_(AnyNode *parent, Args &&...args) :
    AnyNode{
      parent,
      ZuTypeIndex<Data, TL>{}(),
      bool(ZuIs_<Data, AnyNode::String>{}) ? ScalarTC::String : ScalarTC::None},
    data(ZuFwd<Args>(args)...) { }
  ~Node_() = default;

  Data	data;
};

template <typename Data>
using Node_Heap = ZmHeap_<Node_HeapID, Node_<Data, ZuVoid>>;

template <typename Data>
struct Node : public Node_<Data, Node_Heap<Data>> {
  using Base = Node_<Data, Node_Heap<Data>>;
  using Base::Base;
  template <typename ...Args,
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0>
  Node(Args &&...args) : Base(ZuFwd<Args>(args)...) { }
};

template <typename Data>
inline bool AnyNode::has() const {
  return type == ZuTypeIndex<Data, TL>{};
}

template <typename Data>
inline decltype(auto) AnyNode::data(this auto &&self) {
  using Self = decltype(self);
  using NodeT = Node<Data>;
  return ZuFwdLike<Self>(ZuFwdLike<Self, NodeT>(self).data);
}

ZuInline constexpr bool isdigit__(char c) {
  return c >= '0' && c <= '9';
}

inline const AnyNode *AnyNode::resolve(ZuCSpan path) const {
  const AnyNode *node = this;
  while (path) {
    unsigned i = 0, n = path.length();
    if (path[0] == '[') {
      if (!node->has<Array>() || n < 3) return nullptr;
      unsigned index = 0;
      while (++i < n) {
	auto c = path[i];
	if (!isdigit__(c)) break;
	unsigned digit = c - '0';
	if (index > (UINT_MAX - digit) / 10) return nullptr;
	index = (index * 10) + digit;
      }
      if (i == 1 || i >= n || path[i] != ']') return nullptr;
      const auto &array = node->data<Array>();
      if (index >= array.length() || !array[index]) return nullptr;
      node = array[index].ptr();
      ++i;
      if (i < n) {
	if (path[i] == '[') { path.offset(i); continue; }
	if (path[i] != '.' || ++i >= n || path[i] == '[') return nullptr;
      }
    } else {
      if (!node->has<Object>() || path[0] == '.') return nullptr;
      while (++i < n && path[i] != '[' && path[i] != '.');
      ZuCSpan id{path.data(), i};
      const auto &fields = node->data<Object>();
      node = nullptr;
      for (auto &&field: fields)
	if (field.p<0>() == id) node = field.p<1>().ptr();
      if (!node) return nullptr;
      if (i < n && path[i] == '.') {
	if (++i >= n || path[i] == '[') return nullptr;
      }
    }
    path.offset(i);
  }
  return node;
}

// path() intentionally uses linear search
// - it is intended for diagnosing misconfiguration, nothing more
// - it is only used when throwing exceptions
//   - typically during a failing program start
// - it should never be called in a hot path
template <typename S> void AnyNode::path(S &s) const {
  if (!parent) return;
  parent->path(s);
  if (parent->has<Object>()) {
    if (parent->parent) s << '.';
    const auto &fields = parent->data<Object>();
    for (auto &&field: fields)
      if (field.p<1>().ptr() == this) {
	s << field.p<0>();
	break;
      }
  } else {
    const auto &array = parent->data<Array>();
    for (unsigned i = 0, n = array.length(); i < n; i++)
      if (array[i].ptr() == this) {
	s << '[' << i << ']';
	break;
      }
  }
}

using NodeArray = typename AnyNode::Array;
using CNodeArray = const NodeArray;

template <typename Data, typename ...Args>
inline auto newNode(AnyNode *parent, Args && ...args) {
  using T = Node<Data>;
  return ZuPtr<T>{new T(parent, ZuFwd<Args>(args)...)};
}

} // ZfTree

#endif /* ZfTree_HH */
