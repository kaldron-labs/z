//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtDemangle.hh>
#include <zlib/ZtRegex.hh>
#include <zlib/ZmScratch.hh>

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
  // ZuString compile-time strings
  ZtREGEX("ZuString<(\d+)[uUlL]*>{char \[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(ZtRegex::CaptureSpan c, Splice &&splice) {
      ZuBox<unsigned> n(c[2]);
      auto buf = ZtScratch(Buf, n.val() + 8);
      buf << '"';
      ZtREGEX("\(char\)(\d+)(?:,\s*)?").mg(
	c[1], [&buf](ZtRegex::CaptureSpan c) { quote(buf, c[2]); });
      buf << '"';
      splice(buf);
    });

  // ZuArray compile-time strings
  ZtREGEX("Zu_::Array<char,\s*(\d+)[uUlL]*>{Zu_::Array_<char>{},\s*ZuArrayFn<char,\s*ZuCmp<char>\s*>{},\s*\d+[uUlL]*,\s*ZuElem<char>\s*\[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(ZtRegex::CaptureSpan c, Splice &&splice) {
      ZuBox<unsigned> n(c[2]);
      auto buf = ZtScratch(Buf, n.val() + 8);
      buf << '"';
      ZtREGEX("ZuElem<char>{\.v=\((?:\([^)]*\))?(-?\d+)\)}(?:,\s*)?").mg(
	c[1], [&buf](ZtRegex::CaptureSpan c) { quote(buf, c[2]); });
      buf << '"';
      splice(buf);
    });

  // ZuArray compile-time arrays (other than strings)
  ZtREGEX("Zu_::Array<(\w+),\s*(\d+)[uUlL]*>{Zu_::Array_<(?1)>{},\s*ZuArrayFn<(?1),\s*ZuCmp<(?1)>\s*>{},\s*\d+[uUlL]*,\s*ZuElem<(?1)>\s*\[\d+\]({(?:[^{}]++|(?-1))*+})}").sg(
    output, []<typename Splice>(ZtRegex::CaptureSpan c, Splice &&splice) {
      ZuBox<unsigned> n(c[3]);
      auto buf = ZtScratch(Buf, n.val() * 8 + 32); // estimated buffer size
      bool first = true;
      buf << "ZuArray<" << c[2] << ">({";
      ZtREGEX("ZuElem<\w+>{\.v=\((?:\(\w+\))?([^)]+)\)}(?:,\s*)?").mg(
	c[1], [&buf, &first](ZtRegex::CaptureSpan c) {
	  if (!first) buf << ',';
	  first = false;
	  buf << c[2];
	});
      buf << "})";
      splice(buf);
    });

  // ZuStringT<"x">
  ZtREGEX("ZuConstant<ZuString<\d+[uUlL]*>, (\"(?:[^\"\\]|\\[\"\\])*\")>").sg(
    output, []<typename Splice>(ZtRegex::CaptureSpan c, Splice &&splice) {
      auto buf = ZmScratch(
	char, unsigned(c[2].length()) + 12, Buf::VHeap);
      buf << "ZuStringT<" << c[2] << ">";
      splice(buf);
    });

  // ZuStringTL<"x", "y", ...>
  ZtREGEX("ZuTypeList(<(?:[^<>]++|(?-1))*+>)").sg(
    output, []<typename Splice>(ZtRegex::CaptureSpan c, Splice &&splice) {
      auto types = c[2];
      types.offset(1);
      types.trunc(types.length() - 1);
      auto buf = ZmScratch(
	char, unsigned(types.length()) + 12, Buf::VHeap);
      buf << "ZuStringTL<";
      ZtRegexCaptures(d, 3);
      int off = 0;
      bool first = true;
      while (ZtREGEX("\GZuStringT<(\"(?:[^\"\\]|\\[\"\\])*\")>\s*,?\s*").m(types, d, off)) {
	off += d[1].length();
	if (first)
	  first = false;
	else
	  buf << ", ";
	buf << d[2];
      }
      if (off == types.length()) {
	buf << ">";
	splice(buf);
      }
    });
}

ZtExtern void ZtDemangle::init()
{
  ZuDemangle_::transformFn(nullptr, transform, nullptr);
}
