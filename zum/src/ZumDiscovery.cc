//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumDiscovery.hh>

#include <zlib/ZfJSON.hh>


namespace Zum {

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

static void endpoint(String &json, ZuCSpan issuer, ZuCSpan path)
{
  String value{issuer};
  if (value && value[value.length() - 1] == '/') value.length(value.length() - 1);
  value << path;
  ZfJSON::quote(json, value);
}

String metadataJSON(ZuCSpan issuer)
{
  String json;
  json << "{\"issuer\":";
  ZfJSON::quote(json, issuer);
  json << ",\"authorization_endpoint\":";
  endpoint(json, issuer, "/authorize");
  json << ",\"token_endpoint\":";
  endpoint(json, issuer, "/token");
  json << ",\"jwks_uri\":";
  endpoint(json, issuer, "/jwks");
  json << ",\"revocation_endpoint\":";
  endpoint(json, issuer, "/revoke");
  json << ",\"userinfo_endpoint\":";
  endpoint(json, issuer, "/userinfo");
  json << ",\"response_types_supported\":[\"code\"]"
    ",\"grant_types_supported\":[\"authorization_code\",\"refresh_token\","
    "\"client_credentials\"]"
    ",\"token_endpoint_auth_methods_supported\":[\"client_secret_basic\","
    "\"none\"]"
    ",\"revocation_endpoint_auth_methods_supported\":["
    "\"client_secret_basic\",\"none\"]"
    ",\"subject_types_supported\":[\"public\"]"
    ",\"id_token_signing_alg_values_supported\":[\"ES256\"]"
    ",\"scopes_supported\":[\"openid\",\"profile\",\"email\"]"
    ",\"claims_supported\":[\"sub\",\"iss\",\"aud\",\"exp\",\"iat\","
    "\"auth_time\",\"nonce\",\"amr\",\"name\","
    "\"preferred_username\",\"email\"]"
    ",\"code_challenge_methods_supported\":[\"S256\"]}";
  return json;
}

String jwksJSON(const StringVec &publicJwks)
{
  String json{"{\"keys\":["};
  for (unsigned i = 0, n = publicJwks.length(); i < n; ++i) {
    if (i) json << ',';
    json << publicJwks[i];
  }
  json << "]}";
  return json;
}

class JWKSLoad_ : public ZumPolymorph {
public:
  JWKSLoad_(
      DBContext *context, int64_t now, unsigned maxKeys,
      DiscoveryFn complete) :
    m_context{context}, m_now{now}, m_maxKeys{maxKeys},
    m_complete{ZuMv(complete)}, m_json{"{\"keys\":["} { }

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
	if (row.template p<8>() == State::Active &&
	    row.template p<6>() <= self->m_now &&
	    (!row.template p<7>() || row.template p<7>() > self->m_now) &&
	    row.template p<4>()) {
	  if (self->m_keyCount++) self->m_json << ',';
	  self->m_json << row.template p<4>();
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
    if (ok) m_json << "]}";
    complete(ok, ok ? ZuMv(m_json) : String{});
  }

  DBContext	*m_context = nullptr;
  int64_t	m_now = 0;
  unsigned	m_maxKeys = 0;
  DiscoveryFn	m_complete;
  String	m_json;
  unsigned	m_keyCount = 0;
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
