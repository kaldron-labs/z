//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZgrpcMatrix_HH
#define ZgrpcMatrix_HH

#include <zlib/Zgrpc.hh>

using namespace Zgrpc;

struct Packet {
  uint64_t seq = 0;
  ZuBSpan payload;
  uint32_t count = 0, code = 0;
  bool waitCancel = false;
};
ZfStruct(, (Packet, PB),
  (seq, (Ctor<0>, Deflt<0>, PB::FieldID<1>), UInt64),
  (payload, (Ctor<1>, PB::FieldID<2>), Bytes),
  (count, (Ctor<2>, Deflt<0>, PB::FieldID<3>), UInt32),
  (code, (Ctor<3>, Deflt<0>, PB::FieldID<4>), UInt32),
  (waitCancel, (Ctor<4>, Deflt<false>, PB::FieldID<5>), Bool));
Zpb::AsMessage<"zgrpc.interop.Packet"> Zpb_Fmt(Packet *);

struct Unary : Method {
  using Service = ZuStringT<"zgrpc.interop.Matrix">;
  using Name = ZuStringT<"Unary">;
  using Request = Packet;
  using Response = Packet;
  enum { ReqBorrowed = 1, ResBorrowed = 1 };
};
struct ServerStream : Unary {
  using Name = ZuStringT<"ServerStream">;
  enum { Kind = Zgrpc::Kind::ServerStream };
};
struct ClientStream : Unary {
  using Name = ZuStringT<"ClientStream">;
  enum { Kind = Zgrpc::Kind::ClientStream };
};
struct Duplex : Unary {
  using Name = ZuStringT<"Duplex">;
  enum { Kind = Zgrpc::Kind::Duplex };
};
using Methods = ZuTypeList<Unary, ServerStream, ClientStream, Duplex>;
ZgrpcCatalogDerive(Matrix, Methods);
ZgrpcCatalogImpl(Matrix);


#endif /* ZgrpcMatrix_HH */
