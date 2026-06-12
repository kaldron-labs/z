//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP/3 QPACK support

#ifndef ZhttpQPack_HH
#define ZhttpQPack_HH

#ifndef ZhttpQPackTypes_HH
#include <zlib/ZhttpQPackTypes.hh>
#endif

#ifndef ZhttpHPack_HH
#include <zlib/ZhttpHPack.hh>
#endif

#include <zlib/ZiAssert.hh>

namespace Zhttp { namespace H3 {

inline int qpackDecodePrefInt_(
  ZuCSpan in, unsigned &o, unsigned prefixBits, uint64_t &v,
  uint8_t *firstByte = nullptr)
{
  if (!prefixBits || prefixBits > 8) return -1;
  if (o >= in.length()) return -2;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  uint8_t first = uint8_t(in[o++]);
  if (firstByte) *firstByte = first;
  v = first & mask;
  if (v < mask) return 0;

  unsigned shift = 0;
  for (;;) {
    if (o >= in.length()) return -2;
    if (shift >= 56) return -1;
    uint8_t b = uint8_t(in[o++]);
    v += uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) return 0;
    shift += 7;
  }
}

struct CodecBytes {
  static void putSpan(HeaderBytes &out, ZuCSpan s) {
    for (unsigned i = 0; i < s.length(); ++i) out.push(uint8_t(s[i]));
  }
};

struct QPack {
  static int staticIndex(ZuCSpan name, ZuCSpan value);
  static bool staticField(uint64_t, Header &);
  static bool staticName(uint64_t, HeaderName &);
  static bool staticNameIndex(ZuCSpan, uint64_t &);
  static int encodeFieldSectionPrefix(
    HeaderBytes &, const FieldSectionPrefix &, uint64_t = 0);
  static int decodeFieldSectionPrefix(
    ZuCSpan, FieldSectionPrefix &, uint64_t = 0, uint64_t = 0);
  static int decodeFieldSectionPrefix(
    ZuCSpan, EncodedFieldSectionPrefix &);
  static bool fieldSectionBase(
    const EncodedFieldSectionPrefix &, uint64_t, uint64_t &);
  static bool validateFieldSectionPrefix(
    const FieldSectionPrefix &, uint64_t);
  static int encodeFieldLine(HeaderBytes &, Header, const Params &);
  static int encodeDynamicIndexed(HeaderBytes &, uint64_t);
  static int encodeDynamicNameRef(
    HeaderBytes &, uint64_t, ZuCSpan, bool = false);
  static int encodeLiteral(
    HeaderBytes &, ZuSpan<Header>, const Params &,
    const FieldSectionPrefix & = {});
  static int encodeSetCapacity(HeaderBytes &, uint64_t);
  static int encodeInsertWithNameRef(
    HeaderBytes &, uint64_t, bool, ZuCSpan);
  static int encodeInsertLiteral(HeaderBytes &, Header);
  static int encodeDuplicate(HeaderBytes &, uint64_t);
  static int encodeSectionAck(HeaderBytes &, uint64_t);
  static int encodeStreamCancellation(HeaderBytes &, uint64_t);
  static int encodeInsertCountIncrement(HeaderBytes &, uint64_t);
  static int decodeEncoderInstructionOne(ZuCSpan, QPackDecodedInstruction &);
  static int decodeDecoderInstructionOne(ZuCSpan, QPackDecodedInstruction &);
  static int decodeInstructionOne(ZuCSpan, QPackDecodedInstruction &);
  static int decodeInstruction(ZuCSpan, QPackDecodedInstruction &);
  static int decodeHuffman(HeaderBytes &, ZuCSpan);
  static int decodeString(
    HeaderBytes &, ZuCSpan, unsigned &, unsigned, uint8_t, ZuCSpan &);

  template <typename L>
  static int decodeLiteral(
    ZuCSpan in, L l, const Params &params = {}, uint64_t insertCount = 0) {
    FieldSectionPrefix prefix;
    int prefixLen = decodeFieldSectionPrefix(
      in, prefix, insertCount, params.qpackTableCapacity());
    if (prefixLen < 0 || !validateFieldSectionPrefix(prefix, insertCount))
      return -1;
    unsigned o = unsigned(prefixLen);
    unsigned headerBytes = 0;
    while (o < in.length()) {
      uint8_t first = uint8_t(in[o]);
      ZuCSpan name;
      ZuCSpan value;
      HeaderBytes nameStorage;
      HeaderBytes valueStorage;

      if (first & 0x80) {
	uint64_t index = 0;
	uint8_t indexFirst = 0;
	if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0 ||
	    !(indexFirst & 0x40))
	  return -1;
	Header h;
	if (!staticField(index, h)) return -1;
	name = h.name;
	value = h.value;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	uint8_t nameFirst = 0;
	if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0 ||
	    !(nameFirst & 0x10))
	  return -1;
	Header h;
	if (!staticField(index, h)) return -1;
	name = h.name;
	if (decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
      } else if ((first & 0xe0) == 0x20) {
	if (decodeString(nameStorage, in, o, 3, 0x08, name) < 0)
	  return -1;
	if (decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
      } else
	return -1;

      headerBytes += name.length() + value.length();
      if (headerBytes > params.maxHeaderListSize()) return -1;
      l(Header{name, value});
    }
    return int(o);
  }
};

template <typename L>
int decodeLiteralDynamic(
  ZuCSpan in, const QPackRxTable &table, L l,
  const Params &params = {})
{
  FieldSectionPrefix prefix;
  int prefixLen = QPack::decodeFieldSectionPrefix(
    in, prefix, table.insertCount(), table.maxCapacity());
  if (prefixLen < 0 || prefix.requiredInsertCount > table.insertCount())
    return -1;
  uint64_t base = prefix.base;

  unsigned o = unsigned(prefixLen);
  unsigned headerBytes = 0;
  while (o < in.length()) {
    uint8_t first = uint8_t(in[o]);
    ZuCSpan name;
    ZuCSpan value;
    Header indexed;
    HeaderBytes nameStorage;
    HeaderBytes valueStorage;

    auto readValue = [&]() -> bool {
      return QPack::decodeString(valueStorage, in, o, 7, 0x80, value) >= 0;
    };

    if (first & 0x80) {
      uint64_t index = 0;
      uint8_t indexFirst = 0;
      if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0)
	return -1;
      if (indexFirst & 0x40) {
	if (!QPack::staticField(index, indexed)) return -1;
      } else if (!table.lookupRelative(base, index, indexed))
	return -1;
      name = indexed.name;
      value = indexed.value;
    } else if ((first & 0xf0) == 0x10) {
      uint64_t index = 0;
      if (qpackDecodePrefInt_(in, o, 4, index) < 0 ||
	  !table.lookupPostBase(base, index, indexed))
	return -1;
      name = indexed.name;
      value = indexed.value;
    } else if ((first & 0xc0) == 0x40) {
      uint64_t index = 0;
      uint8_t nameFirst = 0;
      if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0)
	return -1;
      if (nameFirst & 0x10) {
	if (!QPack::staticField(index, indexed)) return -1;
      } else if (!table.lookupRelative(base, index, indexed))
	return -1;
      name = indexed.name;
      if (!readValue()) return -1;
    } else if ((first & 0xf0) == 0x00) {
      uint64_t index = 0;
      if (qpackDecodePrefInt_(in, o, 3, index) < 0 ||
	  !table.lookupPostBase(base, index, indexed))
	return -1;
      name = indexed.name;
      if (!readValue()) return -1;
    } else if ((first & 0xe0) == 0x20) {
      if (QPack::decodeString(nameStorage, in, o, 3, 0x08, name) < 0)
	return -1;
      if (!readValue()) return -1;
    } else
      return -1;

    headerBytes += name.length() + value.length();
    if (headerBytes > params.maxHeaderListSize()) return -1;
    l(Header{name, value});
  }
  return int(o);
}

}} // namespace Zhttp::H3

#endif /* ZhttpQPack_HH */
