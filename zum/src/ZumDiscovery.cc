//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumDiscovery.hh>
#include <zlib/ZumKeyDB.hh>

#include <zlib/ZuDerive.hh>

#include <zlib/ZfJSON.hh>


namespace Zum {

struct MetadataWire {
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
ZfStruct(, (MetadataWire, JSON),
  (((issuer),		(Required)),	(String)),
  (((authorizationEndpoint), (JSON::ID<"authorization_endpoint">, Required)), (String)),
  (((tokenEndpoint),	(JSON::ID<"token_endpoint">, Required)), (String)),
  (((jwksURI),		(JSON::ID<"jwks_uri">, Required)), (String)),
  (((revocationEndpoint), (JSON::ID<"revocation_endpoint">, Required)), (String)),
  (((userinfoEndpoint), (JSON::ID<"userinfo_endpoint">, Required)), (String)),
  (((responseTypesSupported), (JSON::ID<"response_types_supported">, Required)), (StringVec)),
  (((grantTypesSupported), (JSON::ID<"grant_types_supported">, Required)), (StringVec)),
  (((tokenAuthMethods), (JSON::ID<"token_endpoint_auth_methods_supported">, Required)), (StringVec)),
  (((revokeAuthMethods), (JSON::ID<"revocation_endpoint_auth_methods_supported">, Required)), (StringVec)),
  (((subjectTypesSupported), (JSON::ID<"subject_types_supported">, Required)), (StringVec)),
  (((idTokenAlgs), (JSON::ID<"id_token_signing_alg_values_supported">, Required)), (StringVec)),
  (((scopesSupported), (JSON::ID<"scopes_supported">, Required)), (StringVec)),
  (((claimsSupported), (JSON::ID<"claims_supported">, Required)), (StringVec)),
  (((codeChallengeMethods), (JSON::ID<"code_challenge_methods_supported">, Required)), (StringVec)));

using PublicJWK = ZfJSON::Union<>;
ZuDerive(PublicJWKArray, (ZtArray<PublicJWK,
  ZtArrayHeapID<"Zum.Discovery.JWKs">>));
struct PublicJWKVec : public PublicJWKArray {
  ZuDerive_(PublicJWKVec, PublicJWKArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(PublicJWKVec *);
};
struct JWKSReply { PublicJWKVec keys; };
ZfStruct(, (JWKSReply, JSON),
  (((keys),		(Required)),	(UDT)));
ZuDerive(PublicJWKRootArray, (ZtArray<ZuPtr<ZfJSON::AnyNode>,
  ZtArrayHeapID<"Zum.Discovery.JWKRoots">>));

static bool publicJWK(
    String &source, PublicJWK &jwk, ZuPtr<ZfJSON::AnyNode> &owner)
{
  auto parsed = ZfJSON::scan(source.span());
  if (parsed.p<0>() != int(source.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !ZfJSON::unique(roots[0]) ||
      !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
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

class SignKeyLoad_ : public ZumObject {
public:
  SignKeyLoad_(DBContext *context, String issuer, int64_t now,
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
    m_context->signKeys->selectRows<0>({}, m_maxKeys + 1, [
      self = ZmRef<SignKeyLoad_>{this}
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
	table->find<0>(0, ZuFwdTuple(ZuMv(id)), [
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

void signKeyLoad(DBContext *context, String issuer, int64_t now,
    int64_t expires, unsigned maxKeys, SignKeyFn complete)
{
  ZmRef<SignKeyLoad_> load = new SignKeyLoad_{
    context, ZuMv(issuer), now, expires, maxKeys, ZuMv(complete)};
  load->start();
}

class DiscoveryComplete_ : public ZumObject {
public:
  DiscoveryComplete_(DiscoveryFn complete) :
    m_complete{ZuMv(complete)} { }

  void request(ZmRef<Request> request) { m_request = ZuMv(request); }

  void complete(bool ok, String value)
  {
    m_request->complete([
      self = ZmRef<DiscoveryComplete_>{this}, ok, value = ZuMv(value)
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

static String endpoint(ZuCSpan issuer, ZuCSpan path)
{
  String value{issuer};
  if (value && value[value.length() - 1] == '/') value.length(value.length() - 1);
  value << path;
  return value;
}

String metadataJSON(ZuCSpan issuer)
{
  String json;
  ZfJSON::save(json, MetadataWire{
    .issuer = issuer,
    .authorizationEndpoint = endpoint(issuer, "/authorize"),
    .tokenEndpoint = endpoint(issuer, "/token"),
    .jwksURI = endpoint(issuer, "/jwks"),
    .revocationEndpoint = endpoint(issuer, "/revoke"),
    .userinfoEndpoint = endpoint(issuer, "/userinfo"),
    .responseTypesSupported = {"code"},
    .grantTypesSupported = {"authorization_code", "refresh_token",
      "client_credentials"},
    .tokenAuthMethods = {"client_secret_basic", "none"},
    .revokeAuthMethods = {"client_secret_basic", "none"},
    .subjectTypesSupported = {"public"},
    .idTokenAlgs = {"ES256"},
    .scopesSupported = {"openid", "profile", "email"},
    .claimsSupported = {"sub", "iss", "aud", "exp", "iat", "auth_time",
      "nonce", "amr", "name", "preferred_username", "email"},
    .codeChallengeMethods = {"S256"}});
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

class JWKSLoad_ : public ZumPolymorph {
public:
  JWKSLoad_(
      DBContext *context, int64_t now, unsigned maxKeys,
      DiscoveryFn complete) :
    m_context{context}, m_now{now}, m_maxKeys{maxKeys},
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || m_now <= 0 || !m_maxKeys || m_maxKeys == UINT_MAX) {
      finish_(false);
      return;
    }
    using Table = SignKeyTable;
    using Tuple = Table::Tuple;
    m_context->signKeys->selectRows<0>({}, m_maxKeys + 1, [
      self = ZmRef<JWKSLoad_>{this}
    ](ZuUnion<void, Tuple> result, unsigned count) mutable {
      if (result.template is<Tuple>()) {
	if (count > self->m_maxKeys) {
	  self->m_overflow = true;
	  return;
	}
	auto row = ZuMv(result).template p<Tuple>();
	if ((row.template p<8>() == State::Active || row.template p<8>() == State::Suspended) &&
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
  int64_t	m_now = 0;
  unsigned	m_maxKeys = 0;
  DiscoveryFn	m_complete;
  StringVec	m_keys;
  bool		m_overflow = false;
  bool		m_done = false;
};

bool jwksLoad(
    Requests *requests, ZuTime deadline, DBContext *context,
    int64_t now, unsigned maxKeys, DiscoveryFn complete)
{
  if (!requests || !complete) return false;
  ZmRef<DiscoveryComplete_> state =
    new DiscoveryComplete_{ZuMv(complete)};
  return requests->run(deadline, [state, context, now, maxKeys](
      ZmRef<Request> request) mutable {
    state->request(ZuMv(request));
    ZmRef<JWKSLoad_> load = new JWKSLoad_{context, now, maxKeys, [state](
	bool ok, String value) mutable {
      state->complete(ok, ZuMv(value));
    }};
    load->start();
  }, [state]() mutable { state->cancel(); });
}

} // namespace Zum
