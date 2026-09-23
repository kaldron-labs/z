//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <zlib/ZmAssert.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZuObject.hh>
#include <zlib/ZuRef.hh>
#include <zlib/ZiAssert.hh>
#include <zlib/ZdbusConnection.hh>

#include "ZdbusStop.hh"

namespace Zdbus_ {

ZtEnumImplNS(CxnState);

using FrameAlloc = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  Wire::MaxMessageSize, "Zdbus.Frame">;

template <typename Heap = ZuVoid>
struct StopDispatch_ : Heap, ZuObject {
  ZmScheduler	*sched;
  CxnStopQueue	waits;
  unsigned	sid;
  unsigned	budget;

  StopDispatch_(ZmScheduler *sched_, unsigned sid_, unsigned budget_,
      CxnStopQueue waits_)
  : sched{sched_}, waits{ZuMv(waits_)}, sid{sid_}, budget{budget_} { }

  void run_();
};
using StopDispatchHeap = ZmHeap<"Zdbus.CxnStopDispatch", StopDispatch_<>>;
ZuDerive(StopDispatch, (StopDispatch_<StopDispatchHeap>));

static void drainStops(CxnStopQueue &waits, unsigned budget)
{
  while (budget-- && waits.count_()) {
    auto wait = waits.shift();
    auto fn = ZuMv(wait->fn);
    wait = {};
    if (fn) fn();
  }
}

template <typename Heap>
void StopDispatch_<Heap>::run_()
{
  drainStops(waits, budget);
  if (waits.count_()) {
    ZuRef<StopDispatch> self{static_cast<StopDispatch *>(this)};
    sched->run([self = ZuMv(self)] { self->run_(); }, sid);
  }
}

void dispatchStops(ZmScheduler *sched, unsigned sid, unsigned budget,
  CxnStopQueue waits)
{
  if (!waits.count_()) return;
  if (!budget) budget = 1;
  drainStops(waits, budget);
  if (!waits.count_()) return;
  ZuRef<StopDispatch> dispatch = new StopDispatch{sched, sid, budget,
    ZuMv(waits)};
  sched->run([dispatch = ZuMv(dispatch)] { dispatch->run_(); }, sid);
}

Connection::~Connection()
{
  ZmAssert_(!m_sched && m_socket < 0 && !m_output.count_() &&
    !m_stopWaits.count_());
}

void Connection::init(
  ZmScheduler *sched, unsigned rxSid, unsigned txSid,
  Address addr, CxnParams params, CxnReadyFn readyFn,
  CxnFrameFn frameFn, CxnFailFn failFn)
{
  ZmAssert_(!m_sched && sched && addr && rxSid && txSid);
  m_sched = sched;
  m_startRequested.store_(0);
  m_txStopping = m_txStopped = m_txActive = false;
  m_rxSid = rxSid;
  m_txSid = txSid;
  m_addr = ZuMv(addr);
  m_params = params;
  if (!m_params.rxBytes) m_params.rxBytes = 1;
  if (!m_params.txBytes) m_params.txBytes = 1;
  if (!m_params.msgs) m_params.msgs = 1;
  if (m_params.frameLimit > Wire::MaxMessageSize)
    m_params.frameLimit = Wire::MaxMessageSize;
  m_readyFn = ZuMv(readyFn);
  m_frameFn = ZuMv(frameFn);
  m_failFn = ZuMv(failFn);
  m_loop.init(sched, rxSid, [this](ZeException) {
    rxFail_({0, 0, CxnError::System});
  });
}

void Connection::final()
{
  ZmAssert_(m_state == CxnState::Stopped &&
    !m_loopStarted && m_socket < 0 && !m_output.count_() &&
    !m_txMsgs && !m_txBytes && m_txStopped &&
    !m_stopWaits.count_());
  m_loop.final();
  m_sched = nullptr;
  m_readyFn = {};
  m_frameFn = {};
  m_failFn = {};
}

void Connection::start()
{
  ZmAssert_(m_sched);
  if (m_startRequested.cmpXch(1, 0)) return;
  m_sched->run([this] { txStart_(); }, m_txSid);
}

void Connection::txStart_()
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "start off Tx", return);
  if (m_txStopping || m_txStopped) return;
  m_loop.start([this](ZiEvent::StartResult result) {
    if (result.is<ZiEvent::Exception>()) {
      rxFail_({0, 0, CxnError::System});
      return;
    }
    m_loopStarted = true;
    connect_();
  });
}

void Connection::ready()
{
  m_loop.invoke([this] { rxReady_(); });
}

void Connection::rxReady_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "ready off Rx", return);
  if (m_state == CxnState::Hello) m_state = CxnState::Ready;
}

void Connection::send(ZmRef<ZiIOBuf> buf, CxnSendFn fn)
{
  m_sched->invoke([this, buf = ZuMv(buf), fn = ZuMv(fn)]() mutable {
    txSend_(ZuMv(buf), ZuMv(fn));
  }, m_txSid);
}

void Connection::txSend_(ZmRef<ZiIOBuf> buf, CxnSendFn fn)
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (), "send off Tx", return);
  unsigned n = buf ? buf->length : 0;
  if (m_txStopping || !m_txActive || !n || n > m_params.frameLimit ||
      m_txMsgs >= m_params.queueMsgs ||
      m_txBytes > m_params.queueBytes ||
      n > m_params.queueBytes - m_txBytes) {
    if (fn) fn(false);
    return;
  }
  m_txBytes += n;
  ++m_txMsgs;
  ZmRef<Out> out = new Out{ZuMv(buf), ZuMv(fn), n};
  m_sched->invoke([this, out = ZuMv(out)]() mutable {
    rxSend_(ZuMv(out));
  }, m_rxSid);
}

void Connection::txDone_(ZmRef<Out> out, bool ok)
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (), "done off Tx", return);
  ZmAssert_(m_txMsgs && m_txBytes >= out->charge);
  --m_txMsgs;
  m_txBytes -= out->charge;
  if (out->fn) out->fn(ok);
}

void Connection::rxSend_(ZmRef<Out> out)
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "enqueue off Rx", return);
  if (m_state == CxnState::Stopped || m_state == CxnState::Stopping ||
      m_socket < 0) {
    m_sched->invoke([this, out = ZuMv(out)]() mutable {
      txDone_(ZuMv(out), false);
    }, m_txSid);
    return;
  }
  m_output.pushNode(ZuMv(out));
  rxWrite_();
}

void Connection::connect_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "connect off Rx", return);
  if (m_state != CxnState::Stopped) return;
  sockaddr_un addr;
  socklen_t length;
  if (!m_addr.socketAddr(addr, length)) {
    rxFail_({0, EINVAL, CxnError::System});
    return;
  }
  m_socket = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (m_socket < 0 || !ZiEventLoop::unblock(m_socket)) {
    rxFail_({0, errno, CxnError::System});
    return;
  }
  int rc = ::connect(m_socket, reinterpret_cast<sockaddr *>(&addr), length);
  if (rc < 0 && errno != EINPROGRESS) {
    rxFail_({0, errno, CxnError::System});
    return;
  }
  if (!m_loop.addSocket(m_socket,
      [this](Zi::Socket) { rxWrite_(); },
      [this](Zi::Socket) { rxRead_(); }, false)) {
    rxFail_({0, errno, CxnError::System});
    return;
  }
  m_state = CxnState::Connecting;
  if (!rc) {
    connected_();
    rxWrite_();
  }
}

void Connection::connected_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "connected off Rx", return);
  m_state = CxnState::Authenticating;
  ZmRef<ZiIOBuf> buf = new FrameAlloc{};
  m_auth.start(*buf, unsigned(geteuid()));
  m_output.pushNode(new Out{ZuMv(buf), {}, 0});
}

void Connection::rxWrite_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "write off Rx", return);
  if (m_socket < 0 || m_state == CxnState::Stopping) return;
  if (m_state == CxnState::Connecting) {
    int error = 0;
    socklen_t length = sizeof(error);
    if (getsockopt(m_socket, SOL_SOCKET, SO_ERROR, &error, &length) < 0)
      error = errno;
    if (error) {
      rxFail_({0, error, CxnError::System});
      return;
    }
    connected_();
  }
  unsigned budget = m_params.txBytes;
  while (budget && m_output.count_()) {
    auto out = m_output.headPtr();
    unsigned left = out->buf->length - out->offset;
    unsigned n = left < budget ? left : budget;
    int sent = int(::send(m_socket, out->buf->data() + out->offset,
      n, MSG_NOSIGNAL));
    if (sent < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      rxFail_({0, errno, CxnError::System});
      return;
    }
    if (!sent) {
      rxFail_({0, ECONNRESET, CxnError::System});
      return;
    }
    out->offset += unsigned(sent);
    budget -= unsigned(sent);
    if (out->offset != out->buf->length) continue;
    auto done = m_output.shift();
    if (done->charge)
      m_sched->invoke([this, done = ZuMv(done)]() mutable {
        txDone_(ZuMv(done), true);
      }, m_txSid);
  }
  if (!budget && m_output.count_())
    m_loop.run([this] { rxWrite_(); });
}

bool Connection::auth_()
{
  unsigned consumed = m_auth.feed(m_input->cspan());
  m_input->advance(consumed);
  if (m_auth.state() == AuthState::Failed) {
    rxFail_({0, 0, CxnError::Auth});
    return false;
  }
  if (m_auth.state() != AuthState::Ready) {
    if (!m_input->length) m_input->clear();
    return true;
  }
  m_state = CxnState::Hello;
  ZmRef<ZiIOBuf> begin = new FrameAlloc{};
  *begin << Auth::begin();
  m_output.pushNode(new Out{ZuMv(begin), {}, 0});
  rxWrite_();
  if (m_state != CxnState::Hello) return false;
  m_sched->invoke([this] { m_txActive = true; }, m_txSid);
  if (m_readyFn) m_readyFn();
  return true;
}

static void retarget(ZuCSpan &span, ZuCSpan from, ZuCSpan to)
{
  if (span.length())
    span = {to.data() + (span.data() - from.data()), span.length()};
}

ZmRef<ZiIOBuf> splitFrame_(ZmRef<ZiIOBuf> &input,
  FrameInfo &info)
{
  ZmAssert_(input && info.total && input->length > info.total);
  unsigned total = info.total;
  unsigned tail = input->length - total;
  ZmRef<ZiIOBuf> copied = new FrameAlloc{};
  if (!copied->ensure(tail < total ? tail : total)) return {};
  if (tail < total) {
    memcpy(copied->data(), input->data() + total, tail);
    copied->length = tail;
    ZmRef<ZiIOBuf> complete = ZuMv(input);
    complete->length = total;
    input = ZuMv(copied);
    // The parsed header spans still point into the retained frame.
    return complete;
  }
  auto from = ZuCSpan{input->cspan()};
  memcpy(copied->data(), input->data(), total);
  copied->length = total;
  auto to = ZuCSpan{copied->cspan()};
  input->advance(total);
  // The frame was validated before splitting; only its borrowed views move.
  auto &h = info.headers;
  retarget(h.path, from, to);
  retarget(h.interface, from, to);
  retarget(h.member, from, to);
  retarget(h.errorName, from, to);
  retarget(h.destination, from, to);
  retarget(h.sender, from, to);
  retarget(h.signature, from, to);
  return copied;
}

bool Connection::frame_()
{
  auto parsed = frame(m_input->cspan(), m_params.frameLimit);
  if (parsed.error == FrameError::NeedMore) return false;
  if (!parsed) {
    rxFail_({parsed.offset, 0, CxnError::Frame});
    return false;
  }
  ZmRef<ZiIOBuf> complete;
  FrameInfo info = parsed.info;
  if (m_input->length == parsed.info.total)
    complete = ZuMv(m_input);
  else if (!(complete = splitFrame_(m_input, info))) {
    rxFail_({0, ENOMEM, CxnError::Resource});
    return false;
  }
  if (m_frameFn) m_frameFn(ZuMv(complete), info);
  return true;
}

bool Connection::consume_(unsigned &msgs)
{
  if (m_state == CxnState::Authenticating && !auth_()) return false;
  while (m_input && m_input->length && msgs < m_params.msgs &&
      (m_state == CxnState::Hello || m_state == CxnState::Ready)) {
    if (!frame_()) return m_state == CxnState::Hello ||
      m_state == CxnState::Ready;
    ++msgs;
  }
  return true;
}

void Connection::rxRead_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "read off Rx", return);
  if (m_socket < 0 || m_state == CxnState::Connecting ||
      m_state == CxnState::Stopping) return;
  unsigned budget = m_params.rxBytes;
  unsigned msgs = 0;
  while (budget && msgs < m_params.msgs) {
    if (!m_input) m_input = new FrameAlloc{};
    if (!consume_(msgs)) return;
    if (msgs == m_params.msgs) break;
    if (!m_input) m_input = new FrameAlloc{};
    unsigned n = m_input->length;
    unsigned want;
    if (m_state == CxnState::Authenticating) {
      if (n >= Auth::MaxLine + 1) {
        rxFail_({0, 0, CxnError::Auth});
        return;
      }
      // A malformed auth line cannot drive allocation up to rxBytes.
      want = Auth::MaxLine + 1 - n;
    } else if (n < Wire::MinHeaderSize)
      want = Wire::MinHeaderSize - n;
    else {
      auto parsed = frame(m_input->cspan(), m_params.frameLimit);
      if (parsed.error != FrameError::NeedMore) {
        if (!consume_(msgs)) return;
        continue;
      }
      want = parsed.info.total - n;
    }
    if (want > budget) want = budget;
    if (!m_input->ensure(n + want)) {
      rxFail_({0, ENOMEM, CxnError::Resource});
      return;
    }
    int received = int(::recv(m_socket, m_input->end(), want, 0));
    if (received < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      rxFail_({0, errno, CxnError::System});
      return;
    }
    if (!received) {
      rxFail_({0, ECONNRESET, CxnError::System});
      return;
    }
    m_input->length += unsigned(received);
    budget -= unsigned(received);
  }
  if (m_socket >= 0)
    m_loop.run([this] { rxRead_(); });
}

void Connection::close_()
{
  if (m_socket < 0) return;
  m_loop.delSocket(m_socket);
  ::close(m_socket);
  m_socket = -1;
}

void Connection::rxFail_(CxnFailure failure)
{
  if (m_state == CxnState::Stopping) return;
  m_state = CxnState::Stopping;
  close_();
  m_input = {};
  if (m_failFn) m_failFn(failure);
  rxFailDrain_();
}

void Connection::rxFailDrain_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (),
    "failure drain off Rx", return);
  if (m_state == CxnState::Stopped) return;
  unsigned budget = m_params.msgs;
  while (budget-- && m_output.count_()) {
    auto out = m_output.shift();
    if (out->charge)
      m_sched->invoke([this, out = ZuMv(out)]() mutable {
        txDone_(ZuMv(out), false);
      }, m_txSid);
  }
  if (m_output.count_())
    m_loop.run([this] { rxFailDrain_(); });
}

void Connection::stop(CxnStopFn fn)
{
  m_sched->invoke([this, fn = ZuMv(fn)]() mutable {
    if (m_txStopped) { if (fn) fn(); return; }
    if (fn) m_stopWaits.pushNode(new CxnStopQueue::Node{ZuMv(fn)});
    if (m_txStopping) return;
    m_txStopping = true;
    m_sched->invoke([this] { rxStop_(); }, m_rxSid);
  }, m_txSid);
}

void Connection::rxStop_()
{
  ZiAssert(m_loop.invoked(), "Zdbus", (), "stop off Rx", return);
  m_state = CxnState::Stopping;
  close_();
  m_input = {};
  rxDrain_();
}

void Connection::rxDrain_()
{
  unsigned budget = m_params.msgs;
  while (budget-- && m_output.count_()) {
    auto out = m_output.shift();
    if (out->charge)
      m_sched->invoke([this, out = ZuMv(out)]() mutable {
        txDone_(ZuMv(out), false);
      }, m_txSid);
  }
  if (m_output.count_()) {
    m_loop.run([this] { rxDrain_(); });
    return;
  }
  if (!m_loopStarted) {
    m_state = CxnState::Stopped;
    m_sched->invoke([this] { txStopped_(); }, m_txSid);
    return;
  }
  m_loop.stop([this](ZiEvent::StopResult) {
    m_loopStarted = false;
    m_state = CxnState::Stopped;
    m_sched->invoke([this] { txStopped_(); }, m_txSid);
  });
}

void Connection::txStopped_()
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "stop completion off Tx", return);
  CxnStopQueue waits{ZuMv(m_stopWaits)};
  m_txStopped = true;
  dispatchStops(m_sched, m_txSid, m_params.msgs, ZuMv(waits));
}

} // Zdbus_
