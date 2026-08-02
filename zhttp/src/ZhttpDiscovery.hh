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

#include <zlib/ZtArray.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmObject.hh>

#include <zlib/ZiIP.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpConfig.hh>
#include <zlib/ZhttpURL.hh>

namespace Zhttp {

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

} // namespace Zhttp

#endif /* ZhttpDiscovery_HH */
