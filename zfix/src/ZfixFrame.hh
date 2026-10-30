//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Standalone FIX envelope values and framing boundary

#ifndef ZfixFrame_HH
#define ZfixFrame_HH

#ifndef ZfixFmtLib_HH
#include <zlib/ZfixFmtLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZtBuiltin.hh>
#include <zlib/ZfixFmt.hh>

namespace Zfix::Fmt {

// Borrowed values: backing storage belongs to the standalone caller, or to
// the enclosing retained receive context in managed sessions.
struct Header {
  ZuCSpan	beginString;
  ZuCSpan	msgType;
  ZuCSpan	sender;
  ZuCSpan	target;
  ZuCSpan	senderSub;
  ZuCSpan	targetSub;
  ZuCSpan	senderLocation;
  ZuCSpan	targetLocation;
  ZuCSpan	appVerID;
  uint64_t	seqNo = 0;
  ZuDateTime	sendingTime;
  ZuDateTime	origSendingTime;
  bool		possDup = false;
  ZuBSpan	secureData;
  ZuCSpan	encoding;
};

struct Trailer {
  unsigned	checksum = 0;
  ZuBSpan	signature;
};

ZfStruct(ZfixFmtAPI, (Header, FIX),
  (sender, (FIX::ID<49>, Required),		String),
  (target, (FIX::ID<56>, Required),		String),
  (senderSub, (FIX::ID<50>),			String),
  (targetSub, (FIX::ID<57>),			String),
  (senderLocation, (FIX::ID<142>),		String),
  (targetLocation, (FIX::ID<143>),		String),
  (appVerID, (FIX::ID<1128>),		String),
  (seqNo, (FIX::ID<34>, Required),		UInt64),
  (possDup, (FIX::ID<43>),			Bool),
  (sendingTime, (FIX::ID<52>, Required),		DateTime),
  (origSendingTime, (FIX::ID<122>),		DateTime),
  (secureData, (FIX::ID<91>, FIX::Len<90>),	Bytes),
  (encoding, (FIX::ID<347>),			String));
ZfStruct(ZfixFmtAPI, (Trailer, FIX),
  (signature, (FIX::ID<89>, FIX::Len<93>),	Bytes));

template <typename Facet> struct Schema<Header, Facet> :
  Schema_<Header, Facet, ZuActiveFields<Header, Facet>, true> {
  enum { Prefix = true };
};

template <typename Facet> struct Schema<Trailer, Facet> :
  Schema_<Trailer, Facet, ZuActiveFields<Trailer, Facet>, true> { };

// One effective scope uses the ordinary metadata and scanner for header, body
// and trailer. The original body schema must independently reject reserved IDs.
template <typename O> struct Wire_ { };

template <typename O, typename Facet> struct Schema<Wire_<O>, Facet> :
  Schema_<Wire_<O>, Facet, ZuTypeConcat<
    typename Schema<Header, ZuFacet::FIX>::Fields,
    typename Schema<O, Facet>::Fields,
    typename Schema<Trailer, ZuFacet::FIX>::Fields>, true> {
  enum {
    HeaderN = Schema<Header, ZuFacet::FIX>::Leaves::N,
    BodyN = Schema<O, Facet>::Leaves::N
  };

  static unsigned region(unsigned field) {
    if (field < HeaderN) return 0;
    if (field < HeaderN + BodyN) return 1;
    return 2;
  }
};

namespace FrameState {

  enum { Incomplete, Complete, Error };
}

struct FrameView {
  ZuCSpan	wire;
  ZuCSpan	beginString;
  ZuCSpan	msgType;
  // Bytes after MsgType and before CheckSum; includes remaining header fields.
  ZuCSpan	content;
  Trailer	trailer;
};

struct FrameResult {
  unsigned	state = FrameState::Incomplete;
  unsigned	consumed = 0;
  unsigned	expected = 0;
  FrameView	view;
  Error		error;
};

// Stateless incremental inspection: append to caller-owned bounded storage and
// retry on Incomplete. Complete consumes exactly one frame, permitting a caller
// to continue over coalesced input. No bytes are retained or allocated here.
ZfixFmtExtern FrameResult frame(ZuCSpan input, Limits limits = {});

// Header-only inspection stops at the first non-header tag. It does not guess
// unknown body/binary metadata, and is suitable for unsupported MsgType errors.
ZfixFmtExtern Error readHeader(const FrameView &, Header &, Limits = {});
ZfixFmtExtern Error readHeader_(const FrameView &, Header &,
  bool (*bodyContains)(unsigned), Limits);
template <typename O, typename Facet = ZuFacet::FIX>
Error readHeader(const FrameView &frame, Header &header, Limits limits = {}) {
  // A known body scope distinguishes legal unknown header fields from the
  // first recognized body/trailer field; no business values are constructed.
  return readHeader_(frame, header, [](unsigned tag) {
    return scopeContains<O, Facet>(tag) || scopeContains<Trailer, ZuFacet::FIX>(tag);
  }, limits);
}
// Shared partial-header load for the typed whole-message and prefix paths.
ZfixFmtExtern Error loadHeader_(ZuCSpan, const Positions &, Header &, Error);

// Construct only from a successful frame() result. All backing storage remains
// caller-owned. Groups and binary pairs are scanned once across the shared
// scope, including recognized fields that an operation does not assign.
template <typename O, typename Facet = ZuFacet::FIX> class MessageHandler {
  ZuAssert((Format<O, Facet>::Kind == Kind::Object));
  using S = Schema<Wire_<O>, Facet>;
public:
  explicit MessageHandler(const FrameView &frame, Limits limits = {}) :
    m_input{frame.content}, m_offset{frame.content.data() ?
      unsigned(frame.content.data() - frame.wire.data()) : 0}
  {
    m_header.beginString = frame.beginString;
    m_header.msgType = frame.msgType;
    m_trailer.checksum = frame.trailer.checksum;
    m_positions.length(S::Leaves::N, false);
    for (unsigned i = 0; i < S::Leaves::N; ++i) m_positions[i].offset = UINT_MAX;
    if (frame.wire.length() > limits.frameBytes ||
	S::Leaves::N > limits.parseBytes/sizeof(Position)) {
      m_error = {ErrorCode::Size}; return;
    }
    Parse p{m_input, limits, m_positions, m_error};
    unsigned offset = 0;
    Scope scope{nullptr, scopeContains<Wire_<O>, Facet>, 0, 0};
    parseScope<Wire_<O>, Facet>(p, 0, offset, scope, false);
    m_error = loadHeader_(m_input, m_positions, m_header, m_error);
    if (m_error) return;
    ObjectView<Trailer, ZuFacet::FIX> trailer{m_input, m_positions,
      S::HeaderN + S::BodyN, m_error};
    trailer.load(m_trailer);
    m_trailer.checksum = frame.trailer.checksum;
  }

  MessageHandler(const MessageHandler &) = delete;
  MessageHandler &operator =(const MessageHandler &) = delete;

  bool valid() const { return !m_error; }

  Error error() const {
    auto error = m_error;
    if (error && error.code != ErrorCode::Required) error.offset += m_offset;
    return error;
  }

  const Header &header() const { return m_header; }
  const Trailer &trailer() const { return m_trailer; }

  template <typename F> ZuCSpan raw() { return view().template raw<F>(); }

  template <typename ...Args> O ctor(Args &&...args) {
    return view().ctor(ZuFwd<Args>(args)...);
  }

  template <typename ...Args> O *alloc(Args &&...args) {
    if (m_error) return nullptr;
    return view().alloc(ZuFwd<Args>(args)...);
  }

  template <typename ...Args> bool new_(void *ptr, Args &&...args) {
    if (m_error) return false;
    return view().new_(ptr, ZuFwd<Args>(args)...);
  }

  bool load(O &o) { return !m_error && view().load(o); }
  bool update(O &o) { return !m_error && view().update(o); }

private:
  auto view() { return ObjectView<O, Facet>{m_input, m_positions, S::HeaderN, m_error}; }

  using Slots = ZtBuiltin<Positions, S::Leaves::N>;
  ZuCSpan	m_input;
  unsigned	m_offset;
  Slots		m_positions;
  Header	m_header;
  Trailer	m_trailer;
  Error		m_error;
};

// Incremental progress without input ownership. Input may move as its caller
// grows storage, but prior bytes must remain unchanged and length must grow.
// Reset after consuming a complete frame or abandoning an errored stream.
class ZfixFmtAPI Framer {
public:
  explicit Framer(Limits limits = {}) : m_limits{limits} { }

  const Limits &limits() const { return m_limits; }

  FrameResult scan(ZuCSpan input);
  void reset();

private:
  FrameResult result(FrameResult r);
  FrameResult pending() const;
  const Limits	m_limits;
  Error		m_error;
  uint64_t	m_seen = 0;
  unsigned	m_search = 0;
  unsigned	m_valueOffset = 0;
  unsigned	m_fields = 0;
  unsigned	m_expected = 0;
  bool		m_complete = false;
};

struct FrameOutput {
  ZuCSpan	data;
  Error		error;

  explicit operator bool() const { return !error; }
};

// Standalone storage only. Publish data only after finish() succeeds. Encoding
// occurs once in reserved envelope headroom. finish() returns the authoritative
// output subspan, which can start after storage.data(); body bytes never move.
// Both fixed and growing storage append the checksum over original bytes.
class ZfixFmtAPI FrameWriter {
public:
  using Grow = Sink::Grow;
  FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Limits limits = {});
  FrameWriter(ZuSpan<char> storage, ZuCSpan beginString, Grow grow, void *owner,
    Limits limits = {});
  FrameWriter(const FrameWriter &) = delete;
  FrameWriter &operator =(const FrameWriter &) = delete;

  Sink &body() { return m_body; }

  FrameOutput finish();

private:
  struct Layout {
    unsigned	prefix;
    unsigned	body;
  };

  static Layout layout(unsigned capacity, unsigned beginLength, Limits);
  FrameWriter(ZuSpan<char>, ZuCSpan, Limits, Layout);
  static ZuSpan<char> growBody(void *, unsigned used, unsigned needed);

  ZuSpan<char>	m_storage;
  ZuCSpan	m_beginString;
  Limits	m_limits;
  unsigned	m_prefix;
  Sink		m_body;
  Grow		m_grow = nullptr;
  void		*m_owner = nullptr;
  Error		m_error;
  bool		m_finished = false;
};

ZfixFmtExtern Error writeHeader_(Sink &, const Header &);
ZfixFmtExtern Error writeHeader(Sink &, const Header &);
ZfixFmtExtern Error writeTrailer(Sink &, const Trailer &);
template <typename Facet> Error writeHeader(Sink &out, const Header &h) {
  if (auto error = writeHeader_(out, h)) return error;
  using Sending = ZfField(Header, sendingTime);
  out.field(52);
  saveScalar<Facet, Sending::Type::Code, typename Sending::Props>(out, h.sendingTime);
  out.end();
  if (h.possDup) {
    using Original = ZfField(Header, origSendingTime);
    out.field(122);
    saveScalar<Facet, Original::Type::Code, typename Original::Props>(out, h.origSendingTime);
    out.end();
  }
  return out.error();
}
template <typename Facet = ZuFacet::FIX,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename HeaderFacet = Facet, typename O>
FrameOutput writeFrame(ZuSpan<char> storage, const Header &header, const O &o,
    Limits limits = {}, const Trailer &trailer = {}) {
  FrameWriter writer{storage, header.beginString, limits};
  if (writer.body().failed()) return writer.finish();
  if (auto error = writeHeader<HeaderFacet>(writer.body(), header)) return {{}, error};
  if (!save<Facet, Filter>(writer.body(), o)) return {{}, writer.body().error()};
  if (auto error = writeTrailer(writer.body(), trailer)) return {{}, error};
  return writer.finish();
}
} // Zfix::Fmt

#endif /* ZfixFrame_HH */
