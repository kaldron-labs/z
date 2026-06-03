//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp3.hh>

using namespace ZuTestUtil;

void testConnectionAndFrames()
{
  ZuTestScope(testConnectionAndFrames);

  Zhttp::H3::Params params;
  params.qpackTableCapacity(128).qpackBlockedStreams(3).
    maxHeaderListSize(4096);
  Zhttp::H3::Connection h3{ZuMv(params)};
  h3.openLocalControl();
  h3.openLocalQPack();
  ZuCHECK(h3.ready(), "H3 critical streams not ready");

  Zhttp::H3::HeaderBytes frame;
  ZuCHECK(h3.encodeSettings(frame) > 0,
    "SETTINGS frame encode failed");
  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame.data()), frame.length()},
    parsed, used),
    "SETTINGS frame parse failed");
  ZuCHECK(parsed.type == Zhttp::H3::H3FrameType::Settings,
    "SETTINGS frame type mismatch");
  ZuCHECK(used == frame.length(), "H3 frame consumed length mismatch");

  Zhttp::H3::Settings settings;
  ZuCHECK(Zhttp::H3::H3FrameCodec::parseSettings(
      parsed.payload, settings) == int(parsed.payload.length()) &&
      settings.hasQPackMaxTableCapacity &&
      settings.qpackMaxTableCapacity == 128 &&
      settings.hasMaxFieldSectionSize &&
      settings.maxFieldSectionSize == 4096 &&
      settings.hasQPackBlockedStreams &&
      settings.qpackBlockedStreams == 3,
    "SETTINGS payload round trip mismatch");
}

void testHeaderEncoding()
{
  ZuTestScope(testHeaderEncoding);

  Zhttp::H3::Connection h3;
  Zhttp::H3::Header reqHeaders[] = {
    { "accept", "application/json" }
  };
  Zhttp::H3::RequestParams req;
  req.authority = "localhost";
  req.headers = ZuSpan<Zhttp::H3::Header>{reqHeaders, 1};
  Zhttp::H3::HeaderBytes frame;
  ZuCHECK(h3.encodeRequest(frame, req) > 0, "request encode failed");

  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame.data()), frame.length()},
    parsed, used), "request HEADERS frame parse failed");
  unsigned headers = 0;
  int consumed = Zhttp::H3::QPack::decodeLiteral(
    parsed.payload,
    [&headers](Zhttp::H3::Header h) {
      if (h.name == "accept" && h.value == "application/json") ++headers;
    });
  ZuCHECK(consumed == int(parsed.payload.length()) && headers == 1,
    "request regular header was not encoded");
  Zhttp::H3::DecodedRequest decodedReq;
  ZuCHECK(h3.decodeRequest(parsed, decodedReq) ==
      int(parsed.payload.length()) &&
      decodedReq.method == Zhttp::Method::GET &&
      decodedReq.scheme == "https" &&
      decodedReq.authority == "localhost" &&
      decodedReq.path == "/" &&
      decodedReq.headers.length() == 1 &&
      decodedReq.headers[0].name == "accept" &&
      decodedReq.headers[0].value == "application/json",
    "request HEADERS decode mismatch");

  Zhttp::H3::Header respHeaders[] = {
    { "server", "z" }
  };
  Zhttp::H3::ResponseParams resp;
  resp.status = 204;
  resp.headers = ZuSpan<Zhttp::H3::Header>{respHeaders, 1};
  ZuCHECK(h3.encodeResponse(frame, resp) > 0, "response encode failed");
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame.data()), frame.length()},
    parsed, used), "response HEADERS frame parse failed");
  Zhttp::H3::DecodedResponse decodedResp;
  ZuCHECK(h3.decodeResponse(parsed, decodedResp) ==
      int(parsed.payload.length()) &&
      decodedResp.status == 204 &&
      decodedResp.headers.length() == 1 &&
      decodedResp.headers[0].name == "server" &&
      decodedResp.headers[0].value == "z",
    "response HEADERS decode mismatch");
  ZuCHECK(h3.encodeResponse(frame, Zhttp::H3::ResponseParams{1000}) < 0,
    "invalid response status encoded");

  ZuCSpan pingBody{"{\"ping\":true}"};
  ZuCHECK(h3.encodeData(frame, pingBody) > 0,
    "DATA frame encode failed");
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame.data()), frame.length()},
    parsed, used), "DATA frame parse failed");
  ZuCSpan body;
  ZuCHECK(h3.decodeData(parsed, body) == int(pingBody.length()) &&
      body == pingBody,
    "DATA frame decode mismatch");

  Zhttp::H3::Header trailerHeaders[] = {
    { "server-timing", "cpu;dur=2.4" }
  };
  ZuSpan<Zhttp::H3::Header> trailerSpan{trailerHeaders, 1};
  ZuCHECK(h3.encodeTrailers(frame, trailerSpan) > 0,
    "trailers encode failed");
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(frame.data()), frame.length()},
    parsed, used), "trailers HEADERS frame parse failed");
  Zhttp::H3::DecodedTrailers decodedTrailers;
  ZuCHECK(h3.decodeTrailers(parsed, decodedTrailers) ==
      int(parsed.payload.length()) &&
      decodedTrailers.headers.length() == 1 &&
      decodedTrailers.headers[0].name == "server-timing" &&
      decodedTrailers.headers[0].value == "cpu;dur=2.4",
    "trailers decode mismatch");
  Zhttp::H3::Header badTrailers[] = { { ":status", "200" } };
  ZuSpan<Zhttp::H3::Header> badTrailerSpan{badTrailers, 1};
  ZuCHECK(h3.encodeTrailers(frame, badTrailerSpan) < 0,
    "trailers accepted pseudo-header");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testConnectionAndFrames);
  ZuTestCall(testHeaderEncoding);
}
