//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// IP address

#ifndef ZiIP_HH
#define ZiIP_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

class ZiIP;

ZtEnumNS(ZiIPType, int8_t, Null, V4, V6);

class ZiAPI ZiIP {
public:
  using Hostname = Zi::Hostname;
  using Addr = ZuUnion<void, in_addr, in6_addr>;
  enum {
    Null = Addr::Index<void>{},
    V4 = Addr::Index<in_addr>{},
    V6 = Addr::Index<in6_addr>{}
  };
  ZuAssert(Null == ZiIPType::Null);
  ZuAssert(V4 == ZiIPType::V4);
  ZuAssert(V6 == ZiIPType::V6);

  ZiIP() = default;

  ZiIP(const ZiIP &a) = default;
  ZiIP &operator =(const ZiIP &a) {
    m_addr = a.m_addr;
    return *this;
  }

  explicit ZiIP(const struct in_addr &ia) { m_addr.p<V4>(ia); }
  ZiIP &operator =(const struct in_addr &ia) {
    m_addr.p<V4>(ia);
    return *this;
  }

  explicit ZiIP(const struct in6_addr &ia) { m_addr.p<V6>(ia); }
  ZiIP &operator =(const struct in6_addr &ia) {
    m_addr.p<V6>(ia);
    return *this;
  }

  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZiIP(S &&s) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (!s) return;
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    // Falls back to DNS resolution after numeric parsing; this may block.
    // Do not call from ZiResolver callbacks.
    ZeError e;
    if (resolve(ZuFwd<S>(s), &e) != Zi::OK) throw e;
  }
  template <typename S>
  ZuMatchString<S &&, ZiIP &> &operator =(S &&s) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (!s) { m_addr.type_(Null); return *this; }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    // Falls back to DNS resolution after numeric parsing; this may block.
    // Do not call from ZiResolver callbacks.
    ZeError e;
    if (resolve(ZuFwd<S>(s), &e) != Zi::OK) throw e;
    return *this;
  }

  ZiIPType::T type() const { return m_addr.type(); }
  bool v4() const { return m_addr.type() == V4; }
  bool v6() const { return m_addr.type() == V6; }
  bool wildcard() const {
    switch (m_addr.type()) {
      case V4:
	return !m_addr.p<V4>().s_addr;
      case V6:
	return !memcmp(
	  m_addr.p<V6>().s6_addr, &in6addr_any,
	  sizeof(in6_addr));
      default:
	return true;
    }
  }
  bool loopback() const {
    switch (m_addr.type()) {
      case V4:
	return m_addr.p<V4>().s_addr == htonl(INADDR_LOOPBACK);
      case V6:
	return !memcmp(
	  m_addr.p<V6>().s6_addr, &in6addr_loopback,
	  sizeof(in6_addr));
      default:
	return false;
    }
  }
  bool mappedV4() const {
    if (m_addr.type() != V6) return false;
    const auto &a = m_addr.p<V6>();
    return !a.s6_addr[0] && !a.s6_addr[1] && !a.s6_addr[2] &&
      !a.s6_addr[3] && !a.s6_addr[4] && !a.s6_addr[5] &&
      !a.s6_addr[6] && !a.s6_addr[7] && !a.s6_addr[8] &&
      !a.s6_addr[9] && a.s6_addr[10] == 0xff && a.s6_addr[11] == 0xff;
  }

  const struct in_addr &inAddr() const { return m_addr.p<V4>(); }
  struct in_addr &inAddr() { return m_addr.p<V4>(); }
  const struct in6_addr &in6Addr() const { return m_addr.p<V6>(); }
  struct in6_addr &in6Addr() { return m_addr.p<V6>(); }

  bool operator !() const { return m_addr.type() == Null; }
  ZuOpBool

  bool equals(const ZiIP &a) const {
    if (m_addr.type() != a.m_addr.type()) return false;
    switch (m_addr.type()) {
      case V4:
	return m_addr.p<V4>().s_addr == a.m_addr.p<V4>().s_addr;
      case V6:
	return !memcmp(
	  m_addr.p<V6>().s6_addr, a.m_addr.p<V6>().s6_addr,
	  sizeof(in6_addr));
      default:
	return true;
    }
  }
  int cmp(const ZiIP &a) const {
    if (int i = ZuCompare(m_addr.type(), a.m_addr.type())) return i;
    switch (m_addr.type()) {
      case V4:
	return ZuCompare(m_addr.p<V4>().s_addr, a.m_addr.p<V4>().s_addr);
      case V6:
	return memcmp(
	  m_addr.p<V6>().s6_addr, a.m_addr.p<V6>().s6_addr,
	  sizeof(in6_addr));
      default:
	return 0;
    }
  }
  friend inline bool operator ==(const ZiIP &l, const ZiIP &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZiIP &l, const ZiIP &r) {
    return l.cmp(r);
  }

  uint32_t hash() const {
    switch (m_addr.type()) {
      case V4:
	return ZuHash<uint8_t>::hash(V4) ^ m_addr.p<V4>().s_addr;
      case V6:
	return ZuHash<uint8_t>::hash(V6) ^
	  ZuHash_FNV::hash(m_addr.p<V6>().s6_addr, sizeof(in6_addr));
      default:
	return ZuHash<uint8_t>::hash(Null);
    }
  }

  template <typename S> void print(S &s) const {
    switch (m_addr.type()) {
      case V4:
	printV4(s);
	break;
      case V6:
	printV6(s);
	break;
      default:
	break;
    }
  }

  template <typename S> void printV4(S &s) const {
    typedef uint8_t Bytes[4];
    ZuPun<uint32_t, Bytes> pun(m_addr.p<V4>().s_addr);
    s <<
      ZuBoxed(pun.out[0]) << '.' <<
      ZuBoxed(pun.out[1]) << '.' <<
      ZuBoxed(pun.out[2]) << '.' <<
      ZuBoxed(pun.out[3]);
  }

  template <typename S> void printV6(S &s) const {
    const auto &a = m_addr.p<V6>();
    uint16_t g[8];
    for (unsigned i = 0; i < 8; i++)
      g[i] = (uint16_t(a.s6_addr[i<<1])<<8) | a.s6_addr[(i<<1) + 1];

    int best = -1, bestLen = 0;
    for (int i = 0; i < 8;) {
      if (g[i]) { ++i; continue; }
      int j = i + 1;
      while (j < 8 && !g[j]) ++j;
      if (j - i > bestLen && j - i > 1) best = i, bestLen = j - i;
      i = j;
    }

    for (int i = 0; i < 8; i++) {
      if (i == best) {
	s << "::";
	i += bestLen - 1;
	continue;
      }
      if (i && i != best + bestLen) s << ':';
      s << ZuBoxed(g[i]).hex<false>();
    }
  }

  bool multicast() const {
    switch (m_addr.type()) {
      case V4: {
	unsigned i = ((uint32_t(ntohl(m_addr.p<V4>().s_addr)))>>24) & 0xff;
	return i >= 224 && i < 240;
      }
      case V6:
	return m_addr.p<V6>().s6_addr[0] == 0xff;
      default:
	return false;
    }
  }

  struct Traits : public ZuBaseTraits<ZiIP> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZiIP *);

  friend ZuPrintFn ZuPrintType(ZiIP *);

public:
  template <typename S>
  ZuMatchString<S &&, int> resolve(S &&s, ZeError *e = 0) {
    // Blocks after numeric parsing; do not call from ZiResolver callbacks.
    Zi::Hostname host{ZuFwd<S>(s)};
    return resolve_(ZuMv(host), e);
  }
  // Blocks on async reverse lookup; do not call from ZiResolver callbacks.
  Hostname name(ZeError *e = 0);

private:
  ZiAPI int resolve_(Zi::Hostname host, ZeError *e);

  Addr		m_addr;
};

class ZiSockAddr {
public:
  using Addr = ZuUnion<void, sockaddr_in, sockaddr_in6>;
  enum {
    Null = Addr::Index<void>{},
    V4 = Addr::Index<sockaddr_in>{},
    V6 = Addr::Index<sockaddr_in6>{}
  };
  ZuAssert(Null == ZiIPType::Null);
  ZuAssert(V4 == ZiIPType::V4);
  ZuAssert(V6 == ZiIPType::V6);
  enum { MaxLen = sizeof(sockaddr_in6) };

  ZiSockAddr() { null(); }
  ZiSockAddr(ZiIP ip, uint16_t port) { init(ip, port); }

  void null() {
    m_addr.type_(Null);
  }
  void init(ZiIPType::T type) {
    switch (type) {
      case ZiIPType::V4:
	m_addr.p<V4>(sockaddr_in{});
	memset(static_cast<void *>(&m_addr), 0, sizeof(sockaddr_in));
	m_addr.p<V4>().sin_family = AF_INET;
	break;
      case ZiIPType::V6:
	m_addr.p<V6>(sockaddr_in6{});
	memset(static_cast<void *>(&m_addr), 0, sizeof(sockaddr_in6));
	m_addr.p<V6>().sin6_family = AF_INET6;
	break;
      default:
	null();
	break;
    }
  }
  void sync() {
    switch (
      reinterpret_cast<const struct sockaddr *>(
	static_cast<const void *>(&m_addr))->sa_family) {
      case AF_INET:
	m_addr.type_(V4);
	break;
      case AF_INET6:
	m_addr.type_(V6);
	break;
      default:
	null();
	break;
    }
  }
  void init(ZiIP ip, uint16_t port) {
    switch (ip.type()) {
      case ZiIPType::V4:
	m_addr.p<V4>(sockaddr_in{});
	memset(&m_addr.p<V4>(), 0, sizeof(sockaddr_in));
	m_addr.p<V4>().sin_family = AF_INET;
	m_addr.p<V4>().sin_port = htons(port);
	m_addr.p<V4>().sin_addr = ip.inAddr();
	break;
      case ZiIPType::V6:
	m_addr.p<V6>(sockaddr_in6{});
	memset(&m_addr.p<V6>(), 0, sizeof(sockaddr_in6));
	m_addr.p<V6>().sin6_family = AF_INET6;
	m_addr.p<V6>().sin6_port = htons(port);
	m_addr.p<V6>().sin6_addr = ip.in6Addr();
	break;
      default:
	null();
	break;
    }
  }

  ZiIPType::T type() const {
    switch (m_addr.type()) {
      case V4:
	return m_addr.p<V4>().sin_family == AF_INET ?
	  ZiIPType::V4 : ZiIPType::Null;
      case V6:
	return m_addr.p<V6>().sin6_family == AF_INET6 ?
	  ZiIPType::V6 : ZiIPType::Null;
      default:
	return ZiIPType::Null;
    }
  }

  ZiIP ip() const {
    switch (type()) {
      case ZiIPType::V4:
	return ZiIP(m_addr.p<V4>().sin_addr);
      case ZiIPType::V6:
	return ZiIP(m_addr.p<V6>().sin6_addr);
      default:
	return ZiIP{};
    }
  }
  uint16_t port() const {
    switch (type()) {
      case ZiIPType::V4:
	return ntohs(m_addr.p<V4>().sin_port);
      case ZiIPType::V6:
	return ntohs(m_addr.p<V6>().sin6_port);
      default:
	return 0;
    }
  }

  const sockaddr_in &sin() const { return m_addr.p<V4>(); }
  sockaddr_in &sin() { return m_addr.p<V4>(); }
  const sockaddr_in6 &sin6() const { return m_addr.p<V6>(); }
  sockaddr_in6 &sin6() { return m_addr.p<V6>(); }

  struct sockaddr *sa() {
    switch (m_addr.type()) {
      case V4:
	return reinterpret_cast<struct sockaddr *>(
	  static_cast<void *>(&m_addr));
      case V6:
	return reinterpret_cast<struct sockaddr *>(
	  static_cast<void *>(&m_addr));
      default:
	return nullptr;
    }
  }
  const struct sockaddr *sa() const {
    return const_cast<ZiSockAddr *>(this)->sa();
  }
  int len() const {
    switch (m_addr.type()) {
      case V4:
	return sizeof(sockaddr_in);
      case V6:
	return sizeof(sockaddr_in6);
      default:
	return MaxLen;
    }
  }

  bool operator !() const { return type() == ZiIP::Null; }
  ZuOpBool

  Addr	m_addr;
};

#endif /* ZiIP_HH */
