//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_discovery.hh>
#include <zlib/zumd_key_db.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>
namespace Zum {

bool appIssuerPath(AppID appID, String &path)
{
  if (!appID) return false;
  path.null();
  ZfURI::savePath(path, AppIssuerPath{.oauth2 = "oauth2", .appID = appID});
  return bool(path);
}

bool appEndpointPath(AppID appID, ZuCSpan group, ZuCSpan endpoint,
    String &path)
{
  if (!appID || !group || !endpoint) return false;
  path.null();
  ZfURI::savePath(path, AppEndpointPath{
    .oauth2 = "oauth2", .appID = appID,
    .group = group, .endpoint = endpoint});
  return bool(path);
}

bool appNestedEndpointPath(AppID appID, ZuCSpan group,
    ZuCSpan section, ZuCSpan endpoint, String &path)
{
  if (!appID || !group || !section || !endpoint) return false;
  path.null();
  ZfURI::savePath(path, AppNestedEndpointPath{
    .oauth2 = "oauth2", .appID = appID, .group = group,
    .section = section, .endpoint = endpoint});
  return bool(path);
}

bool appOIDCMetadataPath(AppID appID, String &path)
{
  if (!appID) return false;
  path.null();
  ZfURI::savePath(path, AppOIDCMetadataPath{
    .oauth2 = "oauth2", .appID = appID,
    .wellKnown = ".well-known", .endpoint = "openid-configuration"});
  return bool(path);
}

bool appOAuthMetadataPath(AppID appID, String &path)
{
  if (!appID) return false;
  path.null();
  ZfURI::savePath(path, AppOAuthMetadataPath{
    .wellKnown = ".well-known",
    .endpoint = "oauth-authorization-server",
    .oauth2 = "oauth2", .appID = appID});
  return bool(path);
}

bool appIssuer(ZuCSpan authorizationBase, AppID appID, String &issuer)
{
  if (!authorizationBase || authorizationBase.find<"?">() >= 0 ||
      authorizationBase.find<"#">() >= 0) return false;
  if (!appID) return false;
  issuer = authorizationBase;
  while (unsigned length = issuer.length()) {
    if (issuer[length - 1] != '/') break;
    issuer.length(length - 1);
  }
  ZfURI::savePath(issuer,
    AppIssuerPath{.oauth2 = "oauth2", .appID = appID});
  return bool(issuer);
}

struct Metadata {
  String issuer;
  String authorizationEndpoint;
  String tokenEndpoint;
  String jwksURI;
  String revocationEndpoint;
  String userinfoEndpoint;
  StringVec responseTypesSupported;
  StringVec grantTypesSupported;
  StringVec tokenAuthMethods;
  StringVec revokeAuthMethods;
  StringVec subjectTypesSupported;
  StringVec idTokenAlgs;
  StringVec scopesSupported;
  StringVec claimsSupported;
  StringVec codeChallengeMethods;
};
ZfStruct(, (Metadata, JSON),
  (((issuer),		(Required)),						String),
  (((authorizationEndpoint), (JSON::ID<"authorization_endpoint">, Required)),	String),
  (((tokenEndpoint),	(JSON::ID<"token_endpoint">, Required)),		String),
  (((jwksURI),		(JSON::ID<"jwks_uri">, Required)),			String),
  (((revocationEndpoint), (JSON::ID<"revocation_endpoint">, Required)),		String),
  (((userinfoEndpoint), (JSON::ID<"userinfo_endpoint">, Required)),		String),
  (((responseTypesSupported), (JSON::ID<"response_types_supported">,
    Required)),									StringVec),
  (((grantTypesSupported), (JSON::ID<"grant_types_supported">, Required)),	StringVec),
  (((tokenAuthMethods), (JSON::ID<"token_endpoint_auth_methods_supported">,
    Required)),									StringVec),
  (((revokeAuthMethods), (JSON::ID<"revocation_endpoint_auth_methods_supported">,
    Required)),									StringVec),
  (((subjectTypesSupported), (JSON::ID<"subject_types_supported">, Required)),	StringVec),
  (((idTokenAlgs), (JSON::ID<"id_token_signing_alg_values_supported">,
    Required)),									StringVec),
  (((scopesSupported), (JSON::ID<"scopes_supported">, Required)),		StringVec),
  (((claimsSupported), (JSON::ID<"claims_supported">, Required)),		StringVec),
  (((codeChallengeMethods), (JSON::ID<"code_challenge_methods_supported">,
    Required)),									StringVec));

using PublicJWK = ZfJSON::Union<>;
ZuDerive(PublicJWKArray, (ZtArray<PublicJWK,
  ZtArrayHeapID<"Zum.Discovery.JWKs">>));
struct PublicJWKVec : public PublicJWKArray {
  ZuDerive_(PublicJWKVec, PublicJWKArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(PublicJWKVec *);
};
struct JWKSReply { PublicJWKVec keys; };
ZfStruct(, (JWKSReply, JSON),
  (((keys),		(Required)),	UDT));
ZuDerive(PublicJWKRootArray, (ZtArray<ZuPtr<ZfJSON::AnyNode>,
  ZtArrayHeapID<"Zum.Discovery.JWKRoots">>));

static bool publicJWK(
    String &source, PublicJWK &jwk, ZuPtr<ZfJSON::AnyNode> &owner)
{
  auto parsed = ZfJSON::scan(source.span());
  if (parsed.p<0>() != int(source.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  auto handler = ZfJSON::handler<PublicJWK>(roots[0]);
  if (!handler.valid) return false;
  jwk = static_cast<const ZfJSON::AnyNode *>(roots[0]);
  owner = ZuMv(parsed.p<1>());
  return true;
}

static bool jwksJSON_(StringVec &sources, String &json, bool strict)
{
  JWKSReply reply;
  PublicJWKRootArray owners;
  for (auto &source: sources) {
    PublicJWK jwk;
    ZuPtr<ZfJSON::AnyNode> owner;
    if (!publicJWK(source, jwk, owner)) {
      if (strict) return false;
      continue;
    }
    reply.keys.push(ZuMv(jwk));
    owners.push(ZuMv(owner));
  }
  ZfJSON::save(json, reply);
  return true;
}

template <typename Heap = ZuVoid>
class SignKeyLoad__ : public Heap, public ZmObject  {
public:
  SignKeyLoad__(DBContext *context, String issuer, int64_t now,
      int64_t expires, unsigned maxKeys, SignKeyFn complete) :
    m_context{context}, m_issuer{ZuMv(issuer)}, m_now{now},
    m_expires{expires}, m_maxKeys{maxKeys}, m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_issuer || m_now <= 0 || m_expires <= m_now ||
	!m_maxKeys || m_maxKeys == UINT_MAX) {
      m_complete(SignKey{});
      return;
    }
    using Tuple = SignKeyTable::Tuple;
    m_context->signKeys->selectRows<2>(ZuFwdTuple(m_issuer), m_maxKeys + 1, [
      self = ZmRef<SignKeyLoad__>{this}
    ](ZuUnion<void, Tuple> result, unsigned count) mutable {
      if (result.template is<Tuple>()) {
	if (count > self->m_maxKeys) {
	  self->m_overflow = true;
	  return;
	}
	const auto &key = result.template p<Tuple>();
	if (key.template p<1>() != self->m_issuer ||
	    key.template p<2>() != "ES256" ||
	    key.template p<8>() != State::Active ||
	    key.template p<6>() > self->m_now ||
	    (key.template p<7>() && key.template p<7>() < self->m_expires) ||
	    bool(key.template p<3>()) == bool(key.template p<5>())) return;
	if (!self->m_id || key.template p<6>() > self->m_start ||
	    (key.template p<6>() == self->m_start &&
	      ZuCmp<String>::cmp(key.template p<0>(), self->m_id) > 0)) {
	  self->m_id = key.template p<0>();
	  self->m_start = key.template p<6>();
	}
	return;
      }
      if (self->m_overflow || !self->m_id) {
	self->m_complete(SignKey{});
	return;
      }
      String id = self->m_id;
      auto table = self->m_context->signKeys;
      table->run(0, [table, id = ZuMv(id), self = ZuMv(self)]() mutable {
	table->template find<0>(0, ZuFwdTuple(ZuMv(id)), [
	  self = ZuMv(self)
	](ZdbRowRef<SignKey> row) mutable {
	  if (!row || row->data().issuer != self->m_issuer ||
	      row->data().algorithm != "ES256" ||
	      row->data().state != State::Active ||
	      row->data().notBefore > self->m_now ||
	      (row->data().retireAfter && row->data().retireAfter < self->m_expires) ||
	      bool(row->data().providerRef) == bool(row->data().privateMaterial)) {
	    self->m_complete(SignKey{});
	    return;
	  }
	  self->m_complete(SignKey{row->data()});
	});
      });
    });
  }

private:
  DBContext *m_context;
  String m_issuer;
  int64_t m_now;
  int64_t m_expires;
  unsigned m_maxKeys;
  SignKeyFn m_complete;
  String m_id;
  int64_t m_start = 0;
  bool m_overflow = false;
};
ZuDerive(SignKeyLoadHeap,
  (ZmHeap<"Zum.zumd.discovery.SignKeyLoad", SignKeyLoad__<>>));
ZuDerive(SignKeyLoad_, (SignKeyLoad__<SignKeyLoadHeap>));

void signKeyLoad(DBContext *context, String issuer, int64_t now,
    int64_t expires, unsigned maxKeys, SignKeyFn complete)
{
  ZmRef<SignKeyLoad_> load = new SignKeyLoad_{
    context, ZuMv(issuer), now, expires, maxKeys, ZuMv(complete)};
  load->start();
}

template <typename Heap = ZuVoid>
class DiscoveryComplete__ : public Heap, public ZmObject  {
public:
  DiscoveryComplete__(DiscoveryFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(bool ok, String value)
  {
    m_request->complete([
      self = ZmRef<DiscoveryComplete__>{this}, ok, value = ZuMv(value)
    ]() mutable {
      auto complete = ZuMv(self->m_complete);
      complete(ok, ZuMv(value));
    });
  }

  void cancel()
  {
    auto complete = ZuMv(m_complete);
    complete(false, String{});
  }

private:
  ZmRef<Request>	m_request;
  DiscoveryFn	m_complete;
};
ZuDerive(DiscoveryCompleteHeap,
  (ZmHeap<"Zum.zumd.discovery.DiscoveryComplete", DiscoveryComplete__<>>));
ZuDerive(DiscoveryComplete_,
  (DiscoveryComplete__<DiscoveryCompleteHeap>));

static bool endpoints(ZuCSpan authorizationBase, ZuCSpan issuer,
    AppID appID, Metadata &metadata)
{
  if (!authorizationBase || !issuer || !appID) return false;
  auto endpoint = [authorizationBase, appID](
      ZuCSpan name, String &value) {
    value = authorizationBase;
    while (unsigned length = value.length()) {
      if (value[length - 1] != '/') break;
      value.length(length - 1);
    }
    ZfURI::savePath(value, AppEndpointPath{
      .oauth2 = "oauth2", .appID = appID,
      .group = "v1", .endpoint = name});
    return bool(value);
  };
  metadata.issuer = issuer;
  return endpoint("authorize", metadata.authorizationEndpoint) &&
    endpoint("token", metadata.tokenEndpoint) &&
    endpoint("keys", metadata.jwksURI) &&
    endpoint("revoke", metadata.revocationEndpoint) &&
    endpoint("userinfo", metadata.userinfoEndpoint);
}

String metadataJSON(
    ZuCSpan authorizationBase, AppID appID, const StringVec &scopes)
{
  StringVec supported{"openid", "profile", "email", "offline_access"};
  for (const auto &scope: scopes) {
    bool duplicate = false;
    for (const auto &existing: supported)
      if (existing == scope) { duplicate = true; break; }
    if (!duplicate) supported.push(scope);
  }
  Metadata metadata;
  String issuer;
  if (!appIssuer(authorizationBase, appID, issuer)) return {};
  if (!endpoints(authorizationBase, issuer, appID, metadata)) return {};
  metadata.responseTypesSupported = {"code"};
  metadata.grantTypesSupported = {"authorization_code", "refresh_token",
    "client_credentials"};
  metadata.tokenAuthMethods = {"client_secret_basic", "none"};
  metadata.revokeAuthMethods = {"client_secret_basic", "none"};
  metadata.subjectTypesSupported = {"public"};
  metadata.idTokenAlgs = {"ES256"};
  metadata.scopesSupported = ZuMv(supported);
  metadata.claimsSupported = {"sub", "iss", "aud", "exp", "iat",
    "auth_time", "nonce", "amr", "name", "preferred_username", "email"};
  metadata.codeChallengeMethods = {"S256"};
  String json;
  ZfJSON::save(json, metadata);
  return json;
}

String jwksJSON(const StringVec &publicJwks)
{
  StringVec sources;
  for (const auto &source: publicJwks) sources.push(source);
  String json;
  jwksJSON_(sources, json, false);
  return json;
}

template <typename Heap = ZuVoid>
class JWKSLoad__ : public Heap, public ZmPolymorph  {
public:
  JWKSLoad__(
      DBContext *context, String issuer, int64_t now, unsigned maxKeys,
      DiscoveryFn complete) :
    m_context{context}, m_issuer{ZuMv(issuer)}, m_now{now}, m_maxKeys{maxKeys},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || m_now <= 0 || !m_maxKeys || m_maxKeys == UINT_MAX) {
      finish_(false);
      return;
    }
    using Table = SignKeyTable;
    using Tuple = Table::Tuple;
    m_context->signKeys->selectRows<2>(ZuFwdTuple(m_issuer), m_maxKeys + 1, [
      self = ZmRef<JWKSLoad__>{this}
    ](ZuUnion<void, Tuple> result, unsigned count) mutable {
      if (result.template is<Tuple>()) {
	if (count > self->m_maxKeys) {
	  self->m_overflow = true;
	  return;
	}
	auto row = ZuMv(result).template p<Tuple>();
	if (row.template p<1>() == self->m_issuer &&
	    (row.template p<8>() == State::Active || row.template p<8>() == State::Suspended) &&
	    row.template p<6>() <= self->m_now &&
	    (!row.template p<7>() || row.template p<7>() > self->m_now) &&
	    row.template p<4>()) {
	  self->m_keys.push(ZuMv(row.template p<4>()));
	}
	return;
      }
      self->finish_(!self->m_overflow);
    });
  }

private:
  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    String json;
    if (ok) ok = jwksJSON_(m_keys, json, true);
    complete(ok, ZuMv(json));
  }

  DBContext	*m_context = nullptr;
  String	m_issuer;
  int64_t	m_now = 0;
  unsigned	m_maxKeys = 0;
  DiscoveryFn	m_complete;
  StringVec	m_keys;
  bool		m_overflow = false;
  bool		m_done = false;
};
ZuDerive(JWKSLoadHeap, (ZmHeap<"Zum.zumd.discovery.JWKSLoad", JWKSLoad__<>>));
ZuDerive(JWKSLoad_, (JWKSLoad__<JWKSLoadHeap>));

bool jwksLoad(
    Requests *requests, ZuTime deadline, DBContext *context,
    ZuCSpan issuer, int64_t now, unsigned maxKeys, DiscoveryFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<DiscoveryComplete_> state =
    new DiscoveryComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, issuer = String{issuer},
      now, maxKeys](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<JWKSLoad_> load = new JWKSLoad_{
      context, ZuMv(issuer), now, maxKeys, [state](
	bool ok, String value) mutable {
      state->complete(ok, ZuMv(value));
    }};
    load->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
