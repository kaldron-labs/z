//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef ZpktEnv_HH
#define ZpktEnv_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZmHeap.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZpktPlugin.hh>

namespace Zpkt {

ZuDerive(CfString, (ZtString<ZtStringHeapID<"Zpkt.CfString">>));

struct PluginLoadCf {
  CfString	module;
  bool		preload = false;
};
ZfStruct(ZpktAPI, (PluginLoadCf, Cf),
  (module, (Mutable, Required),	String),
  (preload, (Mutable),		Bool));

using NifQueryFn = ZuUnion<NifInfo, ZeException> (*)(ZuCSpan);

// Application-serialized setup/teardown. Env precedes and outlives its Nifs.
class ZpktAPI Env : public ZmNHeap<Env>, public ZmPolymorph {
friend inline ZuStringT<"Zpkt.Env"> ZmNHeapID(Env *);
friend class Nif;

public:
  using ZmNHeap<Env>::operator new;
  using ZmNHeap<Env>::operator delete;
  Env();
  ~Env() override;
  Env(const Env &) = delete;
  Env &operator =(const Env &) = delete;

  void init(const ZfCf::AnyNode *);
  ZuUnion<Nif *, ZeException> nif(ZiMultiplex *, ZuCSpan);
  ZuUnion<void, ZeException> final();
  void nifQueryFn(NifQueryFn fn) { ZmAssert_(fn); m_nifQueryFn = fn; }

private:
  void retire(Nif *);
  using Nifs = ZmRBTreeKV<CfString, ZmRef<Nif>,
    ZmRBTreeUnique<true, ZmRBTreeLock<ZmNoLock,
      ZmRBTreeHeapID<"Zpkt.Env.Nifs">>>>;
  ZmRef<Plugin>	m_plugin;
  Nifs		m_nifs;
  NifQueryFn	m_nifQueryFn;
};

} // Zpkt
#endif /* ZpktEnv_HH */
