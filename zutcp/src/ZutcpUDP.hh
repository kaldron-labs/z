//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZutcpUDP_HH
#define ZutcpUDP_HH

#include <zlib/Zutcp.hh>

namespace Zutcp {

class MxPlugin::UdpCxn :
  public ZmNHeap<UdpCxn>, public Cxn, public UdpBindings::Node {
friend inline ZuStringT<"Zutcp.UDP.Cxn"> ZmNHeapID(UdpCxn *);

public:
  using ZmNHeap<UdpCxn>::operator new;
  using ZmNHeap<UdpCxn>::operator delete;
  using State = UdpCxn;

  UdpCxn(MxPlugin *, const ZiCxnInfo &, IPPath);
  ~UdpCxn() override;

  const ZiCxnInfo &info() const { return m_info; }
  ZuUnion<void, ZeException> join();
  bool accepts(ZiIP, const ZiSockAddr &) const;
  ZiIOBuf *rxBuf() const;
  bool receive(uint8_t *, unsigned, unsigned, const ZiSockAddr &, uint8_t, ZiRxBurst *);
  void send(ZiConnection *);
  void close(bool);

  void abortRx() override { close(false); }
  ClosedTupleKey closedKeyRx() const override { return {}; }
  bool established() const override { return mxCxn && !retiring; }
  void recvMx() { } // UDP delivers directly into the currently armed context
  void releaseCxnRx();

private:
  ZiSockAddr destination(const ZiTxContext &) const;
  const IPPath *findPath(const ZiIP &, uint64_t);
  void sendNext(ZiConnection *);
  void routed(ZiConnection *, const ZiSockAddr &, uint64_t, ZuUnion<IPPath, ZeException>);
  int submit(const ZiTxContext &, const ZiSockAddr &, const IPPath &);
  void sent(ZiConnection *, int);

  MxPlugin	*m_owner;
  ZiCxnInfo	m_info;
  int		m_membershipFD = -1; // multicast control only
  // Tx owner. The common same-peer lookup reads only the current slot.
  enum { NPaths = 8 };
  alignas(Zm::CacheLineSize)
  uint64_t	m_generation = 0;
  unsigned	m_pathIndex = 0, m_pathCount = 0, m_nextPath = 0;
  bool		m_sending = false;
  IPPath	m_paths[NPaths];
};

} // Zutcp

#endif /* ZutcpUDP_HH */
