//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// DNS and host resolver

#include <zlib/ZiResolver.hh>

#include <zlib/ZmSingleton.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#ifndef _WIN32
#include <arpa/nameser.h>
#include <resolv.h>
#endif

#ifdef _WIN32

#if defined(__GNUC__) && !defined(GetAddrInfo)

extern "C" {
  typedef struct {
    int			ai_flags;
    int			ai_family;
    int			ai_socktype;
    int			ai_protocol;
    size_t		ai_addrlen;
    wchar_t		**ai_canonname;
    struct sockaddr	*ai_addr;
    struct addrinfoW	*ai_next;
  } ADDRINFOT;
  typedef int (WSAAPI *PGetAddrInfoW)(
      const wchar_t *, const wchar_t *, const ADDRINFOT *, ADDRINFOT **);
  typedef void (WSAAPI *PFreeAddrInfoW)(ADDRINFOT *);
  typedef int (WSAAPI *PGetNameInfoW)(
      const struct sockaddr *, socklen_t,
      wchar_t *, DWORD, wchar_t *, DWORD, int);
}

class ZiResolver_WSDLL {
public:
  int getAddrInfo(
      const wchar_t *, const wchar_t *, const ADDRINFOT *, ADDRINFOT **);
  void freeAddrInfo(ADDRINFOT *);
  int getNameInfo(
      const struct sockaddr *, socklen_t,
      wchar_t *, DWORD, wchar_t *, DWORD, int);

  static ZiResolver_WSDLL *instance();

  ZiResolver_WSDLL();
  ~ZiResolver_WSDLL();

private:
  HMODULE		m_wsdll;
  PGetAddrInfoW		m_getAddrInfoW;
  PFreeAddrInfoW	m_freeAddrInfoW;
  PGetNameInfoW		m_getNameInfoW;
};

ZiResolver_WSDLL *ZiResolver_WSDLL::instance()
{
  return
    ZmSingleton<ZiResolver_WSDLL,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

ZiResolver_WSDLL::ZiResolver_WSDLL()
{
  if (m_wsdll = LoadLibrary(L"ws2_32.dll")) {
    m_getAddrInfoW = (PGetAddrInfoW)GetProcAddress(m_wsdll, "GetAddrInfoW");
    m_freeAddrInfoW = (PFreeAddrInfoW)GetProcAddress(m_wsdll, "FreeAddrInfoW");
    m_getNameInfoW = (PGetNameInfoW)GetProcAddress(m_wsdll, "GetNameInfoW");
  } else {
    m_getAddrInfoW = nullptr;
    m_freeAddrInfoW = nullptr;
    m_getNameInfoW = nullptr;
  }
}

ZiResolver_WSDLL::~ZiResolver_WSDLL()
{
  FreeLibrary(m_wsdll);
}

int ZiResolver_WSDLL::getAddrInfo(
  const wchar_t *node,
  const wchar_t *service,
  const ADDRINFOT *hints,
  ADDRINFOT **result)
{
  if (!m_getAddrInfoW) return WSASYSNOTREADY;
  return (*m_getAddrInfoW)(node, service, hints, result);
}

void ZiResolver_WSDLL::freeAddrInfo(ADDRINFOT *ai)
{
  if (!m_freeAddrInfoW) return;
  (*m_freeAddrInfoW)(ai);
}

int ZiResolver_WSDLL::getNameInfo(
  const struct sockaddr *sa, socklen_t salen,
  wchar_t *host, DWORD hostlen, wchar_t *serv, DWORD servlen,
  int flags)
{
  if (!m_getNameInfoW) return WSASYSNOTREADY;
  return (*m_getNameInfoW)(sa, salen, host, hostlen, serv, servlen, flags);
}

#define GetAddrInfo(node, service, hints, result) \
  (ZiResolver_WSDLL::instance()->getAddrInfo(node, service, hints, result))
#define FreeAddrInfo(ai) (ZiResolver_WSDLL::instance()->freeAddrInfo(ai))
#define GetNameInfo(sa, salen, host, hostlen, serv, servlen, flags) \
  (ZiResolver_WSDLL::instance()->getNameInfo(sa, salen, host, hostlen, \
				       serv, servlen, flags))

#endif /* defined(__GNUC__) && !defined(GetAddrInfo) */

class ZiResolver_WSAStartup {
public:
  static ZiResolver_WSAStartup *instance();

  ZiResolver_WSAStartup();
  ~ZiResolver_WSAStartup();

private:
  bool		m_wsaCleanup;
};

ZiResolver_WSAStartup *ZiResolver_WSAStartup::instance()
{
  return
    ZmSingleton<ZiResolver_WSAStartup,
      ZmSingletonCleanup<ZmCleanup::Platform>>::instance();
}

ZiResolver_WSAStartup::ZiResolver_WSAStartup()
{
  WSADATA wd;
  m_wsaCleanup = !WSAStartup(MAKEWORD(2, 2), &wd);
}

ZiResolver_WSAStartup::~ZiResolver_WSAStartup()
{
  if (m_wsaCleanup) WSACleanup();
}

using ZiResolver_AddrInfo = ADDRINFOT;
#define ZiResolver_GetAddrInfo GetAddrInfo
#define ZiResolver_FreeAddrInfo FreeAddrInfo
#define ZiResolver_GetNameInfo GetNameInfo

#define ZiResolver_InitOnce() (ZiResolver_WSAStartup::instance())

#else

using ZiResolver_AddrInfo = struct addrinfo;
#define ZiResolver_GetAddrInfo getaddrinfo
#define ZiResolver_FreeAddrInfo freeaddrinfo
#define ZiResolver_GetNameInfo getnameinfo

#define ZiResolver_InitOnce() (void())

#endif /* _WIN32 */

namespace ZiResolver_ {

enum {
  DNSHeaderLen = 12,
  DNSTypeA = 1,
  DNSTypeSVCB = 64,
  DNSTypeHTTPS = 65,
  DNSClassIN = 1,
  SvcMandatory = 0,
  SvcALPN = 1,
  SvcNoDefaultALPN = 2,
  SvcPort = 3,
  SvcIPv4Hint = 4,
  NameMaxSteps = 64
};

using Host = ZiResolver::Host;

ZuInline uint16_t u16(const uint8_t *p)
{
  return (uint16_t(p[0])<<8) | p[1];
}

ZuInline uint32_t u32(const uint8_t *p)
{
  return (uint32_t(p[0])<<24) | (uint32_t(p[1])<<16) |
    (uint32_t(p[2])<<8) | p[3];
}

#ifndef _WIN32
ZuInline ZuCSpan hostSpan(const Host &host)
{
  return ZuCSpan(host.data(), host.length());
}
#endif

void setHost(Host &host, ZuCSpan s)
{
#ifndef _WIN32
  host = s;
#else
  host.length(ZuUTF<wchar_t, char>::cvt(host.span(), s));
  host.truncate();
#endif
}

bool readName(
  const uint8_t *msg, unsigned msgLen, unsigned &off, ZtString<> &out)
{
  unsigned p = off, next = off, steps = 0;
  bool jumped = false;
  out.length(0);
  for (;;) {
    if (p >= msgLen || ++steps > NameMaxSteps) return false;
    uint8_t l = msg[p++];
    if (!l) {
      if (!jumped) next = p;
      off = next;
      if (!out.length()) out = ".";
      return true;
    }
    if ((l & 0xc0) == 0xc0) {
      if (p >= msgLen) return false;
      unsigned ptr = ((l & 0x3f)<<8) | msg[p++];
      if (ptr >= msgLen) return false;
      if (!jumped) next = p;
      jumped = true;
      p = ptr;
      continue;
    }
    if (l & 0xc0) return false;
    if (p + l > msgLen) return false;
    if (out.length()) out << '.';
    out << ZuCSpan(reinterpret_cast<const char *>(msg + p), l);
    p += l;
    if (!jumped) next = p;
  }
}

bool knownMandatory(uint16_t key)
{
  switch (key) {
    case SvcMandatory:
    case SvcALPN:
    case SvcNoDefaultALPN:
    case SvcPort:
    case SvcIPv4Hint:
      return true;
    default:
      return false;
  }
}

bool parseSvcParams(
  const uint8_t *msg, unsigned msgLen, unsigned rdataEnd,
  unsigned &off, ZiResolver::HTTPS &https)
{
  uint16_t last = 0;
  bool haveLast = false;
  while (off < rdataEnd) {
    if (off + 4 > rdataEnd) return false;
    uint16_t key = u16(msg + off);
    uint16_t len = u16(msg + off + 2);
    off += 4;
    if (haveLast && key <= last) return false;
    haveLast = true;
    last = key;
    if (off + len > rdataEnd) return false;
    const uint8_t *v = msg + off;
    switch (key) {
      case SvcMandatory:
	if (len & 1) return false;
	for (unsigned i = 0; i < len; i += 2)
	  if (!knownMandatory(u16(v + i))) https.unknownMandatory = true;
	break;
      case SvcALPN: {
	https.hasALPN = true;
	unsigned i = 0;
	while (i < len) {
	  unsigned n = v[i++];
	  if (!n || i + n > len) return false;
	  if (n == 2 && v[i] == 'h' && v[i + 1] == '3') https.hasH3 = true;
	  i += n;
	}
	break;
      }
      case SvcNoDefaultALPN:
	if (len) return false;
	https.noDefaultALPN = true;
	break;
      case SvcPort:
	if (len != 2) return false;
	https.port = u16(v);
	break;
      case SvcIPv4Hint:
	if (len % 4) return false;
	https.hasIPv4Hint = true;
	for (unsigned i = 0; i < len && https.nIPv4Hint < ZiResolver::H3MaxIPs;
	    i += 4) {
	  https.ipv4Hint[https.nIPv4Hint++] =
	    (uint32_t(v[i])<<24) | (uint32_t(v[i + 1])<<16) |
	    (uint32_t(v[i + 2])<<8) | v[i + 3];
	}
	break;
      default:
	break;
    }
    off += len;
  }
  return off == rdataEnd;
}

bool parseHTTPSRData(
  const uint8_t *msg, unsigned msgLen, unsigned &off, unsigned rdataEnd,
  const Host &owner, ZiResolver::HTTPS &https)
{
  if (off + 2 > rdataEnd) return false;
  https = {};
  https.priority = u16(msg + off);
  off += 2;
  ZtString<> target;
  if (!readName(msg, rdataEnd, off, target)) return false;
  setHost(https.target, target == "." ? ZuCSpan(owner) : ZuCSpan(target));
  if (!parseSvcParams(msg, msgLen, rdataEnd, off, https)) return false;
  return true;
}

int parseDNS(
  ZuBSpan span, Host owner, ZmFn<bool(const ZiResolver::HTTPS &)> fn,
  ZeError *e)
{
  const uint8_t *msg = span.data();
  unsigned msgLen = span.length();
  if (msgLen < DNSHeaderLen) goto invalid;
  {
    unsigned qd = u16(msg + 4);
    unsigned an = u16(msg + 6);
    unsigned ns = u16(msg + 8);
    unsigned ar = u16(msg + 10);
    unsigned off = DNSHeaderLen;
    ZtString<> name;
    for (unsigned i = 0; i < qd; i++) {
      if (!readName(msg, msgLen, off, name) || off + 4 > msgLen) goto invalid;
      off += 4;
    }
    unsigned emitted = 0;
    for (unsigned i = 0, n = an + ns + ar; i < n; i++) {
      if (!readName(msg, msgLen, off, name) || off + 10 > msgLen) goto invalid;
      uint16_t type = u16(msg + off);
      uint16_t klass = u16(msg + off + 2);
      uint16_t rdlen = u16(msg + off + 8);
      off += 10;
      if (off + rdlen > msgLen) goto invalid;
      unsigned rdataEnd = off + rdlen;
      if ((type == DNSTypeHTTPS || type == DNSTypeSVCB) &&
	  klass == DNSClassIN) {
	ZiResolver::HTTPS https;
	unsigned rdataOff = off;
	if (!parseHTTPSRData(msg, msgLen, rdataOff, rdataEnd, owner, https))
	  goto invalid;
	if (!https.unknownMandatory) {
	  ++emitted;
	  if (!fn(https)) return Zi::OK;
	}
      }
      off = rdataEnd;
    }
    if (emitted) return Zi::OK;
  }
  if (e) *e = ZeError(EAI_NONAME);
  return Zi::IOError;

invalid:
  if (e) *e = ZeError(ZiEINVAL);
  return Zi::IOError;
}

bool emitIP(
  const ZiResolver::H3Endpoint &ep, ZiIP *seen, unsigned &nSeen,
  ZmFn<bool(const ZiResolver::H3Endpoint &)> fn)
{
  for (unsigned i = 0; i < nSeen; i++)
    if (seen[i] == ep.ip) return true;
  if (nSeen < ZiResolver::H3MaxIPs) seen[nSeen++] = ep.ip;
  return fn(ep);
}

} // ZiResolver_

namespace ZiResolver {

int resolve(Host host, ZmFn<bool(ZiIP)> fn, ZeError *e)
{
  ZiResolver_InitOnce();

  ZiResolver_AddrInfo hints;
  ZiResolver_AddrInfo *result;
  int errno_;

  memset(&hints, 0, sizeof(ZiResolver_AddrInfo));
  hints.ai_family = AF_INET;
  hints.ai_protocol = PF_INET;
  while (errno_ = ZiResolver_GetAddrInfo(host.data(), 0, &hints, &result)) {
    if (errno_ == EAI_AGAIN) continue;
    if (e) *e =
#ifdef EAI_SYSTEM
      errno_ == EAI_SYSTEM ? ZeLastSockError :
#endif
      ZeError(errno_);
    return Zi::IOError;
  }

  unsigned count = 0;
  for (auto ai = result; ai; ai = ai->ai_next) {
    if (!ai->ai_addr || ai->ai_addrlen < sizeof(struct sockaddr_in))
      continue;
    ++count;
    ZiIP ip{((struct sockaddr_in *)ai->ai_addr)->sin_addr};
    if (!fn(ip)) break;
  }
  ZiResolver_FreeAddrInfo(result);
  if (count) return Zi::OK;
  if (e) *e = ZeError(EAI_NONAME);
  return Zi::IOError;
}

Host name(ZiIP ip, ZeError *e)
{
  ZiResolver_InitOnce();

  Host ret;
  struct sockaddr_in sai;
  memset(&sai, 0, sizeof(struct sockaddr_in));
  sai.sin_family = AF_INET;
  sai.sin_addr = ip;
  ret.size(Zi::HostnameMax);
  int errno_;
  while (errno_ = ZiResolver_GetNameInfo(
	reinterpret_cast<sockaddr *>(&sai), sizeof(struct sockaddr_in),
	ret, Zi::HostnameMax, 0, 0, 0)) {
    if (errno_ == EAI_AGAIN) continue;
    if (e) *e =
#ifdef EAI_SYSTEM
      errno_ == EAI_SYSTEM ? ZeLastSockError :
#endif
      ZeError(errno_);
    ret.null();
    return ret;
  }
  ret.calcLength();
  ret.truncate();
  return ret;
}

int parse(
  ZuBSpan msg, Host owner, ZmFn<bool(const HTTPS &)> fn, ZeError *e)
{
  return ZiResolver_::parseDNS(msg, ZuMv(owner), ZuMv(fn), e);
}

int https(Host host, ZmFn<bool(const HTTPS &)> fn, ZeError *e)
{
#ifndef _WIN32
  uint8_t msg[DNSMsgMax];
  int n;
#if defined(HAVE_RES_NQUERY) && defined(HAVE_RES_NINIT) && defined(HAVE_RES_NCLOSE)
  struct __res_state state;
  memset(&state, 0, sizeof(state));
  if (res_ninit(&state) < 0) {
    if (e) *e = ZeLastSockError;
    return Zi::IOError;
  }
  n = res_nquery(&state, ZiResolver_::hostSpan(host).data(),
    ZiResolver_::DNSClassIN, ZiResolver_::DNSTypeHTTPS, msg, sizeof(msg));
  res_nclose(&state);
#elif defined(HAVE_RES_QUERY)
  n = res_query(ZiResolver_::hostSpan(host).data(),
    ZiResolver_::DNSClassIN, ZiResolver_::DNSTypeHTTPS, msg, sizeof(msg));
#else
  if (e) *e = ZeError(EAI_NONAME);
  return Zi::IOError;
#endif
  if (n <= 0 || n > int(sizeof(msg))) {
    if (e) *e = ZeError(EAI_NONAME);
    return Zi::IOError;
  }
  return parse(ZuBSpan(msg, n), ZuMv(host), ZuMv(fn), e);
#else
  if (e) *e = ZeError(WSASYSNOTREADY);
  return Zi::IOError;
#endif
}

int http3(
  Host dnsHost, Host tlsHost, uint16_t port, H3Policy policy,
  ZmFn<bool(const H3Endpoint &)> fn, ZeError *e)
{
  Host query[H3AliasDepth + 1];
  query[0] = dnsHost;
  bool dnsH3 = false;
  ZiIP seen[H3MaxIPs];
  unsigned nSeen = 0;

  for (unsigned depth = 0; depth <= H3AliasDepth; depth++) {
    HTTPS recs[H3MaxALPN];
    unsigned nRecs = 0;
    ZeError httpsErr;
    int rc = https(query[depth],
      ZmFn<bool(const HTTPS &)>{[&recs, &nRecs](const HTTPS &https) {
	if (nRecs < H3MaxALPN) recs[nRecs++] = https;
	return nRecs < H3MaxALPN;
      }}, &httpsErr);
    if (rc != Zi::OK) break;

    bool followed = false;
    for (unsigned i = 0; i < nRecs; i++) {
      auto &rec = recs[i];
      if (rec.alias()) {
	if (depth == H3AliasDepth || !rec.target || rec.target == ".") continue;
	query[depth + 1] = rec.target;
	followed = true;
	break;
      }
      if (!rec.hasH3 || rec.unknownMandatory ||
	  (rec.noDefaultALPN && !rec.hasALPN))
	continue;
      dnsH3 = true;
      Host target = rec.target ? rec.target : query[depth];
      uint16_t epPort = rec.port ? rec.port : port;
      for (unsigned j = 0; j < rec.nIPv4Hint; j++) {
	H3Endpoint ep{target, tlsHost, rec.ipv4Hint[j], epPort, true, true};
	if (!ZiResolver_::emitIP(ep, seen, nSeen, fn)) return Zi::OK;
      }
      ZeError resolveErr;
      resolve(target,
	ZmFn<bool(ZiIP)>{[&](ZiIP ip) {
	  H3Endpoint ep{target, tlsHost, ip, epPort, true, false};
	  return ZiResolver_::emitIP(ep, seen, nSeen, fn);
	}}, &resolveErr);
    }
    if (dnsH3) return nSeen ? Zi::OK : Zi::IOError;
    if (!followed) break;
  }

  if (policy == H3Policy::DNSWithBlindFallback) {
    int rc = resolve(dnsHost,
      ZmFn<bool(ZiIP)>{[&](ZiIP ip) {
	H3Endpoint ep{dnsHost, tlsHost, ip, port, false, false};
	return ZiResolver_::emitIP(ep, seen, nSeen, fn);
      }}, e);
    return rc == Zi::OK && nSeen ? Zi::OK : Zi::IOError;
  }
  if (e) *e = ZeError(EAI_NONAME);
  return Zi::IOError;
}

} // ZiResolver

ZiIP::Hostname ZiIP::name(ZeError *e)
{
  return ZiResolver::name(*this, e);
}
