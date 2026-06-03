//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/Zhttp3.hh>

using namespace ZuTestUtil;

namespace {

void appendBytes(
  Zhttp::H3::HeaderBytes &out, const Zhttp::H3::HeaderBytes &in)
{
  for (unsigned i = 0; i < in.length(); ++i) out.push(in.data()[i]);
}

} // namespace

void testRequestResponseEncoding()
{
  ZuTestScope(testRequestResponseEncoding);

  Zhttp::H3::Connection client;
  Zhttp::H3::Connection server;
  client.openLocalControl();
  client.openLocalQPack();
  server.openLocalControl();
  server.openLocalQPack();

  Zhttp::H3::HeaderBytes req;
  Zhttp::H3::Header reqHeaders[] = {
    { "accept", "application/json" }
  };
  Zhttp::H3::RequestParams rp;
  rp.method = Zhttp::Method::POST;
  rp.authority = "localhost";
  rp.path = "/echo";
  rp.headers = ZuSpan<Zhttp::H3::Header>{reqHeaders, 1};
  ZuCHECK(client.encodeRequest(req, rp) > 0, "H3 request encode failed");
  Zhttp::H3::HeaderBytes data;
  ZuCSpan requestBody{"{\"ping\":true}"};
  ZuCHECK(client.encodeData(data, requestBody) > 0,
    "H3 request DATA encode failed");
  for (unsigned i = 0; i < data.length(); ++i) req.push(data[i]);

  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(req.data()), req.length()},
    parsed, used),
    "H3 request HEADERS parse failed");
  ZuCHECK(parsed.type == Zhttp::H3::H3FrameType::Headers,
    "H3 request frame type mismatch");

  Zhttp::H3::DecodedRequest decodedReq;
  ZuCHECK(server.decodeRequest(parsed, decodedReq) ==
      int(parsed.payload.length()) &&
      decodedReq.method == Zhttp::Method::POST &&
      decodedReq.authority == "localhost" &&
      decodedReq.path == "/echo" &&
      decodedReq.headers.length() == 1 &&
      decodedReq.headers[0].name == "accept" &&
      decodedReq.headers[0].value == "application/json",
    "server did not decode client request");
  Zhttp::H3::H3Frame requestData;
  unsigned requestDataUsed = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{
      reinterpret_cast<const char *>(req.data()) + used,
      req.length() - used},
    requestData, requestDataUsed),
    "H3 request DATA parse failed");
  ZuCSpan decodedBody;
  ZuCHECK(server.decodeData(requestData, decodedBody) ==
      int(requestBody.length()) &&
      decodedBody == requestBody &&
      used + requestDataUsed == req.length(),
    "server did not decode client request body");

  Zhttp::H3::HeaderBytes rsp;
  Zhttp::H3::Header respHeaders[] = {
    { "content-type", "application/json" }
  };
  Zhttp::H3::ResponseParams sp;
  sp.status = 200;
  sp.headers = ZuSpan<Zhttp::H3::Header>{respHeaders, 1};
  ZuCHECK(server.encodeResponse(rsp, sp) > 0, "H3 response encode failed");
  ZuCSpan responseBody{"{\"ok\":true}"};
  ZuCHECK(server.encodeData(data, responseBody) > 0,
    "H3 response DATA encode failed");
  for (unsigned i = 0; i < data.length(); ++i) rsp.push(data[i]);

  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reinterpret_cast<const char *>(rsp.data()), rsp.length()},
    parsed, used), "H3 response HEADERS parse failed");
  Zhttp::H3::DecodedResponse decodedResp;
  ZuCHECK(client.decodeResponse(parsed, decodedResp) ==
      int(parsed.payload.length()) &&
      decodedResp.status == 200 &&
      decodedResp.headers.length() == 1 &&
      decodedResp.headers[0].name == "content-type" &&
      decodedResp.headers[0].value == "application/json",
    "client did not decode server response");
  Zhttp::H3::H3Frame body;
  unsigned bodyUsed = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{
      reinterpret_cast<const char *>(rsp.data()) + used,
      rsp.length() - used},
    body, bodyUsed) &&
      client.decodeData(body, decodedBody) == int(responseBody.length()) &&
      decodedBody == responseBody &&
      used + bodyUsed == rsp.length(),
    "client did not decode server response body");
}

void testDynamicQPackRequestResponseEncoding()
{
  ZuTestScope(testDynamicQPackRequestResponseEncoding);

  Zhttp::H3::Params clientParams;
  clientParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("accept");
  Zhttp::H3::Connection client{ZuMv(clientParams)};

  Zhttp::H3::Params serverParams;
  serverParams.qpackTableCapacity(256).qpackBlockedStreams(4).
    qpackIndex("server").qpackIndex("x-trace");
  Zhttp::H3::Connection server{ZuMv(serverParams)};

  client.openLocalControl();
  client.openLocalQPack();
  server.openLocalControl();
  server.openLocalQPack();

  Zhttp::H3::Header reqHeaders[] = {
    { "accept", "application/json" }
  };
  Zhttp::H3::RequestParams rp;
  rp.method = Zhttp::Method::POST;
  rp.authority = "localhost";
  rp.path = "/dynamic";
  rp.headers = ZuSpan<Zhttp::H3::Header>{reqHeaders, 1};

  Zhttp::H3::HeaderBytes req;
  Zhttp::H3::HeaderBytes reqEncoder;
  ZuCHECK(client.encodeRequestDynamic(req, reqEncoder, rp) > 0 &&
      reqEncoder.length() &&
      client.qpackEncoderState().insertCount() == 1,
    "dynamic loop request encode failed");
  ZuCHECK(server.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(reqEncoder.data()),
	reqEncoder.length()}) == int(reqEncoder.length()) &&
      server.qpackDecoderState().insertCount() == 1,
    "dynamic loop request QPACK stream delivery failed");

  Zhttp::H3::HeaderBytes data;
  ZuCSpan requestBody{"{\"dynamic\":true}"};
  ZuCHECK(client.encodeData(data, requestBody) > 0,
    "dynamic loop request DATA encode failed");
  appendBytes(req, data);

  Zhttp::H3::H3Frame parsed;
  unsigned used = 0;
  ZuCSpan reqSpan{reinterpret_cast<const char *>(req.data()), req.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(reqSpan, parsed, used) &&
      parsed.type == Zhttp::H3::H3FrameType::Headers,
    "dynamic loop request HEADERS parse failed");
  Zhttp::H3::DecodedRequest decodedReq;
  ZuCHECK(server.decodeRequestDynamic(parsed, decodedReq) ==
      int(parsed.payload.length()) &&
      decodedReq.method == Zhttp::Method::POST &&
      decodedReq.authority == "localhost" &&
      decodedReq.path == "/dynamic" &&
      decodedReq.headers.length() == 1 &&
      decodedReq.headers[0].name == "accept" &&
      decodedReq.headers[0].value == "application/json",
    "dynamic loop request decode failed");

  Zhttp::H3::H3Frame requestData;
  unsigned requestDataUsed = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{reqSpan.data() + used, reqSpan.length() - used},
    requestData, requestDataUsed),
    "dynamic loop request DATA parse failed");
  ZuCSpan decodedBody;
  ZuCHECK(server.decodeData(requestData, decodedBody) ==
      int(requestBody.length()) &&
      decodedBody == requestBody &&
      used + requestDataUsed == req.length(),
    "dynamic loop request DATA decode failed");

  Zhttp::H3::Header respHeaders[] = {
    { "server", "zhttp" }
  };
  Zhttp::H3::ResponseParams sp;
  sp.status = 200;
  sp.headers = ZuSpan<Zhttp::H3::Header>{respHeaders, 1};

  Zhttp::H3::HeaderBytes rsp;
  Zhttp::H3::HeaderBytes rspEncoder;
  ZuCHECK(server.encodeResponseDynamic(rsp, rspEncoder, sp) > 0 &&
      rspEncoder.length() &&
      server.qpackEncoderState().insertCount() == 1,
    "dynamic loop response encode failed");
  ZuCHECK(client.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(rspEncoder.data()),
	rspEncoder.length()}) == int(rspEncoder.length()) &&
      client.qpackDecoderState().insertCount() == 1,
    "dynamic loop response QPACK stream delivery failed");

  ZuCSpan responseBody{"{\"ok\":true}"};
  ZuCHECK(server.encodeData(data, responseBody) > 0,
    "dynamic loop response DATA encode failed");
  appendBytes(rsp, data);

  Zhttp::H3::Header trailers[] = {
    { "x-trace", "loop" }
  };
  Zhttp::H3::HeaderBytes trailerFrame;
  Zhttp::H3::HeaderBytes trailerEncoder;
  ZuCHECK(server.encodeTrailersDynamic(
      trailerFrame, trailerEncoder,
      ZuSpan<Zhttp::H3::Header>{trailers, 1}) > 0 &&
      trailerEncoder.length() &&
      server.qpackEncoderState().insertCount() == 2,
    "dynamic loop trailers encode failed");
  ZuCHECK(client.receiveQPackEncoderStream(
      ZuCSpan{reinterpret_cast<const char *>(trailerEncoder.data()),
	trailerEncoder.length()}) == int(trailerEncoder.length()) &&
      client.qpackDecoderState().insertCount() == 2,
    "dynamic loop trailers QPACK stream delivery failed");
  appendBytes(rsp, trailerFrame);

  ZuCSpan rspSpan{reinterpret_cast<const char *>(rsp.data()), rsp.length()};
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(rspSpan, parsed, used) &&
      parsed.type == Zhttp::H3::H3FrameType::Headers,
    "dynamic loop response HEADERS parse failed");
  Zhttp::H3::DecodedResponse decodedResp;
  ZuCHECK(client.decodeResponseDynamic(parsed, decodedResp) ==
      int(parsed.payload.length()) &&
      decodedResp.status == 200 &&
      decodedResp.headers.length() == 1 &&
      decodedResp.headers[0].name == "server" &&
      decodedResp.headers[0].value == "zhttp",
    "dynamic loop response decode failed");

  Zhttp::H3::H3Frame responseData;
  unsigned responseDataUsed = 0;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{rspSpan.data() + used, rspSpan.length() - used},
    responseData, responseDataUsed),
    "dynamic loop response DATA parse failed");
  ZuCHECK(client.decodeData(responseData, decodedBody) ==
      int(responseBody.length()) &&
      decodedBody == responseBody,
    "dynamic loop response DATA decode failed");

  Zhttp::H3::H3Frame trailer;
  unsigned trailerUsed = 0;
  unsigned trailerOffset = used + responseDataUsed;
  ZuCHECK(!Zhttp::H3::H3FrameCodec::parse(
    ZuCSpan{rspSpan.data() + trailerOffset, rspSpan.length() - trailerOffset},
    trailer, trailerUsed) &&
      trailerOffset + trailerUsed == rsp.length(),
    "dynamic loop trailers parse failed");
  Zhttp::H3::DecodedTrailers decodedTrailers;
  ZuCHECK(client.decodeTrailersDynamic(trailer, decodedTrailers) ==
      int(trailer.payload.length()) &&
      decodedTrailers.headers.length() == 1 &&
      decodedTrailers.headers[0].name == "x-trace" &&
      decodedTrailers.headers[0].value == "loop",
    "dynamic loop trailers decode failed");

  ZuCHECK(client.diag().qpackInstructionsRx == 2 &&
      server.diag().qpackInstructionsRx == 1,
    "dynamic loop QPACK instruction diagnostics mismatch");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testRequestResponseEncoding);
  ZuTestCall(testDynamicQPackRequestResponseEncoding);
}
