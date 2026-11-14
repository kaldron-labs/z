//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z QUIC transport API

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
#include <zlib/ZutcpMx.hh>
#include <zlib/ZutcpTypes.hh>
#include <zlib/ZutcpBuf.hh>
#include <zlib/ZutcpPacket.hh>
#include <zlib/ZutcpFrame.hh>
#include <zlib/ZutcpPQueue.hh>
#include <zlib/ZutcpConnectionUtil.hh>
#include <zlib/ZutcpSched.hh>
#include <zlib/ZutcpTransport.hh>
#include <zlib/ZutcpLog.hh>
#include <zlib/ZutcpRecovery.hh>
#include <zlib/ZutcpPath.hh>
#include <zlib/ZutcpSock.hh>
#include <zlib/ZutcpDiag.hh>

namespace Zutcp {

// Public connection metadata
ZuDerive(LogMsg, ZtString<ZtStringHeapID<"Zutcp.Log">>);
ZuDerive(ParamStrings,
  (ZtArray<ParamString, ZtArrayHeapID<"Zutcp.ParamStrings">>));

struct InitialInfo {
  LongHdr	header;
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

inline bool sentFrameRef(
  ZuBSpan bytes, SentFrameRef &ref, bool &ackEliciting)
{
  ref = {};
  ackEliciting = false;
  if (!bytes) return true;
  Frame frame;
  unsigned used = 0;
  if (FrameCodec::parse(bytes, frame, used) < 0 || !used) return false;
  ackEliciting = FrameCodec::ackEliciting(frame.type);
  if (!ackEliciting) return true;
  switch (frame.type) {
  case FrameType::Data:
    ref.kind = SentFrameKind::Data;
    ref.offset = frame.offset;
    ref.length = frame.length;
    ref.fin = frame.fin;
    ref.range = TxRange{
      nullptr, 0, uint32_t(frame.length), frame.offset};
    return true;
  case FrameType::MaxData:
    ref = SentFrameRef::flowUpdate({FrameType::MaxData, frame.value});
    break;
  case FrameType::DataBlocked:
    ref = SentFrameRef::blocked(frame.value);
    break;
  default:
    ref = SentFrameRef::control();
    break;
  }
  return true;
}

struct VersionNeg {
  static bool supported(uint32_t version) { return version == Version1; }
  static uint32_t preferred() { return Version1; }

  static uint32_t choose(const uint32_t *versions, unsigned nVersions) {
    for (unsigned i = 0; i < nVersions; ++i)
      if (supported(versions[i])) return versions[i];
    return 0;
  }

  static int write(
    uint8_t *out, unsigned len, const CxnID &dcid,
    const CxnID &scid) {
    uint32_t versions[] = { Version1 };
    return Pkt::writeVerNeg(out, len, dcid, scid, versions, 1);
  }

  static int parse(
    ZuBSpan in, uint32_t *versions, unsigned capacity, unsigned &nVersions) {
    return Pkt::parseVerNeg(in, versions, capacity, nVersions);
  }
};

struct ServerPktDecision {
  ServerPktAction::T	action = ServerPktAction::Drop;
  LongHdr		header;
  unsigned		responseLength = 0;
};

struct ZutcpAPI ServerPkt {
  static ServerPktDecision routeLongHdr(
    ZuBSpan, uint8_t *response, unsigned responseLen);
};



} // namespace Zutcp

#include <zlib/Zutcp_.hh>

namespace Zutcp {

using Zutcp_::Cxn;
using Zutcp_::CxnRouter;
using Zutcp_::Endpoint_;
using Zutcp_::ServerLinks;

// Connection bootstrap
struct ZutcpAPI CxnIDGen {
  static constexpr unsigned InitialLength = MinCIDLength;

  static bool random(CxnID &, unsigned length = InitialLength);
  static bool randomPair(
    CxnID &initialDCID, CxnID &initialSCID,
    unsigned dcidLength = InitialLength, unsigned scidLength = InitialLength);
};

class ZutcpAPI ClientBootstrap {
public:
  bool started() const { return m_started; }



  const CxnID &initialDCID() const { return m_initialDCID; }
  const CxnID &initialSCID() const { return m_initialSCID; }


  bool start(const CxnID &initialDCID, const CxnID &initialSCID) {
    if (initialDCID.length() < MinCIDLength ||
	initialSCID.length() < MinCIDLength)
      return false;
    m_initialDCID = initialDCID;
    m_initialSCID = initialSCID;
    m_started = true;
    return true;
  }
  bool startRandom(
    unsigned dcidLength = CxnIDGen::InitialLength,
    unsigned scidLength = CxnIDGen::InitialLength);





  bool validateServerParams(
    const TransportParams &params,
    const CxnID &serverInitialSCID) const {
    return m_started && params.origDCID == m_initialDCID &&
      params.initialSCID == serverInitialSCID;
  }

private:
  CxnID		m_initialDCID;
  CxnID		m_initialSCID;
  bool		m_started = false;
};

class ZutcpAPI ServerBootstrap {
public:
  bool accepted() const { return m_accepted; }
  const CxnID &initialDCID() const { return m_initialDCID; }
  const CxnID &origDCID() const { return m_origDCID; }
  const CxnID &clientInitialSCID() const { return m_clientInitialSCID; }
  const CxnID &localInitialSCID() const { return m_localInitialSCID; }

  bool acceptInitial(const LongHdr &, unsigned datagramLength);
  bool transportParams(TransportParams &) const;

private:
  CxnID		m_initialDCID;
  CxnID		m_origDCID;
  CxnID		m_clientInitialSCID;
  CxnID		m_localInitialSCID;
  bool		m_accepted = false;
};







// Hub configuration
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

  HubParams &&maxData(uint64_t v) { m_maxData = v; return ZuMv(*this); }
  HubParams &&maxCxns(unsigned v) { m_maxCxns = v; return ZuMv(*this); }
  HubParams &&maxPendingCxns(unsigned v) { m_maxPendingCxns = v; return ZuMv(*this); }
  HubParams &&closedTupleRingSize(unsigned v) { m_closedTupleRingSize = v; return ZuMv(*this); }
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
  HubParams &&ecn(bool v) { m_ecn = v; return ZuMv(*this); }
  HubParams &&controlBufCount(unsigned v) { m_controlBufCount = v; return ZuMv(*this); }
  HubParams &&txControlCredits(unsigned v) { m_txControlCredits = v; return ZuMv(*this); }
  HubParams &&rxRetainFrames(unsigned v) { m_rxRetainFrames = v; return ZuMv(*this); }
  HubParams &&rxRetainFramesPerCxn(unsigned v) { m_rxRetainPerCxn = v; return ZuMv(*this); }
  HubParams &&rxReserveFrames(unsigned v) { m_rxReserveFrames = v; return ZuMv(*this); }











  HubParams &&errorFn(ErrorFn v) { m_errorFn = ZuMv(v); return ZuMv(*this); }

  ZiMultiplex *mx() const { return m_mx; }





  const ZutcpLogParams &qlogParams() const { return m_qlogParams; }

  uint64_t maxData() const { return m_maxData; }
  unsigned maxCxns() const { return m_maxCxns; }
  unsigned maxPendingCxns() const { return m_maxPendingCxns; }
  unsigned closedTupleRingSize() const { return m_closedTupleRingSize; }
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
	"invalid QUIC heartbeat interval");
    return v;
  }

  ZiMultiplex		*m_mx = nullptr;
  ZutcpLogParams	m_qlogParams;
  uint64_t		m_maxData = DefaultMaxData;
  unsigned		m_maxCxns = DefaultMaxCxns, m_maxPendingCxns = DefaultMaxPendingCxns;
  unsigned		m_closedTupleRingSize = DefaultClosedTupleRingSize;
  ZuTime		m_connectTimeout{DefaultConnectTimeout};
  ZuTime		m_userTimeout{DefaultUserTimeout};
  ZuTime		m_closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime		m_finWaitTimeout{DefaultFinWaitTimeout};
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxPayload = MinPktPayload;
  unsigned		m_controlBufCount = DefaultControlBufCount;
  unsigned		m_txControlCredits = DefaultTxControlCredits;
  RtoParams		m_rtoParams;
  unsigned		m_rxRetainFrames = DefaultRxRetainFrames;
  unsigned		m_rxRetainPerCxn = DefaultRxRetainPerCxn;
  unsigned		m_rxReserveFrames = DefaultRxReserveFrames;
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

  Hub() : m_id{ZuID{} << "quic:" <<
    ZuBoxPtr(this).hex<false, ZuFmt::Alt<>>()} { }

  bool init(HubParams params) {
    return init_(ZuMv(params), Zutcp::Vantage::Unknown,
      [](const HubParams &) { return true; });
  }
  bool start() override { return HubCtl::start(); }
  bool stop() override { return HubCtl::stop(); }

  TelKey telKey() const override {
    return {Ztc::LinkType::QUIC, m_id};
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
    data.linkType = Ztc::LinkType::QUIC;
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
      m_maxCxns = DefaultMaxCxns;
      m_maxPendingCxns = DefaultMaxPendingCxns;
      m_closedTupleRingSize = DefaultClosedTupleRingSize;
      m_connectTimeout = ZuTime{DefaultConnectTimeout};
      m_userTimeout = ZuTime{DefaultUserTimeout};
      m_closeDrainTimeout = ZuTime{DefaultCloseDrainTimeout};
      m_finWaitTimeout = ZuTime{DefaultFinWaitTimeout};
      m_maxIdleTimeout = 0;
      m_heartBeat = {};
      m_maxPayload = MinPktPayload;
      m_controlBufCount = DefaultControlBufCount;
      m_txControlCredits = DefaultTxControlCredits;
      m_rtoParams = {};
      m_rxRetainFrames = DefaultRxRetainFrames;
      m_rxRetainPerCxn = DefaultRxRetainPerCxn;
      m_rxReserveFrames = DefaultRxReserveFrames;
      m_stack = nullptr;
      m_ecn = false;
      return true;
    });
    ZiAssert(ok, "Zutcp", (),
      "QUIC hub finalization while not stopped", return);
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
  uint64_t maxData() const { return m_maxData; }
  unsigned maxCxns() const { return m_maxCxns; }
  unsigned maxPendingCxns() const { return m_maxPendingCxns; }
  unsigned closedTupleRingSize() const { return m_closedTupleRingSize; }
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
  void linkState_(
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
      if (!validate_(params)) return false;
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
	      .connectTimeout = params.connectTimeout(),
	      .userTimeout = params.userTimeout(),
	      .closeDrainTimeout = params.closeDrainTimeout(),
	      .finWaitTimeout = params.finWaitTimeout()}, error)) {
	  error_(ZeEXCEPT(Error, "Zutcp", error));
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
      m_maxCxns = params.maxCxns();
      m_maxPendingCxns = params.maxPendingCxns();
      m_closedTupleRingSize = params.closedTupleRingSize();
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

  void error_(ZeException e) {
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
  template <typename Link>
  void retireLinkRoutes(Link *) { }








private:
  template <typename Params>
  bool validate_(const Params &params) {
    if (ZuUnlikely(!params.mx())) {
      error_(ZeEXCEPT(Error, "Zutcp", "multiplexer is null"));
      return false;
    }
    if (!params.rtoParams().valid()) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid RTO bounds or granularity"));
      return false;
    }
    if (!positiveTimeout(params.userTimeout()) && params.userTimeout() != ZuTime{0}) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid user timeout"));
      return false;
    }
    if (!positiveTimeout(params.closeDrainTimeout()) ||
	!positiveTimeout(params.finWaitTimeout())) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid close timeout"));
      return false;
    }
    if (!params.closedTupleRingSize()) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid closed tuple ring size"));
      return false;
    }
    if (!positiveTimeout(params.connectTimeout())) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid connect timeout"));
      return false;
    }
    if (!params.maxCxns() || !params.maxPendingCxns() ||
	params.maxPendingCxns() > params.maxCxns()) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid connection admission limits"));
      return false;
    }
    if (!params.controlBufCount() || !params.txControlCredits()) {
      error_(ZeEXCEPT(Error, "Zutcp", "control buffer count and credits must be positive"));
      return false;
    }
    if (!params.rxRetainFrames() || !params.rxRetainFramesPerCxn() ||
	!params.rxReserveFrames()) {
      error_(ZeEXCEPT(Error, "Zutcp", "Rx retention limits and reserve must be positive"));
      return false;
    }
    unsigned rxThread = params.mx()->rxThread();
    unsigned txThread = params.mx()->txThread();
    if (!rxThread || !txThread || rxThread == txThread ||
	rxThread > params.mx()->params().nThreads() ||
	txThread > params.mx()->params().nThreads()) {
      error_(ZeEXCEPT(Error, "Zutcp", "invalid multiplexer Rx/Tx owners"));
      return false;
    }
    if (params.maxPayload() > BufSize) {
      error_(ZeEXCEPT(Error, "Zutcp",
	([maxPayload = params.maxPayload()](auto &s) {
	s << "maxPayload " << maxPayload << " exceeds packet buffer size " << BufSize;
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
  ZuTime		m_connectTimeout{DefaultConnectTimeout};
  ZuTime		m_userTimeout{DefaultUserTimeout};
  ZuTime		m_closeDrainTimeout{DefaultCloseDrainTimeout};
  ZuTime		m_finWaitTimeout{DefaultFinWaitTimeout};
  uint64_t		m_maxIdleTimeout = 0;
  ZuTime		m_heartBeat;
  unsigned		m_maxPayload = MinPktPayload;
  unsigned		m_controlBufCount = DefaultControlBufCount;
  unsigned		m_txControlCredits = DefaultTxControlCredits;
  RtoParams		m_rtoParams;
  unsigned		m_rxRetainFrames = DefaultRxRetainFrames;
  unsigned		m_rxRetainPerCxn = DefaultRxRetainPerCxn;
  unsigned		m_rxReserveFrames = DefaultRxReserveFrames;
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
  static constexpr unsigned PNLength = 2;

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
  static constexpr bool EndpointRef = false;
  static constexpr unsigned PNLength = Client<App>::PNLength;

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
    m_routes.final();
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
      "QUIC server link iteration before app initialization", return);
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
    this->error_(ZeEXCEPT(Error, "Zutcp",
      ([transient](auto &s) {
	s << "QUIC server listen failed transient=" << transient;
      })));
  }
  LinkRef accepted(const InitialInfo &) {
    this->error_(ZeEXCEPT(Error, "Zutcp",
      "QUIC server App must provide accepted(const InitialInfo &)"));
    return {};
  }

  ZmRef<PacketBuf> allocTxPkt_(bool control = false) {
    return Endpoint::allocTxPkt(control);
  }

  bool sendPkt_(ZmRef<ZiIOBuf> buf, ZiSockAddr addr, const IPPath &path) {
    if (!this->app()->sendPkt(buf)) return true;
    return Endpoint::send(ZuMv(buf), ZuMv(addr), EcnMark::NotECT, false, &path);
  }
  bool sendPktRaw(
      ZmRef<ZiIOBuf> buf, ZiSockAddr addr,
      EcnMark::T ecn, bool priority, const IPPath &path) {
    return Endpoint::send(ZuMv(buf), ZuMv(addr), ecn, priority, &path);
  }

  void refreshLinkRoutes(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      link->installRoutes_(m_routes);
  }

public:
  void endpointDatagram(Datagram d) {
    this->rxRun([this, d = ZuMv(d)]() mutable {
      received_(ZuMv(d));
    });
  }
  void endpointReady_(Endpoint *) {
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
    if (this->state() != ZmEngineState::Running || !d.buf || !d.buf->length) return;
    CxnID routedDCID;
    Link *link = nullptr;
    auto packet = d.buf->cspan();
    if (Pkt::isLong(packet)) {
      LongHdr header;
      if (Pkt::parseLong(packet, header) < 0) { Endpoint::failure(); return; }
      link = m_routes.find(header.dcid);
      if (!link) {
        if (header.type == PktType::Initial || !VersionNeg::supported(header.version))
          prepareInitial(ZuMv(d), header);
        return;
      }
    } else link = routeShort(d, packet, &routedDCID);
    deliver_(link, ZuMv(d), routedDCID);
  }

  void deliver_(Link *link, Datagram d, const CxnID &routedDCID = {}) {
    if (!link) { Endpoint::failure(); return; }
    if (link->receivedRouted(ZuMv(d), routedDCID)) link->installRoutes_(m_routes);
    else if (this->nativeTx())
      link->abortLocalRx_(0, [link] { link->tcpResetAbort_(false); });
    else link->disconnect();
  }

  void prepareInitial(Datagram d, const LongHdr &header) {
    if (m_pending->findPtr(header.dcid)) return;
    auto stack = this->nativeStack();
    if (header.type == PktType::Initial && VersionNeg::supported(header.version) &&
	(d.tcp.flags & 0x17) != 0x02) return;
    ClosedTuple record;
    if (header.type == PktType::Initial && VersionNeg::supported(header.version) &&
	stack->closedTuple({d.local, d.addr}, record)) return;
    auto slot = stack->reserveCxn();
    if (!slot) return;
    m_pending->add(header.dcid, ZuMv(slot));
    IPPath path{d.local.ip(), d.addr.ip(), {}, {}, 0};
    unsigned generation = Endpoint::generation();
    if (!stack->resolvePath(path,
	[this, stack, d = ZuMv(d), header, generation](
	    IPPath resolved, ZtString<> error) mutable {
	  auto pending = m_pending->findPtr(header.dcid);
	  ZmAssert_(pending);
	  CxnSlot slot = ZuMv(pending->val());
          m_pending->del(header.dcid);
          if (this->state() == ZmEngineState::Running &&
              generation == Endpoint::generation() && !error &&
              tcpPayload(resolved.mtu, resolved.local.type()) >= MinPktPayload) {
            if (!VersionNeg::supported(header.version))
	      this->txRun([this, header, addr = d.addr, path = resolved, generation]() mutable {
		if (generation == Endpoint::generation()) sendVerNeg(header, ZuMv(addr), path);
	      });
            else {
	      ClosedTuple retained;
	      if (!stack->closedTuple({d.local, d.addr}, retained)) {
		InitialInfo info{header, d.addr, d.buf->length, resolved, d.local};
		if (auto link = accept_(info, slot)) {
		  link->initIPPath(resolved);
		  deliver_(link, ZuMv(d));
		}
              }
            }
          }
	  stack->releaseCxn(slot);
          if (m_waitSetup && !m_stopCount && !m_pending->count_()) {
            m_waitSetup = false;
            stop_1();
          }
        })) {
      stack->releaseCxn(m_pending->findPtr(header.dcid)->val());
      m_pending->del(header.dcid);
      Endpoint::failure();
    }
  }

  Link *routeShort(
      const Datagram &, ZuBSpan packet, CxnID *routedDCID) {
    return m_routes.matchShort(packet, routedDCID);
  }



  Link *accept_(const InitialInfo &info, CxnSlot &slot) {
    LinkRef link;
    link = this->app()->accepted(info);
    if (!link || link->hasCxnSlotRx_()) return nullptr;
    ZutcpLOG(app()->qlogTrace(), ([
      local = local(),
      remote = info.peer,
      origDCID = info.header.dcid,
      dcid = info.header.dcid,
      scid = info.header.scid
    ](auto &o, ZuTime time) {
      Zutcp::LinkInfo linkInfo{
	.vantage = Zutcp::Vantage::Server,
	.origDCID = origDCID,
	.groupID = origDCID,
	.dcid = dcid,
	.scid = scid
      };
      o.logCxnStarted(
	CxnStartedEvt{.local = local, .remote = remote, .linkInfo = linkInfo},
	time);
    }));
    Link *ptr = link.ptr();
    if (!addLink(ZuMv(link))) return nullptr;
    ptr->adoptCxnSlotRx_(ZuMv(slot));
    return ptr;
  }

  bool addLink(LinkRef link) {
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
    m_links->del(link);
    if (!link->deferCxnRelease()) link->releaseCxnSlotRx_();
    // The engine can stop before Rx has counted the remaining links.
    if (m_stopCount && !--m_stopCount) stop_1();
  }

  void retireLinkRoutes(Link *link) {
    if (this->state() == ZmEngineState::Running && link)
      link->retireRoutes_(m_routes);
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
	  link->abortLocalRx_(0, [link] { link->tcpResetAbort_(false); });
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

  bool sendVerNeg(const LongHdr &h, ZiSockAddr addr, const IPPath &path) {
    ZiAssert(this->txInvoked(), "Zutcp", (), "version negotiation outside Tx", return false);
    ZmRef<ZiIOBuf> buf = allocTxPkt_(true);
    if (ZuUnlikely(!buf)) return false;
    int n = VersionNeg::write(
      buf->data(), txPktCapacity(buf), h.scid, h.dcid);
    if (n < 0) return false;
    buf->length = unsigned(n);
    bool sent = sendPkt_(ZuMv(buf), ZuMv(addr), path);
    if (sent) {
      ZutcpLOG(app()->qlogTrace(), ([
	version = h.version
      ](auto &o, ZuTime time) {
	VersionEvt event;
	new (event.serverVersions.push()) uint32_t(Version1);
	new (event.clientVersions.push()) uint32_t(version);
	o.logVersionInfo(event, time);
      }));
    }
    return sent;
  }









  // Rx thread exclusive
  alignas(Zm::CacheLineSize)
  ZmRef<LinkTable>	m_links = new LinkTable{
    ZmHashParams().bits(5).loadFactor(1).cBits(3)};
  CxnRouter<Link>	m_routes;
  using Pending = ZmHashKV<CxnID, CxnSlot, ZmHashHeapID<"Zutcp.Pending">>;
  ZmRef<Pending>	m_pending = new Pending;
  unsigned		m_stopCount = 0;
  bool			m_waitSetup = false;
};

} // namespace Zutcp

#include <zlib/ZutcpConnection.hh>
#include <zlib/ZutcpCliCxn.hh>
#include <zlib/ZutcpSrvCxn.hh>

#endif /* Zutcp_HH */
