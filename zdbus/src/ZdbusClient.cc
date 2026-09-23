//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZiAssert.hh>
#include <zlib/ZdbusAdapter.hh>
#include <zlib/ZdbusClient.hh>

#include "ZdbusName.hh"
#include "ZdbusSerial.hh"
#include "ZdbusStop.hh"

namespace Zdbus_ {

namespace ClientPhase {
  enum { Stopped, Hello, Ready, Stopping, Terminated };
}

struct HelloBody { };
ZuTypeList<> ZuFields_(HelloBody *, ZuFacet::DBUS *);

using HelloHeaders = ZuTypeList<
  HeaderEntry<Header::Path, ZuStringT<"/org/freedesktop/DBus">>,
  HeaderEntry<Header::Interface, ZuStringT<"org.freedesktop.DBus">>,
  HeaderEntry<Header::Member, ZuStringT<"Hello">>,
  HeaderEntry<Header::Destination, ZuStringT<"org.freedesktop.DBus">>>;
struct HelloReq : ReqBuilder<HelloReq, HelloBody, HelloHeaders> { };

struct HelloReply {
  ZtString<ZtStringHeapID<"Zdbus.HelloReply">> name;
};
ZfStruct(, HelloReply, (((name), (Mutable)), (String)));
ZfStructRender(, HelloReply, DBUS, name);
struct HelloRes : ResParser<HelloRes, HelloReply> { };

struct MatchArgs { ZuCSpan rule; };
ZfStruct(, MatchArgs, (((rule), (Mutable)), (String)));
ZfStructRender(, MatchArgs, DBUS, rule);
using AddMatchHeaders = ZuTypeList<
  HeaderEntry<Header::Path, ZuStringT<"/org/freedesktop/DBus">>,
  HeaderEntry<Header::Interface, ZuStringT<"org.freedesktop.DBus">>,
  HeaderEntry<Header::Member, ZuStringT<"AddMatch">>,
  HeaderEntry<Header::Destination, ZuStringT<"org.freedesktop.DBus">>>;
using RemoveMatchHeaders = ZuTypeList<
  HeaderEntry<Header::Path, ZuStringT<"/org/freedesktop/DBus">>,
  HeaderEntry<Header::Interface, ZuStringT<"org.freedesktop.DBus">>,
  HeaderEntry<Header::Member, ZuStringT<"RemoveMatch">>,
  HeaderEntry<Header::Destination, ZuStringT<"org.freedesktop.DBus">>>;
struct AddMatchReq : ReqBuilder<AddMatchReq, MatchArgs,
  AddMatchHeaders> { };
struct RemoveMatchReq : ReqBuilder<RemoveMatchReq, MatchArgs,
  RemoveMatchHeaders> { };

Client::~Client()
{
  ZmAssert_(!m_sched && !m_pending.count_() && !m_subs.count_() &&
    !m_stopWaits.count_());
}

void Client::init(ZmScheduler *sched, unsigned rxSid, unsigned txSid,
  Address addr, ClientParams params, ReadyFn readyFn, SignalFn signalFn,
  MethodFn methodFn, CxnFailFn failFn)
{
  ZmAssert_(!m_sched && sched && rxSid && txSid);
  m_sched = sched;
  m_rxSid = rxSid;
  m_txSid = txSid;
  m_nextSerial = 1;
  m_nextSub = 1;
  m_phase = ClientPhase::Stopped;
  m_params = params;
  if (!m_params.pendingLimit) m_params.pendingLimit = 1;
  if (!m_params.subLimit) m_params.subLimit = 1;
  if (!m_params.signalBatch) m_params.signalBatch = 1;
  m_rxClosed = false;
  m_readyFn = ZuMv(readyFn);
  m_signalFn = ZuMv(signalFn);
  m_methodFn = ZuMv(methodFn);
  m_failFn = ZuMv(failFn);
  m_cxn.init(sched, rxSid, txSid, ZuMv(addr), params.cxn,
    [this] { connected_(); },
    [this](ZmRef<ZiIOBuf> frame, FrameInfo info) {
      frame_(ZuMv(frame), info);
    },
    [this](CxnFailure failure) { failed_(failure); });
}

void Client::start()
{
  ZmAssert_(m_sched);
  m_cxn.start();
}

void Client::final()
{
  ZmAssert_(m_phase == ClientPhase::Terminated && !m_pending.count_() &&
    !m_subs.count_() && !m_stopWaits.count_());
  m_cxn.final();
  m_sched = nullptr;
  m_readyFn = {};
  m_signalFn = {};
  m_methodFn = {};
  m_failFn = {};
  m_uniqueName.length(0);
}

void Client::connected_()
{
  m_sched->invoke([this] { txHello_(); }, m_txSid);
}

void Client::frame_(ZmRef<ZiIOBuf> frame, FrameInfo info)
{
  switch (info.type) {
    case MessageType::MethodReturn:
    case MessageType::Error:
      m_sched->invoke([this, frame = ZuMv(frame), info]() mutable {
        txReply_(ZuMv(frame), info);
      }, m_txSid);
      return;
    case MessageType::Signal:
      if (m_signalFn) m_signalFn(frame, info);
      rxSignal_(ZuMv(frame), info, 0, m_nextSub - 1);
      return;
    case MessageType::MethodCall:
      if (m_methodFn) m_methodFn(ZuMv(frame), info);
      return;
  }
}

void Client::failed_(CxnFailure failure)
{
  if (m_failFn) m_failFn(failure);
  stop();
}

void Client::txHello_()
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "Hello off Tx", return);
  if (m_phase != ClientPhase::Stopped) return;
  m_phase = ClientPhase::Hello;
  txCall_([](uint32_t serial) {
    HelloBody body;
    HelloReq req;
    req.init(&body);
    return req.build(serial);
  }, [this](CallResult result) { txHelloDone_(ZuMv(result)); }, {}, true);
}

void Client::txHelloDone_(CallResult result)
{
  if (m_phase == ClientPhase::Stopping) return;
  if (!result || !result.frame ||
      result.info.type != MessageType::MethodReturn ||
      result.info.headers.signature != "s") {
    failed_({0, 0, CxnError::Frame});
    return;
  }
  HelloReply reply;
  HelloRes parser;
  parser.init(&reply);
  if (!parser.load(ZuMv(result.frame), result.info)) {
    failed_({0, 0, CxnError::Frame});
    return;
  }
  m_uniqueName = ZuMv(reply.name);
  m_phase = ClientPhase::Ready;
  m_cxn.ready();
  if (m_readyFn) m_readyFn(m_uniqueName);
}

void Client::call(BuildFn build, CallFn fn, ZuTime timeout)
{
  m_sched->invoke([this, build = ZuMv(build), fn = ZuMv(fn), timeout]() mutable {
    txCall_(ZuMv(build), ZuMv(fn), timeout, false);
  }, m_txSid);
}

void Client::cancel(uint32_t serial, CxnSendFn done)
{
  m_sched->run([this, serial, done = ZuMv(done)]() mutable {
    bool cancelled = txCancel_(serial, ClientError::Cancelled);
    if (done) done(cancelled);
  }, m_txSid);
}

void Client::send(BuildFn build, CxnSendFn fn)
{
  m_sched->invoke([this, build = ZuMv(build), fn = ZuMv(fn)]() mutable {
    txSend_(ZuMv(build), ZuMv(fn));
  }, m_txSid);
}

void Client::addMatch(ZuCSpan rule, CallFn fn, ZuTime timeout)
{
  call([rule = SubText{rule}](uint32_t serial) {
    MatchArgs args{rule};
    AddMatchReq req;
    req.init(&args);
    return req.build(serial);
  }, ZuMv(fn), timeout);
}

void Client::removeMatch(ZuCSpan rule, CallFn fn, ZuTime timeout)
{
  call([rule = SubText{rule}](uint32_t serial) {
    MatchArgs args{rule};
    RemoveMatchReq req;
    req.init(&args);
    return req.build(serial);
  }, ZuMv(fn), timeout);
}

void Client::subscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
  SignalFn fn, SubDoneFn done)
{
  if (!fn || !SubKeyText::fits(path, interface, member) ||
      !ZfDBUS::objectPath(path) || !Name::interface(interface) ||
      !Name::member(member)) {
    m_sched->run([done = ZuMv(done)]() mutable {
      if (done) done(0);
    }, m_rxSid);
    return;
  }
  // The posted closure transfers sole ownership; only the pointer crosses.
  ZuPtr<Sub> sub = new Sub{path, interface, member, ZuMv(fn)};
  m_sched->run([this, sub = ZuMv(sub), done = ZuMv(done)]() mutable {
    rxSubscribe_(ZuMv(sub), ZuMv(done));
  }, m_rxSid);
}

void Client::rxSubscribe_(ZuPtr<Sub> sub, SubDoneFn done)
{
  ZiAssert(m_sched->invoked(m_rxSid), "Zdbus", (),
    "subscribe off Rx", return);
  uint64_t id = 0;
  if (!m_rxClosed && m_subs.count_() < m_params.subLimit) {
    id = m_nextSub++;
    if (!m_nextSub) m_nextSub = 1;
    sub->id = id;
    m_subs.addNode(ZuMv(sub).release());
  }
  if (done) done(id);
}

void Client::unsubscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
  uint64_t id, CxnSendFn done)
{
  if (!id || !SubKeyText::fits(path, interface, member) ||
      !ZfDBUS::objectPath(path) || !Name::interface(interface) ||
      !Name::member(member)) {
    m_sched->run([done = ZuMv(done)]() mutable {
      if (done) done(false);
    }, m_rxSid);
    return;
  }
  // Keep the variable-length key in one owned request until Rx consumes it.
  ZuPtr<SubDrop> drop = new SubDrop{path, interface, member, id};
  m_sched->run([this, drop = ZuMv(drop), done = ZuMv(done)]() mutable {
    rxUnsubscribe_(ZuMv(drop), ZuMv(done));
  }, m_rxSid);
}

void Client::rxUnsubscribe_(ZuPtr<SubDrop> drop, CxnSendFn done)
{
  ZiAssert(m_sched->invoked(m_rxSid), "Zdbus", (),
    "unsubscribe off Rx", return);
  auto removedSub = m_subs.del(SubKey{drop->parts.path(),
    drop->parts.interface(), drop->parts.member(), drop->id});
  bool removed = !!removedSub;
  removedSub = {};
  if (done) done(removed);
}

void Client::rxSignal_(ZmRef<ZiIOBuf> frame, FrameInfo info,
  uint64_t after, uint64_t ceiling)
{
  ZiAssert(m_sched->invoked(m_rxSid), "Zdbus", (),
    "signal off Rx", return);
  if (m_rxClosed || after == UINT64_MAX) return;
  unsigned budget = m_params.signalBatch;
  while (budget--) {
    Sub *next = nullptr;
    {
      auto i = m_subs.iter(SubKey{info.headers.path,
        info.headers.interface, info.headers.member, after + 1});
      next = i();
    }
    if (!next || next->parts.path() != info.headers.path ||
        next->parts.interface() != info.headers.interface ||
        next->parts.member() != info.headers.member ||
        next->id > ceiling) return;
    after = next->id;
    next->fn(frame, info);
  }
  m_sched->run([this, frame = ZuMv(frame), info, after,
      ceiling]() mutable {
    rxSignal_(ZuMv(frame), info, after, ceiling);
  }, m_rxSid);
}

void Client::txSend_(BuildFn build, CxnSendFn fn)
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "send off Tx", return);
  if (m_phase != ClientPhase::Ready) {
    if (fn) fn(false);
    return;
  }
  uint32_t serial = serial_();
  if (!serial) {
    if (fn) fn(false);
    return;
  }
  auto made = build(serial);
  if (!made) {
    if (fn) fn(false);
    return;
  }
  m_cxn.send(ZuMv(made.buf), ZuMv(fn));
}

uint32_t Client::serial_()
{
  return serialProbe(m_nextSerial, m_pending.count_() + 1,
    [this](uint32_t serial) { return bool(m_pending.find(serial)); });
}

void Client::txCall_(BuildFn build, CallFn fn, ZuTime timeout, bool hello)
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "call off Tx", return);
  if ((!hello && m_phase != ClientPhase::Ready) ||
      (hello && m_phase != ClientPhase::Hello)) {
    if (fn) fn(CallResult{{}, {}, ClientError::Stopped});
    return;
  }
  if (m_pending.count_() >= m_params.pendingLimit) {
    if (fn) fn(CallResult{{}, {}, ClientError::Queue});
    return;
  }
  uint32_t serial = serial_();
  if (!serial) {
    if (fn) fn(CallResult{{}, {}, ClientError::Queue});
    return;
  }
  auto made = build(serial);
  if (!made) {
    if (fn) fn(CallResult{{}, {}, ClientError::Build});
    return;
  }
  auto pending = new Pending{serial, ZuMv(fn)};
  m_pending.addNode(pending);
  if (*timeout) {
    m_sched->add(&pending->timer, timeout, ZmScheduler::Update,
      [this, serial](auto &&arm) {
        return arm([this, serial] { txTimeout_(serial); });
      }, m_txSid);
  }
  m_cxn.send(ZuMv(made.buf), [this, serial](bool ok) {
    txSendDone_(serial, ok);
  });
}

void Client::txSendDone_(uint32_t serial, bool ok)
{
  if (!ok) txCancel_(serial, ClientError::Queue);
}

void Client::txTimeout_(uint32_t serial)
{
  txCancel_(serial, ClientError::Timeout);
}

bool Client::txCancel_(uint32_t serial, int error)
{
  auto pending = m_pending.del(serial);
  if (!pending) return false;
  m_sched->del(&pending->timer);
  auto fn = ZuMv(pending->fn);
  pending = {};
  if (fn) fn(CallResult{{}, {}, error});
  return true;
}

void Client::txReply_(ZmRef<ZiIOBuf> frame, FrameInfo info)
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "reply off Tx", return);
  uint32_t serial = info.headers.replySerial;
  auto pending = m_pending.del(serial);
  if (!pending) return;
  m_sched->del(&pending->timer);
  auto fn = ZuMv(pending->fn);
  pending = {};
  if (fn) fn(CallResult{ZuMv(frame), info, ClientError::None});
}

void Client::stop(CxnStopFn fn)
{
  m_sched->invoke([this, fn = ZuMv(fn)]() mutable {
    txStop_(ZuMv(fn));
  }, m_txSid);
}

void Client::txStop_(CxnStopFn fn)
{
  if (m_phase == ClientPhase::Terminated) {
    if (fn) fn();
    return;
  }
  if (fn) m_stopWaits.pushNode(new CxnStopQueue::Node{ZuMv(fn)});
  if (m_phase == ClientPhase::Stopping) return;
  m_phase = ClientPhase::Stopping;
  m_cxn.stop([this] { txStopped_(); });
}

void Client::txStopped_()
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "stop completion off Tx", return);
  m_sched->run([this] { rxClearSubs_(); }, m_rxSid);
}

void Client::rxClearSubs_()
{
  ZiAssert(m_sched->invoked(m_rxSid), "Zdbus", (),
    "subscription cleanup off Rx", return);
  m_rxClosed = true;
  unsigned budget = m_params.signalBatch;
  while (budget-- && m_subs.count_()) {
    auto sub = m_subs.delNode(m_subs.minimumPtr());
    sub = {};
  }
  if (m_subs.count_()) {
    m_sched->run([this] { rxClearSubs_(); }, m_rxSid);
    return;
  }
  m_sched->run([this] { txDrain_(); }, m_txSid);
}

void Client::txDrain_()
{
  ZiAssert(m_sched->invoked(m_txSid), "Zdbus", (),
    "stop drain off Tx", return);
  unsigned budget = m_params.cxn.msgs ? m_params.cxn.msgs : 1;
  while (budget-- && m_pending.count_()) {
    ZuPtr<Pending> pending;
    {
      auto i = m_pending.iter();
      pending = i.del();
    }
    m_sched->del(&pending->timer);
    auto callFn = ZuMv(pending->fn);
    pending = {};
    if (callFn) callFn(CallResult{{}, {}, ClientError::Disconnect});
  }
  if (m_pending.count_()) {
    m_sched->run([this] { txDrain_(); }, m_txSid);
    return;
  }
  m_phase = ClientPhase::Terminated;
  CxnStopQueue waits{ZuMv(m_stopWaits)};
  dispatchStops(m_sched, m_txSid, m_params.cxn.msgs, ZuMv(waits));
}

} // Zdbus_
