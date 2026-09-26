//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// D-Bus bus-client connection and serial/call correlation core

#ifndef ZdbusClient_HH
#define ZdbusClient_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/Zdbus.hh>

namespace Zdbus_ {

using SubKey = ZuTuple<ZuCSpan, ZuCSpan, ZuCSpan, uint64_t>;
ZuDerive(SubText, (ZtString<ZtStringHeapID<"Zdbus.SubText">>));
ZuDerive(SubKeyText, (PathKeyText<"Zdbus.SubKey">));
struct Sub_ {
  SubKeyText	parts;
  SignalFn	fn;
  uint64_t	id; // assigned on Rx before tree insertion

  Sub_(ZuCSpan path_, ZuCSpan interface_, ZuCSpan member_,
      SignalFn fn_)
  : parts{path_, interface_, member_}, fn{ZuMv(fn_)} { }
  static SubKey KeyAxor(const Sub_ &sub) {
    return {sub.parts.path(), sub.parts.interface(), sub.parts.member(),
      sub.id};
  }
};
ZmRBTreeDerive(SubTable, Sub_,
  ZmRBTreeNode<Sub_, ZmRBTreeKey<Sub_::KeyAxor,
    ZmRBTreeUnique<true, ZmRBTreeHeapID<"Zdbus.Sub">>>>);
using Sub = SubTable::Node;

template <typename Heap = ZuVoid>
struct SubDrop_ : Heap {
  SubKeyText	parts;
  uint64_t	id;

  SubDrop_(ZuCSpan path_, ZuCSpan interface_, ZuCSpan member_,
      uint64_t id_)
  : parts{path_, interface_, member_}, id{id_} { }
};
ZuDerive(SubDropHeap, (ZmHeap<"Zdbus.SubDrop", SubDrop_<>>));
ZuDerive(SubDrop, (SubDrop_<SubDropHeap>));

struct Pending_ {
  CallFn			fn;
  ZmScheduler::Timer	timer;
  uint32_t		serial = 0;

  Pending_(uint32_t serial_, CallFn fn_)
  : fn{ZuMv(fn_)}, serial{serial_} { }
  static uint32_t KeyAxor(const Pending_ &p) { return p.serial; }
};
ZmHashDerive(PendingTable, Pending_,
  (ZmHashNode<Pending_,
    ZmHashKey<Pending_::KeyAxor,
      ZmHashHeapID<"Zdbus.Pending">>>));
using Pending = PendingTable::Node;

class ZdbusAPI Client {
public:
  Client() = default;
  ~Client();

  Client(const Client &) = delete;
  Client &operator =(const Client &) = delete;

  void init(ZmScheduler *, unsigned rxSid, unsigned txSid,
    Address, ClientParams, ReadyFn, SignalFn, MethodFn, CxnFailFn);
  void start();
  void stop(CxnStopFn = {});
  void final();
  void call(BuildFn, CallFn, ZuTime timeout = {});
  void cancel(uint32_t serial, CxnSendFn = {});
  template <typename Req, typename Fn>
  void call(BuildFn build, Fn &&fn, ZuTime timeout = {}) {
    call(ZuMv(build), typedCall<Req>(ZuFwd<Fn>(fn)), timeout);
  }
  void send(BuildFn, CxnSendFn = {});
  void addMatch(ZuCSpan rule, CallFn, ZuTime timeout = {});
  void removeMatch(ZuCSpan rule, CallFn, ZuTime timeout = {});
  // Local exact-match dispatch; install/remove the bus match rule separately.
  void subscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
    SignalFn, SubDoneFn);
  template <typename Sig, typename Fn>
  void subscribe(Fn &&fn, SubDoneFn done) {
    using Route = FixedRoute<Sig>;
    subscribe(Route::path(), Route::interface(), Route::member(),
      typedSignal<Sig>(ZuFwd<Fn>(fn)), ZuMv(done));
  }
  void unsubscribe(ZuCSpan path, ZuCSpan interface, ZuCSpan member,
    uint64_t id, CxnSendFn = {});

private:
  void connected_();
  void frame_(ZmRef<ZiIOBuf>, FrameInfo);
  void failed_(CxnFailure);
  void txHello_();
  void txHelloDone_(CallResult);
  void txCall_(BuildFn, CallFn, ZuTime, bool hello);
  void txSend_(BuildFn, CxnSendFn);
  void txReply_(ZmRef<ZiIOBuf>, FrameInfo);
  void txSendDone_(uint32_t, bool);
  void txTimeout_(uint32_t);
  bool txCancel_(uint32_t, int);
  void txStop_(CxnStopFn);
  void txStopped_();
  void txDrain_();
  void rxClearSubs_();
  void rxSubscribe_(ZuPtr<Sub>, SubDoneFn);
  void rxUnsubscribe_(ZuPtr<SubDrop>, CxnSendFn);
  void rxSignal_(ZmRef<ZiIOBuf>, FrameInfo, uint64_t after,
    uint64_t ceiling);
  uint32_t serial_();

private:
  ZmScheduler		*m_sched = nullptr;
  unsigned		m_rxSid = 0;
  unsigned		m_txSid = 0;
  ClientParams		m_params;
  ReadyFn		m_readyFn;
  SignalFn		m_signalFn;
  MethodFn		m_methodFn;
  CxnFailFn		m_failFn;

  alignas(Zm::CacheLineSize) PendingTable m_pending;
  CxnStopQueue		m_stopWaits;
  ZtString<ZtStringHeapID<"Zdbus.UniqueName">> m_uniqueName;
  uint32_t		m_nextSerial = 1;
  uint8_t		m_phase = 0;

  alignas(Zm::CacheLineSize) SubTable m_subs;
  uint64_t		m_nextSub = 1;
  bool			m_rxClosed = false;

  alignas(Zm::CacheLineSize) Connection m_cxn;
};

} // Zdbus_

using ZdbusClient = Zdbus_::Client;

#endif /* ZdbusClient_HH */
