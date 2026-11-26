//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Raw AF_XDP arena, pool and packet facilities.

#ifndef ZpktXDPCf_HH
#define ZpktXDPCf_HH

#include <zlib/ZpktXDPLib.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZfCf.hh>
#include <zlib/ZiPlatform.hh>
#include <zlib/ZpktEnv.hh>

namespace Zpkt::Xdp {

struct NifCf {
  unsigned	rxSpin = (Zi_DefltSpin<<2);
  unsigned	txSpin = Zi_DefltSpin;
  unsigned	txSid = 0;
};
ZfStruct(ZpktXDPAPI, (NifCf, Cf),
  (rxSpin,	(Mutable),				UInt32),
  (txSpin,	(Mutable),				UInt32),
  (txSid,	(Mutable, Required, (Range<1, UINT_MAX>)),	UInt32));

using InterfaceMap = ZmHashKV<CfString, NifCf,
  ZmHashHeapID<"Zpkt.Xdp.Interfaces", ZmHashLock<ZmNoLock>>>;
class Interfaces : public ZmNHeap<Interfaces>, public InterfaceMap {
friend inline ZuStringT<"Zpkt.Xdp.Interfaces"> ZmNHeapID(Interfaces *);
public:
  using ZmNHeap<Interfaces>::operator new;
  using ZmNHeap<Interfaces>::operator delete;
};
inline ZfCf::AsMap<ZfFieldTC::UDT> ZfCf_Fmt(Interfaces *);

struct PluginCf {
  ZmRef<Interfaces> interfaces;
};
ZfStruct(ZpktXDPAPI, (PluginCf, Cf),
  (interfaces, (Mutable, Required),	UDT));

} // Zpkt::Xdp
#endif /* ZpktXDPCf_HH */
