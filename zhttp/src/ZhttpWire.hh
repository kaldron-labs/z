//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library
// - HTTP/1.1, HTTP/2, and HTTP/3
// - caller is responsible for body decompression (if required)

#ifndef ZhttpWire_HH
#define ZhttpWire_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/Zhttp.hh>

#include <string.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZuSeq.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTuple.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiTxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpH1.hh>
#include <zlib/ZhttpH2.hh>
#include <zlib/ZhttpHPack.hh>
#include <zlib/ZhttpH3.hh>
#include "ZhttpNative.hh"

namespace Zhttp {// Low-level protocol Parser CRTP contract for the H1/H2/H3 aliases below.
// The role facades wrap plain application Parser sinks in these
// adapters; application sinks do not derive from them. The protocol invokes
// only the callbacks applicable to the selected request/response role and
// version. Inherited defaults are side-effect-safe. All callbacks are
// synchronous and Rx-shard-affine. Target and received spans are borrowed for
// the duration of the callback. `ProtocolParser` in the API sketch denotes
// the selected alias below.
#if 0
struct ParserAdapter : public ProtocolParser<ParserAdapter, Headers> {
  using Base = ProtocolParser<ParserAdapter, Headers>;
  using State = Base::State;

  // identical to app-facing Parser member functions (see above):
  // - enable1xx, operation, status, header, bodyInfo, bool body(Rx &)
  // complete takes the protocol parser state rather than a success boolean;
  // it is called once per parsed message
  void complete(State::T);

  // H3 stream/QPACK integration
  bool rxComplete() const;		// defaults to finReceived()
  bool finReceived() const;
  const H3::Params &h3Params() const;
  uint64_t streamID() const;
  bool qpackDecoderWrite(ZuBSpan);
  H3::QPackRxTable *qpackRx();
  bool qpackTxInsn(H3::QPackInsn::T, uint64_t);
  bool qpackTxMaxCapacity(uint64_t);
  bool qpackTxBlocked(uint64_t);
};
#endif

// Application message callback contract
//
// Header callbacks precede the first body callback.  Body callbacks receive a
// populated queue-backed ZiRxStream-compatible object on the owning Rx shard.
// Each callback is a synchronous prompt to consume complete application
// frames.  An incomplete trailing frame remains queued for the next prompt;
// the application must not retain the stream reference outside the callback.
//
// Body completion follows successful HTTP framing.  The protocol issues the
// last data prompt before the separate terminal result, then discards any
// unread decoded bytes.  No message callback is made after that result.
//
// Application Tx producers receive a concrete ZiTxStream-compatible body
// stream on the owning Tx shard.  The producer writes entity bytes only;
// libZhttp owns framing and final end-of-stream mapping.

template <
  typename Profile,
  typename Traits = Zhttp::NativeTraits<Profile>>
struct MessageTraits;

template <int> struct HttpTraits;

template <> struct HttpTraits<Version::H1> {
  enum {
    ID = Version::H1,
    OneMessagePerLink = false,
    CloseDelimited = true
  };

  template <typename Impl>
  using RequestParser = H1::Parser<Impl, true>;
  template <typename Impl>
  using ResponseParser = H1::Parser<Impl, false>;
  template <typename Impl, bool HasBody, bool Chunked>
  using Request =
    H1::Request<Impl, HasBody, Chunked>;
  template <typename Impl, bool HasBody, bool Chunked>
  using Response =
    H1::Response<Impl, HasBody, Chunked>;
};

template <> struct HttpTraits<Version::H2> {
  enum {
    ID = Version::H2,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl>
  using RequestParser = H2::Parser<Impl, true>;
  template <typename Impl>
  using ResponseParser = H2::Parser<Impl, false>;
  template <typename Impl, bool HasBody, bool Streaming>
  using Request =
    H2::Request<Impl, HasBody, Streaming>;
  template <typename Impl, bool HasBody, bool Streaming>
  using Response =
    H2::Response<Impl, HasBody, Streaming>;
};

template <> struct HttpTraits<Version::H3> {
  enum {
    ID = Version::H3,
    OneMessagePerLink = true,
    CloseDelimited = false
  };

  template <typename Impl>
  using RequestParser = H3::Parser<Impl, true>;
  template <typename Impl>
  using ResponseParser = H3::Parser<Impl, false>;
  template <typename Impl, bool HasBody, bool Streaming>
  using Request =
    H3::Request<Impl, HasBody, Streaming>;
  template <typename Impl, bool HasBody, bool Streaming>
  using Response =
    H3::Response<Impl, HasBody, Streaming>;
};

template <typename Profile, typename Traits>
struct MessageTraits :
  public HttpTraits<Traits::HTTPVersion> {
  using Transport = Traits::Transport;
  enum { Multiplexed = Traits::Multiplexed };
};

struct HeaderSeed {
  H3::QPackTxString	name;
  H3::QPackTxString	value;
  uint32_t		size = 0;
  bool			exact = false;
};

using HeaderSeeds =
  ZtArray<HeaderSeed, ZtArrayHeapID<"Zhttp.HeaderSeeds">>;

// Immutable declaration inputs are shared with dispatch and encoding.
class HeaderSeedCatalog {
public:
  void clear() { m_entries.length(0); }
  void final() { m_entries.init(); }
  ZhttpAPI void add(const CatalogView &, const H3::Params &, uint32_t);
  const HeaderSeeds &entries() const { return m_entries; }
  bool operator !() const { return !m_entries; }
  ZuOpBool

private:
  HeaderSeeds m_entries;
};

struct HPackSeed {
  H3::QPackTxString	name;
  H3::QPackTxString	value;
  H2::HPackBytes	incremental;
  H2::HPackBytes	fallback;
  uint32_t		size = 0;
};

using HPackSeeds =
  ZtArray<HPackSeed, ZtArrayHeapID<"Zhttp.HPackSeeds">>;

struct HPackSeedPlan {
  HPackSeeds	entries;
};

using HPackSeedPlans =
  ZtArray<HPackSeedPlan, ZtArrayHeapID<"Zhttp.HPackSeedPlans">>;

class HPackSeedCatalog {
public:
  void clear() { m_plans.length(0); }
  void final() { m_plans.init(); }

  ZhttpAPI void add(const CatalogView &, uint32_t);

  const HPackSeedPlans &plans() const { return m_plans; }
  const HPackSeeds &entries() const {
    static const HPackSeeds empty;
    return m_plans ? m_plans[0].entries : empty;
  }

private:
  HPackSeedPlans	m_plans;
};

ZtEnumStruct(ZhttpAPI, QPackSeedState, uint8_t,
  Unseeded, Seeded, Disabled);

ZtEnumStruct(ZhttpAPI, QPackSeedResult, int8_t,
  Failed, Disabled, Seeded);

template <typename Write>
QPackSeedResult::T installQPackSeeds(
    H3::QPackTxTable &tx, const HeaderSeeds &seeds, Write &&write) {
  uint32_t capacity = tx.effectiveCapacity();
  if (!capacity || !seeds) {
    tx.freeze();
    return QPackSeedResult::Disabled;
  }
  uint64_t used = 0;
  unsigned count = 0, n = seeds.length();
  while (count < n) {
    if (seeds[count].size > capacity - used) break;
    used += seeds[count++].size;
  }
  if (!count) {
    tx.freeze();
    return QPackSeedResult::Disabled;
  }
  H3::HdrBytes bytes;
  H3::HdrBytes insn;
  if (H3::QPack::encodeSetCapacity(insn, capacity) < 0)
    return QPackSeedResult::Failed;
  Compression::putBytes(bytes, insn);
  for (unsigned i = 0; i < count; ++i) {
    insn.length(0);
    uint64_t nameIndex = 0;
    int n = H3::QPack::staticNameIndex(seeds[i].name, nameIndex) ?
      H3::QPack::encodeInsertWithNameRef(
	insn, nameIndex, false, seeds[i].value) :
      H3::QPack::encodeInsertLiteral(
	insn, {seeds[i].name, seeds[i].value});
    if (n < 0) return QPackSeedResult::Failed;
    Compression::putBytes(bytes, insn);
  }
  if (!ZuFwd<Write>(write)(bytes)) return QPackSeedResult::Failed;
  if (!tx.setCapacity(capacity)) return QPackSeedResult::Failed;
  tx.capacitySent = true;
  for (unsigned i = 0; i < count; ++i)
    if (!tx.insertView({seeds[i].name, seeds[i].value}))
      return QPackSeedResult::Failed;
  tx.freeze();
  return QPackSeedResult::Seeded;
}

template <typename App, typename = void>
struct AppHeaderSeeds {
  static const HeaderSeeds &get(const App &) {
    static const HeaderSeeds seeds;
    return seeds;
  }
};
template <typename App>
struct AppHeaderSeeds<App, decltype(
  ZuDeclVal<const App &>().qpackSeeds(), void())> {
  static const HeaderSeeds &get(const App &app) { return app.qpackSeeds(); }
};

template <typename App, typename = void>
struct AppHPackSeedPlans {
  static const HPackSeedPlans &get(const App &) {
    static const HPackSeedPlans plans;
    return plans;
  }
};
template <typename App>
struct AppHPackSeedPlans<App, decltype(
  ZuDeclVal<const App &>().hpackSeedPlans(), void())> {
  static const HPackSeedPlans &get(const App &app) {
    return app.hpackSeedPlans();
  }
};

struct RetainedBudget {
  bool add(uint64_t n) {
    if (n > max - size) {
      valid = false;
      return false;
    }
    size += n;
    return true;
  }

  uint64_t	max = uint64_t(-1);
  uint64_t	size = 0;
  bool		valid = true;
};

struct RetainedEntry {
  RetainedEntry() = default;
  RetainedEntry(ZmRef<ZiIOBuf> buf_, bool final_) :
    buf{ZuMv(buf_)}, final{final_} { }

  ZmRef<ZiIOBuf>	buf;
  bool		final;
};

template <typename Lower>
class RetainedTx : public ZiTxStream<RetainedTx<Lower>> {
  using Base = ZiTxStream<RetainedTx<Lower>>;
  using Entries =
    ZtArray<RetainedEntry, ZtArrayHeapID<"Zhttp.RetainedTx.Entries">>;

public:
  RetainedTx(Lower &lower_, RetainedBudget &budget_) :
    Base{lower_.maxSize(), lower_.headRoom(), lower_.tailRoom()},
    m_lower{lower_}, m_budget{budget_} { }

  ~RetainedTx() { this->flush(); }

  ZmRef<ZiIOBuf> allocBuf_(unsigned headRoom) {
    if (ZuUnlikely(m_lower.failed())) return nullptr;
    return m_lower.allocBuf_(headRoom);
  }
  bool sendBuf_(ZmRef<ZiIOBuf> buf, bool final) {
    if (!m_budget.add(buf->length)) {
      m_valid = false;
      return false;
    }
    new (m_entries.push()) RetainedEntry{ZuMv(buf), final};
    return true;
  }

  bool seal() { return this->flush() && valid(); }
  bool valid() const {
    return !this->failed() && m_valid && m_budget.valid;
  }
  bool commit() {
    if (!seal()) { m_entries.length(0); return false; }
    for (unsigned i = 0, n = m_entries.length(); i < n; ++i) {
      m_lower << ZuMv(m_entries[i].buf);
      if (m_entries[i].final) m_lower << Zi::Flush{};
      if (m_lower.failed()) {
	this->fail();
	m_entries.length(0);
	return false;
      }
    }
    m_entries.length(0);
    return true;
  }

private:
  Lower		&m_lower;
  RetainedBudget &m_budget;
  Entries	m_entries;
  bool		m_valid = true;
};

// Shared compile-time outbound message mechanics.  Ops keeps request/response
// accounting, failure, empty-body, and budget policy in the owning component.
template <typename Message, typename Ops>
class MessageTx {
public:
  MessageTx(Ops &ops) : m_ops{&ops} { }

  template <typename Builder>
  bool streaming(Builder &builder, bool optional) {
    auto &link = m_ops->link();
    auto tx = link.transmit_(builder);
    bool emitted = false;
    bool duplicate = false;
    WriteOutcome::T outcome = WriteOutcome::Failed;
    bool headersOK = false;
    uint64_t produced = 0;
    if (optional) {
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &outcome,
	&headersOK, &produced](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	if (!(headersOK = begin_(builder, tx))) return;
	m_ops->headers();
	auto body = builder.body(tx);
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	bool ok = body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!ok || !body.valid()) outcome = WriteOutcome::Failed;
	if (outcome == WriteOutcome::End) builder.finish(tx);
      });
      if (!emitted) return m_ops->empty(builder.appBuilder());
    } else {
      if (!(headersOK = begin_(builder, tx)))
	return m_ops->template fail<true>();
      m_ops->headers();
      builder.emitBody([
	this, &builder, &tx, &emitted, &duplicate, &outcome, &produced](
	    auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	auto body = builder.body(tx);
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
	bool ok = body.flush();
	produced = body.produced();
	m_ops->template produced<true>(produced);
	if (!ok || !body.valid()) outcome = WriteOutcome::Failed;
      });
      if (emitted && outcome == WriteOutcome::End) builder.finish(tx);
    }
    if (!validCardinality_(optional, emitted, duplicate) ||
	!writerEnded_(outcome) ||
	!headersOK || !tx.flush())
      return m_ops->template fail<true>();
    if (!m_ops->complete(produced)) return false;
    link.finish();
    return true;
  }

  template <typename Builder>
  bool fixed(Builder &builder, bool optional) {
    auto &link = m_ops->link();
    auto native = link.transmit_(builder);
    if constexpr (Message::ID == Version::H2)
      return fixedH2_(builder, native, optional);
    else {
      RetainedBudget budget{.max = m_ops->retainedMax()};
      RetainedTx headerTx{native, budget};
      RetainedTx bodyTx{native, budget};
      auto body = builder.body(bodyTx, m_ops->fixedBodyMax());
      bool emitted = false;
      bool duplicate = false;
      WriteOutcome::T outcome = WriteOutcome::Failed;
      builder.emitBody([
	&body, &emitted, &duplicate, &outcome](auto &&write) {
	if (emitted) { duplicate = true; return; }
	emitted = true;
	outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
      });
      bool bodyOK = body.flush();
      if (emitted) {
	builder.produced = body.produced();
	m_ops->template produced<false>(builder.produced);
      }
      if (!validCardinality_(optional, emitted, duplicate) ||
	  (emitted && (!bodyOK || !writerEnded_(outcome) || !body.valid())))
	return m_ops->template fail<false>();
      if (!emitted) return m_ops->empty(builder.appBuilder());
      if (!begin_(builder, headerTx)) return m_ops->template fail<false>();
      m_ops->headers();
      builder.finish(bodyTx);
      if (!headerTx.seal() || !bodyTx.seal())
	return m_ops->template fail<false>();
      if (!headerTx.commit() || !bodyTx.commit())
	return m_ops->template fail<false>();
      if (!m_ops->complete(builder.produced)) return false;
      link.finish();
      return true;
    }
  }

private:
  static bool validCardinality_(
      bool optional, bool emitted, bool duplicate) {
    ZiAssert(optional || emitted, "Zhttp", (),
      "required body emitted no writer", return false);
    ZiAssert(!duplicate, "Zhttp", (),
      "body emitted more than one writer", return false);
    return (optional || emitted) && !duplicate;
  }

  static bool writerEnded_(WriteOutcome::T outcome) {
    switch (outcome) {
      case WriteOutcome::End: return true;
      case WriteOutcome::Failed: return false;
      case WriteOutcome::Stream:
      case WriteOutcome::Abort:
	ZiAssert(false, "Zhttp", (),
	  "streaming outcome returned from a single-turn writer", return false);
	return false;
      default:
	ZiAssert(false, "Zhttp", (), "invalid body writer outcome", return false);
	return false;
    }
  }

  template <typename Builder, typename Tx>
  static bool begin_(Builder &builder, Tx &tx) {
    using R = decltype(builder.begin(tx));
    if constexpr (ZuIsSame<R, void>{})
      builder.begin(tx);
    else if (!builder.begin(tx))
      return false;
    return tx.flush();
  }

  template <typename Builder, typename Tx>
  bool fixedH2_(Builder &builder, Tx &tx, bool optional) {
    tx.defer(m_ops->retainedMax());
    auto body = builder.body(tx, m_ops->fixedBodyMax());
    bool emitted = false;
    bool duplicate = false;
    WriteOutcome::T outcome = WriteOutcome::Failed;
    builder.emitBody([
      &body, &emitted, &duplicate, &outcome](auto &&write) {
      if (emitted) { duplicate = true; return; }
      emitted = true;
      outcome = invokeBodyWriter(ZuFwd<decltype(write)>(write), body);
    });
    bool bodyOK = body.flush();
    if (emitted) {
      builder.produced = body.produced();
      m_ops->template produced<false>(builder.produced);
    }
    if (!validCardinality_(optional, emitted, duplicate) ||
	(emitted && (!bodyOK || !writerEnded_(outcome) || !body.valid())))
      return m_ops->template fail<false>();
    if (!emitted) return m_ops->empty(builder.appBuilder());
    if (!begin_(builder, tx))
      return m_ops->template fail<false>();
    m_ops->headers();
    builder.finish(tx);
    if (!tx.valid()) return m_ops->template fail<false>();
    if (!tx.commit()) return m_ops->template fail<false>();
    if (!m_ops->complete(builder.produced)) return false;
    m_ops->link().finish();
    return true;
  }

  Ops	*m_ops;
};

} // namespace Zhttp

#endif /* ZhttpWire_HH */
