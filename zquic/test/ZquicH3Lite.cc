//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "ZquicH3Lite.hh"

namespace Zquic::H3Lite {

static void append_(Bytes &out, ZuCSpan in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(uint8_t(in[i]));
}

static int putVar_(Bytes &out, uint64_t v)
{
  uint8_t b[8];
  int n = VarInt::encode(b, sizeof(b), v);
  if (n < 0) return -1;
  for (int i = 0; i < n; ++i) out.push(b[i]);
  return 0;
}

static int putPref_(Bytes &out, uint8_t prefix, unsigned prefixBits,
  uint64_t v)
{
  if (!prefixBits || prefixBits > 8) return -1;
  uint8_t mask = uint8_t((1U << prefixBits) - 1U);
  if (v < mask) {
    out.push(prefix | uint8_t(v));
    return 0;
  }

  out.push(prefix | mask);
  v -= mask;
  while (v >= 128) {
    out.push(uint8_t((v & 0x7f) | 0x80));
    v >>= 7;
  }
  out.push(uint8_t(v));
  return 0;
}

static int putString_(Bytes &out, uint8_t prefix, unsigned prefixBits,
  ZuCSpan s)
{
  if (putPref_(out, prefix, prefixBits, s.length()) < 0) return -1;
  append_(out, s);
  return 0;
}

struct HuffmanSymbol_ {
  uint32_t	code;
  uint8_t	bits;
  uint16_t	symbol;
};

static const HuffmanSymbol_ huffman_[] = {
  {0x1ff8U, 13, 0}, {0x7fffd8U, 23, 1}, {0xfffffe2U, 28, 2}, {0xfffffe3U, 28, 3},
  {0xfffffe4U, 28, 4}, {0xfffffe5U, 28, 5}, {0xfffffe6U, 28, 6}, {0xfffffe7U, 28, 7},
  {0xfffffe8U, 28, 8}, {0xffffeaU, 24, 9}, {0x3ffffffcU, 30, 10}, {0xfffffe9U, 28, 11},
  {0xfffffeaU, 28, 12}, {0x3ffffffdU, 30, 13}, {0xfffffebU, 28, 14}, {0xfffffecU, 28, 15},
  {0xfffffedU, 28, 16}, {0xfffffeeU, 28, 17}, {0xfffffefU, 28, 18}, {0xffffff0U, 28, 19},
  {0xffffff1U, 28, 20}, {0xffffff2U, 28, 21}, {0x3ffffffeU, 30, 22}, {0xffffff3U, 28, 23},
  {0xffffff4U, 28, 24}, {0xffffff5U, 28, 25}, {0xffffff6U, 28, 26}, {0xffffff7U, 28, 27},
  {0xffffff8U, 28, 28}, {0xffffff9U, 28, 29}, {0xffffffaU, 28, 30}, {0xffffffbU, 28, 31},
  {0x14U, 6, 32}, {0x3f8U, 10, 33}, {0x3f9U, 10, 34}, {0xffaU, 12, 35},
  {0x1ff9U, 13, 36}, {0x15U, 6, 37}, {0xf8U, 8, 38}, {0x7faU, 11, 39},
  {0x3faU, 10, 40}, {0x3fbU, 10, 41}, {0xf9U, 8, 42}, {0x7fbU, 11, 43},
  {0xfaU, 8, 44}, {0x16U, 6, 45}, {0x17U, 6, 46}, {0x18U, 6, 47},
  {0x0U, 5, 48}, {0x1U, 5, 49}, {0x2U, 5, 50}, {0x19U, 6, 51},
  {0x1aU, 6, 52}, {0x1bU, 6, 53}, {0x1cU, 6, 54}, {0x1dU, 6, 55},
  {0x1eU, 6, 56}, {0x1fU, 6, 57}, {0x5cU, 7, 58}, {0xfbU, 8, 59},
  {0x7ffcU, 15, 60}, {0x20U, 6, 61}, {0xffbU, 12, 62}, {0x3fcU, 10, 63},
  {0x1ffaU, 13, 64}, {0x21U, 6, 65}, {0x5dU, 7, 66}, {0x5eU, 7, 67},
  {0x5fU, 7, 68}, {0x60U, 7, 69}, {0x61U, 7, 70}, {0x62U, 7, 71},
  {0x63U, 7, 72}, {0x64U, 7, 73}, {0x65U, 7, 74}, {0x66U, 7, 75},
  {0x67U, 7, 76}, {0x68U, 7, 77}, {0x69U, 7, 78}, {0x6aU, 7, 79},
  {0x6bU, 7, 80}, {0x6cU, 7, 81}, {0x6dU, 7, 82}, {0x6eU, 7, 83},
  {0x6fU, 7, 84}, {0x70U, 7, 85}, {0x71U, 7, 86}, {0x72U, 7, 87},
  {0xfcU, 8, 88}, {0x73U, 7, 89}, {0xfdU, 8, 90}, {0x1ffbU, 13, 91},
  {0x7fff0U, 19, 92}, {0x1ffcU, 13, 93}, {0x3ffcU, 14, 94}, {0x22U, 6, 95},
  {0x7ffdU, 15, 96}, {0x3U, 5, 97}, {0x23U, 6, 98}, {0x4U, 5, 99},
  {0x24U, 6, 100}, {0x5U, 5, 101}, {0x25U, 6, 102}, {0x26U, 6, 103},
  {0x27U, 6, 104}, {0x6U, 5, 105}, {0x74U, 7, 106}, {0x75U, 7, 107},
  {0x28U, 6, 108}, {0x29U, 6, 109}, {0x2aU, 6, 110}, {0x7U, 5, 111},
  {0x2bU, 6, 112}, {0x76U, 7, 113}, {0x2cU, 6, 114}, {0x8U, 5, 115},
  {0x9U, 5, 116}, {0x2dU, 6, 117}, {0x77U, 7, 118}, {0x78U, 7, 119},
  {0x79U, 7, 120}, {0x7aU, 7, 121}, {0x7bU, 7, 122}, {0x7ffeU, 15, 123},
  {0x7fcU, 11, 124}, {0x3ffdU, 14, 125}, {0x1ffdU, 13, 126}, {0x3fffffffU, 30, 256},
};

int FieldCodec::decodeHuffman(Bytes &out, ZuCSpan in)
{
  out.length(0);
  uint32_t code = 0;
  unsigned bits = 0;
  for (unsigned i = 0; i < in.length(); ++i) {
    uint8_t byte = uint8_t(in[i]);
    for (int bit = 7; bit >= 0; --bit) {
      code = (code << 1) | ((byte >> bit) & 1U);
      ++bits;
      bool matched = false;
      for (auto &sym : huffman_) {
	if (sym.bits != bits || sym.code != code) continue;
	if (sym.symbol == 256) return -1;
	out.push(uint8_t(sym.symbol));
	code = 0;
	bits = 0;
	matched = true;
	break;
      }
      if (!matched && bits > 30) return -1;
    }
  }
  if (!bits) return int(out.length());
  if (bits > 7 || code != ((1U << bits) - 1U)) return -1;
  return int(out.length());
}

int FieldCodec::decodeString(
  Bytes &storage, ZuCSpan in, unsigned &o, unsigned prefixBits,
  uint8_t huffmanMask, ZuCSpan &out)
{
  uint64_t len = 0;
  uint8_t first = 0;
  if (decodePrefInt_(in, o, prefixBits, len, &first) < 0 ||
      in.length() < o + len)
    return -1;
  ZuCSpan raw{in.data() + o, unsigned(len)};
  o += unsigned(len);
  if (!(first & huffmanMask)) {
    out = raw;
    return int(raw.length());
  }
  if (decodeHuffman(storage, raw) < 0) return -1;
  out = ZuCSpan{
    reinterpret_cast<const char *>(storage.data()), storage.length()};
  return int(out.length());
}

static int staticIndex_(ZuCSpan name, ZuCSpan value)
{
  if (name == ":method" && value == "GET") return 17;
  if (name == ":method" && value == "POST") return 20;
  if (name == ":scheme" && value == "https") return 23;
  if (name == ":path" && value == "/") return 1;
  if (name == ":status" && value == "200") return 25;
  if (name == ":status" && value == "204") return 64;
  if (name == "accept" && value == "*/*") return 29;
  if (name == "content-length" && value == "0") return 4;
  return -1;
}

static bool staticField_(uint64_t index, Header &field)
{
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

static bool staticNameIndex_(ZuCSpan name, uint64_t &index)
{
  if (name == ":authority") { index = 0; return true; }
  if (name == ":path") { index = 1; return true; }
  if (name == ":method") { index = 17; return true; }
  if (name == ":scheme") { index = 23; return true; }
  if (name == ":status") { index = 25; return true; }
  if (name == "accept") { index = 29; return true; }
  if (name == "accept-encoding") { index = 31; return true; }
  if (name == "content-length") { index = 4; return true; }
  if (name == "content-type") { index = 46; return true; }
  if (name == "date") { index = 6; return true; }
  if (name == "server") { index = 92; return true; }
  if (name == "user-agent") { index = 95; return true; }
  return false;
}

static bool staticName_(uint64_t index, ZuCSpan &name)
{
  Header field;
  if (staticField_(index, field)) {
    name = field.name;
    return true;
  }
  return false;
}

static uint64_t wireType_(FrameType::T t)
{
  if (t == FrameType::Data) return 0x00;
  if (t == FrameType::Headers) return 0x01;
  if (t == FrameType::Settings) return 0x04;
  if (t == FrameType::Goaway) return 0x07;
  return uint64_t(-1);
}

static FrameType::T frameType_(uint64_t t)
{
  switch (t) {
    case 0x00: return FrameType::Data;
    case 0x01: return FrameType::Headers;
    case 0x04: return FrameType::Settings;
    case 0x07: return FrameType::Goaway;
    default: return FrameType::Unknown;
  }
}

int FrameCodec::write(FrameType::T type, ZuCSpan payload, Bytes &out)
{
  uint64_t t = wireType_(type);
  if (t == uint64_t(-1)) return -1;
  if (putVar_(out, t) < 0 || putVar_(out, payload.length()) < 0) return -1;
  append_(out, payload);
  return out.length();
}

int FrameCodec::parse(ZuCSpan in, Frame &frame, unsigned &used)
{
  uint64_t t = 0, len = 0;
  unsigned n = 0, o = 0;
  if (VarInt::decode(in, t, n) < 0) return -1;
  o += n;
  if (VarInt::decode(ZuCSpan{in.data() + o, in.length() - o}, len, n) < 0)
    return -1;
  o += n;
  if (in.length() < o + len) return -1;
  frame.type = frameType_(t);
  frame.payload = ZuCSpan{in.data() + o, unsigned(len)};
  used = o + len;
  return 0;
}

int FieldCodec::encode(Bytes &out, ZuSpan<Header> headers)
{
  out.length(0);
  if (putPref_(out, 0, 8, 0) < 0 || putPref_(out, 0, 7, 0) < 0)
    return -1;
  for (unsigned i = 0; i < headers.length(); ++i) {
    int index = staticIndex_(headers[i].name, headers[i].value);
    if (index >= 0) {
      if (putPref_(out, 0xc0, 6, unsigned(index)) < 0)
	return -1;
      continue;
    }
    uint64_t nameIndex = 0;
    if (staticNameIndex_(headers[i].name, nameIndex)) {
      if (putPref_(out, 0x50, 4, nameIndex) < 0 ||
	  putString_(out, 0x00, 7, headers[i].value) < 0)
	return -1;
      continue;
    }
    if (putString_(out, 0x20, 3, headers[i].name) < 0 ||
	putString_(out, 0x00, 7, headers[i].value) < 0)
      return -1;
  }
  return out.length();
}

int MessageCodec::writeSettings(Bytes &out, const Settings &settings)
{
  out.length(0);
  Bytes payload;
  if (putVar_(payload, 0x01) < 0 ||
      putVar_(payload, settings.qpackTableCapacity) < 0 ||
      putVar_(payload, 0x06) < 0 ||
      putVar_(payload, settings.maxFieldSectionSize) < 0 ||
      putVar_(payload, 0x07) < 0 ||
      putVar_(payload, settings.qpackBlockedStreams) < 0)
    return -1;
  return FrameCodec::write(
    FrameType::Settings,
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()},
    out);
}

int MessageCodec::writeGoaway(Bytes &out, uint64_t firstRejectedRequestStreamID)
{
  out.length(0);
  Bytes payload;
  if (putVar_(payload, firstRejectedRequestStreamID) < 0) return -1;
  return FrameCodec::write(
    FrameType::Goaway,
    ZuCSpan{reinterpret_cast<const char *>(payload.data()), payload.length()},
    out);
}

int MessageCodec::readGoaway(ZuCSpan in, uint64_t &firstRejectedRequestStreamID)
{
  bool seen = false;
  uint64_t last = uint64_t(-1);
  unsigned o = 0;
  while (o < in.length()) {
    Frame frame;
    unsigned used = 0;
    if (FrameCodec::parse(ZuCSpan{in.data() + o, in.length() - o},
	  frame, used) < 0)
      return -1;
    o += used;
    if (frame.type == FrameType::Settings || frame.type == FrameType::Unknown)
      continue;
    if (frame.type != FrameType::Goaway)
      return -1;
    uint64_t id = 0;
    unsigned n = 0;
    if (VarInt::decode(frame.payload, id, n) < 0 ||
	n != frame.payload.length())
      return -1;
    if (seen && id > last) return -1;
    firstRejectedRequestStreamID = id;
    last = id;
    seen = true;
  }
  return seen ? 0 : -1;
}

bool MessageCodec::requestAccepted(uint64_t streamID, uint64_t goawayID)
{
  return !(streamID & 1) && !(streamID & 2) && streamID < goawayID;
}

int MessageCodec::writeRequest(
  Bytes &out, ZuCSpan method, ZuCSpan authority, ZuCSpan path, ZuCSpan body)
{
  out.length(0);
  Header headers[] = {
    { ":method", method },
    { ":scheme", "https" },
    { ":authority", authority },
    { ":path", path }
  };
  Bytes fields;
  if (FieldCodec::encode(fields, ZuSpan<Header>{headers, 4}) < 0) return -1;
  if (FrameCodec::write(
	FrameType::Headers,
	ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
	out) < 0)
    return -1;
  if (body.length() &&
      FrameCodec::write(FrameType::Data, body, out) < 0)
    return -1;
  return out.length();
}

int MessageCodec::writeResponse(Bytes &out, unsigned status, ZuCSpan body)
{
  out.length(0);
  char s[4] = {
    char('0' + ((status / 100) % 10)),
    char('0' + ((status / 10) % 10)),
    char('0' + (status % 10)),
    0
  };
  Header headers[] = { { ":status", ZuCSpan{s, 3} } };
  Bytes fields;
  if (FieldCodec::encode(fields, ZuSpan<Header>{headers, 1}) < 0) return -1;
  if (FrameCodec::write(
	FrameType::Headers,
	ZuCSpan{reinterpret_cast<const char *>(fields.data()), fields.length()},
	out) < 0)
    return -1;
  if (body.length() &&
      FrameCodec::write(FrameType::Data, body, out) < 0)
    return -1;
  return out.length();
}

int Client::consumeResponse(ZuCSpan in, Response &response)
{
  response = {};
  unsigned o = 0;
  while (o < in.length()) {
    Frame frame;
    unsigned used = 0;
    if (FrameCodec::parse(ZuCSpan{in.data() + o, in.length() - o},
	  frame, used) < 0)
      return -1;
    o += used;
    if (frame.type == FrameType::Headers) {
      if (FieldCodec::decode(frame.payload, [&response](Header h) {
	    if (h.name == ":status" && h.value.length() == 3)
	      response.status =
		(unsigned(h.value[0] - '0') * 100) +
		(unsigned(h.value[1] - '0') * 10) +
		unsigned(h.value[2] - '0');
	  }) < 0)
	return -1;
    } else if (frame.type == FrameType::Data)
      response.body = frame.payload;
  }
  return response.status ? 0 : -1;
}

int Server::consumeRequest(ZuCSpan in, Request &request)
{
  request = {};
  bool seenMethod = false, seenPath = false;
  unsigned o = 0;
  while (o < in.length()) {
    Frame frame;
    unsigned used = 0;
    if (FrameCodec::parse(ZuCSpan{in.data() + o, in.length() - o},
	  frame, used) < 0)
      return -1;
    o += used;
    if (frame.type == FrameType::Headers) {
      if (FieldCodec::decode(frame.payload, [&](Header h) {
	    if (h.name == ":method" && h.value.length()) {
	      request.method = h.value;
	      seenMethod = true;
	    }
	    if (h.name == ":path" && h.value.length()) {
	      request.path = h.value;
	      seenPath = true;
	    }
	  }) < 0)
	return -1;
    } else if (frame.type == FrameType::Data)
      request.body << frame.payload;
  }
  if (!seenMethod || !seenPath) return -1;
  return 0;
}

int Server::respond(ZuCSpan request, Bytes &response, ZuCSpan body)
{
  Request r;
  if (consumeRequest(request, r) < 0) return -1;
  return MessageCodec::writeResponse(response, 200, body);
}

} // namespace Zquic::H3Lite
