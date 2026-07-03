//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

using namespace ZuTestUtil;

void testStreamUtilities()
{
  ZuTestScope(testStreamUtilities);

  uint64_t id = Zquic::StreamID::make(false, Zi::StreamType::Duplex, 7);
  ZuCHECK(id == 28 && Zquic::StreamID::client(id) &&
    Zquic::StreamID::bidi(id) && Zquic::StreamID::ordinal(id) == 7,
    "client bidi stream ID utility mismatch");
  id = Zquic::StreamID::make(true, Zi::StreamType::Simplex, 2);
  ZuCHECK(id == 11 && Zquic::StreamID::server(id) && Zquic::StreamID::uni(id),
    "server uni stream ID utility mismatch");

  Zquic::FlowCredit credit{10};
  ZuCHECK(credit.consume(7) && credit.available() == 3, "flow consume failed");
  ZuCHECK(!credit.consume(4) && credit.blocked() == false,
    "flow over-consume handling mismatch");
  credit.extend(20);
  ZuCHECK(credit.consume(13) && credit.blocked(), "flow extension failed");

  Zquic::StreamRxState rx;
  ZuCHECK(rx.receive(0, 5, false), "stream receive failed");
  ZuCHECK(rx.receive(5, 5, true), "stream FIN receive failed");
  ZuCHECK(rx.complete() && rx.finalSize() == 10, "stream completion mismatch");
  ZuCHECK(!rx.receive(8, 4, false), "final-size violation not rejected");

  Zquic::StreamRxState reorder;
  ZuCHECK(reorder.receive(5, 5, true), "out-of-order receive failed");
  ZuCHECK(!reorder.complete() && reorder.finalSizeKnown() &&
      reorder.finalSize() == 10 && !reorder.rangeCount(),
    "out-of-order final-size state mismatch");
  ZuCHECK(reorder.receive(0, 5, false), "gap fill receive failed");
  ZuCHECK(!reorder.complete() && reorder.delivered() == 5,
    "standalone stream state tracked queue delivery");
  reorder.delivered(10);
  ZuCHECK(reorder.complete(), "stream state did not complete after delivery");
  ZuCHECK(reorder.receive(2, 3, false), "duplicate range rejected");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testStreamUtilities);
}
