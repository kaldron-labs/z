//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Owned DPDK plugin configuration.

#ifndef ZpktDpdkCf_HH
#define ZpktDpdkCf_HH

#include <zlib/ZmHash.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZpktEnv.hh>
#include "ZpktDpdkLib.hh"

namespace Zpkt::Dpdk {

struct NifCf {
  CfString	port;
  unsigned	spin = Zi_DefltSpin;
  unsigned	rxInterval = 50;
};

ZfStruct(ZpktDPDKAPI, (NifCf, Cf),
  (port,	(Mutable, Required),	String),
  (spin,	(Mutable),		UInt32),
  (rxInterval,	(Mutable),		UInt32));

class Interfaces : public ZmNHeap<Interfaces>, public
    ZmHashKV<CfString, NifCf,
      ZmHashHeapID<"Zpkt.Dpdk.Interfaces", ZmHashLock<ZmNoLock>>> {
friend inline ZuStringT<"Zpkt.Dpdk.Interfaces"> ZmNHeapID(Interfaces *);

public:
  using ZmNHeap<Interfaces>::operator new;
  using ZmNHeap<Interfaces>::operator delete;
};
inline ZfCf::AsMap<ZfFieldTC::UDT> ZfCf_Fmt(Interfaces *);

struct PluginCf {
  ZmRef<Interfaces> interfaces;
};

ZfStruct(ZpktDPDKAPI, (PluginCf, Cf),
  (interfaces, (Mutable, Required),	UDT));

} // namespace Zpkt::Dpdk
#endif /* ZpktDpdkCf_HH */
