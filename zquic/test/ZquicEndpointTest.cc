//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZquicEndpoint.hh>

using namespace ZuTestUtil;

void testDatagramOwnership()
{
  ZuTestScope(testDatagramOwnership);

  Zquic::Endpoint ep;
  bool seen = false;
  ep.datagramFn([&seen](Zquic::Datagram d) {
    seen = true;
    ZuCHECK(d.buf && d.buf->length == 4, "datagram buffer length mismatch");
    ZuCHECK(!::memcmp(d.buf->data(), "PING", 4), "datagram payload mismatch");
    ZuCHECK(d.addr.port() == 4433, "datagram address was not preserved");
  });

  ZmRef<ZiIOBuf> tx = ep.allocTxPacket();
  ZuCHECK(tx, "endpoint Tx packet allocation failed");

  ZmRef<ZiIOBuf> buf = new Zquic::PacketRxBufAlloc<>{&ep};
  buf->append(reinterpret_cast<const uint8_t *>("PING"), 4);
  ep.inject(Zquic::Datagram{ZuMv(buf), ZiSockAddr{ZiIP("127.0.0.1"), 4433}});

  ZuCHECK(seen, "datagram callback not invoked");
  ZuCHECK(ep.diag().datagramsRx == 1, "datagram Rx counter mismatch");
  ZuCHECK(ep.diag().bytesRx == 4, "datagram byte counter mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testDatagramOwnership);
}
