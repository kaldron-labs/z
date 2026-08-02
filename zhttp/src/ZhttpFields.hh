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

#include <zlib/ZtString.hh>

#include <zlib/ZhttpURL.hh>
#include <zlib/ZhttpUtil.hh>

namespace Zhttp {

namespace Fields {

enum Section {
  Invalid = -1,
  Informational,
  Final,
  Trailers
};

using String = ZtString<ZtStringHeapID<"Zhttp.Fields.String">>;

ZhttpAPI bool forbidden(ZuCSpan);
ZhttpAPI int pseudo(ZuCSpan);

template <typename Impl, typename = void>
struct HasRuntime : public ZuFalse { };
template <typename Impl>
struct HasRuntime<Impl,
  decltype(ZuDeclVal<Impl *>()->header(
    ZuDeclVal<ZuBSpan>(), ZuDeclVal<ZuBSpan>()), void())> :
    public ZuTrue { };

template <typename Impl, typename L, typename = void>
struct HasRuntimeBuilder : public ZuFalse { };
template <typename Impl, typename L>
struct HasRuntimeBuilder<Impl, L,
  decltype(ZuDeclVal<Impl *>()->header(ZuDeclVal<L &&>()), void())> :
    public ZuTrue { };

template <
  typename Headers, typename Header, typename Static, typename Unknown>
void dispatch(
  ZuBSpan key, ZuBSpan value,
  Header &&header, Static &&static_, Unknown &&unknown) {
  using Keys = ZuTypeSlice<2, 0, Headers>;
  using Values = ZuTypeSlice<2, 1, Headers>;
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
	using KeyValues = ZuType<i, Values>;
	if constexpr (!ZuIsSame<KeyValues, void>{})
	  if constexpr (KeyValues::N) {
	    static constexpr auto matcher = ZuMatcher<KeyValues>();
	    auto j = matcher.exact(value);
	    enum { I = i };
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

inline bool nameOK(ZuCSpan name) {
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

template <bool Request_>
class Semantics {
public:
  enum { Request = Request_ };

  void trailers(bool value) { m_trailers = value; }
  void requestMethod(Method::T value) { m_requestMethod = value; }
  void extendedConnect(bool value) { m_extendedConnect = value; }
  bool bodyAllowed() const { return !m_noBody; }

  template <typename Header>
  bool field(ZuCSpan name, ZuCSpan value, Header &&header) {
    if (!nameOK(name)) return false;
    if (name[0] == ':')
      return pseudo_(name, value, ZuFwd<Header>(header));
    m_regular = true;
    if (forbidden(name)) return false;
    if (m_trailers && name == "content-length") return false;
    if (name == "te" && value != "trailers") return false;
    if (name == "content-length") {
      uint64_t length;
      if (!atou(ZuBSpan{value}, length)) return false;
      if (m_contentLength && length != m_length) return false;
      m_contentLength = true;
      m_length = length;
    }
    header(ZuBSpan{name}, ZuBSpan{value});
    return true;
  }

  template <typename Operation, typename Status, typename Header>
  Section finish(Operation &&operation, Status &&status, Header &&header) {
    if (m_trailers) return m_seen ? Invalid : Trailers;
    if constexpr (Request) {
      if (!(m_seen & MethodSeen)) return Invalid;
      if (m_method == Method::CONNECT) {
	if (!(m_seen & AuthoritySeen)) return Invalid;
	if (m_seen & ProtocolSeen) {
	  if (!m_extendedConnect ||
	      !(m_seen & SchemeSeen) || !(m_seen & PathSeen) ||
	      m_contentLength)
	    return Invalid;
	} else if (m_seen & (SchemeSeen | PathSeen))
	  return Invalid;
      } else {
	if ((m_seen & (SchemeSeen | PathSeen)) !=
	    (SchemeSeen | PathSeen) || (m_seen & ProtocolSeen))
	  return Invalid;
      }
      RequestTarget target;
      auto e = RequestTarget::fromPseudo(
	target, m_method, m_scheme,
	ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(m_authority.data()),
	  m_authority.length()},
	ZuBSpan{m_path}, ZuBSpan{m_protocol});
      if (!e.ok()) return Invalid;
      operation(m_method, target);
      header(ZuBSpan{"host"}, target.authority.raw);
      return Final;
    } else {
      if (m_seen != StatusSeen) return Invalid;
      status(m_status);
      m_noBody =
	m_requestMethod == Method::HEAD ||
	m_status < 200 || m_status == 204 || m_status == 304;
      return m_status < 200 ? Informational : Final;
    }
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

  template <typename Header>
  bool pseudo_(ZuCSpan name, ZuCSpan value, Header &&header) {
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
};

} // namespace Fields

} // namespace Zhttp

#endif /* ZhttpFields_HH */
