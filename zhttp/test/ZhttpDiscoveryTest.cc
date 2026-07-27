//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// HTTPS/SVCB wire-format tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpDiscovery.hh>

using namespace ZuTestUtil;

namespace ZhttpDiscoveryTest_ {

using Bytes =
  ZtArray<uint8_t, ZtArrayHeapID<"Zhttp.Discovery.Test">>;

void u16(Bytes &out, unsigned v)
{
  out.push(uint8_t(v >> 8));
  out.push(uint8_t(v));
}

void name(Bytes &out, ZuCSpan value)
{
  unsigned start = 0;
  while (start < value.length()) {
    unsigned end = start;
    while (end < value.length() && value[end] != '.') ++end;
    out.push(uint8_t(end - start));
    while (start < end) out.push(uint8_t(value[start++]));
    ++start;
  }
  out.push(0);
}

Bytes rdata(
  unsigned priority = 1, unsigned mandatory = 0,
  ZuCSpan alpn = "h3", bool port = true, ZuCSpan target = {})
{
  Bytes rdata;
  u16(rdata, priority);		// service priority
  if (target)
    name(rdata, target);
  else
    rdata.push(0);		// target is the owner
  if (!priority) return rdata;
  if (mandatory) {
    u16(rdata, 0); u16(rdata, 2); u16(rdata, mandatory);
  }
  u16(rdata, 1); u16(rdata, alpn.length() + 1);	// alpn
  rdata.push(uint8_t(alpn.length()));
  for (unsigned i = 0; i < alpn.length(); ++i) rdata.push(alpn[i]);
  if (port) {
    u16(rdata, 3); u16(rdata, 2); u16(rdata, 8443);
  }
  u16(rdata, 4); u16(rdata, 4);	// ipv4hint
  rdata.push(1); rdata.push(2); rdata.push(3); rdata.push(4);
  return rdata;
}

Bytes message(
  const Bytes *records, unsigned count,
  ZuCSpan owner = "example.com", unsigned answers = ~0U)
{
  if (answers == ~0U) answers = count;
  Bytes msg;
  u16(msg, 0x1234); u16(msg, 0x8180);
  u16(msg, 0); u16(msg, answers); u16(msg, 0); u16(msg, count - answers);
  for (unsigned record = 0; record < count; ++record) {
    name(msg, owner);
    u16(msg, 65); u16(msg, 1);
    msg.push(0); msg.push(0); msg.push(0); msg.push(60);
    u16(msg, records[record].length());
    for (unsigned i = 0; i < records[record].length(); ++i)
      msg.push(records[record][i]);
  }
  return msg;
}

Bytes record()
{
  Bytes record = rdata();
  return message(&record, 1);
}

struct Resolver {
  Resolver() : ops{
    .context = this,
    .query = query_,
    .resolve = resolve_,
    .cancel = cancel_
  } { }

  static ZmRef<ZiResolver_::Query> query_(
    void *context, ZiResolver_::Host host, uint16_t, uint16_t,
    ZiResolver_::QueryFn fn)
  {
    auto self = static_cast<Resolver *>(context);
    self->queryHost = ZuMv(host);
    self->queryFn = ZuMv(fn);
    ++self->queries;
    return {};
  }

  static ZmRef<ZiResolver_::Query> resolve_(
    void *context, ZiResolver_::Host host, ZiResolver_::ResolveFn fn)
  {
    auto self = static_cast<Resolver *>(context);
    self->resolveHost = ZuMv(host);
    self->resolveFn = ZuMv(fn);
    ++self->resolves;
    return {};
  }

  static void cancel_(void *context, ZmRef<ZiResolver_::Query>)
  {
    auto self = static_cast<Resolver *>(context);
    self->queryFn = ZiResolver_::QueryFn{};
    self->resolveFn = ZiResolver_::ResolveFn{};
    ++self->cancels;
  }

  void answer(Bytes bytes)
  {
    ZiResolver_::DNSMsg msg;
    for (unsigned i = 0; i < bytes.length(); ++i)
      msg.buf.push(bytes[i]);
    auto fn = ZuMv(queryFn);
    fn(ZiResolver_::QueryResult{ZuMv(msg)});
  }

  void address(ZiIP ip)
  {
    if (!resolveFn(ZiResolver_::ResolveResult{ip}))
      resolveFn = ZiResolver_::ResolveFn{};
  }

  void resolved()
  {
    auto fn = ZuMv(resolveFn);
    fn(ZiResolver_::ResolveResult{});
  }

  Zhttp::DiscoveryResolver ops;
  ZiResolver_::Host	queryHost;
  ZiResolver_::Host	resolveHost;
  ZiResolver_::QueryFn	queryFn;
  ZiResolver_::ResolveFn resolveFn;
  unsigned		queries = 0;
  unsigned		resolves = 0;
  unsigned		cancels = 0;
};

void parse()
{
  ZuTestScope(parse);
  auto msg = record();
  Zhttp::SvcRecords records;
  auto e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && records.length() == 1, "parse service record");
  const auto &record = records[0];
  ZuCHECK(record.priority == 1 && record.target == "example.com" &&
      record.h3 && record.hasALPN && record.port == 8443,
    "retain service parameters");
  ZuCHECK(record.ipv4Hints.length() == 1 &&
      record.ipv4Hints[0] == ZiIP{"1.2.3.4"},
    "retain address hint");
}

void failures()
{
  ZuTestScope(failures);
  auto msg = record();
  Zhttp::SvcRecords records;
  auto limits = Zhttp::DiscoveryLimits{};
  limits.maxHints = 0;
  auto e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", limits);
  ZuCHECK(e.code == Zhttp::DiscoveryCode::HintLimit,
    "explicit hint overflow");

  msg.length(msg.length() - 1);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.code == Zhttp::DiscoveryCode::Malformed,
    "truncated record");

  Bytes unsupported = rdata(1, 42);
  msg = message(&unsupported, 1);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && records[0].unsupportedMandatory,
    "retain unsupported mandatory outcome");

  Bytes missing = rdata(1, 3, "h3", false);
  msg = message(&missing, 1);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && records[0].unsupportedMandatory,
    "mandatory key must be present");

  Bytes alias = rdata(0);
  Bytes service = rdata();
  Bytes mixed[] = {ZuMv(alias), ZuMv(service)};
  msg = message(mixed, 2);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.code == Zhttp::DiscoveryCode::Malformed,
    "AliasMode RRSet must contain exactly one record");

  Bytes aliasParams = rdata(0);
  u16(aliasParams, 1); u16(aliasParams, 3);
  aliasParams.push(2); aliasParams.push('h'); aliasParams.push('3');
  msg = message(&aliasParams, 1);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.code == Zhttp::DiscoveryCode::Malformed,
    "AliasMode record must not contain SvcParams");
}

void alpn()
{
  ZuTestScope(alpn);
  Bytes draft = rdata(1, 0, "h3-29");
  auto msg = message(&draft, 1);
  Zhttp::SvcRecords records;
  auto e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "Example.COM", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && records.length() == 1 && records[0].h3 &&
      records[0].target == "example.com",
    "exact versioned H3 ALPN");

  Bytes falsePositive = rdata(1, 0, "xh3");
  msg = message(&falsePositive, 1);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && !records[0].h3, "no H3 substring matching");
}

void rrset()
{
  ZuTestScope(rrset);
  Bytes source[] = {
    rdata(9, 0, "h3", true, "slow.example"),
    rdata(2, 0, "h3", true, "fast.example"),
    rdata(2, 0, "h3", true, "peer.example")
  };
  auto msg = message(source, 3);
  Zhttp::SvcRecords records;
  auto e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "EXAMPLE.COM.", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.ok() && records.length() == 3 &&
      records[0].priority == 2 && records[0].target == "fast.example" &&
      records[1].priority == 2 && records[1].target == "peer.example" &&
      records[2].priority == 9 && records[2].target == "slow.example",
    "order complete queried answer RRSet by service priority");

  msg = message(source, 3, "other.example");
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.code == Zhttp::DiscoveryCode::NoRecord,
    "ignore unrelated answer owner");

  msg = message(source, 3, "example.com", 0);
  e = Zhttp::parseHTTPS(
    records, ZuBSpan{msg}, "example.com", Zhttp::DiscoveryLimits{});
  ZuCHECK(e.code == Zhttp::DiscoveryCode::NoRecord,
    "ignore additional-section HTTPS records");
}

void cancel()
{
  ZuTestScope(cancel);
  unsigned callbacks = 0;
  int code = Zhttp::DiscoveryCode::OK;
  auto request = Zhttp::discoverH3(
    Zhttp::Origin{"https", "cancel.invalid", 443},
    "cancel.invalid", 443, false, Zhttp::DiscoveryLimits{},
    Zhttp::DiscoveryFn{[&callbacks, &code](
	Zhttp::DiscoveryError error, Zhttp::Endpoints) {
      ++callbacks;
      code = error.code;
    }});
  request->cancel();
  request->cancel();
  ZuCHECK(callbacks == 1 && code == Zhttp::DiscoveryCode::Cancelled,
    "cancel completes exactly once");
  ZiResolver::stop();
  ZiResolver::final();
}

void direct()
{
  ZuTestScope(direct);
  unsigned callbacks = 0;
  int code = Zhttp::DiscoveryCode::ResolveFailure;
  Zhttp::Endpoints resolved;
  Zhttp::Endpoint endpoint{
    .origin = {"https", "origin.example", 443},
    .target = "127.0.0.1",
    .tlsName = "origin.example",
    .port = 8443,
    .priority = 7,
    .source = Zhttp::EndpointSource::AltSvc,
    .httpVersion = Zhttp::Version::H3
  };
  auto request = Zhttp::resolveH3(
    endpoint, Zhttp::DiscoveryLimits{},
    Zhttp::DiscoveryFn{[&callbacks, &code, &resolved](
	Zhttp::DiscoveryError error, Zhttp::Endpoints endpoints) {
      ++callbacks;
      code = error.code;
      resolved = ZuMv(endpoints);
    }});
  request->cancel();
  ZuCHECK(callbacks == 1 && code == Zhttp::DiscoveryCode::OK &&
      resolved.length() == 1,
    "literal target resolves and completes exactly once");
  ZuCHECK(resolved[0].origin == endpoint.origin &&
      resolved[0].target == endpoint.target &&
      resolved[0].tlsName == endpoint.tlsName &&
      resolved[0].ip == ZiIP{"127.0.0.1"} &&
      resolved[0].port == endpoint.port &&
      resolved[0].priority == endpoint.priority &&
      resolved[0].source == endpoint.source,
    "direct resolution preserves Alt-Svc endpoint identity");
  ZiResolver::stop();
  ZiResolver::final();
}

void traversal()
{
  ZuTestScope(traversal);
  Resolver resolver;
  unsigned callbacks = 0;
  Zhttp::DiscoveryError result{0, Zhttp::DiscoveryCode::ResolveFailure};
  Zhttp::Endpoints endpoints;
  auto request = Zhttp::discoverH3(
    Zhttp::Origin{"https", "example.com", 443},
    "example.com", 443, false, Zhttp::DiscoveryLimits{},
    Zhttp::DiscoveryFn{[&](auto error, auto value) {
      ++callbacks;
      result = error;
      endpoints = ZuMv(value);
    }}, &resolver.ops);
  ZuCHECK(resolver.queries == 1 && resolver.queryHost == "example.com",
    "query origin");

  Bytes alias = rdata(0, 0, {}, true, "alias.example");
  resolver.answer(message(&alias, 1, "example.com"));
  ZuCHECK(resolver.queries == 2 && resolver.queryHost == "alias.example",
    "traverse AliasMode target");

  Bytes service = rdata(2, 0, "h3", true, "svc.example");
  resolver.answer(message(&service, 1, "alias.example"));
  ZuCHECK(resolver.resolves == 1 && resolver.resolveHost == "svc.example",
    "resolve ServiceMode target");

  resolver.address(ZiIP{"5.6.7.8"});
  resolver.address(ZiIP{"::1"});
  resolver.resolved();
  ZuCHECK(callbacks == 1 && result.ok() && endpoints.length() == 3,
    "complete once after multi-response resolution");
  ZuCHECK(endpoints[0].ip == ZiIP{"1.2.3.4"} &&
      endpoints[1].ip == ZiIP{"5.6.7.8"} &&
      endpoints[2].ip == ZiIP{"::1"},
    "preserve hint then resolver response order");
  bool identity = true;
  for (unsigned i = 0; i < endpoints.length(); ++i)
    identity &= endpoints[i].target == "svc.example" &&
      endpoints[i].tlsName == "example.com" &&
      endpoints[i].port == 8443 && endpoints[i].priority == 2;
  ZuCHECK(identity, "preserve discovered endpoint identity");
  request->cancel();
  ZuCHECK(callbacks == 1, "late cancellation is suppressed");
}

void aliasLoop()
{
  ZuTestScope(aliasLoop);
  Resolver resolver;
  unsigned callbacks = 0;
  int code = Zhttp::DiscoveryCode::OK;
  auto request = Zhttp::discoverH3(
    Zhttp::Origin{"https", "example.com", 443},
    "example.com", 443, false, Zhttp::DiscoveryLimits{},
    Zhttp::DiscoveryFn{[&](auto error, auto) {
      ++callbacks;
      code = error.code;
    }}, &resolver.ops);
  Bytes alias = rdata(0, 0, {}, true, "loop.example");
  resolver.answer(message(&alias, 1, "example.com"));
  alias = rdata(0, 0, {}, true, "example.com");
  resolver.answer(message(&alias, 1, "loop.example"));
  ZuCHECK(callbacks == 1 && code == Zhttp::DiscoveryCode::AliasLoop,
    "reject AliasMode loop exactly once");
  request->cancel();
  ZuCHECK(callbacks == 1, "suppress cancellation after alias failure");
}

} // namespace ZhttpDiscoveryTest_

int main(int argc, char **argv)
{
  using namespace ZhttpDiscoveryTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(parse);
  ZuTestCall(failures);
  ZuTestCall(alpn);
  ZuTestCall(rrset);
  ZuTestCall(cancel);
  ZuTestCall(direct);
  ZuTestCall(traversal);
  ZuTestCall(aliasLoop);
  return 0;
}
