//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtDemangle.hh>
#include <zlib/ZtRegex.hh>

ZuDerive(Buf,
  (ZtArray<char, ZtArrayHeapID<"ZtDemangle.Buf", ZtArraySharded<true>>>));

template <typename S>
static void quote(S &s, ZuBox<uint8_t> c) {
  if (c == '\n')
    s << "\\n";
  else if (c == '\r')
    s << "\\r";
  else if (c == '\t')
    s << "\\t";
  else if (c < 0x20)
    s << "\\x" << c.hex<false, ZuFmt::Right<2, '0'>>();
  else if (c == '"')
    s << "\\\"";
  else if (c == '\\')
    s << "\\\\";
  else
    s << char(c);
}

static void transform(void *, ZuSpan<char> &output)
{
  // clean up ZuString compile-time strings
  ZtREGEX("ZuString<(\d+)[uUlL]*>{char \[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(const ZtRegex::Captures &c, Splice &&splice) {
      ZuBox<unsigned> n(c[2]);
      auto buf = ZtLocalArray(Buf, n.val() + 8);
      buf << '"';
      ZtREGEX("\(char\)(\d+)(?:,\s*)?").mg(
	c[1], [&buf](const ZtRegex::Captures &c) { quote(buf, c[2]); });
      buf << '"';
      splice(buf);
    });

  // clean up ZuArray compile-time strings
  ZtREGEX("Zu_::Array<char,\s*(\d+)[uUlL]*>{Zu_::Array_<char>{},\s*ZuArrayFn<char,\s*ZuCmp<char>\s*>{},\s*\d+[uUlL]*,\s*ZuElem<char>\s*\[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(const ZtRegex::Captures &c, Splice &&splice) {
      ZuBox<unsigned> n(c[2]);
      auto buf = ZtLocalArray(Buf, n.val() + 8);
      buf << '"';
      ZtREGEX("ZuElem<char>{(?:ZuElem<char>::)?{unnamed\s*type#\d+}{\.v=\(\(char\)(\d+)\)}}(?:,\s*)?").mg(
	c[1], [&buf](const ZtRegex::Captures &c) { quote(buf, c[2]); });
      buf << '"';
      splice(buf);
    });

  // clean up other ZuArray compile-time values
  ZtREGEX("Zu_::Array<(\w+),\s*(\d+)[uUlL]*>{Zu_::Array_<(?1)>{},\s*ZuArrayFn<(?1),\s*ZuCmp<(?1)>\s*>{},\s*\d+[uUlL]*,\s*ZuElem<(?1)>\s*\[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(const ZtRegex::Captures &c, Splice &&splice) {
      ZuBox<unsigned> n(c[3]);
      auto buf = ZtLocalArray(Buf, n.val() * 8 + 32); // estimated buffer size
      bool first = true;
      buf << "ZuArray<" << c[2] << ">({";
      ZtREGEX("ZuElem<\w+>{(?:ZuElem<\w+>::)?{unnamed\s*type#\d+}{\.v=\((?:\(\w+\))?([^)]+)\)}}(?:,\s*)?").mg(
	c[1], [&buf, &first](const ZtRegex::Captures &c) {
	  if (!first) buf << ',';
	  first = false;
	  buf << c[2];
	});
      buf << "})";
      splice(buf);
    });
}

ZtExtern void ZtDemangle::init()
{
  ZuDemangle_::transformFn(nullptr, transform, nullptr);
}
