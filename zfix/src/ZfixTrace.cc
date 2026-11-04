//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuBox.hh>
#include <zlib/ZuFmt.hh>
#include <zlib/ZfixTrace.hh>
#include <zlib/ZfixIOBuf.hh>
#include <zlib/ZfixSession.hh>

namespace Zfix {

template <typename F> using SessionWire = Fmt::WireField<Fmt::tag<F>(), Fmt::payloadTag<F>()>;

TraceField traceBuiltin(ZuCSpan type, int32_t tag)
{
  switch (tag) {
    case 8: case 9: case 35: case 10:
    case 553: case 554: case 925: return {.known = true};
  }
  using Fields = TraceFields<ZuTypeMap<SessionWire, SessionFields<Fmt::FrameView>>>;
  auto standard = Fields::index.findVal(tag);
  TraceField f = standard ? *standard : TraceField{};
  if (type.length() == 1) switch (type[0]) {
    case 'A':
      f.merge(traceField<Session::Logon>(tag));
      if (tag == 95) f.secret = true;
      break;
    case '0': f.merge(traceField<Session::HeartBt>(tag)); break;
    case '1': f.merge(traceField<Session::TestReq>(tag)); break;
    case '2': f.merge(traceField<Session::ResendReq>(tag)); break;
    case '3': f.merge(traceField<Session::Reject>(tag)); break;
    case '4': f.merge(traceField<Session::SeqReset>(tag)); break;
    case '5': f.merge(traceField<Session::Logout>(tag)); break;
  }
  return f;
}

static bool secret(int32_t tag)
{
  switch (tag) {
    case 553: case 554: case 925: // user, password, new password
      return true;
    default: return false;
  }
}
static void escaped(Fmt::Sink &out, ZuCSpan value)
{
  for (auto c : value) {
    unsigned v = uint8_t(c);
    if (v >= 32 && v < 127 && c != '\\' && c != '"') out << c;
    else {
      out << "\\x" << ZuBoxed(v).hex<true, ZuFmt::Right<2, '0'>>();
    }
    if (!out.available()) break;
  }
}

TraceRef WireTrace::capture_(ZuCSpan wire, TxContext context, Resolve resolve, void *owner)
{
  return capture_(wire, nullptr, nullptr, {}, context, resolve, owner);
}
TraceRef WireTrace::capture_(
  const Fmt::FrameView &frame, TxContext context, Resolve resolve, void *owner)
{
  return capture_(frame.wire, &frame, nullptr, {}, context, resolve, owner);
}
TraceRef WireTrace::capture_(const TxImage &image, ZuCSpan type,
  TxContext context, Resolve resolve, void *owner)
{
  return capture_({}, nullptr, &image, type, context, resolve, owner);
}
TraceRef WireTrace::capture_(ZuCSpan wire, const Fmt::FrameView *known,
  const TxImage *image, ZuCSpan imageType,
  TxContext context, Resolve resolve, void *owner)
{
  if (!enabled_()) return {};
  using Alloc = Zi::IOBufAlloc<TraceQueue::Node,
    ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.TraceBuf">>;
  uint64_t charge = uint64_t(ZmHeapAllocSize<sizeof(Alloc)>{}) +
    (m_limits.recordBytes > Alloc::Size ? jumboBytes(m_limits.recordBytes) : 0);
  if (m_limits.recordBytes > Alloc::MaxSize || charge > m_limits.bytes) {
    ++m_stats.dropped; return {};
  }
  while (m_stats.bytes > m_limits.bytes - charge) { pop_(); ++m_stats.rotated; }
  ZmRef<TraceQueue::Node> node = new Alloc{nullptr, 0};
  ZiIOBuf *buffer = node.ptr();
  buffer->alloc(m_limits.recordBytes);
  Fmt::FrameResult parsed;
  if (!image && !known) {
    Fmt::Limits limits; limits.frameBytes = wire.length(); limits.fieldBytes = wire.length();
    parsed = Fmt::frame(wire, limits);
    if (parsed.state == Fmt::FrameState::Complete && parsed.consumed == wire.length())
      known = &parsed.view;
  }
  Fmt::Limits outputLimits; outputLimits.frameBytes = m_limits.recordBytes;
  Fmt::Sink out{{buffer->data(), m_limits.recordBytes}, outputLimits};
  // The sink's logical frame bound also follows this explicit excerpt budget.
  bool truncated = false;
  if (!image && !known)
    out << "<invalid frame>";
  else {
    auto type = image ? imageType : known->msgType;
    bool logon = type == "A";
    bool stopped = false;
    unsigned rendered = 0;
    unsigned total = image ? image->length() : wire.length();
    auto render = [&](ZuCSpan wire) {
      if (stopped) return;
      unsigned offset = 0;
      unsigned length = wire.length();
      while (offset < length && out.available() && !out.failed()) {
	Fmt::Token token;
	if (Fmt::prefix(wire, offset, token)) { out << "<malformed>"; stopped = true; break; }
	auto field = traceBuiltin(type, token.tag);
	if (resolve) field.merge(resolve(owner, type, token.tag));
	if (!field.known || field.ambiguous) { out << "<opaque>"; stopped = true; break; }
	Fmt::Limits limits;
	// Only content can contain declared pairs. Never lend a binary count
	// the outer checksum or the next coalesced frame.
	auto bounded = field.reader && !image ?
	ZuCSpan{wire.data(), uint64_t(known->content.data() - wire.data()) +
	  known->content.length()} : wire;
	limits.fieldBytes = limits.frameBytes = bounded.length();
	Fmt::Error error;
	unsigned value = token.offset;
	unsigned consumed = field.reader ? field.reader(bounded, token, limits, error) :
	Fmt::consume(bounded, token, limits, error);
	if (!consumed) { out << "<malformed>"; stopped = true; break; }
	out << token.tag << '=';
	bool sensitive = field.secret || secret(token.tag);
	if (logon) switch (token.tag) {
	case 8: case 9: case 35: case 10: break;
	default: sensitive = true; break;
	}
	if (sensitive) out << "<redacted>";
	else {
	if (field.reader) {
	  escaped(out, {wire.data() + value, token.countLength});
	  out << "\\x01" << token.valueTag << '=';
	}
	escaped(out, {wire.data() + token.offset, token.length});
	}
	out << "\\x01";
	if (sensitive) { out << "<opaque>"; stopped = true; break; }
	offset = value + consumed;
	if (!out.available() && (offset < length || rendered + offset < total)) truncated = true;
      }
      rendered += offset;
    };
    if (image) image->each(render);
    else render(wire);
  }
  truncated |= out.failed();
  buffer->length = out.length();
  auto &record = node->data();
  record.context = context; record.charge = charge;
  record.wireBytes = image ? image->length() : wire.length(); record.truncated = truncated;
  m_queue.pushNode(node);
  ++m_stats.records; m_stats.bytes += charge;
  if (truncated) ++m_stats.truncated;
  return node;
}
TraceRef WireTrace::pop_()
{
  auto node = m_queue.shift();
  if (node) m_stats.bytes -= node->data().charge;
  return node;
}
void WireTrace::clear_() { while (m_queue.count_()) pop_(); }
} // Zfix

