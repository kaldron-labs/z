//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef ZpktPlugin_HH
#define ZpktPlugin_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZmPolymorph.hh>
#include <zlib/ZfCf.hh>
#include <zlib/Zpkt.hh>

namespace Zpkt {

struct ZpktAPI Plugin : public ZmPolymorph {
  virtual bool loopback() const { return false; }
  // Backends that supply native interface geometry skip the host query.
  virtual bool hostNifInfo() const { return true; }
  virtual ZuUnion<Nif *, ZeException> nif(
    ZiMultiplex *, ZuCSpan name, const NifInfo &) = 0;
  virtual ZuUnion<void, ZeException> final() = 0;
};
typedef Plugin *(*PluginFn)(const ZfCf::AnyNode *);

} // Zpkt

extern "C" { typedef Zpkt::PluginFn ZpktPluginFn; }
#define ZpktPluginFnSym "ZpktPlugin"

#endif /* ZpktPlugin_HH */
