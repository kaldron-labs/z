//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// test HTTP client

#include <iostream>
#include <string.h>

#include <zlib/ZuLib.hh>
#include <zlib/ZuICmp.hh>

#include <zlib/ZmGuard.hh>
#include <zlib/ZmBackTrace.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRandom.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>

#include <zlib/ZtCLI.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/ZiResolver.hh>
#include <zlib/ZiHashCSV.hh>
#include <zlib/ZiHeapCSV.hh>

#include <zlib/Ztcp.hh>
#include <zlib/Ztls.hh>
#include <zlib/Zquic.hh>

#include <zlib/Zhttp.hh>

ZtEnumNS(Http3Mode, int8_t, force, prefer, disable);

constexpr unsigned ClientTimeout = 15;
constexpr unsigned H3StallTimeout = 15;
constexpr unsigned H3QuietTimeout = 2;

namespace H3Policy {
  enum T {
    DNSOnly,
    DNSWithBlindFallback
  };
}

enum {
  H3MaxIPs = 8,
  H3MaxALPN = 8,
  H3AliasDepth = 4,
  DNSHeaderLen = 12,
  SvcMandatory = 0,
  SvcALPN = 1,
  SvcNoDefaultALPN = 2,
  SvcPort = 3,
  SvcIPv4Hint = 4,
  SvcIPv6Hint = 6,
  NameMaxSteps = 64
};

using DNSHost = ZiResolver_::Host;

struct HTTPS {
  uint16_t	priority = 0;
  DNSHost	target;
  uint16_t	port = 0;
  ZiIP		ipv4Hint[H3MaxIPs];
  ZiIP		ipv6Hint[H3MaxIPs];
  uint8_t	nIPv4Hint = 0;
  uint8_t	nIPv6Hint = 0;
  bool		noDefaultALPN = false;
  bool		hasALPN = false;
  bool		hasIPv4Hint = false;
  bool		hasIPv6Hint = false;
  bool		unknownMandatory = false;
  bool		hasH3 = false;

  bool alias() const { return !priority; }
};

struct H3Endpoint {
  DNSHost	dnsHost;
  DNSHost	tlsHost;
  ZiIP		ip;
  uint16_t	port = 443;
  bool		fromHTTPS = false;
  bool		fromIPv4Hint = false;
  bool		fromIPv6Hint = false;
};

ZuInline uint16_t dnsU16(const uint8_t *p)
{
  return (uint16_t(p[0])<<8) | p[1];
}

void setDNSHost(DNSHost &host, ZuCSpan s)
{
#ifndef _WIN32
  host = s;
#else
  host.length(ZuUTF<wchar_t, char>::cvt(host.span(), s));
  host.truncate();
#endif
}

bool readDNSName(
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

bool knownSvcMandatory(uint16_t key)
{
  switch (key) {
    case SvcMandatory:
    case SvcALPN:
    case SvcNoDefaultALPN:
    case SvcPort:
    case SvcIPv4Hint:
    case SvcIPv6Hint:
      return true;
    default:
      return false;
  }
}

bool parseSvcParams(
  const uint8_t *msg, unsigned rdataEnd, unsigned &off, HTTPS &https)
{
  uint16_t last = 0;
  bool haveLast = false;
  while (off < rdataEnd) {
    if (off + 4 > rdataEnd) return false;
    uint16_t key = dnsU16(msg + off);
    uint16_t len = dnsU16(msg + off + 2);
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
	  if (!knownSvcMandatory(dnsU16(v + i))) https.unknownMandatory = true;
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
	https.port = dnsU16(v);
	break;
      case SvcIPv4Hint:
	if (len % 4) return false;
	https.hasIPv4Hint = true;
	for (unsigned i = 0; i < len && https.nIPv4Hint < H3MaxIPs; i += 4) {
	  in_addr addr;
	  addr.s_addr = htonl(
	    (uint32_t(v[i])<<24) | (uint32_t(v[i + 1])<<16) |
	    (uint32_t(v[i + 2])<<8) | v[i + 3]);
	  https.ipv4Hint[https.nIPv4Hint++] = addr;
	}
	break;
      case SvcIPv6Hint:
	if (len % sizeof(in6_addr)) return false;
	https.hasIPv6Hint = true;
	for (unsigned i = 0; i < len && https.nIPv6Hint < H3MaxIPs;
	    i += sizeof(in6_addr)) {
	  in6_addr addr;
	  for (unsigned j = 0; j < sizeof(addr.s6_addr); j++)
	    addr.s6_addr[j] = v[i + j];
	  https.ipv6Hint[https.nIPv6Hint++] = addr;
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
  const uint8_t *msg, unsigned &off, unsigned rdataEnd,
  const DNSHost &owner, HTTPS &https)
{
  if (off + 2 > rdataEnd) return false;
  https = {};
  https.priority = dnsU16(msg + off);
  off += 2;
  ZtString<> target;
  if (!readDNSName(msg, rdataEnd, off, target)) return false;
  setDNSHost(https.target, target == "." ? ZuCSpan(owner) : ZuCSpan(target));
  if (!parseSvcParams(msg, rdataEnd, off, https)) return false;
  return true;
}

int parseHTTPS(
  ZuBSpan span, DNSHost owner, ZmFn<bool(const HTTPS &)> fn, ZeError *e)
{
  const uint8_t *msg = span.data();
  unsigned msgLen = span.length();
  if (msgLen < DNSHeaderLen) goto invalid;
  {
    unsigned qd = dnsU16(msg + 4);
    unsigned an = dnsU16(msg + 6);
    unsigned ns = dnsU16(msg + 8);
    unsigned ar = dnsU16(msg + 10);
    unsigned off = DNSHeaderLen;
    ZtString<> name;
    for (unsigned i = 0; i < qd; i++) {
      if (!readDNSName(msg, msgLen, off, name) || off + 4 > msgLen)
	goto invalid;
      off += 4;
    }
    unsigned emitted = 0;
    for (unsigned i = 0, n = an + ns + ar; i < n; i++) {
      if (!readDNSName(msg, msgLen, off, name) || off + 10 > msgLen)
	goto invalid;
      uint16_t type = dnsU16(msg + off);
      uint16_t klass = dnsU16(msg + off + 2);
      uint16_t rdlen = dnsU16(msg + off + 8);
      off += 10;
      if (off + rdlen > msgLen) goto invalid;
      unsigned rdataEnd = off + rdlen;
      if ((type == ZiDNSType::HTTPS || type == ZiDNSType::SVCB) &&
	  klass == ZiDNSClass::IN) {
	HTTPS https;
	unsigned rdataOff = off;
	if (!parseHTTPSRData(msg, rdataOff, rdataEnd, owner, https))
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

int resolveDNS(DNSHost host, ZmFn<bool(ZiIP)> fn, ZeError *e)
{
  ZeError error;
  bool ok = ZmBlock<bool>{}([host = ZuMv(host), fn = ZuMv(fn), &error](
      auto wake) mutable {
    ZiResolver::resolve(ZuMv(host),
      ZiResolver_::ResolveFn{[fn = ZuMv(fn), &error, wake](
	  auto result) mutable {
	if (result.template is<ZiResolver_::Event>()) {
	  error = result.template p<ZiResolver_::Event>();
	  wake(false);
	  return false;
	}
	if (result.template is<void>()) return false;
	fn(result.template p<ZiIP>());
	wake(true);
	return false;
      }});
  });
  if (ok) return Zi::OK;
  if (e) *e = error;
  return Zi::IOError;
}

int httpsDNS(DNSHost host, ZmFn<bool(const HTTPS &)> fn, ZeError *e)
{
  ZeError error;
  int rc = ZmBlock<int>{}([host, fn = ZuMv(fn), &error](auto wake) mutable {
    ZiResolver::query(host, ZiDNSType::HTTPS, ZiDNSClass::IN,
      ZiResolver_::QueryFn{[
	host = ZuMv(host), fn = ZuMv(fn), &error, wake](auto result) mutable {
	if (result.template is<ZiResolver_::Event>()) {
	  error = result.template p<ZiResolver_::Event>();
	  wake(Zi::IOError);
	  return;
	}
	auto msg = ZuMv(result).template p<ZiDNSMsg>();
	int rc = parseHTTPS(
	  ZuBSpan{msg.buf.data(), msg.buf.length()}, ZuMv(host), ZuMv(fn),
	  &error);
	wake(rc);
      }});
  });
  if (rc != Zi::OK && e) *e = error;
  return rc;
}

bool emitH3IP(
  const H3Endpoint &ep, ZiIP *seen, unsigned &nSeen,
  ZmFn<bool(const H3Endpoint &)> fn)
{
  for (unsigned i = 0; i < nSeen; i++)
    if (seen[i] == ep.ip) return true;
  if (nSeen < H3MaxIPs) seen[nSeen++] = ep.ip;
  return fn(ep);
}

int http3DNS(
  DNSHost dnsHost, DNSHost tlsHost, uint16_t port, H3Policy::T policy,
  ZmFn<bool(const H3Endpoint &)> fn, ZeError *e)
{
  DNSHost query[H3AliasDepth + 1];
  query[0] = dnsHost;
  bool dnsH3 = false;
  ZiIP seen[H3MaxIPs];
  unsigned nSeen = 0;

  for (unsigned depth = 0; depth <= H3AliasDepth; depth++) {
    HTTPS recs[H3MaxALPN];
    unsigned nRecs = 0;
    ZeError httpsErr;
    int rc = httpsDNS(query[depth],
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
      DNSHost target = rec.target ? rec.target : query[depth];
      uint16_t epPort = rec.port ? rec.port : port;
      for (unsigned j = 0; j < rec.nIPv4Hint; j++) {
	H3Endpoint ep{target, tlsHost, rec.ipv4Hint[j], epPort, true, true};
	if (!emitH3IP(ep, seen, nSeen, fn)) return Zi::OK;
      }
      for (unsigned j = 0; j < rec.nIPv6Hint; j++) {
	H3Endpoint ep{target, tlsHost, rec.ipv6Hint[j], epPort, true, false, true};
	if (!emitH3IP(ep, seen, nSeen, fn)) return Zi::OK;
      }
      ZeError resolveErr;
      resolveDNS(target,
	ZmFn<bool(ZiIP)>{[&target, &tlsHost, epPort, &seen, &nSeen, &fn](
	    ZiIP ip) {
	  H3Endpoint ep{target, tlsHost, ip, epPort, true, false};
	  return emitH3IP(ep, seen, nSeen, fn);
	}}, &resolveErr);
    }
    if (dnsH3) return nSeen ? Zi::OK : Zi::IOError;
    if (!followed) break;
  }

  if (policy == H3Policy::DNSWithBlindFallback) {
    int rc = resolveDNS(dnsHost,
      ZmFn<bool(ZiIP)>{[&dnsHost, &tlsHost, port, &seen, &nSeen, &fn](
	  ZiIP ip) {
	H3Endpoint ep{dnsHost, tlsHost, ip, port, false, false};
	return emitH3IP(ep, seen, nSeen, fn);
      }}, e);
    return rc == Zi::OK && nSeen ? Zi::OK : Zi::IOError;
  }
  if (e) *e = ZeError(EAI_NONAME);
  return Zi::IOError;
}

template <typename Link>
Zquic::RuntimeDiag runtimeDiag(Link *link)
{
  return ZmBlock<Zquic::RuntimeDiag>{}(
    [link](auto wake) { link->runtimeDiag(ZuMv(wake)); });
}

template <typename Link>
Zquic::RuntimeDiag runtimeDiag(const ZmRef<Link> &link)
{
  return runtimeDiag(link.ptr());
}

template <typename Link>
Zquic::EndpointDiag endpointDiag(Link *link)
{
  return ZmBlock<Zquic::EndpointDiag>{}(
    [link](auto wake) { link->endpointDiag(ZuMv(wake)); });
}

template <typename Link>
Zquic::EndpointDiag endpointDiag(const ZmRef<Link> &link)
{
  return endpointDiag(link.ptr());
}

struct Options {
  ZuCSpan	ca;
  ZuCSpan	output{"index.html"};
  bool		discardResponse = false;
  uint32_t	requests = 1;
  uint32_t	concurrency = 1;
  uint32_t	timeout = ClientTimeout;
  uint32_t	stallTimeout = H3StallTimeout;
  uint32_t	quietTimeout = H3QuietTimeout;
  ZuCSpan	keyLog;
  ZuCSpan	url;
  Http3Mode::T	http3 = Http3Mode::prefer;
  bool		verbose = false;
#ifdef ZiMultiplex_DEBUG
  bool		debug = false;
  bool		frag = false;
  bool		yield = false;
#endif
#ifdef ZiMultiplex_FILTER
  ZuCSpan	quicRxDrop;
  ZuCSpan	quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  uint32_t	memDiag = 0;
  bool		help = false;
};

ZtStruct((Options, CLI),
  (((ca),        (CLI::Opt<'c'>,  CLI::Long<"ca">)),         (String)),
  (((output),    (CLI::Opt<'o'>,  CLI::Long<"output">)),     (String, "index.html")),
  (((discardResponse),
    (CLI::Long<"discard-response">)),                         (Bool, false)),
  (((requests),  (CLI::Opt<'n'>,  CLI::Long<"requests">)),   (UInt32, 1)),
  (((concurrency), (CLI::Opt<'j'>, CLI::Long<"jobs">)),       (UInt32, 1)),
  (((timeout),   (CLI::Long<"timeout">)),                    (UInt32, ClientTimeout)),
  (((stallTimeout),
    (CLI::Long<"stall-timeout">)),                            (UInt32, H3StallTimeout)),
  (((quietTimeout),
    (CLI::Long<"quiet-timeout">)),                            (UInt32, H3QuietTimeout)),
  (((keyLog),    (CLI::Long<"key-log">)),                    (String)),
  (((http3),     (Enum<Http3Mode::Map>,
		  CLI::Opt<'3'>, CLI::Long<"http3">)),       (Int8,
								 Http3Mode::prefer)),
  (((verbose),   (CLI::Flag<'v'>, CLI::Long<"verbose">)),    (Bool, false)),
#ifdef ZiMultiplex_DEBUG
  (((debug),     (CLI::Long<"debug">)),                      (Bool, false)),
  (((frag),      (CLI::Long<"frag">)),                       (Bool, false)),
  (((yield),     (CLI::Long<"yield">)),                      (Bool, false)),
#endif
#ifdef ZiMultiplex_FILTER
  (((quicRxDrop), (CLI::Long<"quic-rx-drop">)),              (String)),
  (((quicTxDrop), (CLI::Long<"quic-tx-drop">)),              (String)),
#endif
#ifdef Zquic_DEBUG
  (((quicDiag),   (CLI::Long<"quic-diag">)),                 (UInt32, 0)),
#endif
  (((memDiag),    (CLI::Long<"mem-diag">)),                  (UInt32, 0)),
  (((url),       (CLI::Arg<1>)),                             (String)),
  (((help),      (CLI::Flag<'h'>, CLI::Long<"help">)),       (Bool, false)));

void usage(int code = 1)
{
  std::cerr <<
    "Usage: zhttp [OPTION]... URL\n\n"
    "Options:\n"
    "  -c, --ca=PATH       CA path for https:\n"
    "  -o, --output=PATH   response body output path\n"
    "  --discard-response  discard response bodies instead of writing files\n"
    "  -n, --requests=N    submit N GET requests, default 1\n"
    "  -j, --jobs=M        run up to M requests concurrently, default 1\n"
    "  --timeout=N         completion timeout in seconds, default 15, 0 disables\n"
    "  --stall-timeout=N   no-progress stall timeout in seconds, default 15,\n"
    "                      0 disables; currently applies to HTTP/3\n"
    "  --quiet-timeout=N   quiet transport timeout in seconds, default 2,\n"
    "                      0 disables; currently applies to HTTP/3\n"
    "  --key-log=PATH      append HTTP/3 TLS secrets for tshark/Wireshark;\n"
    "                      defaults to SSLKEYLOGFILE when set\n"
    "  -3, --http3=MODE   HTTP/3 mode for https: force, prefer, disable;\n"
    "                      default prefer\n"
    "  -v, --verbose       show DNS and Alt-Svc probing\n"
#ifdef ZiMultiplex_DEBUG
    "  --debug             enable ZiMultiplex and HTTP/3 debug logging\n"
    "  --frag              fragment ZiMultiplex I/O in debug builds\n"
    "  --yield             yield in ZiMultiplex in debug builds\n"
#endif
#ifdef ZiMultiplex_FILTER
    "  --quic-rx-drop=N%   randomly drop N% of received QUIC UDP packets\n"
    "  --quic-tx-drop=N%   randomly drop N% of transmitted QUIC UDP packets\n"
#endif
#ifdef Zquic_DEBUG
    "  --quic-diag=N       print HTTP/3 QUIC counters every N seconds\n"
#endif
    "  --mem-diag=N        print memory counters every N seconds\n"
    "  -h, --help          show help\n\n"
    "For N > 1, response bodies are written to PATH.0, PATH.1, ...\n" <<
    std::flush;
  ::exit(code);
}

bool parseDrop(ZuCSpan s, double &drop)
{
  drop = 0.0;
  if (!s) return true;
  if (s.length() < 2 || s[s.length() - 1] != '%') return false;
  ZuBox<double> pct;
  if (pct.scan(s.data(), s.length() - 1) != s.length() - 1) return false;
  double v = pct;
  if (v < 0.0 || v > 100.0) return false;
  drop = v * 0.01;
  return true;
}

bool validateOptions(Options &options, int argc)
{
  if (argc < 0 || argc != 2) return false;
  if (!options.requests || !options.concurrency) return false;
  if (options.http3 < 0 || options.http3 >= Http3Mode::N) return false;
#ifdef ZiMultiplex_FILTER
  double drop;
  if (!parseDrop(options.quicRxDrop, drop)) return false;
  if (!parseDrop(options.quicTxDrop, drop)) return false;
#endif
  return true;
}

void printMemDiag()
{
  ZiLOG(Info, "zhttp", ([](auto &s) {
    s << "Hash Tables:\n" << ZmHashMgr::csv();
    s << "Heaps:\n" << ZmHeapMgr::csv();
  }));
}

struct IntervalMonitor {
  bool active() const {
    return timeout || memDiag
#ifdef Zquic_DEBUG
      || quicDiag
#endif
      ;
  }
  unsigned nextStep(unsigned cap = 0) const {
    unsigned step = cap;
    auto limit = [&step](unsigned v) {
      if (!step || v < step) step = v;
    };
    if (timeout) limit(timeout - elapsed);
    if (memDiag) limit(memDiag - memElapsed);
#ifdef Zquic_DEBUG
    if (quicDiag) limit(quicDiag - quicElapsed);
#endif
    return step;
  }
  void advance(unsigned step) {
    if (timeout) elapsed += step;
    if (memDiag) memElapsed += step;
#ifdef Zquic_DEBUG
    if (quicDiag) quicElapsed += step;
#endif
  }
  template <typename MemFn>
  void intervals(MemFn memFn) {
    if (memDiag && memElapsed >= memDiag) {
      memElapsed = 0;
      memFn();
    }
  }
#ifdef Zquic_DEBUG
  template <typename MemFn, typename QuicFn>
  void intervals(MemFn memFn, QuicFn quicFn) {
    if (quicDiag && quicElapsed >= quicDiag) {
      quicElapsed = 0;
      quicFn();
    }
    intervals(memFn);
  }
#endif
  bool timedOut() const { return timeout && elapsed >= timeout; }

  uint32_t	timeout = 0;
  uint32_t	memDiag = 0;
#ifdef Zquic_DEBUG
  uint32_t	quicDiag = 0;
#endif
  unsigned	elapsed = 0;
  unsigned	memElapsed = 0;
#ifdef Zquic_DEBUG
  unsigned	quicElapsed = 0;
#endif
};

bool waitMonitored(ZmSemaphore &sem, uint32_t timeout, uint32_t memDiag)
{
  IntervalMonitor mon{timeout, memDiag
#ifdef Zquic_DEBUG
    , 0
#endif
  };
  if (!mon.active()) {
    sem.wait();
    return true;
  }
  for (;;) {
    unsigned step = mon.nextStep();
    if (!step) return false;
    if (sem.timedwait(Zm::now(step)) == 0) return true;
    mon.advance(step);
    mon.intervals([]() { printMemDiag(); });
    if (mon.timedOut()) return false;
  }
}

ZuDerive(HdrString, ZtString<ZtStringHeapID<"zhttp.HdrString">>);

HdrString outputPath(ZuCSpan base, unsigned reqID, unsigned requests)
{
  HdrString path;
  path << base;
  if (requests > 1) path << '.' << ZuBoxed(reqID);
  return path;
}

using RequestHeaders = ZhttpHeaders(
  "user-agent",
  "accept");
using ResponseHeaders = ZhttpHeaders(
  "alt-svc",
  "connection",
  "location");

struct URL {
  ZuCSpan	scheme;
  HdrString	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  HdrString	target;
};

ZtEnumNS(Protocol, int8_t, H1, H3);

constexpr unsigned H3MaxAttempts = 8;
constexpr unsigned MaxRedirects = 8;
constexpr uint64_t RespBodyMax = 100<<20;
constexpr unsigned BufBuiltin = 8<<10;
constexpr unsigned BufMax = 100<<20;
constexpr uint64_t H3DataMax = 100<<20;
constexpr uint64_t H3StreamDataMax = 16<<20;
constexpr uint64_t H3BidiMax = 100;
constexpr uint64_t H3UniMax = 16;

using H3CxnState = Zhttp::H3::CxnState;

ZuDerive(H3Ticket, (ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.H3Ticket">>));

struct H3EarlySession {
  H3Ticket		ticket;
  Zquic::TransportParams params;
};

struct Req {
  unsigned	id = 0;
  unsigned	requests = 1;
  URL		url;
  Protocol::T	protocol = Protocol::H1;
  H3CxnState::T h3State = Zhttp::H3::CxnState::Init;
  HdrString	output;
  bool		discardResponse = false;
  bool		logResponse = false;
  HdrString	altSvcHost;
  uint16_t	altSvcPort = 0;
  HdrString	location;
  int64_t	responseStreamID = -1;
  unsigned	redirects = 0;
  unsigned	status = 0;
  ZiFile	bodyFile;
  int64_t	contentLength = -1;
  uint64_t	bodyBytes = 0;
  unsigned	bodyChunks = 0;
  bool		bodyFileOpen = false;
  bool		chunked = false;
  bool		connectionClose = false;
  bool		connectionKeepAlive = false;
  bool		http10 = false;
  bool		closeDelimited = false;
  bool		altSvcH3 = false;
  bool		redirecting = false;
  bool		framingLogged = false;
  bool		truncateOutput = false;
  bool		h3Active = false;
  bool		h3EarlyData = false;
  bool		done = false;
  bool		failed = false;
  uint64_t	h3RxBytes = 0;
  uint64_t	h3FinalSize = 0;
  unsigned	h3RxPending = 0;
  unsigned	h3RxQueued = 0;
  bool		h3FinReceived = false;
};

using State = Req;

bool hotLog(const Options &options)
{
  return options.verbose
#ifdef ZiMultiplex_DEBUG
    || options.debug
#endif
    ;
}

struct ReqLogCtx {
  unsigned	id = 0;
  unsigned	requests = 1;
};

inline ReqLogCtx reqLogCtx(const Req &req)
{
  return {req.id, req.requests};
}

struct AltSvcEndpoint {
  HdrString	host;
  Zi::Hostname	dnsHost;
  uint16_t	port = 0;
  bool		h3 = false;
};

struct Origin {
  HdrString	scheme;
  HdrString	host;
  uint16_t	port = 0;

  bool equals(const Origin &o) const {
    return scheme == o.scheme && host == o.host && port == o.port;
  }
  friend bool operator ==(const Origin &l, const Origin &r) {
    return l.equals(r);
  }
  uint32_t hash() const {
    uint32_t h = ZuHash<HdrString>::hash(scheme);
    h ^= ZuHash<HdrString>::hash(host) + 0x9e3779b9U + (h<<6) + (h>>2);
    h ^= ZuHash<uint16_t>::hash(port) + 0x9e3779b9U + (h<<6) + (h>>2);
    return h;
  }
};

struct OriginDiscovery {
  bool		dnsChecked = false;
  bool		dnsH3 = false;
  AltSvcEndpoint dnsEndpoint;
  bool		altSvcChecked = false;
  bool		altSvcH3 = false;
  AltSvcEndpoint altSvcEndpoint;
};

using DiscoveryCache = ZmHashKV<Origin, OriginDiscovery,
  ZmHashHeapID<"Zhttp.Discovery">>;
using DiscoveryCacheRef = ZmRef<DiscoveryCache>;

struct Run {
  Options	options;
  URL		originalURL;
  ZtArray<Req, ZtArrayHeapID<"Zhttp.Req">> reqs;
  DiscoveryCacheRef discovery{new DiscoveryCache};
  H3EarlySession	h3EarlySession;
  ZmSemaphore	done;
  unsigned	scheduled = 0;
  unsigned	active = 0;
  unsigned	complete = 0;
  unsigned	failed = 0;
  bool		fatal = false;
  bool		preloadedReqs = false;
};

struct RequestResult {
  unsigned	status = 0;
  HdrString	location;
  AltSvcEndpoint altSvc;
};

void resultFromReq(RequestResult *result, const Req &req)
{
  if (!result) return;
  *result = {};
  result->status = req.status;
  result->location = req.location;
  if (req.altSvcH3) {
    result->altSvc.host = req.altSvcHost;
    result->altSvc.dnsHost = req.url.dnsHost;
    result->altSvc.port = req.altSvcPort;
    result->altSvc.h3 = true;
  }
}

void setHost(URL &url, ZuCSpan host)
{
  url.host = host;
#ifndef _WIN32
  url.dnsHost = host;
#else
  url.dnsHost.length(ZuUTF<wchar_t, char>::cvt(
    ZuSpan<wchar_t>(url.dnsHost.data(), url.dnsHost.size() - 1), host));
#endif
}

bool parseAuthority(
  ZuCSpan authority, ZuCSpan &host, ZuCSpan &port, ZuCSpan &error)
{
  port = {};
  if (!authority) {
    error = "missing URL host";
    return false;
  }
  if (authority[0] == '[') {
    ZuCSpan rest = authority;
    rest.offset(1);
    auto close = rest.find([](auto c) { return c == ']'; });
    if (close < 0) {
      error = "invalid URL authority";
      return false;
    }
    host = rest;
    host.trunc(close);
    rest.offset(close + 1);
    if (!host) {
      error = "invalid URL authority";
      return false;
    }
    if (!rest) return true;
    if (rest[0] != ':') {
      error = "invalid URL authority";
      return false;
    }
    port = rest;
    port.offset(1);
    if (!port) {
      error = "invalid URL authority";
      return false;
    }
    return true;
  }

  for (unsigned i = 0; i < authority.length(); ++i) {
    if (authority[i] != ':') continue;
    host = authority;
    host.trunc(i);
    port = authority;
    port.offset(i + 1);
    if (!host || !port) {
      error = "invalid URL authority";
      return false;
    }
    return true;
  }

  host = authority;
  return true;
}

bool parseURL(ZuCSpan input, URL &url, ZeException *error = nullptr)
{
  auto fail = [error](ZuCSpan msg) {
    if (error) *error = ZeEXCEPT(Error, "Zhttp", msg);
    return false;
  };

  ZuCSpan rest;
  if (input.match("http://")) {
    url.scheme = "http";
    url.port = 80;
    rest = input;
    rest.offset(7);
  } else if (input.match("https://")) {
    url.scheme = "https";
    url.port = 443;
    rest = input;
    rest.offset(8);
  } else
    return fail("unsupported URL scheme");

  auto slash = rest.find([](auto c) { return c == '/'; });
  if (slash < 0) slash = rest.length();

  ZuCSpan authority = rest;
  authority.trunc(slash);
  ZuCSpan target = rest;
  target.offset(slash);
  if (!target) target = "/";

  ZuCSpan host, port, msg;
  if (!parseAuthority(authority, host, port, msg)) return fail(msg);
  if (port) {
    unsigned p = ZuBox<unsigned>(port);
    if (!p || p > 65535) return fail("invalid URL port");
    setHost(url, host);
    url.port = p;
    url.target = target;
    return true;
  }

  setHost(url, host);
  url.target = target;
  return true;
}

void splitTarget(ZuCSpan target, ZuCSpan &path, ZuCSpan &query)
{
  path = target;
  query = {};
  if (auto i = target.find([](auto c) { return c == '?'; }); i >= 0) {
    path.trunc(i);
    query = target;
    query.offset(i + 1);
  }
  if (!path) path = "/";
}

bool parsePort(ZuCSpan s, uint16_t &port)
{
  unsigned p = ZuBox<unsigned>(s);
  if (!p || p > 65535) return false;
  port = p;
  return true;
}

bool parseAltSvc(State &state, ZuCSpan value)
{
  if (value.match("clear")) return false;
  int h = -1;
  for (unsigned i = 0; i + 3 <= value.length(); ++i) {
    if (value[i] == 'h' && value[i + 1] == '3' && value[i + 2] == '=') {
      h = i;
      break;
    }
  }
  if (h < 0) return false;
  ZuCSpan rest = value;
  rest.offset(h + 3);
  if (!rest || rest[0] != '"') return false;
  rest.offset(1);
  int q = rest.find([](auto c) { return c == '"'; });
  if (q < 0) return false;
  ZuCSpan authority = rest;
  authority.trunc(q);
  if (!authority) return false;

  ZuCSpan host = state.url.host;
  uint16_t port = state.url.port;
  if (authority[0] == ':') {
    ZuCSpan port_ = authority;
    port_.offset(1);
    if (!parsePort(port_, port)) return false;
  } else {
    ZuCSpan port_, msg;
    if (!parseAuthority(authority, host, port_, msg)) return false;
    if (port_ && !parsePort(port_, port)) return false;
  }

  state.altSvcHost = host;
  state.altSvcPort = port;
  state.altSvcH3 = true;
  return true;
}

void closeBody(Req &req)
{
  if (req.bodyFileOpen) {
    req.bodyFile.close();
    req.bodyFileOpen = false;
  }
}

void resetAttempt(Req &req, bool truncateOutput)
{
  closeBody(req);
  req.altSvcHost.length(0);
  req.altSvcPort = 0;
  req.location.length(0);
  req.responseStreamID = -1;
  req.status = 0;
  req.contentLength = -1;
  req.bodyBytes = 0;
  req.bodyChunks = 0;
  req.chunked = false;
  req.connectionClose = false;
  req.connectionKeepAlive = false;
  req.http10 = false;
  req.closeDelimited = false;
  req.altSvcH3 = false;
  req.redirecting = false;
  req.framingLogged = false;
  req.truncateOutput = truncateOutput;
  req.done = false;
  req.failed = false;
  req.h3RxBytes = 0;
  req.h3FinalSize = 0;
  req.h3RxPending = 0;
  req.h3RxQueued = 0;
  req.h3FinReceived = false;
  req.h3EarlyData = false;
}

void initReq(Req &req, const Run &run, unsigned id)
{
  req = {};
  req.id = id;
  req.requests = run.options.requests;
  req.url = run.originalURL;
  if (!run.options.discardResponse)
    req.output = outputPath(run.options.output, id, run.options.requests);
  req.discardResponse = run.options.discardResponse;
  req.logResponse = hotLog(run.options);
}

template <typename S>
void reqLogPrefix(const Req &req, S &s)
{
  if (req.requests > 1) s << "req=" << req.id << ' ';
}
template <typename S>
void reqLogPrefix(const ReqLogCtx &ctx, S &s)
{
  if (ctx.requests > 1) s << "req=" << ctx.id << ' ';
}

bool truncateOutputPath(Req &req)
{
  if (req.discardResponse) return true;
  if (!req.truncateOutput) return true;
  ZiFile f{req.output, ZiFile::Write | ZiFile::GC};
  if (!f) {
    auto ctx = reqLogCtx(req);
    ZiLOG(Error, "zhttp", ([ctx, output = ZeString(req.output)](auto &s) {
      reqLogPrefix(ctx, s);
      s << "failed to open " << output;
    }));
    req.failed = true;
    req.done = true;
    return false;
  }
  f.close();
  req.truncateOutput = false;
  return true;
}

bool redirectStatus(unsigned status)
{
  return status == 301 || status == 302 || status == 303 ||
    status == 307 || status == 308;
}

bool parseLocation(const URL &base, ZuCSpan location, URL &url)
{
  ZeException error;
  if (location.match("http://") || location.match("https://"))
    return parseURL(location, url, &error);
  url = base;
  if (location.match("//")) {
    HdrString absolute;
    absolute << base.scheme << ':' << location;
    return parseURL(absolute, url, &error);
  }
  if (location.match("/")) {
    url.target = location;
    return true;
  }

  ZuCSpan target{base.target};
  auto q = target.find([](auto c) { return c == '?'; });
  if (q >= 0) target.trunc(q);
  int slash = -1;
  for (unsigned i = 0; i < target.length(); ++i)
    if (target[i] == '/') slash = i;
  HdrString next;
  if (slash >= 0) {
    ZuCSpan dir = target;
    dir.trunc(slash + 1);
    next << dir;
  } else
    next << '/';
  next << location;
  url.target = next;
  return true;
}

struct RequestOps {
  RequestOps(const State &state_) : state{&state_} { }

  template <typename L>
  void operation(L &&l) {
    ZuCSpan path;
    ZuCSpan query;
    splitTarget(state->url.target, path, query);
    l(Zhttp::Method::GET, path, query);
  }
  template <typename L>
  void host(L &&l) { l(ZuCSpan{state->url.host}); }
  template <typename Key, typename L>
  void header(L &&l) {
    if constexpr (Key{}() == "user-agent")
      l("zhttp/1.0");
    else if constexpr (Key{}() == "accept")
      l("*/*");
    else
      l("");
  }

  const State *state = nullptr;
};

template <template <typename, typename> typename Builder>
struct RequestBuilder :
  public Builder<RequestBuilder<Builder>, RequestHeaders>,
  public RequestOps
{
  using Base = Builder<RequestBuilder<Builder>, RequestHeaders>;

  RequestBuilder(const State &state_) : RequestOps{state_} { }

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;
};
template <typename Impl, typename Headers>
using H1RequestBuilder_ = Zhttp::H1ReqBuilder<Impl, Headers>;
using H1RequestBuilder = RequestBuilder<H1RequestBuilder_>;

ZuDerive(H3ReqPayload,
  (ZtArray<uint8_t, ZtArrayHeapID<"zhttp.H3ReqPayload">>));

struct H3ReqTx {
  H3ReqTx(H3ReqPayload &payload_) : payload{&payload_} { }

  H3ReqTx &operator <<(ZuBSpan span) {
    for (unsigned i = 0; i < span.length(); ++i)
      payload->push(uint8_t(span[i]));
    return *this;
  }
  H3ReqTx &operator <<(char c) {
    payload->push(uint8_t(c));
    return *this;
  }
  void flush() { }

private:
  H3ReqPayload	*payload = nullptr;
};

template <typename H3Cxn_>
struct H3RequestBuilder :
  public Zhttp::H3ReqBuilder<H3RequestBuilder<H3Cxn_>, RequestHeaders>,
  public RequestOps
{
  using Base = Zhttp::H3ReqBuilder<H3RequestBuilder<H3Cxn_>, RequestHeaders>;
  using H3Cxn = H3Cxn_;

  H3RequestBuilder(const State &state_, H3Cxn &h3_, uint64_t streamID_) :
    RequestOps{state_}, h3{&h3_}, streamID_{streamID_} { }

  H3Cxn &h3Cxn() const { return *h3; }
  uint64_t streamID() const { return streamID_; }

  using RequestOps::host;
  using RequestOps::header;
  using RequestOps::operation;

  H3Cxn		*h3 = nullptr;
  uint64_t	streamID_ = 0;
};

template <typename StreamRef>
void sendH1Request(State &state, StreamRef stream)
{
  auto tx = stream->txStream();
  H1RequestBuilder builder{state};
  builder.request(tx);
  builder.finish(tx);
}

template <typename StreamRef>
void sendH3Request(State &state, StreamRef stream)
{
  if (state.logResponse)
    ZiLOG(Debug, "zhttp.h3", ([
      reqID = state.id, requests = state.requests,
      target = ZeString(state.url.target), id = stream->id()
    ](auto &s) mutable {
      if (requests > 1) s << "req=" << reqID << ' ';
      s << "send request stream=" << id << " target=" << target;
    }));
  using H3Cxn = ZuDecay<decltype(stream->link()->h3)>;
  H3RequestBuilder<H3Cxn> builder{
    state, stream->link()->h3, uint64_t(stream->id())};
  H3ReqPayload payload;
  H3ReqTx tx{payload};
  builder.request(tx);
  builder.finish(tx);
  if (!payload.length() || !stream->link()->send(stream, payload, true))
    ZiLOG(Error, "zhttp.h3", ([
      reqID = state.id, id = stream->id(), bytes = payload.length(),
      tx = stream->link()->app()->txInvoked()
    ](auto &s) {
      s << "send request req=" << reqID <<
	" stream=" << id <<
	" bytes=" << bytes <<
	" tx=" << int(tx);
    }));
}

void logFraming(State &state)
{
  if (state.framingLogged) return;
  state.framingLogged = true;
  if (!state.logResponse) return;
  auto ctx = reqLogCtx(state);
  auto chunked = state.chunked;
  auto contentLength = state.contentLength;
  ZiLOG(Info, "zhttp.response", ([ctx, chunked, contentLength](auto &s) {
    reqLogPrefix(ctx, s);
    s << "framing: ";
    if (chunked)
      s << "chunked";
    else if (contentLength >= 0)
      s << "content-length=" << contentLength;
    else
      s << "no content-length";
  }));
}

template <typename Link>
struct ResponseSink {
  ResponseSink() = default;
  ResponseSink(Link *link_, State *state_) : link{link_}, state{state_} { }
  void bind(Link *link_, State *state_) {
    link = link_;
    state = state_;
  }

  void status(unsigned status) {
    state->status = status;
    state->redirecting = redirectStatus(status);
    if (!state->redirecting && !state->discardResponse &&
	!truncateOutputPath(*state)) return;
    if (!state->logResponse) return;
    auto ctx = reqLogCtx(*state);
    ZiLOG(Info, "zhttp.response", ([ctx, status](auto &s) {
      reqLogPrefix(ctx, s);
      s << "status: " << status;
    }));
  }
  void contentLength(uint64_t contentLength) {
    state->contentLength = contentLength;
  }
  void chunked() { state->chunked = true; }
  void version(ZuBSpan version) {
    state->http10 = ZuCSpan(version) == "HTTP/1.0";
  }

  template <typename Key>
  void header(ZuBSpan value) {
    if constexpr (Key{}() == "alt-svc") {
      if (parseAltSvc(*state, ZuCSpan(value))) {
	if (state->logResponse) {
	  auto ctx = reqLogCtx(*state);
	  ZeString host;
	  host << state->altSvcHost;
	  auto port = state->altSvcPort;
	  ZiLOG(Info, "zhttp.response", ([ctx, host = ZuMv(host), port](auto &s) {
	    reqLogPrefix(ctx, s);
	    s << "alt-svc: h3=\"" << host << ':' << port << '"';
	  }));
	}
      }
    } else if constexpr (Key{}() == "connection") {
      if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "close"))
	state->connectionClose = true;
      else if (ZuICmp<ZuCSpan>::equals(ZuCSpan(value), "keep-alive"))
	state->connectionKeepAlive = true;
    } else if constexpr (Key{}() == "location") {
      state->location = ZuCSpan(value);
    }
    if (!state->logResponse) return;
    auto ctx = reqLogCtx(*state);
    ZeString value_;
    value_ << ZuCSpan(value);
    ZiLOG(Info, "zhttp.response", ([ctx, value = ZuMv(value_)](auto &s) {
      reqLogPrefix(ctx, s);
      s << "header " << Key{}() << ": " << value;
    }));
  }

  void body(ZuBSpan span) {
    logFraming(*state);
    if (!span || state->redirecting) return;
    state->bodyBytes += span.length();
    ++state->bodyChunks;
    if (state->discardResponse) return;
    if (!truncateOutputPath(*state)) return;
    if (!state->bodyFileOpen) {
      state->bodyFile = ZiFile(state->output, ZiFile::Write | ZiFile::GC);
      if (!state->bodyFile) {
	auto ctx = reqLogCtx(*state);
	ZiLOG(Error, "zhttp", ([ctx, output = ZeString(state->output)](auto &s) {
	  reqLogPrefix(ctx, s);
	  s << "failed to open " << output;
	}));
	state->failed = true;
	state->done = true;
	return;
      }
      state->bodyFileOpen = true;
    }
    if (state->bodyFile.write(span.data(), span.length()) != Zi::OK) {
      auto ctx = reqLogCtx(*state);
      ZiLOG(Error, "zhttp", ([ctx](auto &s) {
	reqLogPrefix(ctx, s);
	s << "failed to write body chunk";
      }));
      state->failed = true;
      state->done = true;
      return;
    }
  }

  template <typename ParserState>
  void complete(typename ParserState::T parserState) {
    if (state->done) return;
    auto ctx = reqLogCtx(*state);
    if (parserState == ParserState::Complete) {
      if (state->logResponse) {
	auto bodyBytes = state->bodyBytes;
	auto bodyChunks = state->bodyChunks;
	ZiLOG(Info, "zhttp.response", ([ctx, bodyBytes, bodyChunks](auto &s) {
	  reqLogPrefix(ctx, s);
	  s << "body complete: " << bodyBytes << " bytes in " << bodyChunks <<
	    " chunks";
	}));
      }
    } else
      ZiLOG(Error, "zhttp.response", ([ctx, parserState](auto &s) {
	reqLogPrefix(ctx, s);
	s << "response " << parserState;
      }));
    if (parserState != ParserState::Complete) state->failed = true;
    closeBody(*state);
    state->done = true;
    link->responseComplete(state, parserState == ParserState::Complete);
  }

  Link		*link = nullptr;
  State		*state = nullptr;
};

template <
  typename Link,
  template <typename, bool, typename, uint64_t> typename Parser>
struct ResponseParser :
  public Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, RespBodyMax>,
  public ResponseSink<Link> {
  using ParserBase =
    Parser<ResponseParser<Link, Parser>, false, ResponseHeaders, RespBodyMax>;
  using SinkBase = ResponseSink<Link>;
  using State = typename ParserBase::State;

  ResponseParser() = default;
  ResponseParser(Link *link_, ::State *state_) : SinkBase{link_, state_} { }
  void bind(Link *link_, ::State *state_) { SinkBase::bind(link_, state_); }

  auto &h3Cxn() const { return this->link->h3; }
  uint64_t streamID() const {
    return uint64_t(this->state->responseStreamID);
  }
  void complete(typename State::T state) {
    SinkBase::template complete<State>(state);
  }

  using SinkBase::body;
  using SinkBase::chunked;
  using SinkBase::contentLength;
  using SinkBase::header;
  using SinkBase::status;
  using SinkBase::version;
};

template <typename Impl, bool Request, typename Headers, uint64_t MaxBody>
using H1ResponseParser_ =
  Zhttp::H1RespParser<Impl, Headers, MaxBody>;
template <typename Link>
using H1ResponseParser = ResponseParser<Link, H1ResponseParser_>;

template <typename Impl, bool Request, typename Headers, uint64_t MaxBody>
using H3ResponseParser_ =
  Zhttp::H3RespParser<Impl, Headers, MaxBody>;
template <typename Link>
using H3ResponseParser = ResponseParser<Link, H3ResponseParser_>;

template <typename Link, bool H3> struct ResponseParser_;
template <typename Link> struct ResponseParser_<Link, false> {
  using T = H1ResponseParser<Link>;
};
template <typename Link> struct ResponseParser_<Link, true> {
  using T = H3ResponseParser<Link>;
};

template <bool H3, typename Link, typename Stream>
int processResponse(Link &link, State &state, Stream &rx)
{
  using Parser = typename ResponseParser_<Link, H3>::T;
  auto parserState = link.parser.process(rx);
  if (parserState == Parser::State::Error) return -1;
  if (parserState == Parser::State::Complete) return 1;
  if (state.done) return -1;
  if (parserState != Parser::State::Complete) return 0;
  return 1;
}

ZuCSpan transportName(Zi::Transport::T transport)
{
  if (transport == Zi::Transport::TLS) return "TLS";
  if (transport == Zi::Transport::QUIC) return "QUIC";
  return "TCP";
}

void logConnected(const State &state, Zi::Connected info)
{
  ZiLOG(Info, "zhttp", ([
    transport = info.transport,
    host = ZeString(state.url.host),
    version = info.version,
    alpn = ZeString(info.alpn)
  ](auto &s) {
    s << transportName(transport) << " connected (hostname: " << host;
    if (version) s << " version: " << version;
    if (alpn) s << " ALPN: " << alpn;
    s << ')';
  }));
}

template <typename App_, typename Base_, bool H3_ = false>
struct CliLink : public Base_ {
  using App = App_;
  using Base = Base_;
  enum { H3 = H3_ };
  using Parser = typename ResponseParser_<CliLink, H3>::T;

  using H3Cxn = Zhttp::H3::Cxn<CliLink, typename Base::StreamRef>;

  CliLink(App *app) : Base{app}, parser{this, &app->state} { }

  void connected(Zi::Connected info) {
    logConnected(this->app()->state, info);
    typename Base::StreamRef stream;
    if constexpr (H3) {
      if (!h3.openLocal(*this)) {
	this->app()->state.failed = true;
	this->app()->done();
	return;
      }
      stream = this->stream(Zi::StreamType::Duplex);
      this->app()->state.responseStreamID = stream ? stream->id() : -1;
    } else
      stream = this->stream();
    if (!stream) {
      this->app()->state.failed = true;
      this->app()->done();
      return;
    }
    if constexpr (H3)
      sendH3Request(this->app()->state, stream);
    else
      sendH1Request(this->app()->state, stream);
  }
  void disconnected(bool) {
    ZiLOG(Info, "zhttp", "disconnected");
    if (!this->app()->state.done) {
      if constexpr (!H3)
	parser.eof();
      if (!this->app()->state.done) this->app()->state.failed = true;
    }
    this->app()->done();
  }
  void connectFailed(bool transient) {
    ZiLOG(Error, "zhttp", ([transient](auto &s) {
      s << "failed to connect";
      if (transient) s << " (transient)";
    }));
    this->app()->state.failed = true;
    this->app()->done();
  }
  void responseComplete(State *, bool) { this->disconnect(); }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<H3>(*this, this->app()->state, rx);
  }

  Parser		parser;
  H3Cxn			h3;
};

template <
  typename App,
  template <typename> class Client_,
  template <typename, typename, typename, typename> class Link__>
struct Client : public Client_<App> {
  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  using RxBufAlloc = Ztcp::RxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  using TxBufAlloc = Ztcp::TxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  template <typename Impl>
  using Link_ = Link__<App, Impl, RxBufAlloc, TxBufAlloc>;
  struct Link : public CliLink<App, Link_<Link>> {
    using Base = CliLink<App, Link_<Link>>;
    using Base::Base;
  };

  ZmSemaphore sem;
  State state;

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }
};

struct TCPClient : public Client<TCPClient, Ztcp::Client, Ztcp::CliLink> { };
struct TLSClient : public Client<TLSClient, Ztls::Client, Ztls::CliLink> { };

template <typename Client>
bool h1TransportOK(const URL &url)
{
  if constexpr (Client::Transport == Zi::Transport::TCP)
    return url.scheme == "http";
  else
    return url.scheme == "https";
}

bool sameOrigin(const URL &a, const URL &b)
{
  return a.scheme == b.scheme && a.host == b.host && a.port == b.port;
}

Origin originOf(const URL &url)
{
  Origin origin;
  origin.scheme = url.scheme;
  origin.host = url.host;
  origin.port = url.port;
  return origin;
}

OriginDiscovery &discoveryFor(Run &run, const URL &url)
{
  Origin origin = originOf(url);
  if (auto node = run.discovery->find(origin)) return node->val();
  run.discovery->add(origin, OriginDiscovery{});
  return run.discovery->find(origin)->val();
}

template <typename App_, typename Base_>
struct H1PoolLink : public Base_ {
  using App = App_;
  using Base = Base_;
  using Parser = H1ResponseParser<H1PoolLink>;

  H1PoolLink(App *app_, unsigned id_) : Base{app_}, id{id_} {
    parser.bind(this, nullptr);
  }

  void assign(Req *req_) {
    req = req_;
    retryConnects = 0;
    if (!req) return;
    resetAttempt(*req, true);
    parser.bind(this, req);
    parser.reset();
  }
  void sendCurrent() {
    if (!req) return;
    parser.bind(this, req);
    parser.reset();
    sendH1Request(*req, this->stream());
  }
  void connected(Zi::Connected info) {
    if (req) logConnected(*req, info);
    if (!this->app()->running()) {
      this->disconnect_();
      return;
    }
    sendCurrent();
  }
  void disconnected(bool) {
    ZiLOG(Info, "zhttp", ([id = id](auto &s) {
      s << "worker=" << id << " disconnected";
    }));
    if (!this->app()->running() || !req) {
      closing = false;
      this->app()->workerStopped(this->impl());
      return;
    }
    if (closing) {
      closing = false;
      this->connect(req->url.host, req->url.port);
      return;
    }
    if (req && !req->done) {
      if (!req->status && retryConnects < 1) {
	++retryConnects;
	parser.bind(this, req);
	parser.reset();
	connectCurrent();
	return;
      }
      req->closeDelimited = true;
      parser.eof();
      if (!req || req->done) return;
      req->failed = true;
      req->done = true;
      responseComplete(req, false);
    }
  }
  void connectFailed(bool transient) {
    this->app()->rxRun([
      link = ZmMkRef(this->impl()), transient
    ]() mutable {
      link->connectFailed_(transient);
    });
  }
  void connectFailed_(bool transient) {
    ZiLOG(Error, "zhttp", ([id = id, transient](auto &s) {
      s << "worker=" << id << " failed to connect";
      if (transient) s << " (transient)";
    }));
    if (!this->app()->running()) {
      this->app()->workerStopped(this->impl());
      return;
    }
    if (req) {
      req->failed = true;
      req->done = true;
      responseComplete(req, false);
    } else
      this->app()->workerIdle(this->impl(), false);
  }
  void responseComplete(State *state, bool ok) {
    if (!req || state != req) return;
    ok = ok && !req->failed;
    if (ok && req->redirecting) {
      URL next;
      if (!req->location || req->redirects >= MaxRedirects ||
	  !parseLocation(req->url, req->location, next) ||
	  !h1TransportOK<App>(next)) {
	req->failed = true;
	ok = false;
      } else {
	URL prev = req->url;
	req->url = ZuMv(next);
	++req->redirects;
	bool reuse = reusable() && sameOrigin(prev, req->url);
	resetAttempt(*req, true);
	if (reuse)
	  sendCurrent();
	else if (this->cxn()) {
	  closing = true;
	  this->disconnect_();
	} else
	  connectCurrent();
	return;
      }
    }
    bool reuse = ok && reusable();
    this->app()->finishReq(req, ok);
    req = this->app()->nextReq(id);
    if (!req) {
      if (this->cxn())
	this->disconnect_();
      else
	this->app()->workerStopped(this->impl());
      return;
    }
    assign(req);
    if (reuse)
      sendCurrent();
    else if (this->cxn()) {
      closing = true;
      this->disconnect_();
    } else
      connectCurrent();
  }
  template <typename Rx>
  int process(Rx &rx) {
    return processResponse<false>(*this, *req, rx);
  }
  bool reusable() const {
    return req && !req->connectionClose && !req->closeDelimited &&
      (!req->http10 || req->connectionKeepAlive) && !req->failed;
  }
  void connectCurrent() {
    if (!req) return;
    this->connect(req->url.host, req->url.port);
  }

  unsigned	id = 0;
  Req		reqSlot;
  Req		*req = nullptr;
  bool		closing = false;
  bool		stopped = false;
  unsigned	retryConnects = 0;
  Parser	parser;
};

template <
  typename App,
  template <typename> class Client_,
  template <typename, typename, typename, typename> class Link__>
struct H1PoolClient : public Client_<App> {
  auto impl() const { return static_cast<const App *>(this); }
  auto impl() { return static_cast<App *>(this); }

  using BaseClient = Client_<App>;

  using RxBufAlloc = Ztcp::RxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  using TxBufAlloc = Ztcp::TxBufAlloc<BufBuiltin, BufMax, "Zhttp.Buf">;
  template <typename Impl>
  using Link_ = Link__<App, Impl, RxBufAlloc, TxBufAlloc>;
  struct Link : public H1PoolLink<App, Link_<Link>> {
    using Base = H1PoolLink<App, Link_<Link>>;
    Link(App *app, unsigned id) : Base{app, id} { }
  };

  Run			*run = nullptr;
  ZtArray<ZmRef<Link>, ZtArrayHeapID<"Zhttp.H1Worker">> links;
  ZmSemaphore		sem;
  unsigned		next = 0;
  unsigned		complete = 0;
  unsigned		failed = 0;
  unsigned		stopped = 0;

  Req *nextReq(unsigned slot) {
    if (!run || next >= run->options.requests) return nullptr;
    auto &req = links[slot]->reqSlot;
    initReq(req, *run, next++);
    return &req;
  }
  void finishReq(Req *req, bool ok) {
    if (!req) return;
    closeBody(*req);
    req->failed = !ok;
    ++complete;
    if (!ok) ++failed;
  }
  void workerIdle(Link *link, bool reconnect) {
    Req *req = nextReq(link->id);
    if (!req) {
      link->disconnect();
      return;
    }
    link->assign(req);
    if (reconnect)
      link->connectCurrent();
    else
      link->sendCurrent();
  }
  void workerStopped(Link *link) {
    if (link) {
      if (link->stopped) return;
      link->stopped = true;
    }
    ++stopped;
    if ((!this->running() || complete >= run->options.requests) &&
	stopped >= links.length())
      sem.post();
    if (!this->running() && stopped >= links.length())
      BaseClient::stop_();
  }
  void stop_() {		// client thread - disconnect links before base stop
    if (!links.length() || stopped >= links.length()) {
      BaseClient::stop_();
      return;
    }
    for (unsigned i = 0; i < links.length(); ++i) {
      auto link = links[i];
      if (!link) continue;
      link->disconnect();
    }
  }
  unsigned reconnFreq() const { return 0; }
};

struct H1TCPClient :
  public H1PoolClient<H1TCPClient, Ztcp::Client, Ztcp::CliLink> { };
struct H1TLSClient :
  public H1PoolClient<H1TLSClient, Ztls::Client, Ztls::CliLink> { };

struct QUICClient : public Zquic::Client<QUICClient> {
  struct Link;
  struct Stream;

#ifdef ZiMultiplex_FILTER
  double m_rxDrop = 0.0;
  double m_txDrop = 0.0;
  ZmRandom m_rng;
#endif

  ZmSemaphore sem;
  State state;
  Run *run = nullptr;
  H3EarlySession *h3Session = nullptr;
  ZmLock lock;
  ZmAtomic<int> up = 0;
  unsigned scheduled = 0;
  unsigned active = 0;
  unsigned complete = 0;
  unsigned failed = 0;
  bool linkFailed = false;
  ZtArray<Req *, ZtArrayHeapID<"Zhttp.H3Pending">> pending;
  unsigned pendingHead = 0;

  void done() { sem.post(); }
  unsigned reconnFreq() const { return 0; }
  void dropRates(const Options &options) {
#ifdef ZiMultiplex_FILTER
    parseDrop(options.quicRxDrop, m_rxDrop);
    parseDrop(options.quicTxDrop, m_txDrop);
#else
    (void)options;
#endif
  }
  void filters() {
#ifdef ZiMultiplex_FILTER
    auto mx = this->mx();
    ZiAssert(mx, "zhttp", (), "QUIC client filters before initialization",
      return);
    if (m_rxDrop)
      mx->rxFilter(FilterFn{this, [](QUICClient *client,
	  ZiConnection *cxn, uint8_t *data, unsigned len) {
	(void)data; (void)len;
	if (ZuUnlikely(!cxn->info().options.udp())) return false;
	return client->m_rng.rand() < client->m_rxDrop;
      }});
    if (m_txDrop)
      mx->txFilter(FilterFn{this, [](QUICClient *client,
	  ZiConnection *cxn, uint8_t *data, unsigned len) {
	(void)data; (void)len;
	if (ZuUnlikely(!cxn->info().options.udp())) return false;
	return client->m_rng.rand() < client->m_txDrop;
      }});
#endif
  }
  void clearFilters() {
#ifdef ZiMultiplex_FILTER
    auto mx = this->mx();
    if (!mx) return;
    if (m_rxDrop) mx->rxFilter({});
    if (m_txDrop) mx->txFilter({});
#endif
  }
  void printDiag(Link *, ZuCSpan);
  uint64_t maxStreamsBidi() const {
    ZiAssert(run, "zhttp", (), "QUIC client stream limit without run",
      return H3BidiMax);
    return run->options.concurrency > H3BidiMax ?
      run->options.concurrency : H3BidiMax;
  }
  uint64_t maxStreamsUni() const { return H3UniMax; }
  H3EarlySession *earlySession_() {
    ZiAssert(h3Session, "zhttp", (), "QUIC client early data without session",
      return nullptr);
    return h3Session;
  }
  void openH3Streams(Link *);
  void openH3Streams_(Link *);
  bool activateH3Req(Req *);
  void sendH3Req(Link *, ZmRef<Stream>, Req *);
  void sendH3Req_(Link *, ZmRef<Stream>, Req *);
  void queueH3Req(Req *);
  Req *popH3Req();
  void bindH3Stream(Link *, ZmRef<Stream>);
  void bindH3Stream_(Link *, ZmRef<Stream>);
  void finishH3Req(Link *, Req *, bool);
  void finishH3Req_(Link *, Req *, bool);
  void failH3Link();
  bool earlyData(
    Link *, ZuBSpan &, const Zquic::TransportParams *&, ZuBSpan &);
  void saveEarlyData(Link *, ZuBSpan, const Zquic::TransportParams &);
  bool allowEarlyStream(Link *, uint64_t, bool);
};

struct QUICClient::Stream :
  public Zquic::CliStream<QUICClient::Link, QUICClient::Stream>,
  public Zhttp::H3::CxnParser<QUICClient::Stream> {
  using Base = Zquic::CliStream<QUICClient::Link, QUICClient::Stream>;
  using CxnParser = Zhttp::H3::CxnParser<QUICClient::Stream>;
  using Base::Base;

  int process(Zquic::RxStream &);
  H3CxnState::T h3State() const;
  void h3State(H3CxnState::T);
  bool peerControlStream();
  bool peerEncoderStream();
  bool peerDecoderStream();
  Zhttp::H3::QPackRxTable *qpackRx();
  Zhttp::H3::QPackTxTable *qpackTx();

  Req *req = nullptr;
  H3ResponseParser<QUICClient::Link> parser;
};

struct QUICClient::Link :
  public CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true> {
  using Base = CliLink<QUICClient,
    Zquic::CliLink<QUICClient, QUICClient::Link, QUICClient::Stream>, true>;
  using Base::Base;

  void connected(Zi::Connected info) {
    ++this->app()->up;
    m_countedUp = true;
    logConnected(this->app()->state, info);
    if (!this->h3.openLocal(*this)) {
      this->app()->failH3Link();
      return;
    }
    this->app()->openH3Streams(this);
  }
  void disconnected(bool peer) {
    ZiLOG(Info, "zhttp", "disconnected");
    int up = this->app()->up.load_();
    if (m_countedUp) {
      m_countedUp = false;
      up = --this->app()->up;
      ZiAssert(up >= 0, "zhttp", (),
	"QUIC client link up counter underflow", return);
    }
    if (!peer) {
      if (!up) this->app()->done();
      return;
    }
    this->app()->failH3Link();
  }
  void transportClose(Zquic::FrameType::T type, uint64_t errorCode) {
    ZiLOG(Error, "zhttp", ([type, errorCode](auto &s) {
      s << "h3 transport close type=" << int(type) <<
	" error=" << errorCode;
    }));
  }
  void statelessReset() {
    ZiLOG(Error, "zhttp", "h3 stateless reset");
  }
  void connectFailed(bool transient) {
    ZiLOG(Error, "zhttp", ([transient](auto &s) {
      s << "failed to connect";
      if (transient) s << " (transient)";
    }));
    this->app()->failH3Link();
  }
  void responseComplete(State *state, bool ok) {
    if (auto stream = this->findStream(state->responseStreamID))
      stream->req = nullptr;
    this->app()->finishH3Req(this, state, ok);
  }
  void streamed(ZmRef<Stream> stream) {
    this->app()->bindH3Stream(this, ZuMv(stream));
  }

private:
  bool	m_countedUp = false;
};

#ifdef ZmObject_DEBUG
struct H3StreamRefDumpCtx {
  ZuCSpan	label;
  unsigned	req = 0;
  int64_t	streamID = -1;
};

void dumpH3StreamRef(void *ctx_, const void *referrer, const ZmBackTrace *bt)
{
  auto ctx = static_cast<H3StreamRefDumpCtx *>(ctx_);
  ZiLOG(Info, "zhttp.stream.ref", ([
    label = ctx->label,
    req = ctx->req,
    streamID = ctx->streamID,
    referrer,
    bt
  ](auto &s) {
    s << "h3 " << label <<
      " stream ref req=" << req <<
      " stream=" << streamID <<
      " referrer=" << ZuBoxPtr(referrer).hex() << '\n' << *bt;
  }));
}
#endif

void QUICClient::printDiag(Link *link, ZuCSpan label)
{
#ifdef Zquic_DEBUG
  if (!link) return;
  Zquic::RuntimeDiag diag = runtimeDiag(link);
  Zquic::EndpointDiag epDiag = endpointDiag(link);
  unsigned complete, failed, scheduled, active, pending;
  {
    ZmGuard<ZmLock> guard(lock);
    complete = this->complete;
    failed = this->failed;
    scheduled = this->scheduled;
    active = this->active;
    pending = this->pending.length() - this->pendingHead;
  }
  ZiLOG(Info, "zhttp", ([
    label, complete, failed, scheduled, active, pending,
    streams = link->streamCount(),
    limit = link->peerStreamLimit(Zi::StreamType::Duplex),
    opened = link->localStreamsOpened(Zi::StreamType::Duplex),
    queued = link->queuedLocalStreams(Zi::StreamType::Duplex),
    packetsRx = diag.rx.packetsRx, packetsTx = diag.tx.packetsTx,
    duplicatePackets = diag.rx.duplicatePacketsRx,
    ackCommits = diag.rx.ackCommitsRx,
    ackEliciting = diag.rx.ackElicitingRx,
    ackImmediate = diag.rx.ackImmediateRx,
    ackPosts = diag.rx.ackSnapshotPostsRx,
    streamNoData = diag.rx.streamNoDataRx,
    ackOnly = diag.tx.ackOnlyPacketsTx,
    streamOnly = diag.tx.streamOnlyPacketsTx,
    ackStream = diag.tx.ackStreamPacketsTx,
    ackInstalls = diag.tx.ackSnapshotInstallsTx,
    ackDueInstalls = diag.tx.ackDueInstallsTx,
    ackAppend = diag.tx.ackAppendTx,
    ackAppendEmpty = diag.tx.ackAppendEmptyTx,
    ackAppendNotDue = diag.tx.ackAppendNotDueTx,
    ackSent = diag.tx.ackSentTx,
    controlOnly = diag.tx.controlOnlyPacketsTx,
    ackControl = diag.tx.ackControlPacketsTx,
    streamControl = diag.tx.streamControlPacketsTx,
    ackStreamControl = diag.tx.ackStreamControlPacketsTx,
    cryptoPkts = diag.tx.cryptoPacketsTx,
    otherPkts = diag.tx.otherPacketsTx,
    streamFrames = diag.tx.streamFramesTx,
    controlFrames = diag.tx.controlFramesTx,
    cryptoFrames = diag.tx.cryptoFramesTx,
    streamMaxClosed = diag.rx.streamMaxClosedRx,
    streamMaxInvalid = diag.rx.streamMaxInvalidRx,
    streamCtlClosed = diag.rx.streamCtlClosedRx,
    streamCtlInvalid = diag.rx.streamCtlInvalidRx,
    streamDataInvalid = diag.rx.streamDataInvalidRx,
    streamDataState = diag.rx.streamDataStateRx,
    streamDataFinal = diag.rx.streamDataFinalRx,
    streamRxDeqState = diag.rx.streamRxDeqStateRx,
    streamRxDeqFinal = diag.rx.streamRxDeqFinalRx,
    streamBlockedClosed = diag.rx.streamBlockedClosedRx,
    streamBlockedInvalid = diag.rx.streamBlockedInvalidRx,
    streamBlockedFinal = diag.rx.streamBlockedFinalRx,
    maxData = diag.tx.maxDataTx,
    maxStreamData = diag.tx.maxStreamDataTx,
    maxStreams = diag.tx.maxStreamsTx,
    dataBlocked = diag.tx.dataBlockedTx,
    streamDataBlocked = diag.tx.streamDataBlockedTx,
    streamsBlocked = diag.tx.streamsBlockedTx,
    resetStream = diag.tx.resetStreamTx,
    stopSending = diag.tx.stopSendingTx,
    pathChallenge = diag.tx.pathChallengeTx,
    pathResponse = diag.tx.pathResponseTx,
    handshakeDone = diag.tx.handshakeDoneTx,
    pathObserved = diag.tx.pathRxObserved,
    pathSame = diag.tx.pathRxSame,
    pathNull = diag.tx.pathRxNull,
    pathActive = diag.tx.pathValidationActive,
    pathStarted = diag.tx.pathValidationStarted,
    pathPromoted = diag.tx.pathValidationPromoted,
    pathUnknown = diag.tx.pathResponseUnknown,
    ptoSched = diag.tx.ptoSched,
    ptoNoLevel = diag.tx.ptoNoLevel,
    ptoArmed = diag.tx.ptoArmed,
    ptoExpired = diag.tx.ptoExpired,
    ptoFlush = diag.tx.ptoFlush,
    ptoRetx = diag.tx.ptoRetx,
    ptoProbe = diag.tx.ptoProbe,
    ptoBackoff = diag.tx.ptoBackoff,
    ptoTimeoutUS = diag.tx.ptoTimeoutUS,
    lossArmed = diag.tx.lossArmed,
    lossCanceled = diag.tx.lossCanceled,
    lossExpired = diag.tx.lossExpired,
    dgramsRx = epDiag.rx.datagramsRx, dgramsTx = epDiag.tx.datagramsTx,
    epSendCalls = epDiag.tx.sendCalls,
    epDirect = epDiag.tx.directCalls,
    epAsync = epDiag.tx.asyncCalls,
    epSubmit = epDiag.tx.submittedTx,
    epSubmitBytes = epDiag.tx.submittedBytes,
    epBytesRx = epDiag.rx.bytesRx, epBytesTx = epDiag.tx.bytesTx,
    epBackPressure = epDiag.tx.txBackPressure,
    epDropped = epDiag.tx.txDropped,
    epPending = epDiag.txPending,
    epQueued = epDiag.txQueued,
    epFailures = epDiag.failures(),
    streamRx = diag.rx.streamBytesRx, streamTx = diag.tx.streamBytesTx,
    invalidStream = diag.rx.invalidStreamFrames,
    closedStream = diag.rx.closedStreamFrames,
    suspiciousCloses = diag.rx.suspiciousStreamCloses,
    invalidKeys = diag.rx.invalidKeyPhases,
    failures = diag.failures(),
    pto = diag.tx.ptoCount, retx = diag.tx.retransmittedFrames,
    ptoTimer = diag.tx.ptoTimerActive, lossTimer = diag.tx.lossTimerActive,
    if0 = diag.tx.pktBytesInFlight[0],
    if1 = diag.tx.pktBytesInFlight[1],
    if2 = diag.tx.pktBytesInFlight[2],
    sp0 = diag.tx.sentPackets[0],
    sp1 = diag.tx.sentPackets[1],
    sp2 = diag.tx.sentPackets[2],
    rp0 = diag.tx.retransmitPending[0],
    rp1 = diag.tx.retransmitPending[1],
    rp2 = diag.tx.retransmitPending[2],
    rt0 = diag.tx.retransmittable[0],
    rt1 = diag.tx.retransmittable[1],
    rt2 = diag.tx.retransmittable[2],
    cwnd = diag.tx.congestionWindow,
    ssthresh = diag.tx.congestionSSThresh,
    pc = diag.tx.persistentCongestion,
    bif = diag.tx.congestionBytesInFlight
  ](auto &s) {
    s << "h3 " << label <<
      " complete=" << complete <<
      " failed=" << failed <<
      " scheduled=" << scheduled <<
      " active=" << active <<
      " pending=" << pending <<
      " streams=" << streams <<
      " limit=" << limit <<
      " opened=" << opened <<
      " queued=" << queued <<
      " packetsRx=" << packetsRx <<
      " packetsTx=" << packetsTx <<
      " ackDiag=[" << duplicatePackets << ',' << ackCommits << ',' <<
	ackEliciting << ',' << ackImmediate << ',' << ackPosts << ',' <<
	streamNoData << ',' << ackInstalls << ',' << ackDueInstalls << ',' <<
	ackAppend << ',' << ackAppendEmpty << ',' << ackAppendNotDue << ',' <<
	ackSent << ']' <<
      " pktMix=[" << ackOnly << ',' << streamOnly << ',' << ackStream <<
	',' << controlOnly << ',' << ackControl << ',' <<
	streamControl << ',' << ackStreamControl << ',' << cryptoPkts <<
	',' << otherPkts << ']' <<
      " frameMix=[" << streamFrames << ',' << controlFrames << ',' <<
	cryptoFrames << ']' <<
      " streamFault=[" << invalidStream << ',' << closedStream << ',' <<
	suspiciousCloses << ',' << streamMaxClosed << ',' <<
	streamMaxInvalid << ',' << streamCtlClosed << ',' <<
	streamCtlInvalid << ',' << streamDataInvalid << ',' <<
	streamDataState << ',' << streamDataFinal << ',' <<
	streamRxDeqState << ',' << streamRxDeqFinal << ',' <<
	streamBlockedClosed << ',' << streamBlockedInvalid << ',' <<
	streamBlockedFinal << ']' <<
      " ctrlTx=[" << maxData << ',' << maxStreamData << ',' <<
	maxStreams << ',' << dataBlocked << ',' << streamDataBlocked <<
	',' << streamsBlocked << ',' << resetStream << ',' <<
	stopSending << ',' << pathChallenge << ',' << pathResponse << ',' <<
	handshakeDone << ']' <<
      " pathDiag=[" << pathObserved << ',' << pathSame << ',' <<
	pathNull << ',' << pathActive << ',' << pathStarted << ',' <<
	pathPromoted << ',' << pathUnknown << ']' <<
      " ptoDiag=[" << ptoSched << ',' << ptoNoLevel << ',' <<
	ptoArmed << ',' << ptoExpired << ',' << ptoFlush << ',' <<
	ptoRetx << ',' << ptoProbe << ']' <<
      " ptoBackoff=" << ptoBackoff <<
      " ptoTimeoutUS=" << ptoTimeoutUS <<
      " lossDiag=[" << lossArmed << ',' << lossCanceled << ',' <<
	lossExpired << ']' <<
      " dgramsRx=" << dgramsRx <<
      " dgramsTx=" << dgramsTx <<
      " epSendCalls=" << epSendCalls <<
      " epDirect=" << epDirect <<
      " epAsync=" << epAsync <<
      " epSubmit=" << epSubmit <<
      " epSubmitBytes=" << epSubmitBytes <<
      " epBytesRx=" << epBytesRx <<
      " epBytesTx=" << epBytesTx <<
      " epBackPressure=" << epBackPressure <<
      " epDropped=" << epDropped <<
      " epPending=" << epPending <<
      " epQueued=" << epQueued <<
      " epFailures=" << epFailures <<
      " streamRx=" << streamRx <<
      " streamTx=" << streamTx <<
      " invalidStream=" << invalidStream <<
      " closedStream=" << closedStream <<
      " suspiciousCloses=" << suspiciousCloses <<
      " invalidKeys=" << invalidKeys <<
      " failures=" << failures <<
      " pto=" << pto <<
      " retx=" << retx <<
      " ptoTimer=" << int(ptoTimer) <<
      " lossTimer=" << int(lossTimer) <<
      " pktIF=[" << if0 << ',' << if1 << ',' << if2 << ']' <<
      " sentPkts=[" << sp0 << ',' << sp1 << ',' << sp2 << ']' <<
      " retxPend=[" << rp0 << ',' << rp1 << ',' << rp2 << ']' <<
      " retxTotal=[" << rt0 << ',' << rt1 << ',' << rt2 << ']' <<
      " cwnd=" << cwnd <<
      " ssthresh=" << ssthresh <<
      " pc=" << pc <<
      " bif=" << bif;
  }));
  ZiAssert(run, "zhttp", (), "QUIC client diagnostics without run", return);
  ZmGuard<ZmLock> guard(lock);
  for (unsigned i = 0; i < run->reqs.length(); ++i) {
    const Req &req = run->reqs[i];
    if (!req.h3Active || req.done) continue;
    auto stream = link->findStream(req.responseStreamID);
    uint64_t txBytes = stream ? stream->txBytes() : 0;
    uint64_t txBuffered = stream ? stream->txBufferedBytes() : 0;
    unsigned txRanges = stream ? stream->txRangeCount() : 0;
    unsigned txUnackd = stream ? stream->txUnackdCount() : 0;
    uint64_t txUnackdBytes = stream ? stream->txUnackdBytes() : 0;
    uint64_t txCredit = stream ? stream->txCreditAvailable() : 0;
    uint64_t txLimit = stream ? stream->txCreditLimit() : 0;
    uint64_t liveRxBytes = stream ? stream->rxBytes() : 0;
    uint64_t liveFinalSize = stream ? stream->finalSize() : 0;
    unsigned liveRxPending = stream ? stream->rxPending() : 0;
    unsigned liveRxQueued = stream ? stream->rxQueued() : 0;
    bool liveFinReceived = stream && stream->finReceived();
    bool finSent = stream && stream->finSent();
    bool finDequeued = stream && stream->finDequeued();
    ZiLOG(Info, "zhttp", ([
      label, id = req.id, streamID = req.responseStreamID,
      status = req.status, contentLength = req.contentLength,
      bodyBytes = req.bodyBytes, bodyChunks = req.bodyChunks,
      rxBytes = req.h3RxBytes, finalSize = req.h3FinalSize,
      rxPending = req.h3RxPending, rxQueued = req.h3RxQueued,
      fin = req.h3FinReceived, haveStream = !!stream,
      txBytes, txBuffered, txRanges, txUnackd, txUnackdBytes,
      txCredit, txLimit,
      liveRxBytes, liveFinalSize, liveRxPending, liveRxQueued,
      liveFinReceived,
      finSent, finDequeued
    ](auto &s) {
      s << "h3 " << label <<
	" req=" << id <<
	" stream=" << streamID <<
	" status=" << status <<
	" contentLength=" << contentLength <<
	" bodyBytes=" << bodyBytes <<
	" bodyChunks=" << bodyChunks <<
	" rxBytes=" << rxBytes <<
	" finalSize=" << finalSize <<
	" rxPending=" << rxPending <<
	" rxQueued=" << rxQueued <<
	" fin=" << int(fin) <<
	" haveStream=" << int(haveStream) <<
	" liveRxBytes=" << liveRxBytes <<
	" liveFinalSize=" << liveFinalSize <<
	" liveRxPending=" << liveRxPending <<
	" liveRxQueued=" << liveRxQueued <<
	" liveFin=" << int(liveFinReceived) <<
	" txBytes=" << txBytes <<
	" txBuffered=" << txBuffered <<
	" txRanges=" << txRanges <<
	" txUnackd=" << txUnackd <<
	" txUnackdBytes=" << txUnackdBytes <<
	" txCredit=" << txCredit <<
	" txLimit=" << txLimit <<
	" finSent=" << int(finSent) <<
	" finDequeued=" << int(finDequeued);
    }));
#ifdef ZmObject_DEBUG
    if (stream && label != ZuCSpan{"diag"}) {
      H3StreamRefDumpCtx ctx{label, req.id, req.responseStreamID};
      stream->dump(&ctx, dumpH3StreamRef);
    }
#endif
  }
#endif
}

struct QUICDrain : public ZmObject {
  ZmSemaphore sem;
};

template <typename Link>
void disconnectDrained(Link *link)
{
  ZmRef<QUICDrain> drained = new QUICDrain;
  link->disconnect([drained]() { drained->sem.post(); });
  drained->sem.wait();
}

void waitThread(ZiMultiplex &mx, unsigned thread)
{
  ZmSemaphore done;
  mx.invoke([&done]() { done.post(); }, thread);
  done.wait();
}

void waitDisconnect(ZiMultiplex &mx)
{
  waitThread(mx, mx.txThread());
  waitThread(mx, mx.rxThread());
  waitThread(mx, mx.txThread());
}

template <typename Link>
void abortDrained(Link *link)
{
  ZmRef<QUICDrain> drained = new QUICDrain;
  link->abort([drained]() { drained->sem.post(); });
  drained->sem.wait();
}

int QUICClient::Stream::process(Zquic::RxStream &)
{
  if (Zquic::StreamID::uni(uint64_t(this->id()))) {
    auto s = this->CxnParser::process(*this);
    if (s == H3CxnState::Error) {
      this->link()->app()->done();
      return -1;
    }
    return 0;
  }
  if (req) {
    req->h3RxBytes = this->rxBytes();
    req->h3FinalSize = this->finalSize();
    req->h3RxPending = this->rxPending();
    req->h3RxQueued = this->rxQueued();
    req->h3FinReceived = this->finReceived();
    auto parserState = parser.process(*this);
    if (req) {
      req->h3RxBytes = this->rxBytes();
      req->h3FinalSize = this->finalSize();
      req->h3RxPending = this->rxPending();
      req->h3RxQueued = this->rxQueued();
      req->h3FinReceived = this->finReceived();
    }
    using Parser = ZuDecay<decltype(parser)>;
    if (parserState == Parser::State::Error) return -1;
    if (parserState == Parser::State::Complete) return 1;
    if (req && req->done) return -1;
    return 0;
  }
  return 0;
}

H3CxnState::T QUICClient::Stream::h3State() const
{
  return this->link()->h3.state;
}

void QUICClient::Stream::h3State(H3CxnState::T state)
{
  this->link()->h3.state = state;
}

bool QUICClient::Stream::peerControlStream()
{
  return this->link()->h3.peerControlStream();
}

bool QUICClient::Stream::peerEncoderStream()
{
  return this->link()->h3.peerEncoderStream();
}

bool QUICClient::Stream::peerDecoderStream()
{
  return this->link()->h3.peerDecoderStream();
}

Zhttp::H3::QPackRxTable *QUICClient::Stream::qpackRx()
{
  return this->link()->h3.qpackRx();
}

Zhttp::H3::QPackTxTable *QUICClient::Stream::qpackTx()
{
  return this->link()->h3.qpackTx();
}

void QUICClient::openH3Streams(Link *link_)
{
  if (!link_) return;
  this->txInvoke([this, link = link_]() mutable {
    openH3Streams_(link);
  });
}

void QUICClient::openH3Streams_(Link *link_)
{
  if (!link_) return;
  for (;;) {
    Req *req = nullptr;
    {
      ZmGuard<ZmLock> guard(lock);
      ZiAssert(run, "zhttp", (), "QUIC client open streams without run",
	return);
      if (active >= run->options.concurrency ||
	  scheduled >= run->options.requests)
	return;
      if (run->preloadedReqs) {
	if (scheduled >= run->reqs.length()) return;
	req = &run->reqs[scheduled++];
      } else
	for (unsigned i = 0; i < run->reqs.length(); ++i) {
	  if (run->reqs[i].h3Active) continue;
	  req = &run->reqs[i];
	  initReq(*req, *run, scheduled++);
	  break;
	}
      if (!req) return;
      if (req->logResponse) {
		auto ctx = reqLogCtx(*req);
		ZiLOG(Debug, "zhttp.h3", ([
		  ctx, scheduled = scheduled, active = active,
		  concurrency = run->options.concurrency
		](auto &s) {
		  reqLogPrefix(ctx, s);
		  s << "schedule h3 request scheduled=" << scheduled <<
		    " active=" << active << " concurrency=" << concurrency;
		}));
      }
    }
    resetAttempt(*req, true);
    if (!activateH3Req(req)) return;
    auto stream = link_->stream(Zi::StreamType::Duplex);
    if (!stream) {
      if (req->logResponse) {
		auto ctx = reqLogCtx(*req);
		ZiLOG(Debug, "zhttp.h3", ([ctx](auto &s) {
		  reqLogPrefix(ctx, s);
		  s << "no stream credit, queue request";
		}));
      }
      queueH3Req(req);
      return;
    }
    sendH3Req_(link_, ZuMv(stream), req);
  }
}

bool QUICClient::activateH3Req(Req *req)
{
  if (!req) return false;
  ZmGuard<ZmLock> guard(lock);
  ZiAssert(run, "zhttp", (), "QUIC client activate without run",
    return false);
  if (req->done) return false;
  if (!req->h3Active) {
    ++active;
    req->h3Active = true;
  }
  return true;
}

bool QUICClient::allowEarlyStream(Link *link, uint64_t streamID, bool fin)
{
  if (!fin || streamID > uint64_t(INT64_MAX)) return false;
  int64_t id = int64_t(streamID);
  if (!link) return false;
  auto stream = link->findStream(id);
  if (!stream) return false;
  Req *req = stream->req;
  if (!req) return false;
  ZmGuard<ZmLock> guard(lock);
  return req->h3EarlyData && req->h3Active && !req->done && !req->failed;
}

bool QUICClient::earlyData(
  Link *, ZuBSpan &ticket, const Zquic::TransportParams *&params,
  ZuBSpan &appParams)
{
  ZmGuard<ZmLock> guard(lock);
  H3EarlySession *session = earlySession_();
  if (!session || !session->ticket) return false;
  ticket = session->ticket;
  params = &session->params;
  appParams = {};
  return true;
}

void QUICClient::saveEarlyData(
  Link *, ZuBSpan ticket, const Zquic::TransportParams &params)
{
  ZmGuard<ZmLock> guard(lock);
  H3EarlySession *session = earlySession_();
  if (!session) return;
  if (!ticket) {
    session->ticket = {};
    session->params = {};
    return;
  }
  session->ticket = ticket;
  session->params = params;
}

void QUICClient::sendH3Req(Link *link_, ZmRef<Stream> stream, Req *req)
{
  this->txInvoke([
    this,
    link = link_,
    stream = ZuMv(stream),
    req
  ]() mutable {
    if (req && req->done) return;
    sendH3Req_(link, ZuMv(stream), req);
  });
}

void QUICClient::sendH3Req_(Link *link_, ZmRef<Stream> stream, Req *req)
{
  if (!link_ || !stream || !req) {
    if (!req || req->logResponse) {
      auto haveReq = !!req;
      auto ctx = haveReq ? reqLogCtx(*req) : ReqLogCtx{};
      ZiLOG(Debug, "zhttp.h3", ([haveReq, ctx](auto &s) {
	if (haveReq) reqLogPrefix(ctx, s);
	s << "send h3 request failed before send";
      }));
    }
    finishH3Req_(link_, req, false);
    return;
  }
  req->protocol = Protocol::H3;
  req->responseStreamID = stream->id();
  req->h3EarlyData =
    Zhttp::earlyDataSafeRequest(Zhttp::Method::GET, false);
  stream->req = req;
  stream->parser.bind(link_, req);
  stream->parser.reset();
  if (req->logResponse) {
    auto ctx = reqLogCtx(*req);
    ZiLOG(Debug, "zhttp.h3", ([ctx, id = stream->id()](auto &s) {
      reqLogPrefix(ctx, s);
      s << "activate stream=" << id;
    }));
  }
  sendH3Request(*req, stream);
}

void QUICClient::queueH3Req(Req *req)
{
  if (!req) return;
  ZmGuard<ZmLock> guard(lock);
  pending.push(req);
  if (req->logResponse) {
    auto ctx = reqLogCtx(*req);
    ZiLOG(Debug, "zhttp.h3", ([
      ctx, n = pending.length() - pendingHead
    ](auto &s) {
      reqLogPrefix(ctx, s);
      s << "queued pending=" << n;
    }));
  }
}

Req *QUICClient::popH3Req()
{
  ZmGuard<ZmLock> guard(lock);
  if (pendingHead >= pending.length()) return nullptr;
  Req *req = pending[pendingHead++];
  if (pendingHead >= pending.length()) {
    pending.length(0);
    pendingHead = 0;
  }
  return req;
}

void QUICClient::bindH3Stream(Link *link_, ZmRef<Stream> stream)
{
  if (!link_) return;
  this->txInvoke([this, link = link_, stream = ZuMv(stream)]() mutable {
    bindH3Stream_(link, ZuMv(stream));
  });
}

void QUICClient::bindH3Stream_(Link *link_, ZmRef<Stream> stream)
{
  if (!link_ || !stream) return;
  Req *req = popH3Req();
  if (!req) return;
  if (!activateH3Req(req)) return;
  if (req->logResponse) {
    auto ctx = reqLogCtx(*req);
    ZiLOG(Debug, "zhttp.h3", ([ctx, id = stream->id()](auto &s) {
      reqLogPrefix(ctx, s);
      s << "bind returned stream=" << id;
    }));
  }
  sendH3Req_(link_, ZuMv(stream), req);
}

void QUICClient::finishH3Req(Link *link_, Req *req, bool ok)
{
  this->rxInvoke([
    this,
    link = link_,
    req,
    ok
  ]() mutable {
    finishH3Req_(link, req, ok);
  });
}

void QUICClient::finishH3Req_(Link *link_, Req *req, bool ok)
{
  bool done = false;
  ok = ok && req && !req->failed;
  if (req && ok && req->redirecting) {
    URL next;
    if (!req->location || req->redirects >= MaxRedirects ||
	!parseLocation(req->url, req->location, next) ||
	!sameOrigin(req->url, next)) {
      ok = false;
    } else {
      req->url = ZuMv(next);
      ++req->redirects;
      resetAttempt(*req, true);
      this->txInvoke([
	this,
	link = link_,
	req
      ]() mutable {
	if (!link || !req || req->done) return;
	auto stream = link->stream(Zi::StreamType::Duplex);
	if (!stream) {
	  queueH3Req(req);
	  return;
	}
	sendH3Req_(link, ZuMv(stream), req);
      });
      return;
    }
  }
  {
    ZmGuard<ZmLock> guard(lock);
    if (req) {
      if (req->h3Active) {
	if (active) --active;
	req->h3Active = false;
      }
      closeBody(*req);
      req->done = true;
      req->failed = !ok;
    }
    ++complete;
    if (!ok) ++failed;
    done = complete >= run->options.requests;
    if (!req || req->logResponse) {
      auto haveReq = !!req;
      auto ctx = haveReq ? reqLogCtx(*req) : ReqLogCtx{};
      ZiLOG(Debug, "zhttp.h3", ([
	haveReq, ctx, ok, active = active, complete = complete,
	failed = failed, done
      ](auto &s) {
	if (haveReq) reqLogPrefix(ctx, s);
	s << "finish h3 request ok=" << ok <<
	  " active=" << active << " complete=" << complete <<
	  " failed=" << failed << " done=" << done;
      }));
    }
  }
  if (done)
    sem.post();
  else
    openH3Streams(link_);
}

void QUICClient::failH3Link()
{
  ZmGuard<ZmLock> guard(lock);
  ZiAssert(run, "zhttp", (), "QUIC client link failure without run", return);
  linkFailed = true;
  ZiLOG(Debug, "zhttp.h3", ([
    complete = complete, failed = failed, active = active,
    scheduled = scheduled
  ](auto &s) {
    s << "fail h3 link scheduled=" << scheduled <<
      " active=" << active << " complete=" << complete <<
      " failed=" << failed;
  }));
  for (unsigned i = 0; i < run->reqs.length(); ++i) {
    auto &req = run->reqs[i];
    if (!req.h3Active || req.done) continue;
    closeBody(req);
    req.done = false;
    req.failed = false;
    req.h3Active = false;
  }
  active = 0;
  pending.length(0);
  pendingHead = 0;
  sem.post();
}

template <typename Client>
int run(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult *result = nullptr)
{
  const auto &options = run_.options;
  Client client;
  client.state.id = req.id;
  client.state.requests = req.requests;
  client.state.url = req.url;
  client.state.output = req.output;
  client.state.discardResponse = req.discardResponse;
  client.state.logResponse = req.logResponse;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    client.state.protocol = Protocol::H1;
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      ZiLOG(Error, "zhttp", "TCP client initialization failed");
      return 1;
    }
  } else if constexpr (Client::Transport == Zi::Transport::TLS) {
    client.state.protocol = Protocol::H1;
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4").alpn(alpn).caPath(options.ca))) {
      ZiLOG(Error, "zhttp", "TLS client initialization failed");
      return 1;
    }
  } else {
    static_assert(
      Client::Transport == Zi::Transport::TCP ||
      Client::Transport == Zi::Transport::TLS,
      "unsupported one-shot transport");
  }
  if (!client.start()) {
    ZiLOG(Error, "zhttp", "client start failed");
    client.final();
    return 1;
  }

  {
    using Link = typename Client::Link;
    ZmRef<Link> link = new Link(&client);
    link->connect(client.state.url.host, client.state.url.port);
    if (!waitMonitored(client.sem, options.timeout, options.memDiag)) {
      ZiLOG(Error, "zhttp", "timed out");
      client.state.failed = true;
      link->disconnect();
      client.sem.timedwait(Zm::now(2));
    }
  }
  resultFromReq(result, client.state);
  int rc = client.state.failed ? 1 : 0;
  closeBody(client.state);
  req = ZuMv(client.state);
  client.stop();
  client.final();
  return rc;
}

template <typename Client>
int runH1Pool(ZiMultiplex &mx, Run &run)
{
  Client client;
  client.run = &run;

  if constexpr (Client::Transport == Zi::Transport::TCP) {
    if (!client.init(Ztcp::ClientParams(&mx, "3", "4"))) {
      ZiLOG(Error, "zhttp", "TCP client initialization failed");
      return 1;
    }
  } else {
    ZuCSpan alpn[] = { "http/1.1" };
    if (!client.init(
	  Ztls::ClientParams(&mx, "3", "4")
	    .alpn(alpn).caPath(run.options.ca))) {
      ZiLOG(Error, "zhttp", "TLS client initialization failed");
      return 1;
    }
  }
  if (!client.start()) {
    ZiLOG(Error, "zhttp", "client start failed");
    client.final();
    return 1;
  }

  unsigned n = run.options.concurrency;
  if (n > run.options.requests) n = run.options.requests;
  client.links.length(n);
  for (unsigned i = 0; i < n; ++i) {
    auto link = new typename Client::Link(&client, i);
    client.links[i] = link;
    Req *req = client.nextReq(i);
    if (!req) continue;
    link->assign(req);
    link->connectCurrent();
  }

  bool timedOut = false;
  if (!waitMonitored(client.sem, run.options.timeout, run.options.memDiag)) {
    ZiLOG(Error, "zhttp", "timed out");
    timedOut = true;
  }

  client.stop();
  if (timedOut)
    client.failed += run.options.requests - client.complete;
  run.complete = client.complete;
  run.failed = client.failed;
  client.final();
  return client.failed ? 1 : 0;
}

void h3RunCounts(Run &run, unsigned &complete, unsigned &failed)
{
  complete = failed = 0;
  for (unsigned i = 0; i < run.reqs.length(); ++i) {
    Req &req = run.reqs[i];
    if (!req.done) continue;
    ++complete;
    if (req.failed) ++failed;
  }
}

void resetH3Incomplete(Run &run)
{
  for (unsigned i = 0; i < run.reqs.length(); ++i) {
    Req &req = run.reqs[i];
    if (req.done) continue;
    resetAttempt(req, true);
    req.h3Active = false;
  }
}

int runH3Multi(ZiMultiplex &mx, Run &run)
{
  unsigned n = run.options.concurrency;
  if (n > run.options.requests) n = run.options.requests;
  if (!run.preloadedReqs)
    run.reqs.length(n);
  for (unsigned attempt = 0; attempt < H3MaxAttempts; ++attempt) {
    QUICClient client;
    client.run = &run;
    client.h3Session = &run.h3EarlySession;
    client.complete = run.complete;
    client.failed = run.failed;
    client.scheduled = run.complete;
    client.linkFailed = false;
    client.state.requests = run.options.requests;
    client.state.url = run.originalURL;
    client.state.discardResponse = run.options.discardResponse;
    client.state.logResponse = hotLog(run.options);
    if (client.complete >= run.options.requests) {
      run.complete = client.complete;
      run.failed = client.failed;
      return run.failed ? 1 : 0;
    }
    client.dropRates(run.options);
    ZuCSpan alpn[] = { "h3" };
    if (!client.init(
	  Zquic::ClientParams(&mx, "3", "4")
	    .alpn(alpn)
	    .caPath(run.options.ca)
	    .keyLogPath(run.options.keyLog)
	    .maxData(H3DataMax)
	    .maxStreamData(H3StreamDataMax)
	    .maxStreamsBidi(client.maxStreamsBidi())
	    .maxStreamsUni(H3UniMax))) {
      ZiLOG(Error, "zhttp", "QUIC client initialization failed");
      return 1;
    }
    client.filters();
    if (!client.start()) {
      ZiLOG(Error, "zhttp", "QUIC client start failed");
      client.clearFilters();
      client.final();
      return 1;
    }

    ZmRef<QUICClient::Link> link = new QUICClient::Link(&client);
    link->connect(run.originalURL.host, run.originalURL.port);
    bool timedOut = false;
    bool stalled = false;
    IntervalMonitor mon{
      run.options.timeout,
      run.options.memDiag
#ifdef Zquic_DEBUG
      , run.options.quicDiag
#endif
    };
    unsigned idle = 0;
    unsigned lastComplete = client.complete;
    unsigned quiet = 0;
    Zquic::RuntimeDiag lastDiag = runtimeDiag(link);
    for (;;) {
      unsigned step = mon.nextStep(1);
      if (!step) {
	timedOut = true;
	break;
      }
      if (client.sem.timedwait(Zm::now(step)) == 0) break;
      mon.advance(step);
      Zquic::RuntimeDiag diag = runtimeDiag(link);
      bool pktMoved =
	diag.rx.packetsRx != lastDiag.rx.packetsRx ||
	diag.tx.packetsTx != lastDiag.tx.packetsTx;
      if (client.complete != lastComplete) {
	lastComplete = client.complete;
	idle = 0;
	quiet = 0;
      } else
	idle += step;
      if (pktMoved) {
	lastDiag = diag;
	quiet = 0;
      } else
	quiet += step;
#ifdef Zquic_DEBUG
      mon.intervals(
	[]() { printMemDiag(); },
	[&client, link]() { client.printDiag(link, "diag"); });
#else
      mon.intervals([]() { printMemDiag(); });
#endif
      bool incomplete = client.complete < run.options.requests;
      if (run.options.quietTimeout && (client.scheduled || client.active) &&
	  quiet >= run.options.quietTimeout &&
	  incomplete) {
	stalled = true;
	break;
      }
      if (run.options.stallTimeout && incomplete &&
	  idle >= run.options.stallTimeout) {
	stalled = true;
	break;
      }
      if (mon.timedOut()) {
	timedOut = true;
	break;
      }
    }
    if (timedOut || stalled) {
      client.printDiag(link, timedOut ? "timeout" : "stall");
      Zquic::RuntimeDiag diag = runtimeDiag(link);
      unsigned complete, failed, scheduled, active, pending;
      {
	ZmGuard<ZmLock> guard(client.lock);
	complete = client.complete;
	failed = client.failed;
	scheduled = client.scheduled;
	active = client.active;
	pending = client.pending.length() - client.pendingHead;
      }
      ZiLOG(Error, "zhttp", ([
	complete, failed, scheduled, active, pending,
	streams = link->streamCount(),
	limit = link->peerStreamLimit(Zi::StreamType::Duplex),
	opened = link->localStreamsOpened(Zi::StreamType::Duplex),
	queued = link->queuedLocalStreams(Zi::StreamType::Duplex),
	packetsRx = diag.rx.packetsRx,
	packetsTx = diag.tx.packetsTx,
	pto = diag.tx.ptoCount,
	retx = diag.tx.retransmittedFrames,
	ptoBackoff = diag.tx.ptoBackoff,
	ptoTimeoutUS = diag.tx.ptoTimeoutUS
      ](auto &s) {
	s << "h3 timeout state complete=" << complete <<
	  " failed=" << failed <<
	  " scheduled=" << scheduled <<
	  " active=" << active <<
	  " pending=" << pending <<
	  " streams=" << streams <<
	  " limit=" << limit <<
	  " opened=" << opened <<
	  " queued=" << queued <<
	  " packetsRx=" << packetsRx <<
	  " packetsTx=" << packetsTx <<
	  " pto=" << pto <<
	  " retx=" << retx <<
	  " ptoBackoff=" << ptoBackoff <<
	  " ptoTimeoutUS=" << ptoTimeoutUS;
      }));
      ZiLOG(Error, "zhttp", ([attempt, stalled](auto &s) {
	s << "h3 " << (stalled ? "stalled" : "timed out") <<
	  ", reconnecting attempt=" << attempt + 1;
      }));
    }
    if (timedOut || stalled)
      abortDrained(link.ptr());
    else
      disconnectDrained(link.ptr());
    waitDisconnect(mx);
    bool linkFailed = false;
    {
      ZmGuard<ZmLock> guard(client.lock);
      run.complete = client.complete;
      run.failed = client.failed;
      linkFailed = client.linkFailed;
    }
    if (linkFailed && run.complete < run.options.requests)
      resetH3Incomplete(run);
    if (timedOut || stalled)
      resetH3Incomplete(run);
    client.stop();
    client.clearFilters();
    client.final();
    if (linkFailed && run.complete < run.options.requests)
      continue;
    if (!timedOut && !stalled)
      return run.failed ? 1 : 0;
    if (run.complete >= run.options.requests)
      return run.failed ? 1 : 0;
  }
  run.failed += run.options.requests - run.complete;
  return 1;
}

int runH3Single(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  Run run;
  run.options = run_.options;
  run.options.requests = 1;
  run.options.concurrency = 1;
  run.originalURL = req.url;
  run.h3EarlySession = run_.h3EarlySession;
  run.preloadedReqs = true;
  run.reqs.length(1);
  run.reqs[0] = req;

  int rc = runH3Multi(mx, run);
  run_.h3EarlySession = ZuMv(run.h3EarlySession);
  if (run.reqs.length()) {
    Req &h3Req = run.reqs[0];
    resultFromReq(&result, h3Req);
    h3Req.id = req.id;
    h3Req.requests = req.requests;
    req = ZuMv(h3Req);
  } else
    req.failed = rc != 0;
  return rc;
}

ZiMxParams mxParams(const Options &options)
{
  auto params = ZiMxParams()
    .scheduler([](auto &s) {
      s.nThreads(4)
      .thread(1, [](auto &t) { t.isolated(1); })
      .thread(2, [](auto &t) { t.isolated(1); })
      .thread(3, [](auto &t) { t.isolated(1); })
      .thread(4, [](auto &t) { t.isolated(1); }); })
    .rxThread(1).txThread(2);
#ifdef ZiMultiplex_DEBUG
  if (options.debug) params.debug(true);
  if (options.frag) params.frag(true);
  if (options.yield) params.yield(true);
#else
  (void)options;
#endif
  return params;
}

bool resolveH3(const URL &url, H3Policy::T policy)
{
  H3Endpoint eps[H3MaxIPs];
  unsigned n = 0;
  bool advertised = false;
  ZeError e;
  int rc = http3DNS(url.dnsHost, url.dnsHost, url.port, policy,
    ZmFn<bool(const H3Endpoint &)>{[&eps, &n, &advertised](
	const auto &ep) {
      for (unsigned i = 0; i < n; ++i)
	if (eps[i].ip == ep.ip && eps[i].port == ep.port) return true;
      advertised |= ep.fromHTTPS;
      if (n < H3MaxIPs) eps[n++] = ep;
      return n < H3MaxIPs;
    }}, &e);
  if (rc != Zi::OK || !n) {
    ZiLOG(Info, "zhttp", ([host = ZeString(url.host)](auto &s) {
      s << "DNS did not advertise HTTP/3 for " << host;
    }));
    return false;
  }
  ZeString dns;
  dns << "DNS: " << url.host <<
      (advertised ? " advertised HTTP/3 at" : " blind HTTP/3 probe at");
  for (unsigned i = 0; i < n; ++i)
    dns << ' ' << eps[i].ip << ':' << ZuBoxed(eps[i].port);
  ZiLOG(Info, "zhttp", ([dns = ZuMv(dns)](auto &s) {
    s << dns;
  }));
  return true;
}

bool resolveH3Cached(Run &run_, const URL &url, H3Policy::T policy)
{
  if (policy != H3Policy::DNSOnly)
    return resolveH3(url, policy);
  auto &discovery = discoveryFor(run_, url);
  if (discovery.dnsChecked) return discovery.dnsH3;
  discovery.dnsChecked = true;
  discovery.dnsH3 = resolveH3(url, policy);
  return discovery.dnsH3;
}

URL altSvcURL(const URL &url, const AltSvcEndpoint &altSvc)
{
  URL h3URL = url;
  if (altSvc.host) setHost(h3URL, altSvc.host);
  if (altSvc.port) h3URL.port = altSvc.port;
  return h3URL;
}

void cacheAltSvc(Run &run_, const URL &url, const AltSvcEndpoint &altSvc)
{
  auto &discovery = discoveryFor(run_, url);
  discovery.altSvcChecked = true;
  discovery.altSvcH3 = altSvc.h3;
  discovery.altSvcEndpoint = altSvc;
}

bool cachedAltSvc(Run &run_, const URL &url, AltSvcEndpoint &altSvc)
{
  auto &discovery = discoveryFor(run_, url);
  if (!discovery.altSvcChecked || !discovery.altSvcH3) return false;
  altSvc = discovery.altSvcEndpoint;
  return true;
}

int runH3Direct(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  auto ctx = reqLogCtx(req);
  ZiLOG(Info, "zhttp", ([
    ctx, host = ZeString(req.url.host), port = req.url.port
  ](auto &s) {
    reqLogPrefix(ctx, s);
    s << "HTTP/3 direct: " << host << ':' << ZuBoxed(port);
  }));
  resetAttempt(req, true);
  return runH3Single(mx, run_, req, result);
}

int runH1AltSvcFirst(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  AltSvcEndpoint cached;
  if (cachedAltSvc(run_, req.url, cached)) {
    RequestResult h3Result;
    URL h1URL = req.url;
    req.url = altSvcURL(h1URL, cached);
    auto ctx = reqLogCtx(req);
    ZiLOG(Info, "zhttp", ([
      ctx, host = ZeString(req.url.host), port = req.url.port
    ](auto &s) {
      reqLogPrefix(ctx, s);
      s << "cached Alt-Svc HTTP/3 probe: " << host << ':' <<
	ZuBoxed(port);
    }));
    resetAttempt(req, true);
    int h3rc = runH3Single(mx, run_, req, h3Result);
    if (!h3rc) {
      result = ZuMv(h3Result);
      return 0;
    }
    req.url = ZuMv(h1URL);
  }

  resetAttempt(req, true);
  int rc = run<TLSClient>(mx, run_, req, &result);
  if (result.altSvc.h3) cacheAltSvc(run_, req.url, result.altSvc);
  if (rc || redirectStatus(result.status) || !result.altSvc.h3) return rc;

  RequestResult h3Result;
  URL h1URL = req.url;
  req.url = altSvcURL(h1URL, result.altSvc);
  auto ctx = reqLogCtx(req);
  ZiLOG(Info, "zhttp", ([
    ctx, host = ZeString(req.url.host), port = req.url.port
  ](auto &s) {
    reqLogPrefix(ctx, s);
    s << "Alt-Svc HTTP/3 probe: " << host << ':' << ZuBoxed(port);
  }));
  resetAttempt(req, true);
  int h3rc = runH3Single(mx, run_, req, h3Result);
  if (!h3rc) {
    result = ZuMv(h3Result);
    return 0;
  }

  req.url = ZuMv(h1URL);
  result = {};
  resetAttempt(req, true);
  return run<TLSClient>(mx, run_, req, &result);
}

int runH3DNSAltSvcFallback(
  ZiMultiplex &mx, Run &run_, Req &req,
  RequestResult &result)
{
  RequestResult h3Result;
  if (resolveH3Cached(run_, req.url, H3Policy::DNSOnly)) {
    resetAttempt(req, true);
    if (!runH3Single(mx, run_, req, h3Result)) {
      result = ZuMv(h3Result);
      return 0;
    }
  }

  result = {};
  return runH1AltSvcFirst(mx, run_, req, result);
}

int runReqSerial(ZiMultiplex &mx, Run &run_, Req &req)
{
  const auto &options = run_.options;
  int rc = 1;
  for (req.redirects = 0; req.redirects <= MaxRedirects; ++req.redirects) {
    RequestResult result;
    if (req.url.scheme == "http") {
      resetAttempt(req, true);
      rc = run<TCPClient>(mx, run_, req, &result);
    } else if (options.http3 == Http3Mode::force)
      rc = runH3Direct(mx, run_, req, result);
    else if (options.http3 == Http3Mode::disable) {
      resetAttempt(req, true);
      rc = run<TLSClient>(mx, run_, req, &result);
    } else
      rc = runH3DNSAltSvcFallback(mx, run_, req, result);

    if (rc || !redirectStatus(result.status) || !result.location) break;
    URL next;
    if (!parseLocation(req.url, result.location, next)) {
      auto ctx = reqLogCtx(req);
      ZiLOG(Error, "zhttp", ([
	ctx, location = ZeString(result.location)
      ](auto &s) {
	reqLogPrefix(ctx, s);
	s << "invalid redirect location: " << location;
      }));
      rc = 1;
      break;
    }
    auto ctx = reqLogCtx(req);
    ZiLOG(Info, "zhttp", ([
      ctx, scheme = ZeString(next.scheme), host = ZeString(next.host),
      target = ZeString(next.target)
    ](auto &s) {
      reqLogPrefix(ctx, s);
      s << "redirect: " << scheme << "://" << host << target;
    }));
    req.url = ZuMv(next);
    if (req.redirects == MaxRedirects) {
      auto ctx = reqLogCtx(req);
      ZiLOG(Error, "zhttp", ([ctx](auto &s) {
	reqLogPrefix(ctx, s);
	s << "too many redirects";
      }));
      rc = 1;
    }
  }
  req.failed = rc != 0;
  return rc;
}

int main(int argc, char **argv)
{
  ZiHeapCSV::init(::getenv("Z_HEAPTUNE"));
  ZiHashCSV::init(::getenv("Z_HASHTUNE"));

  Options options;
  argc = ZtCLI::load(options, argc, argv);
  if (options.help) usage(0);
  if (!validateOptions(options, argc)) usage();

  URL url;
  ZeException error;
  if (!parseURL(options.url, url, &error)) {
    ZiLogEvent(ZuMv(error));
    usage();
  }

  ZiLog::init("zhttp");
  ZiLog::level(
#ifdef ZiMultiplex_DEBUG
    options.debug ? Ze::Debug :
#endif
#ifdef Zquic_DEBUG
    options.quicDiag ? Ze::Info :
#endif
    options.memDiag ? Ze::Debug :
    options.verbose ? Ze::Info : Ze::Warning);
  ZiLog::sink(ZiLog::fileSink(ZiSinkOptions{}.path("&2")));
  ZiLog::start();

  ZiMultiplex mx(mxParams(options));
  if (!mx.start()) {
    ZiLOG(Error, "zhttp", "ZiMultiplex start failed");
    return 1;
  }

  Run run;
  run.options = options;
  run.originalURL = url;
  int rc = 0;
  if (options.http3 == Http3Mode::force && url.scheme == "https") {
    rc = runH3Multi(mx, run);
  } else if (url.scheme == "http") {
    rc = runH1Pool<H1TCPClient>(mx, run);
  } else if (options.http3 == Http3Mode::disable && url.scheme == "https") {
    rc = runH1Pool<H1TLSClient>(mx, run);
  } else {
    Req req;
    for (unsigned i = 0; i < options.requests; ++i) {
      initReq(req, run, i);
      if (runReqSerial(mx, run, req)) {
	++run.failed;
	rc = 1;
      }
      ++run.complete;
    }
  }

  mx.stop();
  ZiLog::stop();

  return rc;
}
