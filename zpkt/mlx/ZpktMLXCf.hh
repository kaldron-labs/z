//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Configuration for the Mellanox plugin.

#ifndef ZpktMLXCf_HH
#define ZpktMLXCf_HH

#ifndef ZpktLib_HH
#include <zlib/ZpktLib.hh>
#endif

#include <zlib/ZmHash.hh>
#include <zlib/ZfCf.hh>
#include <zlib/Zpkt.hh>

namespace Zpkt::Mlx {

struct NifCf {
  unsigned	rxDepth = 512;
  unsigned	txDepth = 512;
};
ZfStruct(, (NifCf, Cf),
  (rxDepth,	(Mutable),	UInt32),
  (txDepth,	(Mutable),	UInt32));

using InterfaceMap = ZmHashKV<CfString, NifCf,
  ZmHashHeapID<"Zpkt.Mlx.Interfaces", ZmHashLock<ZmNoLock>>>;
class Interfaces : public ZmNHeap<Interfaces>, public InterfaceMap {
friend inline ZuStringT<"Zpkt.Mlx.Interfaces"> ZmNHeapID(Interfaces *);
public:
  using ZmNHeap<Interfaces>::operator new;
  using ZmNHeap<Interfaces>::operator delete;
};
inline ZfCf::AsMap<ZfFieldTC::UDT> ZfCf_Fmt(Interfaces *);

struct PluginCf {
  ZmRef<Interfaces> interfaces;
};
ZfStruct(, (PluginCf, Cf),
  (interfaces, (Mutable, Required), UDT));

} // namespace Zpkt::Mlx
#endif /* ZpktMLXCf_HH */
