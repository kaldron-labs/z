//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// object base class

#include <zlib/ZuDerive.hh>

#include <zlib/ZmObjectDebug.hh>

#ifdef ZmObject_DEBUG
#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmPLock.hh>

ZmRBTreeKVDerive(DebugTree, const void *, const ZmBackTrace *,
  ZmRBTreeUnique<true,
    ZmRBTreeLock<ZmPLock>>);

void ZmObjectDebug::debug() const
{
  if (!m_debug) {
    void *n = ::malloc(sizeof(DebugTree));
    new (n) DebugTree();
    if (m_debug.cmpXch(n, 0)) ::free(n);
  }
}

void ZmObjectDebug::dump(void *context, DumpFn fn) const
{
  if (!m_debug) return;
  auto i =
    (static_cast<DebugTree *>(m_debug.operator void *()))->citer();
  DebugTree::NodeRef n;
  while (n = i()) (*fn)(context, n->key(), n->val());
}

void ZmObject_ref(const ZmObjectDebug *o, const void *referrer)
{
  ZmBackTrace *bt = new ZmBackTrace();
  bt->capture(1);
  (static_cast<DebugTree *>(o->m_debug.operator void *()))->add(
      ZuFwdTuple(referrer, bt));
}

void ZmObject_deref(const ZmObjectDebug *o, const void *referrer)
{
  auto n =
    (static_cast<DebugTree *>(o->m_debug.operator void *()))->del(referrer);
  if (n) delete n->val();
}

#else

// void ZmObject_ref(const ZmObjectDebug *, const void *) { }
// void ZmObject_deref(const ZmObjectDebug *, const void *) { }

#endif
