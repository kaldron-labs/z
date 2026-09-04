//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZuID.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmTime.hh>

#include <zlib/ZfStruct.hh>
#include <zlib/ZfASN1.hh>
#include <zlib/ZtHexDump.hh>

using namespace ZuTestUtil;

ZuStructFacet(Bah);

struct Foo {
  ZuCSpan string;
  ZtArray<ZtArray<uint8_t>> bytesVec;
  ZuDateTime t;
  int32_t iv[4] = { 1, 3, 101, 112 };

  friend ZfStructPrint ZuPrintType(Foo *);
};

ZfStruct(Foo,
  (((string, Rd),	(Ctor<0>)),		(String, "hello \"world\"")),
  (((bytesVec),		(Ctor<1>)),		(BytesVec)),
  (((t),		(Ctor<2>, NDP<3>)),	(DateTime)),
  (((iv, Lambda,
    ([](const auto &_) { return ZuSpan(_.iv); }),
    ([](auto &_, auto v) {
      unsigned n = sizeof(_.iv) / sizeof(_.iv[0]);
      for (unsigned i = 0; i < n; i++) _.iv[i] = v[i];
    }))),					(Int32Vec))
  );

using namespace ZfASN1::Encoding;

ZfStructRender(Foo, Bah,
  (string, (ASN1::Fmt<3, tagU(), set_(0), tag(0)>)),
  bytesVec, t,
  (iv,     ASN1::Type<OID>));

void roundTrip()
{
  ZuTestScope(roundTrip);

  Foo foo;
  foo.string = "hello world";
  foo.t = Zm::now();
  foo.bytesVec = { "xxx", "yyyy", "zzzzz" };

  ZtString<> asn1;
  ZfASN1::save<ZuFacet::Bah>(asn1, foo);
  log("asn1:", ZtHexDump_{asn1.span()});

  Foo bar = ZfASN1::handler<Foo, ZuFacet::Bah>(asn1).ctor();
  log(bar);
  ZuCheck(bar.string == foo.string);
  ZuCheck(bar.bytesVec.length() == foo.bytesVec.length());

  ZuPtr<Foo> allocated =
    ZfASN1::handler<Foo, ZuFacet::Bah>(asn1).alloc();
  ZuCheck(allocated->string == foo.string);
  ZuCheck(allocated->bytesVec.length() == foo.bytesVec.length());
  ZuCheck(allocated->t == bar.t);
  ZuCheck(allocated->iv[0] == foo.iv[0]);
  ZuCheck(allocated->iv[3] == foo.iv[3]);

  char intsASN[] = { 0x02, 0x01, 0x2a };
  using Ints = ZtArray<int32_t>;
  using IntsHandler =
    ZfASN1::AsArray<ZfFieldTC::Int32>::Handler<Ints, ZuFacet::Bah>;
  ZuPtr<Ints> ints =
    IntsHandler{ZuFalse{}, ZuSpan<char>{intsASN, sizeof(intsASN)}}.alloc();
  ZuCheck(ints->length() == 1);
  ZuCheck((*ints)[0] == 42);

  ZtString<> asn2;
  ZfASN1::save<ZuFacet::Bah>(asn2, bar);
  log("asn2:", ZtHexDump_{asn2.span()});
  ZuCheck(asn1 == asn2);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(roundTrip);
  return 0;
}
