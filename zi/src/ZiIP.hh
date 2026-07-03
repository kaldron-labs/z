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
#include <zlib/ZuTraits.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmFn.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

class ZiIP;

namespace ZiResolver {

ZiExtern int resolve(
  Zi::Hostname host, ZmFn<bool(ZiIP)> fn, ZeError *e);
ZiExtern Zi::Hostname name(ZiIP ip, ZeError *e);

}

class ZiAPI ZiIP : public in_addr {
public:
  using Hostname = Zi::Hostname;

  ZiIP() { s_addr = 0; }

  ZiIP(const ZiIP &a) { s_addr = a.s_addr; }
  ZiIP &operator =(const ZiIP &a) {
    s_addr = a.s_addr;
    return *this;
  }

  explicit ZiIP(struct in_addr ia) { s_addr = ia.s_addr; }
  ZiIP &operator =(struct in_addr ia) {
    s_addr = ia.s_addr;
    return *this;
  }

  ZiIP(uint32_t n) { s_addr = htonl(n); }
  ZiIP &operator =(uint32_t n) { s_addr = htonl(n); return *this; }

  template <typename S, decltype(ZuMatchString<S>(), int()) = 0>
  ZiIP(S &&s) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Waddress"
#pragma GCC diagnostic ignored "-Wnonnull-compare"
#endif
    if (!s) { s_addr = 0; return; }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
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
    if (!s) { s_addr = 0; return *this; }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    ZeError e;
    if (resolve(ZuFwd<S>(s), &e) != Zi::OK) throw e;
    return *this;
  }

  bool operator !() const { return !s_addr; }
  ZuOpBool

  bool equals(const ZiIP &a) const { return s_addr == a.s_addr; }
  int cmp(const ZiIP &a) const {
    return ZuCompare(s_addr, a.s_addr);
  }
  friend inline bool operator ==(const ZiIP &l, const ZiIP &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZiIP &l, const ZiIP &r) {
    return l.cmp(r);
  }

  operator uint32_t() const { return ntohl(s_addr); }

  uint32_t hash() const { return s_addr; }

  template <typename S> void print(S &s) const {
    typedef uint8_t Bytes[4];
    ZuPun<uint32_t, Bytes> pun(s_addr);
    s <<
      ZuBoxed(pun.out[0]) << '.' <<
      ZuBoxed(pun.out[1]) << '.' <<
      ZuBoxed(pun.out[2]) << '.' <<
      ZuBoxed(pun.out[3]);
  }

  bool multicast() const {
    unsigned i = ((uint32_t(ntohl(s_addr)))>>24) & 0xff;
    return i >= 224 && i < 240;
  }

  struct Traits : public ZuBaseTraits<ZiIP> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZiIP *);

  friend ZuPrintFn ZuPrintType(ZiIP *);

public:
  template <typename S>
  ZuMatchString<S &&, int> resolve(S &&s, ZeError *e = 0) {
    Zi::Hostname host{ZuFwd<S>(s)};
    return ZiResolver::resolve(ZuMv(host), ZmFn<bool(ZiIP)>{[this](ZiIP ip) {
      s_addr = ip.s_addr;
      return false;
    }}, e);
  }
  Hostname name(ZeError *e = 0);
};

class ZiSockAddr {
public:
  ZiSockAddr() { null(); }
  ZiSockAddr(ZiIP ip, uint16_t port) { init(ip, port); }

  void null() { m_sin.sin_family = AF_UNSPEC; }
  void init(ZiIP ip, uint16_t port) {
    m_sin.sin_family = AF_INET;
    m_sin.sin_port = htons(port);
    m_sin.sin_addr = ip;
    memset(&m_sin.sin_zero, 0, sizeof(m_sin.sin_zero));
  }

  ZiIP ip() const { return ZiIP(m_sin.sin_addr); }
  uint16_t port() const { return ntohs(m_sin.sin_port); }

  struct sockaddr *sa() { return (struct sockaddr *)&m_sin; }
  int len() const { return sizeof(struct sockaddr_in); }

  bool operator !() const { return m_sin.sin_family == AF_UNSPEC; }
  ZuOpBool

  struct sockaddr_in	m_sin;
};

#endif /* ZiIP_HH */
