//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Test-only minimal HTTP/3 harness for Zquic interop scaffolding.

#ifndef ZquicH3Lite_HH
#define ZquicH3Lite_HH

#include <zlib/ZquicPacket.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>
#include <zlib/ZtString.hh>

namespace Zquic::H3Lite {

inline int decodePrefInt_(ZuCSpan in, unsigned &o, unsigned prefixBits,
  uint64_t &v, uint8_t *firstByte = nullptr)
{
  if (o >= in.length() || !prefixBits || prefixBits > 8) return -1;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  uint8_t first = uint8_t(in[o++]);
  if (firstByte) *firstByte = first;
  v = first & mask;
  if (v < mask) return 0;

  unsigned shift = 0;
  for (;;) {
    if (o >= in.length() || shift >= 56) return -1;
    uint8_t b = uint8_t(in[o++]);
    v += uint64_t(b & 0x7f) << shift;
    if (!(b & 0x80)) return 0;
    shift += 7;
  }
}

struct FrameType {
  ZtEnum(FrameType, int8_t, Data, Headers, Settings, Goaway, Unknown);
};

struct Header {
  ZuCSpan	name;
  ZuCSpan	value;
};

using Bytes = ZtArray<uint8_t, ZtArrayHeapID<"Zquic.H3Lite.Bytes">>;
using Text = ZtString<ZtStringHeapID<"Zquic.H3Lite.Text">>;

struct Frame {
  FrameType::T	type = FrameType::Unknown;
  ZuCSpan	payload;
};

struct Settings {
  uint64_t	qpackTableCapacity = 0;
  uint64_t	maxFieldSectionSize = 65536;
  uint64_t	qpackBlockedStreams = 0;
};

struct Response {
  unsigned	status = 0;
  Text		body;
};

struct Request {
  Text		method;
  Text		path;
  Text		body;
};

struct FrameCodec {
  static int write(FrameType::T, ZuCSpan, Bytes &);
  static int parse(ZuCSpan, Frame &, unsigned &);
};

struct FieldCodec {
  static int encode(Bytes &, ZuSpan<Header>);
  static int decodeHuffman(Bytes &, ZuCSpan);
  static int decodeString(
    Bytes &, ZuCSpan, unsigned &, unsigned, uint8_t, ZuCSpan &);

  template <typename L>
  static int decode(ZuCSpan in, L l) {
    uint64_t requiredInsertCount = 0, base = 0;
    unsigned o = 0;
    if (decodePrefInt_(in, o, 8, requiredInsertCount) < 0 ||
	decodePrefInt_(in, o, 7, base) < 0)
      return -1;
    if (requiredInsertCount || base) return -1;

    while (o < in.length()) {
      uint8_t first = uint8_t(in[o]);
      ZuCSpan name;
      ZuCSpan value;
      Header indexed;
      Bytes nameStorage;
      Bytes valueStorage;

      auto readValue = [&]() -> bool {
	return decodeString(valueStorage, in, o, 7, 0x80, value) >= 0;
      };

      if (first & 0x80) {
	uint64_t index = 0;
	if (decodePrefInt_(in, o, 6, index) < 0 || !(first & 0x40) ||
	    !staticField(index, indexed))
	  return -1;
	name = indexed.name;
	value = indexed.value;
      } else if ((first & 0xc0) == 0x40) {
	uint64_t index = 0;
	if (decodePrefInt_(in, o, 4, index) < 0 || !(first & 0x10) ||
	    !staticName(index, name) || !readValue())
	  return -1;
      } else if ((first & 0xe0) == 0x20) {
	if (decodeString(nameStorage, in, o, 3, 0x08, name) < 0)
	  return -1;
	if (!readValue()) return -1;
      } else
	return -1;
      l(Header{name, value});
    }
    return int(o);
  }

  static bool staticField(uint64_t index, Header &field) {
    if (index == 0) { field = Header{":authority", ""}; return true; }
    if (index == 1) { field = Header{":path", "/"}; return true; }
    if (index == 4) { field = Header{"content-length", "0"}; return true; }
    if (index == 6) { field = Header{"date", ""}; return true; }
    if (index == 17) { field = Header{":method", "GET"}; return true; }
    if (index == 20) { field = Header{":method", "POST"}; return true; }
    if (index == 23) { field = Header{":scheme", "https"}; return true; }
    if (index == 25) { field = Header{":status", "200"}; return true; }
    if (index == 29) { field = Header{"accept", "*/*"}; return true; }
    if (index == 31) {
      field = Header{"accept-encoding", "gzip, deflate, br"};
      return true;
    }
    if (index >= 44 && index <= 54) {
      field = Header{"content-type", ""};
      return true;
    }
    if (index == 64) { field = Header{":status", "204"}; return true; }
    if (index == 92) { field = Header{"server", ""}; return true; }
    if (index == 95) { field = Header{"user-agent", ""}; return true; }
    return false;
  }

  static bool staticName(uint64_t index, ZuCSpan &name) {
    Header field;
    if (staticField(index, field)) {
      name = field.name;
      return true;
    }
    return false;
  }
};

struct MessageCodec {
  static int writeSettings(Bytes &, const Settings & = {});
  static int writeGoaway(Bytes &, uint64_t);
  static int readGoaway(ZuCSpan, uint64_t &);
  static bool requestAccepted(uint64_t streamID, uint64_t goawayID);
  static int writeRequest(
    Bytes &, ZuCSpan method, ZuCSpan authority, ZuCSpan path, ZuCSpan body);
  static int writeResponse(Bytes &, unsigned status, ZuCSpan body);
};

class Client {
public:
  int request(Bytes &out, ZuCSpan authority, ZuCSpan path, ZuCSpan body = {}) {
    return MessageCodec::writeRequest(out, "GET", authority, path, body);
  }
  int post(Bytes &out, ZuCSpan authority, ZuCSpan path, ZuCSpan body) {
    return MessageCodec::writeRequest(out, "POST", authority, path, body);
  }
  int gracefulClose(Bytes &out, uint64_t firstRejectedRequestStreamID) {
    return MessageCodec::writeGoaway(out, firstRejectedRequestStreamID);
  }
  int consumeGracefulClose(ZuCSpan in, uint64_t &firstRejectedRequestStreamID) {
    return MessageCodec::readGoaway(in, firstRejectedRequestStreamID);
  }
  int consumeResponse(ZuCSpan, Response &);
};

class Server {
public:
  int gracefulClose(Bytes &out, uint64_t firstRejectedRequestStreamID) {
    return MessageCodec::writeGoaway(out, firstRejectedRequestStreamID);
  }
  int consumeGracefulClose(ZuCSpan in, uint64_t &firstRejectedRequestStreamID) {
    return MessageCodec::readGoaway(in, firstRejectedRequestStreamID);
  }
  int consumeRequest(ZuCSpan, Request &);
  int respond(ZuCSpan request, Bytes &response, ZuCSpan body);
};

} // namespace Zquic::H3Lite

#endif /* ZquicH3Lite_HH */
