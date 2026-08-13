//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z HTTP wire-neutral field dispatch and semantics

#ifndef ZhttpFields_HH
#define ZhttpFields_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <zlib/ZuMatcher.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/Zu_aton.hh>

#include <zlib/ZmAssert.hh>

#include <zlib/ZtEnum.hh>
#include <zlib/ZtArray.hh>

#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiRxStream.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>

namespace Zhttp {

ZtEnumNS(ZhttpAPI, FieldSection, int8_t, Informational, Final, Trailers);
namespace FieldSection { enum { Invalid = -1 }; }

class BodyRx {
public:
  using Queue = ZiRxQueue;
  using Stream = ZiRxStream<Queue>;
  using BufAlloc = Zi::IOBufAlloc<
    Queue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    ZuStringT<"Zhttp.Body.Rx">>;
  using WireBufAlloc = Zi::IOBufAlloc<
    Queue::Node, ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    ZuStringT<"Zhttp.Wire.Rx.Split">>;

  BodyRx(uint64_t max = uint64_t(-1)) : m_max{max} { }

  void reset() {
    m_rx.clean();
    m_received = m_consumed = 0;
  }
  void reset(uint64_t max) { m_max = max; reset(); }
  uint64_t discard() {
    uint64_t n = m_rx.length();
    m_rx.clean();
    m_consumed += n;
    return n;
  }

  Stream &rx() { return m_rx; }
  const Stream &rx() const { return m_rx; }
  uint64_t received() const { return m_received; }
  uint64_t consumed() const { return m_consumed; }
  uint64_t max() const { return m_max; }

  template <typename NodeRef, typename L>
  bool push(NodeRef &&node, L &&l) {
    unsigned n = node->length;
    if (ZuUnlikely(uint64_t(n) > m_max - m_received)) return false;
    if (!n) return true;
    m_received += n;
    m_rx.push(ZuFwd<NodeRef>(node));
    prompt(ZuFwd<L>(l));
    return true;
  }

  template <typename SrcQueue, typename Frame,
    typename SrcAlloc, typename DstAlloc, typename L,
    typename Transform = Zi::RxPass>
  int64_t splice(
    ZiRxStream<SrcQueue> &src, uint64_t length, Frame &&frame,
    SrcAlloc &&srcAlloc, DstAlloc &&dstAlloc,
    uint64_t headLen, uint64_t tailLen, L &&l,
    Transform &&transform = {})
  {
    if (ZuUnlikely(length > m_max - m_received)) return -1;
    int64_t n = src.splice(
      m_rx, ZuFwd<Frame>(frame),
      ZuFwd<SrcAlloc>(srcAlloc), ZuFwd<DstAlloc>(dstAlloc),
      headLen, tailLen, ZuFwd<Transform>(transform));
    if (n <= 0) return n;
    m_received += length;
    if (length) prompt(ZuFwd<L>(l));
    return n;
  }

  template <typename L>
  uint64_t prompt(L &&l) {
    uint64_t before = m_rx.length();
    ZuFwd<L>(l)(m_rx);
    uint64_t after = m_rx.length();
    ZmAssert(after <= before);
    uint64_t n = before - after;
    m_consumed += n;
    return n;
  }

private:
  Stream	m_rx;
  uint64_t	m_max;
  uint64_t	m_received = 0;
  uint64_t	m_consumed = 0;
};

template <typename Rx, typename L>
bool bodyEach(Rx &rx, L &&l) {
  while (rx) {
    int64_t n = rx.consume(
      [](ZuSpan<uint8_t> span) -> int64_t { return span.length(); },
      l);
    if (ZuUnlikely(n <= 0)) return false;
  }
  return true;
}

template <typename Rx>
bool bodyDrain(Rx &rx) {
  return bodyEach(rx, [](ZuSpan<uint8_t>) { });
}


namespace Fields {

using String = ZtBArray<ZtArrayHeapID<"Zhttp.Fields.String">>;

ZhttpAPI bool forbidden(ZuBSpan);
ZhttpAPI int pseudo(ZuBSpan);

template <typename Impl, typename = void>
struct HasRuntime : public ZuFalse { };
template <typename Impl>
struct HasRuntime<Impl,
  decltype(ZuDeclVal<Impl *>()->header(
    FieldSection::Final, ZuDeclVal<ZuBSpan>(),
    ZuDeclVal<ZuSpan<uint8_t>>()), void())> :
    public ZuTrue { };

template <
  typename Headers, typename Header, typename Static, typename Unknown>
void dispatch(
  ZuBSpan key, ZuSpan<uint8_t> value,
  Header &&header, Static &&static_, Unknown &&unknown) {
  using List = HeaderList<Headers>;
  using Keys = typename List::Keys;
  if constexpr (Keys::N) {
    static constexpr auto matcher = ZuMatcher<Keys>();
    auto i = matcher.exact(key);
    if (i < 0) {
      unknown(key, value);
      return;
    }
    ZuSwitch::dispatch<Keys::N>(
      i, [&value, &header, &static_](auto i) {
	using Key = ZuType<i, Keys>;
	using KeyValues = typename List::template Value<i>;
	if constexpr (KeyValues::N) {
	  static constexpr auto matcher = ZuMatcher<KeyValues>();
	  auto j = matcher.exact(value);
	  if (j >= 0) {
	    ZuSwitch::dispatch<KeyValues::N>(
	      j, [&static_](auto j) {
		static_(Key{}, ZuType<j, KeyValues>{});
	      });
	    return;
	  }
	}
	header(Key{}, value);
      });
  } else
    unknown(key, value);
}

inline bool nameOK(ZuBSpan name) {
  if (!name) return false;
  unsigned offset = name[0] == ':';
  if (offset == name.length()) return false;
  for (unsigned i = offset; i < name.length(); ++i) {
    int c = name[i];
    if (c >= 'A' && c <= 'Z') return false;
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) continue;
    switch (c) {
      case '!': case '#': case '$': case '%': case '&': case '\'':
      case '*': case '+': case '-': case '.': case '^': case '_':
      case '`': case '|': case '~':
	break;
      default:
	return false;
    }
  }
  return true;
}

inline bool uint64(ZuBSpan value, uint64_t &result) {
  if (!value) return false;
  uint64_t n = 0;
  for (unsigned i = 0; i < value.length(); ++i) {
    unsigned digit = unsigned(value[i] - '0');
    if (digit > 9 || n > (uint64_t(-1) - digit) / 10) return false;
    n = n * 10 + digit;
  }
  result = n;
  return true;
}

template <bool Request_>
class Semantics {
public:
  enum { Request = Request_ };

  void trailers(bool value) { m_trailers = value; }
  void requestMethod(Method::T value) { m_requestMethod = value; }
  void extendedConnect(bool value) { m_extendedConnect = value; }
  bool bodyAllowed() const { return !m_noBody; }

  template <typename Header>
  bool field(ZuBSpan name, ZuSpan<uint8_t> value, Header &&header) {
    if (!nameOK(name)) return false;
    if (name[0] == ':')
      return pseudo_(name, value, ZuFwd<Header>(header));
    m_regular = true;
    if (forbidden(name)) return false;
    if (m_trailers && name == "content-length") return false;
    if (name == "te" && value != "trailers") return false;
    if (name == "content-length") {
      uint64_t length;
      if (!uint64(value, length))
	return false;
      if (m_contentLength && length != m_length) return false;
      m_contentLength = true;
      m_length = length;
    }
    header(name, value);
    return true;
  }

  template <typename Operation, typename Status, typename Header>
  bool start(Operation &&operation, Status &&status, Header &&header) {
    if (m_trailers || m_started) return true;
    if (!valid_()) return false;
    if constexpr (Request) {
      Target target;
      auto e = Target::fromPseudo(
	target, m_method, m_scheme,
	m_authority.span(), m_path.span(), m_protocol.span());
      if (!e.ok()) return false;
      m_started = true;
      operation(m_method, target);
      header("host", m_authority.span());
    } else {
      m_started = true;
      status(m_status);
      m_noBody =
	m_requestMethod == Method::HEAD ||
	m_status < 200 || m_status == 204 || m_status == 304;
    }
    return true;
  }

  template <typename Operation, typename Status, typename Header>
  FieldSection::T finish(
      Operation &&operation, Status &&status, Header &&header) {
    if (m_trailers)
      return m_seen ? FieldSection::Invalid : FieldSection::Trailers;
    if (!valid_() ||
	!start(ZuFwd<Operation>(operation), ZuFwd<Status>(status),
	  ZuFwd<Header>(header)))
      return FieldSection::Invalid;
    if constexpr (Request)
      return FieldSection::Final;
    else
      return m_status < 200 ? FieldSection::Informational : FieldSection::Final;
  }

  FieldSection::T section() const {
    if (m_trailers) return FieldSection::Trailers;
    if constexpr (Request) return FieldSection::Final;
    return m_status < 200 ? FieldSection::Informational : FieldSection::Final;
  }

private:
  enum : uint8_t {
    MethodSeen = 1U<<0,
    PathSeen = 1U<<1,
    SchemeSeen = 1U<<2,
    AuthoritySeen = 1U<<3,
    ProtocolSeen = 1U<<4,
    StatusSeen = 1U<<0
  };

  bool valid_() const {
    if constexpr (Request) {
      if (!(m_seen & MethodSeen)) return false;
      if (m_method == Method::CONNECT) {
	if (!(m_seen & AuthoritySeen)) return false;
	if (m_seen & ProtocolSeen)
	  return m_extendedConnect &&
	    (m_seen & SchemeSeen) && (m_seen & PathSeen) &&
	    !m_contentLength;
	return !(m_seen & (SchemeSeen | PathSeen));
      }
      return (m_seen & (SchemeSeen | PathSeen)) ==
	(SchemeSeen | PathSeen) && !(m_seen & ProtocolSeen);
    } else
      return m_seen == StatusSeen;
  }

  template <typename Header>
  bool pseudo_(ZuBSpan name, ZuBSpan value, Header &&header) {
    if (m_trailers || m_regular) return false;
    if constexpr (Request) {
      int i = pseudo(name);
      if (i < 0) return false;
      uint8_t bit = uint8_t(1U << unsigned(i));
      if (m_seen & bit) return false;
      m_seen |= bit;
      switch (i) {
	case 0:
	  m_method = Method::lookup(value);
	  return m_method >= 0;
	case 1:
	  if (!value) return false;
	  m_path = value;
	  return true;
	case 2:
	  m_scheme = Scheme::parse(value);
	  return m_scheme >= 0;
	case 3:
	  if (!value) return false;
	  m_authority = value;
	  return true;
	case 4:
	  if (!value) return false;
	  m_protocol = value;
	  return true;
      }
      return false;
    } else {
      if (name != ":status" || m_seen) return false;
      if (value.length() != 3) return false;
      unsigned status = 0;
      for (unsigned i = 0; i < 3; ++i) {
	int c = value[i];
	if (c < '0' || c > '9') return false;
	status = status * 10 + unsigned(c - '0');
      }
      if (status < 100) return false;
      m_seen = StatusSeen;
      m_status = status;
      return true;
    }
  }

  String	m_path;
  String	m_authority;
  String	m_protocol;
  uint64_t	m_length = 0;
  unsigned	m_status = 0;
  Method::T	m_method = -1;
  Method::T	m_requestMethod = -1;
  Scheme::T	m_scheme = -1;
  uint8_t	m_seen = 0;
  bool		m_regular = false;
  bool		m_trailers = false;
  bool		m_contentLength = false;
  bool		m_noBody = false;
  bool		m_extendedConnect = false;
  bool		m_started = false;
};

} // namespace Fields

} // namespace Zhttp

#endif /* ZhttpFields_HH */
