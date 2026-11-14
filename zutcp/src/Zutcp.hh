//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TCP transport API

#ifndef Zutcp_HH
#define Zutcp_HH

#ifndef ZutcpLib_HH
#include <zlib/ZutcpLib.hh>
#endif

#include <string.h>

#ifndef _WIN32
#include <sys/socket.h>
#endif


#include <zlib/ZuObject.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmEngine.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmQueue.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZmScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZtcHub.hh>
#include <zlib/ZxdpAlloc.hh>


#if defined(ZDEBUG) && !defined(Zutcp_DEBUG)
#define Zutcp_DEBUG	// enable testing / debugging
#endif

namespace Zutcp {
  inline constexpr uint64_t U64Null = ZuCmp<uint64_t>::null();
}
namespace Zutcp_ { using namespace Zutcp; }

#include <zlib/ZutcpIP.hh>
#include <zlib/ZutcpOptions.hh>
#include <zlib/ZutcpMx.hh>
#include <zlib/ZutcpTypes.hh>
#include <zlib/ZutcpBuf.hh>
#include <zlib/ZutcpPQueue.hh>
#include <zlib/ZutcpConnectionUtil.hh>
#include <zlib/ZutcpSched.hh>
#include <zlib/ZutcpLog.hh>
#include <zlib/ZutcpRecovery.hh>
#include <zlib/ZutcpPath.hh>
#include <zlib/ZutcpSock.hh>
#include <zlib/ZutcpDiag.hh>

namespace Zutcp_ {

struct PendingInitial {
  CxnSlot slot;
  ClosedTupleKey tuple;
  uint64_t evidenceID = 0; // Nonzero also marks a chosen ISN; zero ISN is valid.
  uint32_t localISN = 0;
};

} // namespace Zutcp_

namespace Zutcp {

// Public connection metadata
ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zutcp.Log">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zutcp.ParamStrings">>));

struct InitialInfo {
  TCPHeader	header;
  ZiSockAddr	peer;
  unsigned	datagramLength = 0;
  IPPath	path{};
  ZiSockAddr	local;
};

using ErrorFn = ZmFn<void(ZeException), ZmFnHeapID<"Zutcp.ErrorFn">>;

inline ErrorFn defaultErrorFn()
{
  return ErrorFn{[](ZeException e) { ZiLogEvent(ZuMv(e)); }};
}

inline ZuBSpan byteSpan(const uint8_t *data, unsigned len)
{
  return ZuBSpan{data, len};
}

} // namespace Zutcp

#include <zlib/Zutcp_.hh>

namespace Zutcp {

using Zutcp_::TupleRouter;
using Zutcp_::Endpoint_;
using Zutcp_::ServerLinks;

class HubParams {
public:
  HubParams(ZiMultiplex *mx = nullptr)
  : m_mx{mx}, m_errorFn{defaultErrorFn()}
  {
  }





  HubParams &&qlog(bool v) {
    m_qlogParams.enabled(v);
    return ZuMv(*this);
  }
  HubParams &&qlogPath(ZuCSpan v) {
    m_qlogParams.path(v);
    return ZuMv(*this);
  }
  HubParams &&qlogRingSize(unsigned v) {
    m_qlogParams.ringSize(v);
    return ZuMv(*this);
  }
  HubParams &&qlogAge(unsigned v) {
    m_qlogParams.age(v);
    return ZuMv(*this);
  }

  HubParams &&sackPermitted(bool v) { m_sackPermitted = v; return ZuMv(*this); }
  HubParams &&windowScale(bool v) { m_windowScale = v; return ZuMv(*this); }
  HubParams &&timestamps(bool v) { m_timestamps = v; return ZuMv(*this); }
  HubParams &&maxData(uint64_t v) { m_maxData = v; return ZuMv(*this); }
  HubParams &&maxCxns(unsigned v) { m_maxCxns = v; return ZuMv(*this); }
  HubParams &&maxPendingCxns(unsigned v) { m_maxPendingCxns = v; return ZuMv(*this); }
  HubParams &&closedTupleRingSize(unsigned v) { m_closedTupleRingSize = v; return ZuMv(*this); }
  HubParams &&tfoCookieCacheSize(unsigned v) { m_tfoCookieCacheSize = v; return ZuMv(*this); }
  HubParams &&connectTimeout(ZuTime v) { m_connectTimeout = v; return ZuMv(*this); }
  HubParams &&userTimeout(ZuTime v) { m_userTimeout = v; return ZuMv(*this); }
  HubParams &&closeDrainTimeout(ZuTime v) { m_closeDrainTimeout = v; return ZuMv(*this); }
  HubParams &&finWaitTimeout(ZuTime v) { m_finWaitTimeout = v; return ZuMv(*this); }
  HubParams &&maxIdleTimeout(uint64_t v) {
    m_maxIdleTimeout = v;
    return ZuMv(*this);
  }
  HubParams &&heartBeat(ZuTime v) {
    m_heartBeat = normalizeHeartBeat(v);
    return ZuMv(*this);
  }
  HubParams &&maxPayload(unsigned v) { m_maxPayload = v; return ZuMv(*this); }
  HubParams &&initialRto(ZuTime v) { m_rtoParams.initial = v; return ZuMv(*this); }
  HubParams &&minRto(ZuTime v) { m_rtoParams.min = v; return ZuMv(*this); }
  HubParams &&maxRto(ZuTime v) { m_rtoParams.max = v; return ZuMv(*this); }
  HubParams &&rtoGranularity(ZuTime v) { m_rtoParams.granularity = v; return ZuMv(*this); }
  HubParams &&tlpPeerAckDelay(ZuTime v) { m_tlpPeerAckDelay = v; return ZuMv(*this); }
  HubParams &&ecn(bool v) { m_ecn = v; return ZuMv(*this); }
  HubParams &&controlBufCount(unsigned v) { m_controlBufCount = v; return ZuMv(*this); }
  HubParams &&txControlCredits(unsigned v) { m_txControlCredits = v; return ZuMv(*this); }
  HubParams &&rxRetainFrames(unsigned v) { m_rxRetainFrames = v; return ZuMv(*this); }
  HubParams &&rxRetainFramesPerCxn(unsigned v) { m_rxRetainPerCxn = v; return ZuMv(*this); }
  HubParams &&rxReserveFrames(unsigned v) { m_rxReserveFrames = v; return ZuMv(*this); }











  HubParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx() const { return m_mx; }





  const ZutcpLogParams &qlogParams() const { return m_qlogParams; }

  bool sackPermitted() const { return m_sackPermitted; }
  bool windowScale() const { return m_windowScale; }
  bool timestamps() const { return m_timestamps; }
  uint64_t maxData() const { return m_maxData; }
  unsigned maxCxns() const { return m_maxCxns; }
  unsigned maxPendingCxns() const { return m_maxPendingCxns; }
  unsigned closedTupleRingSize() const { return m_closedTupleRingSize; }
  unsigned tfoCookieCacheSize() const { return m_tfoCookieCacheSize; }
  ZuTime connectTimeout() const { return m_connectTimeout; }
  ZuTime userTimeout() const { return m_userTimeout; }
  ZuTime closeDrainTimeout() const { return m_closeDrainTimeout; }
  ZuTime finWaitTimeout() const { return m_finWaitTimeout; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  ZuTime heartBeat() const { return m_heartBeat; }
  unsigned maxPayload() const { return m_maxPayload; }
  bool ecn() const { return m_ecn; }
  unsigned controlBufCount() const { return m_controlBufCount; }
  unsigned txControlCredits() const { return m_txControlCredits; }
  const RtoParams &rtoParams() const { return m_rtoParams; }
  ZuTime tlpPeerAckDelay() const { return m_tlpPeerAckDelay; }
  unsigned rxRetainFrames() const { return m_rxRetainFrames; }
  unsigned rxRetainFramesPerCxn() const { return m_rxRetainPerCxn; }
  unsigned rxReserveFrames() const { return m_rxReserveFrames; }






  const ErrorFn &errorFn() const { return m_errorFn; }
  ErrorFn &errorFn() { return m_errorFn; }

private:

  static ZuTime normalizeHeartBeat(ZuTime v) {
    if (!*v || v == ZuTime{0}) return {};
    if (v < ZuTime{0} || v.nsec() < 0 || v.nsec() >= 1000000000)
      throw ZeEXCEPT(Error, "Zutcp",
	"invalid TCP heartbeat interval");
    return v;
  }

  ZiMultiplex		*m_mx = nullptr;
  ZutcpLogParams	m_qlogParams;
  uint64_t		m_maxData = DefaultMaxData;
  unsigned		m_maxCxns = DefaultMaxCxns, m_maxPendingCxns = DefaultMaxPendingCxns;
  unsigned		m_closedTupleRingSize = DefaultClosedTupleRingSize;
  unsigned		m_tfoCookieCacheSize = DefaultTFOCookieCacheSize;
  ZuTime		m_connectTimeout{DefaultConnectTimeout};
  ZuTime		m_userTimeout{DefaultUserTimeout};
  ZuTime		m_closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime		m_finWaitTimeout{DefaultFinWaitTimeout};
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxPayload = DefaultMaxPayload;
  unsigned		m_controlBufCount = DefaultControlBufCount;
  unsigned		m_txControlCredits = DefaultTxControlCredits;
  RtoParams		m_rtoParams;
  ZuTime		m_tlpPeerAckDelay{0, 200000000};
  unsigned		m_rxRetainFrames = DefaultRxRetainFrames;
  unsigned		m_rxRetainPerCxn = DefaultRxRetainPerCxn;
  unsigned		m_rxReserveFrames = DefaultRxReserveFrames;
  bool			m_sackPermitted = true, m_windowScale = true, m_timestamps = true;
  bool			m_ecn = false;
  ErrorFn		m_errorFn;
};

using ClientParams = HubParams;
using ServerParams = HubParams;

template <typename App_> class Hub :
  public ZmPolymorph,
  public ZmEngine<App_>,
  public Ztc::Hub {
friend ZmEngine<App_>;
template <typename, typename, typename>
friend class Connection;
template <typename, typename, typename>
friend class CliCxn;
template <typename, typename, typename>
friend class SrvCxn;
template <typename>
friend class Zutcp_::Endpoint_;

public:
  using App = App_;
  using HubCtl = ZmEngine<App>;

  using HubCtl::start;
  using HubCtl::stop;
  using HubCtl::state;
  using HubCtl::stopping;

  const App *app() const { return static_cast<const App *>(this); }
  App *app() { return static_cast<App *>(this); }

  ZuTime transportNow() const { return Zm::now(); }

  Hub() : m_id{ZuID{} << "tcp:" <<
    ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }

  bool init(HubParams params) {
    return init_(ZuMv(params), Zutcp::Vantage::Unknown,
      [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  TelKey telKey() const override {
    return {Ztc::LinkType::TCP, m_id};
  }
  void telemetry(Ztc::HubTelemetry &data) const override {
    data.id = m_id;
    if (m_mx) data.mxID = m_mx->id();
    data.down = m_down.load_();
    data.disabled = 0;
    data.transient = m_transient.load_();
    data.up = m_up.load_();
    data.reconn = 0;
    data.failed = 0;
    data.nLinks = m_nLinks.load_();
    data.rxThread = m_rxThread;
    data.txThread = m_txThread;
    data.linkType = Ztc::LinkType::TCP;
    data.state = state();
  }
  unsigned allLinks(Ztc::Hub::AllLinksFn fn) const override {
    return app()->allLinks_(ZuMv(fn));
  }
  unsigned allPools(Ztc::Hub::AllPoolsFn fn) const override {
    return app()->allPools_(ZuMv(fn));
  }

  void final() {
    bool ok = HubCtl::lock(ZmEngineState::Stopped, [this]() {
      if (m_registered) {
	Ztc::HubMgr::del(this);
	m_registered = false;
      }
      ZutcpLogger::final(m_qlogTrace);
      m_dataTxAllocator = nullptr;
      m_mx = nullptr;
      m_rxThread = 0;
      m_txThread = 0;
      m_errorFn = ErrorFn{};
      m_qlogParams = {};
      m_maxData = DefaultMaxData;
      m_sackPermitted = m_windowScale = m_timestamps = true;
      m_maxCxns = DefaultMaxCxns;
      m_maxPendingCxns = DefaultMaxPendingCxns;
      m_closedTupleRingSize = DefaultClosedTupleRingSize;
      m_tfoCookieCacheSize = DefaultTFOCookieCacheSize;
      m_connectTimeout = ZuTime{DefaultConnectTimeout};
      m_userTimeout = ZuTime{DefaultUserTimeout};
      m_closeDrainTimeout = ZuTime{DefaultCloseDrainTimeout};
      m_finWaitTimeout = ZuTime{DefaultFinWaitTimeout};
      m_maxIdleTimeout = 0;
      m_heartBeat = {};
      m_maxPayload = DefaultMaxPayload;
      m_controlBufCount = DefaultControlBufCount;
      m_txControlCredits = DefaultTxControlCredits;
      m_rtoParams = {};
      m_tlpPeerAckDelay = ZuTime{0, 200000000};
      m_rxRetainFrames = DefaultRxRetainFrames;
      m_rxRetainPerCxn = DefaultRxRetainPerCxn;
      m_rxReserveFrames = DefaultRxReserveFrames;
      m_stack = nullptr;
      m_ecn = false;
      return true;
    });
    ZiAssert(ok, "Zutcp", (),
      "TCP hub finalization while not stopped", return);
  }

  ZiMultiplex *mx() const { return m_mx; }
  bool ioActive() const { return m_mx && m_mx->running(); }



  Zi::IOBufAllocator *dataRxAllocator() const { return inlineAllocator(); }
  bool retainRx(ZiIOBuf *packet, ZmRef<RxAccount> &account, uint64_t generation) {
    if (!m_stack || !packet->registered()) return true;
    if (!account) account = new RxAccount{m_stack->rxCxnLimit(), generation};
    return m_stack->retainRx(packet, account);
  }
  Zi::IOBufAllocator *dataTxAllocator() const { return m_dataTxAllocator; }
  bool nativeTx() const { return m_stack; }
  Stack *nativeStack() const { return m_stack; }
  Zxdp::BufPool *txPool(Zi::IOBufAllocator *allocator) const {
    if (!m_stack || !allocator || allocator->mx != m_mx ||
	allocator->type != Zi::IOBufType::TxTCP) return nullptr;
    auto provider = static_cast<Allocator *>(allocator->provider());
    return provider ? &provider->pool : nullptr;
  }
  bool txWritable(const TxBacking &source) {
    if (!m_stack || !source.pool) return false;
    if (source.pool->writable(source.data)) return true;
    m_stack->reconcileTx();
    return source.pool->writable(source.data);
  }
  unsigned rxThread() const { return m_rxThread; }
  unsigned txThread() const { return m_txThread; }









  ZutcpLogger::Trace &qlogTrace() { return m_qlogTrace; }
  ZutcpLogDiag qlogDiag() const { return ZutcpLogger::diag(); }
  bool sackPermitted() const { return m_sackPermitted; }
  bool windowScale() const { return m_windowScale; }
  bool timestamps() const { return m_timestamps; }
  uint64_t maxData() const { return m_maxData; }
  unsigned maxCxns() const { return m_maxCxns; }
  unsigned maxPendingCxns() const { return m_maxPendingCxns; }
  unsigned closedTupleRingSize() const { return m_closedTupleRingSize; }
  unsigned tfoCookieCacheSize() const { return m_tfoCookieCacheSize; }
  ZuTime connectTimeout() const { return m_connectTimeout; }
  ZuTime userTimeout() const { return m_userTimeout; }
  ZuTime closeDrainTimeout() const { return m_closeDrainTimeout; }
  ZuTime finWaitTimeout() const { return m_finWaitTimeout; }
  uint64_t maxIdleTimeout() const { return m_maxIdleTimeout; }
  ZuTime heartBeat() const { return m_heartBeat; }
  unsigned maxPayload() const { return m_maxPayload; }
  bool ecn() const { return m_ecn; }
  unsigned controlBufCount() const { return m_controlBufCount; }
  unsigned txControlCredits() const { return m_txControlCredits; }
  const RtoParams &rtoParams() const { return m_rtoParams; }
  ZuTime tlpPeerAckDelay() const { return m_tlpPeerAckDelay; }
  unsigned rxRetainFrames() const { return m_rxRetainFrames; }
  unsigned rxRetainFramesPerCxn() const { return m_rxRetainPerCxn; }
  unsigned rxReserveFrames() const { return m_rxReserveFrames; }







  template <typename ...Args>
  void rxRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_rxThread);
  }
  template <typename ...Args>
  void rxInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_rxThread);
  }
  bool rxInvoked() { return m_mx && m_rxThread && m_mx->invoked(m_rxThread); }
  template <typename ...Args>
  void txRun(Args &&...args) {
    m_mx->run(ZuFwd<Args>(args)..., m_txThread);
  }
  template <typename ...Args>
  void txInvoke(Args &&...args) {
    m_mx->invoke(ZuFwd<Args>(args)..., m_txThread);
  }
  bool txInvoked() { return m_mx && m_txThread && m_mx->invoked(m_txThread); }

private:
  void linkAdded_(Ztc::Link *link) {
    Ztc::Hub::linkAdded_(link);
  }
  void linkDeleted_(Ztc::Link *link, Ztc::LinkState::T state) {
    switch (state) {
      case Ztc::LinkState::Down: Ztc::Hub::linkDownDec_(); break;
      case Ztc::LinkState::Up: Ztc::Hub::linkUpDec_(); break;
      default: Ztc::Hub::linkTransientDec_(); break;
    }
    Ztc::Hub::linkDeleted_(link);
  }

public:
  void linkState(
      Ztc::LinkState::T oldState, Ztc::LinkState::T newState) {
    switch (oldState) {
      case Ztc::LinkState::Down: Ztc::Hub::linkDownDec_(); break;
      case Ztc::LinkState::Up: Ztc::Hub::linkUpDec_(); break;
      default: Ztc::Hub::linkTransientDec_(); break;
    }
    switch (newState) {
      case Ztc::LinkState::Down: Ztc::Hub::linkDownInc_(); break;
      case Ztc::LinkState::Up: Ztc::Hub::linkUpInc_(); break;
      default: Ztc::Hub::linkTransientInc_(); break;
    }
  }

protected:
  template <typename Params, typename L>
  bool init_(Params params, Zutcp::Vantage::T vantage, L &&l, Stack *native = nullptr) {
    bool ok = HubCtl::lock(
	ZmEngineState::Stopped,
	[this, params = ZuMv(params), vantage, native, l = ZuFwd<L>(l)]() mutable -> bool {
      m_errorFn = ZuMv(params.errorFn());
      if (!m_errorFn) m_errorFn = defaultErrorFn();
      if (!validateParams(params)) return false;
      auto mx = params.mx();
      auto stack = native ? native : dynamic_cast<Stack *>(mx->plugin());
      if (stack && !native) {
	ZtString<> error;
	if (!stack->configure({
	      .controlBufCount = params.controlBufCount(),
	      .txControlCredits = params.txControlCredits(),
	      .rxRetainFrames = params.rxRetainFrames(),
	      .rxRetainFramesPerCxn = params.rxRetainFramesPerCxn(),
	      .rxReserveFrames = params.rxReserveFrames(),
	      .maxData = params.maxData(),
	      .maxCxns = params.maxCxns(),
	      .maxPendingCxns = params.maxPendingCxns(),
	      .closedTupleRingSize = params.closedTupleRingSize(),
	      .tfoCookieCacheSize = params.tfoCookieCacheSize(),
	      .connectTimeout = params.connectTimeout(),
	      .userTimeout = params.userTimeout(),
	      .closeDrainTimeout = params.closeDrainTimeout(),
	      .finWaitTimeout = params.finWaitTimeout()}, error)) {
	  reportError(ZeEXCEPT(Error, "Zutcp", error));
	  return false;
	}
      }
      m_mx = mx;
      m_stack = stack;
      m_dataTxAllocator = stack ? stack->dataTxAllocator() : nullptr;
      m_rxThread = m_mx->rxThread();
      m_txThread = m_mx->txThread();
      m_qlogParams = params.qlogParams();
      m_maxData = params.maxData();
      m_sackPermitted = params.sackPermitted();
      m_windowScale = params.windowScale();
      m_timestamps = params.timestamps();
      m_maxCxns = params.maxCxns();
      m_maxPendingCxns = params.maxPendingCxns();
      m_closedTupleRingSize = params.closedTupleRingSize();
      m_tfoCookieCacheSize = params.tfoCookieCacheSize();
      m_connectTimeout = params.connectTimeout();
      m_userTimeout = params.userTimeout();
      m_closeDrainTimeout = params.closeDrainTimeout();
      m_finWaitTimeout = params.finWaitTimeout();
      m_maxIdleTimeout = params.maxIdleTimeout();
      m_heartBeat = params.heartBeat();
      m_maxPayload = params.maxPayload();
      m_controlBufCount = params.controlBufCount();
      m_txControlCredits = params.txControlCredits();
      m_rtoParams = params.rtoParams();
      m_tlpPeerAckDelay = params.tlpPeerAckDelay();
      m_rxRetainFrames = params.rxRetainFrames();
      m_rxRetainPerCxn = params.rxRetainFramesPerCxn();
      m_rxReserveFrames = params.rxReserveFrames();
      m_ecn = params.ecn();
      if (m_qlogParams.enabled() &&
	  !ZutcpLogger::init(m_qlogTrace, m_qlogParams, vantage))
	return false;
      return l(params);
    });
    if (ok) {
      Ztc::HubMgr::add(this);
      m_registered = true;
    }
    return ok;
  }

  unsigned allLinks_(Ztc::Hub::AllLinksFn) const { return 0; }
  unsigned allPools_(Ztc::Hub::AllPoolsFn) const { return 0; }

  void reportError(ZeException e) {
    if (m_errorFn)
      m_errorFn(ZuMv(e));
    else
      ZiLogEvent(ZuMv(e));
  }



  void start_() {
    startQLog([this]() {
      this->started(true);
    });
  }

  void stop_() {		// hub callback - enter Rx thread
    rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - enter Tx thread
    txRun([this]() { stop_1(); });
  }

  void stop_1() {		// Tx thread - stop qlog
    stopQLog([this]() { stop_2(true); });
  }

  void stop_2(bool ok) {	// qlog callback - complete stop
    this->stopped(ok);
  }

  template <typename L>
  bool spawn(L &&l) {
    if (!mx() || !mx()->running()) return false;
    rxRun(ZuFwd<L>(l));
    return true;
  }

  void wake() {
    if (!mx() || !mx()->running()) return;
    rxRun([this]() { this->stopped(); });
  }

  template <typename L>
  void startQLog(L &&l) {
    if (!m_qlogParams.enabled()) { l(); return; }
    ZutcpLogger::start([this, l = ZuFwd<L>(l)](bool) mutable {
      rxRun(ZuMv(l));
    });
  }

  template <typename L>
  void stopQLog(L &&l) {
    if (!m_qlogParams.enabled()) { l(); return; }
    ZutcpLogger::close(m_qlogTrace, [this, l = ZuFwd<L>(l)]() mutable {
      ZutcpLogger::stopIdle([this, l = ZuMv(l)](bool) mutable {
	rxRun(ZuMv(l));
      });
    });
  }

  template <typename Link>
  void disconnected(Link *, bool) { }
public:
  // Fast Open carries application-authorized, replay-safe work only.
  template <typename Link> bool earlyData(Link *) { return false; }
  template <typename Link> uint32_t maxEarlyData(Link *) { return 0; }
  template <typename Link> bool allowEarlyStream(Link *, uint64_t, bool) { return false; }
  template <typename Link> void earlyDataAccepted(Link *) { }
  template <typename Link> void earlyDataRejected(Link *) { }

protected:

  template <typename Link>
  void retireLinkRoutes(Link *) { }








private:
  template <typename Params>
  bool validateParams(const Params &params) {
    if (ZuUnlikely(!params.mx())) {
      reportError(ZeEXCEPT(Error, "Zutcp", "multiplexer is null"));
      return false;
    }
    if (!params.rtoParams().valid()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid RTO bounds or granularity"));
      return false;
    }
    if (!positiveTimeout(params.userTimeout()) && params.userTimeout() != ZuTime{0}) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid user timeout"));
      return false;
    }
    if (!positiveTimeout(params.closeDrainTimeout()) ||
	!positiveTimeout(params.finWaitTimeout())) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid close timeout"));
      return false;
    }
    if (!params.tfoCookieCacheSize()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid Fast Open cookie cache size"));
      return false;
    }
    if (!params.closedTupleRingSize()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid closed tuple ring size"));
      return false;
    }
    if (!positiveTimeout(params.connectTimeout())) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid connect timeout"));
      return false;
    }
    if (!params.maxCxns() || !params.maxPendingCxns() ||
	params.maxPendingCxns() > params.maxCxns()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid connection admission limits"));
      return false;
    }
    if (params.tlpPeerAckDelay() < ZuTime{0}) {
      reportError(ZeEXCEPT(Error, "Zutcp", "negative peer ACK allowance"));
      return false;
    }
    if (!params.controlBufCount() || !params.txControlCredits()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "control buffer count and credits must be positive"));
      return false;
    }
    if (!params.rxRetainFrames() || !params.rxRetainFramesPerCxn() ||
	!params.rxReserveFrames()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "Rx retention limits and reserve must be positive"));
      return false;
    }
    unsigned rxThread = params.mx()->rxThread();
    unsigned txThread = params.mx()->txThread();
    if (!rxThread || !txThread || rxThread == txThread ||
	rxThread > params.mx()->params().nThreads() ||
	txThread > params.mx()->params().nThreads()) {
      reportError(ZeEXCEPT(Error, "Zutcp", "invalid multiplexer Rx/Tx owners"));
      return false;
    }
    if (!params.maxPayload() || params.maxPayload() > BufSize) {
      reportError(ZeEXCEPT(Error, "Zutcp",
	([maxPayload = params.maxPayload()](auto &s) {
	s << "invalid maxPayload " << maxPayload << "; packet buffer size " << BufSize;
      })));
      return false;
    }
    return true;
  }




  // immutable after init()
  const ZuID		m_id;
  ZiMultiplex		*m_mx = nullptr;
  Zi::IOBufAllocator	*m_dataTxAllocator = nullptr;
  Stack		*m_stack = nullptr;
  unsigned		m_rxThread = 0;
  unsigned		m_txThread = 0;
  ErrorFn		m_errorFn;
  ZutcpLogParams	m_qlogParams;
  ZutcpLogger::Trace	m_qlogTrace;
  uint64_t		m_maxData = DefaultMaxData;
  unsigned		m_maxCxns = DefaultMaxCxns, m_maxPendingCxns = DefaultMaxPendingCxns;
  unsigned		m_closedTupleRingSize = DefaultClosedTupleRingSize;
  unsigned		m_tfoCookieCacheSize = DefaultTFOCookieCacheSize;
  ZuTime		m_connectTimeout{DefaultConnectTimeout};
  ZuTime		m_userTimeout{DefaultUserTimeout};
  ZuTime		m_closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime		m_finWaitTimeout{DefaultFinWaitTimeout};
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxPayload = DefaultMaxPayload;
  unsigned		m_controlBufCount = DefaultControlBufCount;
  unsigned		m_txControlCredits = DefaultTxControlCredits;
  RtoParams		m_rtoParams;
  ZuTime		m_tlpPeerAckDelay{0, 200000000};
  unsigned		m_rxRetainFrames = DefaultRxRetainFrames;
  unsigned		m_rxRetainPerCxn = DefaultRxRetainPerCxn;
  unsigned		m_rxReserveFrames = DefaultRxReserveFrames;
  bool			m_sackPermitted = true, m_windowScale = true, m_timestamps = true;
  bool			m_ecn = false;
  bool			m_registered = false;

};

// CRTP - the concrete client connection owns application byte processing:
#if 0
struct App : public Zutcp::Client<App> { class Cxn; };
class App::Cxn : public ZmNHeap<Cxn>, public Zutcp::CliCxn<App, Cxn> {
friend inline ZuStringT<"App.Cxn"> ZmNHeapID(Cxn *);
public:
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(App *);
  // Rx: consume bytes from reader, then retireRx(the exact consumed length).
  int process(Zutcp::Reader &reader);
  void connected(Zutcp::Connected);
  void disconnected(bool peer);
  void connectFailed(bool transient);
  unsigned reconnFreq() const; // optional
};
#endif
template <typename App_> class Client : public Hub<App_> {
public:
  using App = App_;
  using Base = Hub<App>;

  bool init(ClientParams params) {
    return this->init_(ZuMv(params), Zutcp::Vantage::Client,
      [](const ClientParams &) { return true; });
  }

  void final() {
    Base::final();
  }

  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
};

// CRTP - accepted connections own their reader, writer and direction state:
#if 0
class AppCxn;
struct App : public Zutcp::Server<App, AppCxn> {
  ZmRef<AppCxn> accepted(const Zutcp::InitialInfo &);
};
class AppCxn : public ZmNHeap<AppCxn>, public Zutcp::SrvCxn<App, AppCxn> {
friend inline ZuStringT<"App.Cxn"> ZmNHeapID(AppCxn *);
public:
  using ZmNHeap<AppCxn>::operator new;
  using ZmNHeap<AppCxn>::operator delete;
  AppCxn(App *);
  // Rx: consume bytes, then retire their credit. Tx: writer_()/send_().
  int process(Zutcp::Reader &reader);
  void connected(Zutcp::Connected);
  void disconnected(bool peer);
};
#endif
template <typename App_, typename Link_> class Server :
  public Hub<App_>,
  public Endpoint_<Server<App_, Link_>> {
public:
  using App = App_;
  using Link = Link_;
  using LinkRef = ZmRef<Link>;
  using LinkTable = ServerLinks;
  using LinkRefs =
    ZtArray<LinkRef, ZtArrayHeapID<"Zutcp.Server.LinkRefs">>;
  using Base = Hub<App>;
  using Endpoint = Endpoint_<Server>;
  using Base::app;
  using Base::allLinks;
  enum { EndpointRef = false };

template <typename, typename, typename>
friend class SrvCxn;
friend ZmEngine<App>;

  ~Server() = default;

  bool init(ServerParams params) {
    return this->init_(ZuMv(params), Zutcp::Vantage::Server,
      [](const ServerParams &) { return true; });
  }

  void final() {
    m_links = nullptr;
    m_routes.clear();
    Base::final();
  }

  bool listening() const { return Endpoint::listening(); }
  bool connected() const { return Endpoint::connected(); }
  const ZiSockAddr &local() const { return Endpoint::local(); }
  template <typename L>
  void endpointDiag(L &&l) const {
    auto mx = this->mx();
    if (!mx || mx->invoked(mx->txThread())) {
      EndpointDiag diag = Endpoint::diag();
      l(diag);
      return;
    }
    auto server = const_cast<Server *>(this);
    mx->txRun([server, l = ZuFwd<L>(l)]() mutable {
      EndpointDiag diag = server->Endpoint::diag();
      l(diag);
    });
  }

  template <typename L, typename Done>
  void allLinks(L &&l, Done &&done) {
    ZiAssert(this->mx() && this->rxThread(), "Zutcp", (),
      "TCP server link iteration before app initialization", return);
    if (this->rxInvoked()) {
      if (this->state() == ZmEngineState::Running)
	allLinks_0([&l](LinkRef &ref) { l(ZuMv(ref)); });
      done();
      return;
    }
    this->rxInvoke([this, l = ZuFwd<L>(l), done = ZuFwd<Done>(done)]() mutable {
      allLinks(ZuMv(l), ZuMv(done));
    });
  }

  unsigned allLinks_(Ztc::Hub::AllLinksFn fn) const {
    unsigned n = 0;
    allLinks_0([&fn, &n](LinkRef &ref) {
      ++n;
      fn(ref.ptr());
    });
    return n;
  }

  ZiIP localIP() const { return ZiIP{}; }
  uint16_t localPort() const { return 0; }
  bool sendPkt(const ZmRef<ZiIOBuf> &) { return true; }
  bool sendFrame(const SentFrameRef &) { return true; }

  void listenFailed(bool transient) {
    this->reportError(ZeEXCEPT(Error, "Zutcp",
      ([transient](auto &s) {
	s << "TCP server listen failed transient=" << transient;
      })));
  }
  LinkRef accepted(const InitialInfo &) {
    this->reportError(ZeEXCEPT(Error, "Zutcp",
      "TCP server App must provide accepted(const InitialInfo &)"));
    return {};
  }

  ZmRef<PacketBuf> allocTxPkt_(bool control = false) {
    return Endpoint::allocTxPkt(control);
  }

  bool sendPktRaw(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn, bool priority, const IPPath &path) {
    return Endpoint::send(ZuMv(buf), ZuMv(addr), ecn, priority, &path);
  }

  void refreshLinkRoutes(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      m_routes.add(link->closedKeyRx(), link);
  }

public:
  void endpointDatagram(Datagram d) {
    if (this->rxInvoked()) { received_(ZuMv(d)); return; }
    this->rxRun([this, d = ZuMv(d)]() mutable { received_(ZuMv(d)); });
  }
  void endpointReady(Endpoint *) {
    this->rxRun([this]() {
      switch (this->state()) {
	case ZmEngineState::Starting:
	case ZmEngineState::StopPending:
	  this->started(true);
	  break;
	default:
	  break;
      }
      this->app()->listening();
    });
  }
  void endpointFailed(bool transient) {
    this->rxRun([this, transient]() {
      failed_0(transient);
    });
  }
  void endpointDown_(Endpoint *) { }
private:
  template <typename L>
  void allLinks_0(L &&l) const {
    ZmRef<LinkTable> table = m_links;
    if (!table) return;
    unsigned n = table->count_();
    if (!n) return;
    auto links = ZmScratch(LinkRef, n, typename LinkRefs::VHeap);
    {
      auto i = table->citer();
      while (auto node = i())
	links.push(LinkRef{static_cast<Link *>(node)});
    }
    links.all(ZuFwd<L>(l));
  }

  void failed_0(bool transient) {
    switch (this->state()) {
      case ZmEngineState::Starting:
      case ZmEngineState::StopPending:
	this->started(false);
	return;
      default:
	break;
    }
    this->app()->listenFailed(transient);
  }

  void received_(Datagram d) {
    if (this->state() != ZmEngineState::Running || !d.buf) return;
    ClosedTupleKey tuple{d.local, d.addr};
    if (auto link = m_routes.find(tuple)) {
      deliver(link, ZuMv(d));
      return;
    }
    auto stack = this->nativeStack();
    if (stack->recoverClosed(d)) return;
    if ((d.tcp.flags & 0x17) != 0x02) {
      stack->resetUnmatched(d);
      return;
    }
    prepareSYN(ZuMv(d));
  }

  void deliver(Link *link, Datagram d) {
    if (!link) return;
    if (!link->acceptsLiveTCP(d) && this->nativeStack()->recoverClosed(d)) return;
    (void)link->receivedRouted(ZuMv(d));
  }

  bool tupleClaimed(const ClosedTupleKey &tuple, bool includePending = true) const {
    if (includePending && m_pending->findPtr(tuple)) return true;
    auto links = m_links->citer();
    while (auto node = links())
      if (static_cast<Link *>(node)->closedKeyRx() == tuple) return true;
    return false;
  }

  bool qualifyReuse(const Datagram &d, Zutcp_::PendingInitial &pending) {
    auto stack = this->nativeStack();
    ClosedTuple current;
    if (!stack->closedTuple(pending.tuple, current)) return true;
    if (!reusePeerTuple(current, d.tcp, this->timestamps(), this->transportNow())) return false;
    if (!pending.evidenceID) {
      if (!stack->chooseISN(d.local, d.addr, pending.localISN)) return false;
      pending.evidenceID = current.id;
    }
    return reuseSequence(pending.localISN, uint32_t(current.txNext - 1)) &&
      reusePeerTuple(current, d.tcp, this->timestamps(), this->transportNow());
  }

  void prepareSYN(Datagram d) {
    auto stack = this->nativeStack();
    ClosedTupleKey tuple{d.local, d.addr};
    if (tupleClaimed(tuple)) return;
    ClosedTuple current;
    if (stack->closedTuple(tuple, current) &&
	!reusePeerTuple(current, d.tcp, this->timestamps(), this->transportNow())) return;
    auto slot = stack->reserveCxn();
    if (!slot) { stack->resetUnmatched(d); return; }
    Zutcp_::PendingInitial pending{ZuMv(slot), tuple};
    if (!qualifyReuse(d, pending)) {
      stack->releaseCxn(pending.slot);
      return;
    }
    m_pending->add(tuple, ZuMv(pending));
    // All header/options are already copied; ordinary SYN needs no packet
    // ownership while cold route work runs. Positive early DATA stays in place.
    if (!d.buf->length) d.buf = nullptr;
    IPPath path{d.local.ip(), d.addr.ip(), {}, {}, 0};
    unsigned generation = Endpoint::generation();
    if (!stack->resolvePath(path,
	[this, stack, d = ZuMv(d), tuple, generation](
	    IPPath resolved, ZtString<> error) mutable {
	  auto node = m_pending->findPtr(tuple);
	  ZmAssert_(node);
	  auto &pending = node->val();
	  CxnSlot slot = ZuMv(pending.slot);
	  if (this->state() == ZmEngineState::Running &&
	      generation == Endpoint::generation() && !error &&
	      tcpPayload(resolved.mtu, resolved.local.type()) &&
	      !tupleClaimed(tuple, false) && qualifyReuse(d, pending)) {
	    InitialInfo info{d.tcp, d.addr, d.buf ? d.buf->length : 0, resolved, d.local};
	    if (auto link = acceptInitial(info, slot, pending)) {
	      link->initIPPath(resolved);
	      deliver(link, ZuMv(d));
	      if (m_links->findPtr(link) && !link->abortingRx() &&
		  link->closedKeyRx() == tuple)
		m_routes.add(tuple, link);
	    }
	  }
	  // Keep the tuple claim through factory reentrancy and first delivery.
	  stack->releaseCxn(slot);
	  m_pending->del(tuple);
	  if (m_waitSetup && !m_stopCount && !m_pending->count_()) {
	    m_waitSetup = false;
	    stop_1();
	  }
	})) {
      stack->releaseCxn(m_pending->findPtr(tuple)->val().slot);
      m_pending->del(tuple);
      Endpoint::failure();
    }
  }

  Link *acceptInitial(
    const InitialInfo &info, CxnSlot &slot, const Zutcp_::PendingInitial &pending)
  {
    LinkRef link;
    link = this->app()->accepted(info);
    if (!link || link->hasCxnSlotRx()) return nullptr;
    if (pending.evidenceID &&
	!link->stageReuseISNRx(pending.localISN, pending.evidenceID)) {
      link->discardUnadoptedRx();
      return nullptr;
    }
    ZutcpLOG(app()->qlogTrace(), ([local = info.local, remote = info.peer](
      auto &o, ZuTime time) {
      LinkInfo linkInfo{.vantage = Vantage::Server, .local = local, .remote = remote};
      o.logCxnStarted(
	CxnStartedEvt{.local = local, .remote = remote, .linkInfo = linkInfo}, time);
    }));
    Link *ptr = link.ptr();
    if (!addLink(ZuMv(link))) {
      link->discardUnadoptedRx();
      return nullptr;
    }
    ptr->adoptCxnSlotRx(ZuMv(slot));
    return ptr;
  }

  bool addLink(LinkRef &&link) {
    if (!link) return false;
    if (this->state() != ZmEngineState::Running) return false;
    Link *ptr = link.ptr();
    if (m_links->findPtr(ptr)) return true;
    m_links->addNode(ZmRef<LinkTable::Node>{ZuMv(link)});
    return true;
  }

public:
  void disconnected(Link *link, bool) {
    LinkRef hold{link};
    m_routes.retire(link->closedKeyRx(), link);
    m_links->del(link);
    if (!link->deferCxnRelease()) link->releaseCxnSlotRx();
    // The engine can stop before Rx has counted the remaining links.
    if (m_stopCount && !--m_stopCount) stop_1();
  }

  void retireLinkRoutes(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      m_routes.retire(link->closedKeyRx(), link);
  }

private:
  void start_() {
    if (!m_links)
      m_links = new LinkTable{
	ZmHashParams().bits(5).loadFactor(1).cBits(3)};
    m_routes.init();
    m_stopCount = 0;
    Base::startQLog([this]() { start_1(); });
  }

  void start_1() {
    ZiIP localIP = this->app()->localIP();
    uint16_t localPort = this->app()->localPort();
    if (!Endpoint::init(this->mx()) ||
	!Endpoint::open(PathMode::ServerUnconnected, localIP, localPort)) {
      Base::stopQLog([this]() { this->started(false); });
    }
  }

  void stop_() {		// hub callback - enter Rx thread
    Base::rxRun([this]() { stop_0(); });
  }

  void stop_0() {		// Rx thread - disconnect links
    m_stopCount = 0;
    {
      auto i = m_links->citer();
      while (auto node = i()) {
	LinkRef ref{static_cast<Link *>(node)};
	if (this->nativeTx()) {
	  auto link = ref.ptr();
	  link->abortLocalRx(0, [link] { link->tcpResetAbort(false); });
	} else ref->disconnect();
	++m_stopCount;
      }
    }
    m_routes.clear();
    if (!m_stopCount) stop_1();
  }

  void stop_1() {		// Rx thread - clean links / drain endpoint
    if (m_pending->count_()) { m_waitSetup = true; return; }
    m_links->clean();
    Base::txRun([this]() {
      Endpoint::disconnect([this]() {
	Base::rxRun([this]() { Base::stop_0(); });
      });
    });
  }

  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  TupleRouter<Link>	m_routes;
  using Pending = ZmHashKV<ClosedTupleKey, Zutcp_::PendingInitial, ZmHashHeapID<"Zutcp.Pending">>;
  ZmRef<Pending>	m_pending = new Pending;
  unsigned		m_stopCount = 0;
  bool			m_waitSetup = false;
};

} // namespace Zutcp

#include <zlib/ZutcpConnection.hh>
#include <zlib/ZutcpCliCxn.hh>
#include <zlib/ZutcpSrvCxn.hh>

#endif /* Zutcp_HH */
