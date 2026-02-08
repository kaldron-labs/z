//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtDemangle.hh>
#include <zlib/ZtRegex.hh>

ZuDerive(Buf,
  (ZtArray<char, ZtArrayHeapID<"ZtDemangle.Buf", ZtArraySharded<true>>>));

static void transform(void *, ZuSpan<char> &output)
{
  ZtREGEX("Zu_::Array<char,\s*(\d+)[uUlL]*>{Zu_::Array_<char>{},\s*ZuArrayFn<char,\s*ZuCmp<char>\s*>{},\s*\d+[uUlL]*,\s*ZuElem<char>\s*\[\d+\]({(?:[^{}]++|(?-1))*+})}}").sg(
    output, []<typename Splice>(const ZtRegex::Captures &c, Splice &&splice) {
      ZuBox<unsigned> n(c[2]);
      auto buf = ZtLocalArray(Buf, n + 2);
      buf << '"';
      ZtREGEX("ZuElem<char>{(?:ZuElem<char>::)?{unnamed\s*type#\d+}{\.v=\(\(char\)(\d+)\)}}(?:,\s*)?").mg(
	c[1], [&buf](const ZtRegex::Captures &c) {
	  buf << char(ZuBox<unsigned>(c[2]).val());
	});
      buf << '"';
      splice(buf);
    });
}

ZtExtern void ZtDemangle::init()
{
  ZuDemangle_::transformFn(nullptr, transform, nullptr);
}
