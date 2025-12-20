//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// ZmNodeFn - node ownership (referencing/dereferencing)

#ifndef ZmNodeFn_HH
#define ZmNodeFn_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuObjectTraits.hh>
#include <zlib/ZuPtr.hh>

template <bool Shadow, bool IsObject>
struct ZmNodeFn_;
// ref-counted nodes
template <> struct ZmNodeFn_<false, true> {
  template <typename T> using Ref = ZmRef<T>;
  template <typename T> using MvRef = ZmRef<T>;
#ifdef ZmObject_DEBUG
  template <typename T> void nodeRef(T *o) { ZmREF(o); }
  template <typename T> void nodeRef(const Ref<T> &o) { ZmREF(o); }
  template <typename T> void nodeDeref(T *o) { ZmDEREF(o); }
  template <typename T> void nodeDeref(const Ref<T> &o) { ZmDEREF(o); }
#else
  template <typename T> static void nodeRef(T *o) { ZmREF(o); }
  template <typename T> static void nodeRef(const Ref<T> &o) { ZmREF(o); }
  template <typename T> static void nodeDeref(T *o) { ZmDEREF(o); }
  template <typename T> static void nodeDeref(const Ref<T> &o) { ZmDEREF(o); }
#endif
  template <typename T>
  static Ref<T> nodeAcquire(T *o) { return Ref<T>::acquire(o); }
  template <typename T>
  static T *nodeRelease(Ref<T> &&o) { return ZuMv(o).release(); }
  template <typename T> static void nodeDelete(T *) { }
  template <typename T> static void nodeDelete(const Ref<T> &) { }
};
// own nodes, delete if not returned to caller
template <>
struct ZmNodeFn_<false, false> {
  template <typename T> using Ref = T *;
  template <typename T> using MvRef = ZuPtr<T>;
  template <typename T> static void nodeRef(T *) { }
  template <typename T> static void nodeDeref(T *) { }
  template <typename T> static T *nodeAcquire(T *o) { return o; }
  template <typename T> static T *nodeRelease(T *o) { return o; }
  template <typename T> static void nodeDelete(T *o) { delete o; }
};
// shadow nodes, never delete
template <bool IsObject>
struct ZmNodeFn_<true, IsObject> {
  template <typename T> using Ref = T *;
  template <typename T> using MvRef = T *;
  template <typename T> static void nodeRef(T *) { }
  template <typename T> static void nodeDeref(T *) { }
  template <typename T> static T *nodeAcquire(T *o) { return o; }
  template <typename T> static T *nodeRelease(T *o) { return o; }
  template <typename T> static void nodeDelete(T *) { }
};

template <bool Shadow, typename NodeBase>
using ZmNodeFn = ZmNodeFn_<Shadow, ZuObjectTraits<NodeBase>::IsObject>;

#endif /* ZmNodeFn_HH */
