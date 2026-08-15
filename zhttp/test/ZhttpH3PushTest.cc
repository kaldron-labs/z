//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuDerive.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZiRxStream.hh>
#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

namespace ZhttpH3PushTest_ {

ZuDerive(RxQueue,
  (ZmList<ZiIOBuf, ZmListNode<ZiIOBuf,
    ZmListHeapID<"">>>));
using RxBufAlloc = Zi::IOBufAlloc<RxQueue::Node, 256, 1<<20,
  ZuStringT<"ZhttpH3PushTest.RxBuf">>;
using RxStream = ZiRxStream<RxQueue>;

static ZmRef<RxQueue::Node> rxBuf(ZuBSpan span)
{
  ZmRef<RxQueue::Node> buf = new RxBufAlloc{};
  if (span.length()) buf->append(span);
  return buf;
}

static void append(
  Zhttp::H3::HdrBytes &out, ZuBSpan bytes)
{
  for (unsigned i = 0, n = bytes.length(); i < n; ++i) out.push(bytes[i]);
}

static void frame(
  Zhttp::H3::HdrBytes &out, uint64_t type, ZuBSpan payload)
{
  Zhttp::H3::putVar(out, type);
  Zhttp::H3::putVar(out, payload.length());
  append(out, payload);
}

static Zhttp::H3::HdrBytes pushPayload(uint64_t id)
{
  Zhttp::H3::HdrBytes payload;
  Zhttp::H3::putVar(payload, id);
  payload.push(0); // QPACK Required Insert Count
  payload.push(0); // QPACK Delta Base
  return payload;
}

struct CxnInput : public Zhttp::H3::CxnParser<CxnInput> {
  using Base = Zhttp::H3::CxnParser<CxnInput>;

  RxStream &rxStream() { return rx; }
  bool retireRx(uint64_t length) { retired += length; return true; }
  void rescheduleDequeue() { ++reschedules; }
  bool resetReceived() const { return false; }
  bool finReceived() const { return fin; }
  bool h3Server() const { return server; }
  void h3Error(uint64_t code) {
    if (!errors) error = code;
    ++errors;
  }
  void push(ZuBSpan bytes) { rx.push(rxBuf(bytes)); }

  RxStream	rx;
  uint64_t	error = 0;
  uint64_t	retired = 0;
  unsigned	reschedules = 0;
  unsigned	errors = 0;
  bool		server = false;
  bool		fin = false;
};

template <bool Request>
struct MsgInput :
  public Zhttp::Parser,
  public Zhttp::H3::Parser<MsgInput<Request>, Request> {
  using Base = Zhttp::H3::Parser<MsgInput<Request>, Request>;
  using State = typename Base::State;
  using Headers = ZuTypeList<>;

  MsgInput() {
    Base::h3(nullptr, this, nullptr, this,
      [](void *ptr, uint64_t code) {
	auto input = static_cast<MsgInput *>(ptr);
	if (!input->errors) input->error = code;
	++input->errors;
      }, 0);
  }

  RxStream &rxStream() { return rx; }
  bool retireRx(uint64_t length) { retired += length; return true; }
  void rescheduleDequeue() { ++reschedules; }
  bool resetReceived() const { return false; }
  bool stopReceived() const { return false; }
  bool finReceived() const { return fin; }
  void complete(State::T state) {
    completeState = state;
    ++completions;
  }
  void push(ZuBSpan bytes) { rx.push(rxBuf(bytes)); }

  RxStream	rx;
  uint64_t	error = 0;
  uint64_t	retired = 0;
  unsigned	reschedules = 0;
  unsigned	errors = 0;
  unsigned	completions = 0;
  bool		fin = false;
  State::T	completeState = State::Initial;
};

static Zhttp::H3::HdrBytes controlFrame(
  uint64_t type, ZuBSpan payload)
{
  Zhttp::H3::HdrBytes bytes;
  Zhttp::H3::putVar(bytes, 0); // control stream
  frame(bytes, 0x04, {}); // SETTINGS is the mandatory initial frame
  frame(bytes, type, payload);
  return bytes;
}

static void checkCxnError(
  bool server, uint64_t type, ZuBSpan payload, uint64_t error)
{
  ZuTestScope(checkCxnError);

  CxnInput input;
  input.server = server;
  auto bytes = controlFrame(type, payload);
  input.push(bytes);
  ZuCHECK(input.process(input) == Zhttp::H3::CxnState::Error,
    "control push violation did not fail the connection");
  ZuCHECK(input.errors == 1 && input.error == error,
    "control push violation returned the wrong error");
  input.process(input);
  ZuCHECK(input.errors == 1,
    "control push violation reported the connection error twice");
}

template <bool Request>
static void checkMsgError(
  uint64_t type, ZuBSpan payload, uint64_t error)
{
  ZuTestScope(checkMsgError);

  MsgInput<Request> input;
  Zhttp::H3::HdrBytes bytes;
  frame(bytes, type, payload);
  input.push(bytes);
  ZuCHECK(input.process(input) == Zhttp::H3::ParserState::Error,
    "request-stream push violation did not fail the connection");
  ZuCHECK(input.errors == 1 && input.error == error,
    "request-stream push violation returned the wrong error");
  ZuCHECK(input.completions == 1 &&
      input.completeState == Zhttp::H3::ParserState::Error,
    "request-stream push violation did not complete exactly once");
  input.process(input);
  ZuCHECK(input.errors == 1 && input.completions == 1,
    "request-stream push violation completed twice");
}

struct CloseLink {
  bool disconnect(uint64_t code) {
    if (!calls) error = code;
    ++calls;
    return true;
  }

  uint64_t	error = 0;
  unsigned	calls = 0;
};

} // namespace ZhttpH3PushTest_

using namespace ZhttpH3PushTest_;

static void testControlFrames()
{
  ZuTestScope(testControlFrames);

  Zhttp::H3::HdrBytes id;
  Zhttp::H3::putVar(id, 3);
  auto promise = pushPayload(3);
  uint8_t malformed[] = { 0x40 };
  uint8_t extra[] = { 0, 0 };

  ZuTestCall(checkCxnError,
    false, 0x05, promise, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkCxnError,
    true, 0x05, promise, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkCxnError,
    false, 0x05, ZuBSpan{malformed}, Zhttp::H3::FrameError);
  ZuTestCall(checkCxnError,
    false, 0x0d, id, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkCxnError,
    true, 0x0d, ZuBSpan{extra}, Zhttp::H3::FrameError);
  ZuTestCall(checkCxnError,
    false, 0x03, id, Zhttp::H3::IDError);
  ZuTestCall(checkCxnError,
    true, 0x03, id, Zhttp::H3::IDError);
  ZuTestCall(checkCxnError,
    true, 0x03, ZuBSpan{malformed}, Zhttp::H3::FrameError);

  CxnInput max;
  max.server = true;
  Zhttp::H3::HdrBytes bytes;
  Zhttp::H3::HdrBytes next;
  Zhttp::H3::putVar(next, 4);
  Zhttp::H3::putVar(bytes, 0);
  frame(bytes, 0x04, {});
  frame(bytes, 0x0d, id);
  frame(bytes, 0x0d, next);
  max.push(bytes);
  max.process(max);
  ZuCHECK(!max.errors, "increasing MAX_PUSH_ID was rejected");

  Zhttp::H3::HdrBytes lower;
  Zhttp::H3::putVar(lower, 2);
  bytes.length(0);
  frame(bytes, 0x0d, lower);
  max.push(bytes);
  ZuCHECK(max.process(max) == Zhttp::H3::CxnState::Error &&
      max.error == Zhttp::H3::IDError && max.errors == 1,
    "decreasing MAX_PUSH_ID did not produce H3_ID_ERROR");

  CxnInput extension;
  bytes.length(0);
  Zhttp::H3::putVar(bytes, 0);
  frame(bytes, 0x04, {});
  frame(bytes, 0x21, {});
  extension.push(bytes);
  extension.process(extension);
  ZuCHECK(!extension.errors,
    "unknown control-stream extension frame was not ignored");
}

static void testPushStreams()
{
  ZuTestScope(testPushStreams);

  Zhttp::H3::HdrBytes bytes;
  Zhttp::H3::putVar(bytes, 1);
  Zhttp::H3::putVar(bytes, 7);

  CxnInput client;
  client.push(bytes);
  ZuCHECK(client.process(client) == Zhttp::H3::CxnState::Error &&
      client.error == Zhttp::H3::IDError && client.errors == 1,
    "client accepted an unpermitted push stream");

  CxnInput server;
  server.server = true;
  server.push(bytes);
  ZuCHECK(server.process(server) == Zhttp::H3::CxnState::Error &&
      server.error == Zhttp::H3::StreamCreationError &&
      server.errors == 1,
    "server accepted a client-initiated push stream");

  CxnInput truncated;
  truncated.fin = true;
  bytes.length(0);
  Zhttp::H3::putVar(bytes, 1);
  bytes.push(0x40);
  truncated.push(bytes);
  ZuCHECK(truncated.process(truncated) == Zhttp::H3::CxnState::Error &&
      truncated.error == Zhttp::H3::StreamCreationError,
    "truncated push stream header returned the wrong error");

  CxnInput extension;
  extension.fin = true;
  bytes.length(0);
  Zhttp::H3::putVar(bytes, 0x21);
  bytes.push(0xaa);
  extension.push(bytes);
  extension.process(extension);
  ZuCHECK(!extension.errors,
    "unknown unidirectional stream was not ignored");
}

static void testRequestFrames()
{
  ZuTestScope(testRequestFrames);

  auto promise = pushPayload(1);
  Zhttp::H3::HdrBytes id;
  Zhttp::H3::putVar(id, 1);
  uint8_t malformed[] = { 0x40 };
  uint8_t extra[] = { 0, 0 };

  ZuTestCall(checkMsgError<true>,
    0x05, promise, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkMsgError<false>,
    0x05, promise, Zhttp::H3::IDError);
  ZuTestCall(checkMsgError<false>,
    0x05, ZuBSpan{malformed}, Zhttp::H3::FrameError);
  ZuTestCall(checkMsgError<true>,
    0x03, id, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkMsgError<false>,
    0x0d, id, Zhttp::H3::FrameUnexpected);
  ZuTestCall(checkMsgError<false>,
    0x03, ZuBSpan{extra}, Zhttp::H3::FrameError);

  MsgInput<false> extension;
  Zhttp::H3::HdrBytes bytes;
  frame(bytes, 0x21, {});
  extension.push(bytes);
  extension.process(extension);
  ZuCHECK(!extension.errors && !extension.completions,
    "unknown request-stream extension frame was not ignored");
}

static void testApplicationClose()
{
  ZuTestScope(testApplicationClose);

  CloseLink link;
  Zhttp::H3::Cxn<CloseLink, int> cxn;
  cxn.link_ = &link;
  cxn.error(Zhttp::H3::IDError);
  cxn.error(Zhttp::H3::FrameError);
  ZuCHECK(link.calls == 1 && link.error == Zhttp::H3::IDError &&
      cxn.errorCode == Zhttp::H3::IDError &&
      cxn.state == Zhttp::H3::CxnState::Error,
    "H3 error funnel did not preserve the first application close");
}

struct TxErrorStream {
  void txErrorFn(ZiTxErrorFn fn_) { fn = ZuMv(fn_); }
  ZiTxErrorFn fn;
};

static void testTxErrorPropagation()
{
  ZuTestScope(testTxErrorPropagation);

  CloseLink link;
  TxErrorStream control, enc, dec;
  Zhttp::H3::Cxn<CloseLink, TxErrorStream *> cxn;
  cxn.link_ = &link;
  cxn.control = &control;
  cxn.enc = &enc;
  cxn.dec = &dec;
  unsigned calls = 0;
  cxn.txErrorFn(ZiTxErrorFn{[&calls](ZeException &) {
    ++calls;
    return false;
  }});
  auto e = ZeEXCEPT(Error, "Zhttp", "test transmit error");
  ZuCHECK(control.fn && enc.fn && dec.fn &&
      !control.fn(e) && !enc.fn(e) && !dec.fn(e) &&
      calls == 3,
    "H3 Tx error handler did not propagate to connection streams");
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testControlFrames);
  ZuTestCall(testPushStreams);
  ZuTestCall(testRequestFrames);
  ZuTestCall(testApplicationClose);
  ZuTestCall(testTxErrorPropagation);
}
