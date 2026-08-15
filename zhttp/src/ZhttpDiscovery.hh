//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z http library - HTTPS/SVCB records and HTTP endpoint discovery

#ifndef ZhttpDiscovery_HH
#define ZhttpDiscovery_HH

#ifndef ZhttpLib_HH
#include <zlib/ZhttpLib.hh>
#endif

#include <stdint.h>

#include <zlib/ZuBox.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTime.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtEnum.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpCore.hh>
#include <zlib/ZhttpURL.hh>

namespace Zhttp {

ZtEnumStruct(ZhttpAPI, EndpointSource, int8_t,
  Origin, HTTPS, IPv4Hint, IPv6Hint, AltSvc);

struct DiscoveryLimits {
  unsigned	maxRecords = 16;
  unsigned	maxHints = 16;
  unsigned	maxEndpoints = 32;
  unsigned	maxAliasDepth = 8;
};

struct DiscoveryCode {
  enum {
    OK,
    NoRecord,
    Malformed,
    UnsupportedMandatory,
    RecordLimit,
    HintLimit,
    EndpointLimit,
    AliasLimit,
    AliasLoop,
    ResolveFailure,
    Cancelled
  };
};

struct DiscoveryError {
  uint32_t	offset = 0;
  int8_t	code = DiscoveryCode::OK;

  bool ok() const { return code == DiscoveryCode::OK; }
};

using IPArray = ZtArray<ZiIP, ZtArrayHeapID<"Zhttp.Discovery.IP">>;

struct SvcRecord {
  URLString	target;
  IPArray	ipv4Hints;
  IPArray	ipv6Hints;
  uint32_t	order = 0;
  uint16_t	priority = 0;
  uint16_t	port = 0;
  bool		noDefaultALPN = false;
  bool		hasALPN = false;
  bool		unsupportedMandatory = false;
  bool		h3 = false;

  bool alias() const { return !priority; }
};

using SvcRecords =
  ZtArray<SvcRecord, ZtArrayHeapID<"Zhttp.Discovery.Record">>;

struct Endpoint {
  Origin	origin;
  URLString	target;
  URLString	tlsName;
  ZiIP		ip;
  uint16_t	port = 0;
  uint16_t	priority = 0;
  EndpointSource::T source = EndpointSource::Origin;
  Version::T	httpVersion = Version::H1;
};

using Endpoints =
  ZtArray<Endpoint, ZtArrayHeapID<"Zhttp.Discovery.Endpoint">>;

ZhttpAPI DiscoveryError parseHTTPS(
  SvcRecords &, ZuBSpan, ZuCSpan owner, const DiscoveryLimits &);

using DiscoveryFn = ZmFn<void(DiscoveryError, Endpoints),
  ZmFnHeapID<"Zhttp.Discovery.Fn">>;

// Injectable resolver operations for deterministic discovery tests.
// Null selects ZiResolver; an injected object must outlive its requests.
struct DiscoveryResolver {
  using QueryFn = ZmRef<ZiResolver_::Query> (*)(
    void *, ZiResolver_::Host, uint16_t, uint16_t,
    ZiResolver_::QueryFn);
  using ResolveFn = ZmRef<ZiResolver_::Query> (*)(
    void *, ZiResolver_::Host, ZiResolver_::ResolveFn);
  using CancelFn = void (*)(void *, ZmRef<ZiResolver_::Query>);

  void		*context = nullptr;
  QueryFn	query = nullptr;
  ResolveFn	resolve = nullptr;
  CancelFn	cancel = nullptr;
};

// One asynchronous HTTPS/SVCB lookup, alias traversal, and address resolution.
// Cancellation completes once with Cancelled and suppresses resolver callbacks.
class ZhttpAPI DiscoveryRequest : public ZmObject {
public:
  DiscoveryRequest(
    Origin, ZuCSpan host, uint16_t port, bool blindFallback,
    DiscoveryLimits, DiscoveryFn, const DiscoveryResolver *);
  DiscoveryRequest(
    Endpoint, DiscoveryLimits, DiscoveryFn, const DiscoveryResolver *);

  void start();
  void cancel();

private:
  void query();
  void queried(ZiResolver_::QueryResult);
  void resolveNext();
  bool resolved(ZiResolver_::ResolveResult);
  void installResolver(uint64_t, ZmRef<ZiResolver_::Query>);
  bool add(Endpoint);
  void finish(DiscoveryError);

  Origin		m_origin;
  Endpoint		m_directEndpoint;
  URLString		m_host;
  URLString		m_query;
  URLString		m_target;
  SvcRecords		m_records;
  Endpoints		m_endpoints;
  ZtArray<URLString,
    ZtArrayHeapID<"Zhttp.Discovery.Alias">> m_aliases;
  DiscoveryFn		m_fn;
  ZmRef<ZiResolver_::Query> m_resolver;
  const DiscoveryResolver *m_resolverOps = nullptr;
  DiscoveryLimits	m_limits;
  uint64_t		m_resolverGeneration = 0;
  unsigned		m_record = 0;
  uint16_t		m_port = 0;
  ZmAtomic<unsigned>	m_done = 0;
  bool			m_blindFallback = false;
  bool			m_advertised = false;
  bool			m_blind = false;
  bool			m_direct = false;
};

using DiscoveryRequestRef = ZmRef<DiscoveryRequest>;

ZhttpAPI DiscoveryRequestRef discoverH3(
  Origin, ZuCSpan host, uint16_t port, bool blindFallback,
  DiscoveryLimits, DiscoveryFn, const DiscoveryResolver * = nullptr);
ZhttpAPI DiscoveryRequestRef resolveH3(
  Endpoint, DiscoveryLimits, DiscoveryFn,
  const DiscoveryResolver * = nullptr);

ZtEnumNS(ZhttpAPI, AltSvcParseCode, int8_t,
  OK, InvalidSyntax, InvalidAuthority, InvalidPort, TooManyAlternatives);

struct AltSvcParseError {
  uint32_t		offset = 0;
  AltSvcParseCode::T	code = AltSvcParseCode::OK;

  bool ok() const { return code == AltSvcParseCode::OK; }
};

ZhttpAPI bool h3Protocol(ZuBSpan);

struct AltSvcValue {
  ZuBSpan	protocolID;
  ZuBSpan	host;
  uint32_t	maxAge = 86400;
  uint16_t	port = 0;
  bool		persist = false;
  bool		h3 = false;
  bool		ipv6Literal = false;
  bool		hostOmitted = false;
  bool		normalized = false;

  template <typename S> void print(S &s) const {
    s << protocolID << "=\"";
    if (hostOmitted) {
      s << ':' << ZuBoxed(port);
    } else {
      if (ipv6Literal) s << '[';
      s << host;
      if (ipv6Literal) s << ']';
      s << ':' << ZuBoxed(port);
    }
    s << '"';
    s << "; ma=" << ZuBoxed(maxAge);
    if (persist) s << "; persist=1";
  }
  friend ZuPrintFn ZuPrintType(AltSvcValue *);
};

struct AltSvcValuesView {
  ZuSpan<const AltSvcValue>	values;

  template <typename S> void print(S &s) const {
    for (unsigned i = 0, n = values.length(); i < n; ++i) {
      if (i) s << ", ";
      s << values[i];
    }
  }
  friend ZuPrintFn ZuPrintType(AltSvcValuesView *);
};

struct AltSvcClear {
  template <typename S> void print(S &s) const { s << "clear"; }
  friend ZuPrintFn ZuPrintType(AltSvcClear *);
};

class AltSvcCursor {
public:
  AltSvcCursor(ZuBSpan, OriginView, unsigned maxAlternatives);
  AltSvcCursor(ZuSpan<uint8_t>, OriginView, unsigned maxAlternatives);

  bool next(AltSvcValue &);
  AltSvcParseError error() const { return m_error; }
  bool clear() const { return m_clear; }

  template <typename L> bool each(L &&l) {
    AltSvcValue value;
    while (next(value)) l(value);
    return m_error.ok();
  }

private:
  void init_();

  ZuBSpan	m_input;
  OriginView	m_origin;
  uint8_t	*m_mutable = nullptr;
  AltSvcParseError m_error;
  unsigned	m_maxAlternatives = 0;
  unsigned	m_offset = 0;
  unsigned	m_count = 0;
  bool		m_clear = false;
  bool		m_more = false;
  bool		m_done = false;
};

struct AltSvcValueStorage {
  struct Part {
    uint32_t offset = 0;
    uint32_t length = 0;
  };

  URLString	data;
  Part		protocolID;
  Part		host;
  uint32_t	maxAge = 86400;
  uint16_t	port = 0;
  bool		persist = false;
  bool		h3 = false;
  bool		ipv6Literal = false;
  bool		hostOmitted = false;

  AltSvcValueStorage() = default;
  AltSvcValueStorage(const AltSvcValue &);

  AltSvcValue view(OriginView) const;
};

struct CachedAltSvc {
  AltSvcValueStorage	value;
  ZuTime		expires;
};

using CachedAltSvcValues =
  ZtArray<CachedAltSvc, ZtArrayHeapID<"Zhttp.AltSvc.CacheValue">>;
struct AltSvcEntry { CachedAltSvcValues values; };
using AltSvcTable = ZmHashKV<
  Origin, AltSvcEntry, ZmHashHeapID<"Zhttp.AltSvc.Cache">>;
using AltSvcTableRef = ZmRef<AltSvcTable>;

class AltSvcCache {
public:
  explicit AltSvcCache(unsigned maxOrigins) :
    m_entries{new AltSvcTable}, m_maxOrigins{maxOrigins} { }

  bool update(
    const Origin &, ZuBSpan, unsigned maxAlternatives, ZuTime now);
  bool update(
    const Origin &, ZuSpan<uint8_t>, unsigned maxAlternatives, ZuTime now);

  template <typename L>
  bool get(const Origin &origin, L &&l, ZuTime now) {
    auto node = m_entries->find(origin);
    if (!node) return false;
    auto &cached = node->val().values;
    unsigned n = 0;
    for (unsigned i = 0, m = cached.length(); i < m; ++i) {
      if (cached[i].expires <= now) continue;
      if (n != i) cached[n] = ZuMv(cached[i]);
      l(cached[n].value.view(origin.view()));
      ++n;
    }
    if (n < cached.length()) cached.splice(n);
    if (n) return true;
    m_entries->del(origin);
    return false;
  }

  bool hasH3(const Origin &origin, ZuTime now) {
    bool found = false;
    get(origin, [&found](const AltSvcValue &value) { found |= value.h3; }, now);
    return found;
  }
  void del(const Origin &origin) { m_entries->del(origin); }
  unsigned count() const { return m_entries->count_(); }

private:
  template <typename Span>
  bool update_(const Origin &, Span, unsigned, ZuTime);

  AltSvcTableRef	m_entries;
  unsigned	m_maxOrigins = 0;
};


} // namespace Zhttp

#endif /* ZhttpDiscovery_HH */
