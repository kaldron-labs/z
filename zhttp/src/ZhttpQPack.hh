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
#include <zlib/ZtLocalArray.hh>

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
  static int decodeFieldSection(
    ZuCSpan in, const QPackRxTable *table, L l,
    const Params &params = {}, uint64_t insertCount = 0,
    uint64_t maxCapacity = 0, FieldSectionPrefix *decodedPrefix = nullptr) {
    if (table) {
      insertCount = table->insertCount();
      maxCapacity = table->maxCapacity();
    }
    FieldSectionPrefix prefix;
    int prefixLen = decodeFieldSectionPrefix(
      in, prefix, insertCount, maxCapacity);
    if (prefixLen < 0 || !validateFieldSectionPrefix(prefix, insertCount))
      return -1;
    if (decodedPrefix) *decodedPrefix = prefix;

    unsigned o = unsigned(prefixLen);
    uint64_t base = prefix.base;
    uint64_t headerBytes = 0;
    // Non-Huffman strings are returned as spans into the input section.
    // Huffman strings use these per-section scratch buffers, reused for each
    // field rather than allocated inside the representation loop.
    auto nameStorage = ZtLocalArray(HeaderBytes, HPack::declen(in.length()));
    auto valueStorage = ZtLocalArray(HeaderBytes, HPack::declen(in.length()));

    auto countHeader = [&headerBytes, &params](ZuCSpan name, ZuCSpan value) {
      if (headerBytes > params.maxHeaderListSize() - name.length())
	return false;
      headerBytes += name.length();
      if (headerBytes > params.maxHeaderListSize() - value.length())
	return false;
      headerBytes += value.length();
      return true;
    };
    auto readValue = [&](ZuCSpan &value) {
      valueStorage.length(0);
      return decodeString(valueStorage, in, o, 7, 0x80, value) >= 0;
    };

    while (o < in.length()) {
      uint8_t first = uint8_t(in[o]);
      ZuCSpan name;
      ZuCSpan value;
      Header indexed;
      HeaderName indexedNameStorage;
      QPackFieldFlags flags;

      if (first & 0x80) {
	uint64_t index = 0;
	uint8_t indexFirst = 0;
	if (qpackDecodePrefInt_(in, o, 6, index, &indexFirst) < 0)
	  return -1;
	if (indexFirst & 0x40) {
	  if (!staticField(index, indexed)) return -1;
	  flags.staticRef = true;
	} else {
	  if (!table || !table->lookupRelative(base, index, indexed))
	    return -1;
	  flags.dynamicRef = true;
	}
	name = indexed.name;
	value = indexed.value;
      } else if ((first & 0xf0) == 0x10) {
	uint64_t index = 0;
	if (qpackDecodePrefInt_(in, o, 4, index) < 0 ||
	    !table || !table->lookupPostBase(base, index, indexed))
	  return -1;
	name = indexed.name;
	value = indexed.value;
	flags.dynamicRef = true;
	flags.postBase = true;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	uint8_t nameFirst = 0;
	if (qpackDecodePrefInt_(in, o, 4, index, &nameFirst) < 0)
	  return -1;
	flags.neverIndex = nameFirst & 0x20;
	if (nameFirst & 0x10) {
	  if (!staticName(index, indexedNameStorage)) return -1;
	  name = indexedNameStorage;
	  flags.staticRef = true;
	} else {
	  if (!table || !table->lookupRelative(base, index, indexed))
	    return -1;
	  name = indexed.name;
	  flags.dynamicRef = true;
	}
	if (!readValue(value)) return -1;
      } else if ((first & 0xf0) == 0x00) {
	uint64_t index = 0;
	if (qpackDecodePrefInt_(in, o, 3, index) < 0 ||
	    !table || !table->lookupPostBase(base, index, indexed))
	  return -1;
	flags.neverIndex = first & 0x08;
	flags.dynamicRef = true;
	flags.postBase = true;
	name = indexed.name;
	if (!readValue(value)) return -1;
      } else if ((first & 0xe0) == 0x20) {
	nameStorage.length(0);
	valueStorage.length(0);
	if (decodeString(nameStorage, in, o, 3, 0x08, name) < 0 ||
	    decodeString(valueStorage, in, o, 7, 0x80, value) < 0)
	  return -1;
	flags.neverIndex = first & 0x10;
      } else
	return -1;

      if (!countHeader(name, value)) return -1;
      l(Header{name, value}, flags);
    }
    return int(o);
  }

  template <typename L>
  static int decodeLiteral(
    ZuCSpan in, L l, const Params &params = {}, uint64_t insertCount = 0) {
    return decodeFieldSection(
      in, nullptr,
      [&l](Header h, QPackFieldFlags) { l(h); },
      params, insertCount, params.qpackTableCapacity());
  }
};

struct QPackInsnParser {
  template <typename Decode, typename Apply>
  bool parse(ZuBSpan span, Decode decode, Apply apply) {
    if (bytes.length() != offset) {
      for (unsigned i = 0; i < span.length(); ++i) bytes.push(span[i]);
      return drain_(decode, apply);
    }

    unsigned o = 0;
    while (o < span.length()) {
      QPackDecodedInstruction insn;
      int n = decode(ZuCSpan{
	reinterpret_cast<const char *>(span.data() + o), span.length() - o},
	insn);
      if (n == -2) {
	for (unsigned i = o; i < span.length(); ++i) bytes.push(span[i]);
	offset = 0;
	return bytes.length() <= MaxBuffered;
      }
      if (n < 0 || !apply(insn)) return false;
      o += unsigned(n);
    }
    return true;
  }

  void reset() {
    bytes.length(0);
    offset = 0;
  }

private:
  template <typename Decode, typename Apply>
  bool drain_(Decode decode, Apply apply) {
    for (;;) {
      QPackDecodedInstruction insn;
      int n = decode(ZuCSpan{
	reinterpret_cast<const char *>(bytes.data() + offset),
	bytes.length() - offset}, insn);
      if (n == -2) {
	if (offset > 4096 && offset > (bytes.length()>>1)) {
	  bytes.splice(0, offset);
	  offset = 0;
	}
	return bytes.length() - offset <= MaxBuffered;
      }
      if (n < 0 || !apply(insn)) return false;
      offset += unsigned(n);
      if (offset == bytes.length()) {
	reset();
	return true;
      }
    }
  }

  HeaderBytes	bytes;
  unsigned	offset = 0;
  static constexpr unsigned MaxBuffered = 1<<16;
};

template <typename L>
int decodeLiteralDynamic(
  ZuCSpan in, const QPackRxTable &table, L l,
  const Params &params = {})
{
  return QPack::decodeFieldSection(
    in, &table,
    [&l](Header h, QPackFieldFlags) { l(h); },
    params);
}

}} // namespace Zhttp::H3

#endif /* ZhttpQPack_HH */
