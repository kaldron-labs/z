//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include "zumd_daemon.hh"
#include <zlib/zumd_app_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_key_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_secret.hh>

#include <zlib/ZuBase64URL.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZhttpURL.hh>

namespace Zum {

// Auth-route discovery has a wider fixed mapping ceiling than token
// authority resolution; the extra row distinguishes overflow from a full set.
namespace AuthRouteLimit {
  enum { RoleMappings = 256, Scan = RoleMappings + 1 };
}

static String encode(ZuBSpan data)
{
  String value;
  value.length(ZuBase64URL::enclen(data.length()));
  value.length(ZuBase64URL::encode(value.span(), data));
  return value;
}

String Daemon::page_(AppID appID, Bytes ceremonyID, String options)
{
  String loginPath;
  String passkeyFinishPath;
  if (!appEndpointPath(appID, "v1", "login", loginPath) ||
      !appNestedEndpointPath(appID, "v1", "passkey", "finish",
        passkeyFinishPath)) return {};
  String page;
  page << "<!doctype html><meta charset=utf-8><title>Zum login</title>"
    "<meta name=referrer content=no-referrer><h1>Zum login</h1>"
    "<label>Login <input id=user autocomplete=username></label>"
    "<button id=route>Continue</button>"
    "<p><button id=login>Use passkey</button></p><pre id=out></pre><script>"
    "const id='" << encode(ceremonyID) << "',o=" << options <<
    ",loginPath='" << loginPath << "',passkeyFinishPath='" <<
    passkeyFinishPath << "';"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "return x}function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),"
    "type:c.type,response:{clientDataJSON:e(r.clientDataJSON),"
    "authenticatorData:e(r.authenticatorData),signature:e(r.signature),"
    "userHandle:r.userHandle?e(r.userHandle):null}}}"
    "route.onclick=()=>{const v=user.value.trim();if(!v)return;"
    "const f=document.createElement('form');f.method='post';f.action=loginPath;"
    "for(const [n,x] of [['id',id],['login',v]]){const i=document.createElement('input');"
    "i.type='hidden';i.name=n;i.value=x;f.append(i)}document.body.append(f);f.submit()};"
    "login.onclick=async()=>{try{out.textContent='Waiting for passkey';"
    "const c=await navigator.credentials.get(opts(o));const r=await fetch("
    "passkeyFinishPath+'?id='+id,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c)),redirect:'error'});if(!r.ok)throw Error(await r.text());"
    "if((r.headers.get('content-type')||'').startsWith('text/html')){"
    "const html=await r.text();document.open();document.write(html);document.close();return}"
    "const j=await r.json();if(!j.redirectURI)throw Error('missing redirect');"
    "location.assign(j.redirectURI)}"
    "catch(x){out.textContent=x}}</script>";
  return page;
}

String Daemon::bootstrapPage_(AppID appID)
{
  String beginPath, finishPath;
  if (!appNestedEndpointPath(appID, "v1", "passkey", "begin", beginPath) ||
      !appNestedEndpointPath(appID, "v1", "passkey", "finish", finishPath))
    return {};
  String page{
    "<!doctype html><meta charset=utf-8><title>Enroll Zum administrator</title>"
    "<meta name=referrer content=no-referrer><h1>Enroll Zum administrator</h1>"
    "<button id=enroll>Create passkey</button><pre id=out></pre><script>"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "x.publicKey.user.id=d(x.publicKey.user.id);return x}"
    "function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),type:c.type,"
    "response:{clientDataJSON:e(r.clientDataJSON),attestationObject:e(r.attestationObject)}}}"
    "enroll.onclick=async()=>{try{const cap=new URLSearchParams(location.search).get('capability');"
    "if(!cap)throw Error('missing capability');out.textContent='Creating passkey';"
    "let r=await fetch('"};
  page << beginPath << "',{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify({purpose:'bootstrap',capability:cap})}),j=await r.json();"
    "if(!r.ok)throw Error(JSON.stringify(j));const c=await navigator.credentials.create(opts(j.options));"
    "r=await fetch('" << finishPath << "?id='+j.ceremony,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c))});if(!r.ok)throw Error(await r.text());"
    "history.replaceState({},'',location.pathname);out.textContent='Administrator enrolled'}"
    "catch(x){out.textContent=x}}</script>";
  return page;
}

String Daemon::enrollPage_(AppID appID)
{
  String beginPath, finishPath;
  if (!appNestedEndpointPath(appID, "v1", "passkey", "begin", beginPath) ||
      !appNestedEndpointPath(appID, "v1", "passkey", "finish", finishPath))
    return {};
  String page{
    "<!doctype html><meta charset=utf-8><title>Enroll Zum passkey</title>"
    "<meta name=referrer content=no-referrer><h1>Enroll Zum passkey</h1>"
    "<button id=enroll>Create passkey</button><pre id=out></pre><script>"
    "const d=s=>Uint8Array.from(atob(s.replace(/-/g,'+').replace(/_/g,'/')+"
    "'==='.slice((s.length+3)%4)),c=>c.charCodeAt(0));"
    "const e=b=>btoa(String.fromCharCode(...new Uint8Array(b)))"
    ".replace(/\\+/g,'-').replace(/\\//g,'_').replace(/=+$/,'');"
    "function opts(x){x=structuredClone(x);x.publicKey.challenge=d(x.publicKey.challenge);"
    "x.publicKey.user.id=d(x.publicKey.user.id);return x}"
    "function wire(c){const r=c.response;return{id:c.id,rawId:e(c.rawId),type:c.type,"
    "response:{clientDataJSON:e(r.clientDataJSON),attestationObject:e(r.attestationObject)}}}"
    "enroll.onclick=async()=>{try{const cap=new URLSearchParams(location.search).get('capability');"
    "if(!cap)throw Error('missing capability');out.textContent='Creating passkey';"
    "const purpose=new URLSearchParams(location.search).get('purpose')||'enrollment';"
    "let r=await fetch('"};
  page << beginPath << "',{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify({purpose,capability:cap})}),j=await r.json();"
    "if(!r.ok)throw Error(JSON.stringify(j));const c=await navigator.credentials.create(opts(j.options));"
    "r=await fetch('" << finishPath << "?id='+j.ceremony,{method:'POST',headers:{'content-type':'application/json'},"
    "body:JSON.stringify(wire(c))});if(!r.ok)throw Error(await r.text());"
    "history.replaceState({},'',location.pathname);out.textContent='Passkey enrolled'}"
    "catch(x){out.textContent=x}}</script>";
  return page;
}

class AuthRouteLoad_ : public ZumObject {
public:
  AuthRouteLoad_(DBContext *context, Bytes dbKey, String authorizationBase,
      AppID appID, String login, AuthRouteDoneFn complete) :
    m_context{context}, m_dbKey{ZuMv(dbKey)},
    m_authorizationBase{ZuMv(authorizationBase)}, m_appID{appID},
    m_login{ZuMv(login)}, m_complete{ZuMv(complete)} { }

  ~AuthRouteLoad_()
  {
    if (m_dbKey && m_dbKey.mutable_())
      ZuClear(m_dbKey.data(), m_dbKey.length());
  }

  void start()
  {
    if (!m_context || !m_appID || !loginNormalize(m_login)) {
      finish_(AuthRouteType::Error);
      return;
    }
    auto users = m_context->users;
    users->run(0, [self = ZmRef<AuthRouteLoad_>{this}, users]() mutable {
      auto key = ZuFwdTuple(UserSource::Local, self->m_login);
      users->find<2>(0, ZuMv(key), [
          self = ZuMv(self)](ZdbRowRef<User> user) mutable {
        // Presence is authoritative: a disabled or suspended local identity
        // must never fall through to an OIDC identity of the same name.
        if (user) { self->finish_(AuthRouteType::Local); return; }
        self->policy_();
      });
    });
  }

private:
  void policy_()
  {
    auto policies = m_context->authPolicies;
    policies->run(0, [self = ZmRef<AuthRouteLoad_>{this}, policies]() mutable {
      auto key = ZuFwdTuple(self->m_appID);
      policies->find<0>(0, ZuMv(key), [
          self = ZuMv(self)](ZdbRowRef<AuthPolicy> row) mutable {
	if (!row || row->data().state != State::Active ||
	    row->data().owner || !row->data().providerID) {
	  self->finish_(AuthRouteType::Local);
	  return;
	}
	const auto &policy = row->data();
	if (!policy.localFirst || !policy.assignmentMaxAge ||
	    (policy.eligibilityMode == EligibilityMode::ClaimValues &&
	     (!policy.eligibilityClaim || !policy.eligibilityValues))) {
	  self->finish_(AuthRouteType::Error);
	  return;
	}
	self->m_providerID = policy.providerID;
	self->m_config.appID = policy.appID;
	self->m_config.providerID = policy.providerID;
	self->m_config.policyVersion = policy.version;
	self->m_config.assignmentMaxAge = policy.assignmentMaxAge;
	self->m_config.eligibilityMode = policy.eligibilityMode;
	self->m_config.eligibilityClaim = policy.eligibilityClaim;
	self->m_config.eligibilityValues = policy.eligibilityValues;
	self->provider_();
      });
    });
  }

  void provider_()
  {
    auto providers = m_context->providers;
    providers->run(0, [self = ZmRef<AuthRouteLoad_>{this}, providers]() mutable {
      auto key = ZuFwdTuple(self->m_providerID);
      providers->find<0>(0, ZuMv(key), [
          self = ZuMv(self)](ZdbRowRef<Provider> row) mutable {
	if (!row || row->data().state != State::Active || row->data().owner ||
	    !row->data().issuer || !row->data().clientID ||
	    !row->data().roleClaim || !row->data().scopes ||
	    (row->data().claimSource != ClaimSource::IDToken &&
	     row->data().claimSource != ClaimSource::UserInfo)) {
	  self->finish_(AuthRouteType::Error);
	  return;
	}
	const auto &provider = row->data();
	self->m_config.issuer = provider.issuer;
	self->m_config.clientID = provider.clientID;
	self->m_config.oidcScopes = provider.scopes;
	self->m_config.roleClaim = provider.roleClaim;
	self->m_config.claimSource = provider.claimSource;
	self->m_config.loginHint = self->m_login;
	self->m_config.roles = OIDCRoles::Mapped;
	self->m_config.clientAuth = provider.clientSecret ?
	  OIDCClientAuth::Basic : OIDCClientAuth::None;
	String callbackPath;
	if (!appNestedEndpointPath(self->m_appID, "v1", "oidc", "callback",
	    callbackPath)) {
	  self->finish_(AuthRouteType::Error);
	  return;
	}
	self->m_config.redirectURI = self->m_authorizationBase;
	if (self->m_config.redirectURI[
	    self->m_config.redirectURI.length() - 1] == '/')
	  self->m_config.redirectURI.length(
	    self->m_config.redirectURI.length() - 1);
	self->m_config.redirectURI << callbackPath;
	if (provider.clientSecret) {
	  String recordID;
	  recordID << provider.id;
	  Bytes plain;
	  if (!serverSecretDecrypt(self->m_dbKey, self->m_authorizationBase,
	      "provider", recordID, "clientSecret", provider.clientSecret,
	      plain)) {
	    self->finish_(AuthRouteType::Error);
	    return;
	  }
	  self->m_config.clientSecret = plain;
	  ZuClear(plain.data(), plain.length());
	}
	self->maps_();
      });
    });
  }

  void maps_()
  {
    using Table = RoleMapTable;
    using Tuple = Table::Tuple;
    auto maps = m_context->roleMaps;
    maps->selectRows<0>(ZuFwdTuple(m_appID, m_providerID),
        AuthRouteLimit::Scan, [
        self = ZmRef<AuthRouteLoad_>{this}](
          ZuUnion<void, Tuple> result, unsigned count) mutable {
      if (result.template is<Tuple>()) {
        if (count > AuthRouteLimit::RoleMappings) {
          self->m_overflow = true; return;
        }
        auto tuple = ZuMv(result).template p<Tuple>();
        ZuTupleCall(ZuMv(tuple), [self](auto &&...args) mutable {
          RoleMap map{ZuFwd<decltype(args)>(args)...};
          if (map.state == State::Active && !map.owner && map.roleID)
            self->m_maps.push(OIDCRoleMap{
              ZuMv(map.value), map.roleID});
        });
        return;
      }
      if (self->m_overflow || !self->m_maps) {
        self->finish_(AuthRouteType::Error);
        return;
      }
      self->role_();
    });
  }

  void role_()
  {
    if (m_mapIndex >= m_maps.length()) {
      if (!oidcConfigValid(m_config)) finish_(AuthRouteType::Error);
      else finish_(AuthRouteType::OIDC);
      return;
    }
    auto roles = m_context->roles;
    roles->run(0, [self = ZmRef<AuthRouteLoad_>{this}, roles]() mutable {
      auto map = self->m_maps[self->m_mapIndex++];
      auto key = ZuFwdTuple(self->m_appID, map.roleID);
      roles->find<0>(0, ZuMv(key), [
          self = ZuMv(self), map = ZuMv(map)](
          ZdbRowRef<Role> row) mutable {
	if (row && row->data().state == State::Active &&
	    !row->data().owner && !row->data().tombstone)
	  self->m_config.roleMap.push(ZuMv(map));
	self->role_();
      });
    });
  }

  void finish_(unsigned type)
  {
    if (m_done) return;
    m_done = true;
    if (type != AuthRouteType::OIDC && m_config.clientSecret &&
        m_config.clientSecret.mutable_())
      ZuClear(m_config.clientSecret.data(), m_config.clientSecret.length());
    auto complete = ZuMv(m_complete);
    complete(AuthRoute{.oidc = type == AuthRouteType::OIDC ?
      ZuMv(m_config) : OIDCConfig{}, .type = type});
  }

  DBContext		*m_context = nullptr;
  Bytes			m_dbKey;
  String		m_authorizationBase;
  AppID			m_appID = 0;
  ProviderID		m_providerID = 0;
  String		m_login;
  OIDCConfig		m_config;
  OIDCRoleMapVec	m_maps;
  unsigned		m_mapIndex = 0;
  AuthRouteDoneFn	m_complete;
  bool			m_overflow = false;
  bool			m_done = false;
};

void Daemon::authRoute_(
    AppID appID, String login, AuthRouteDoneFn complete)
{
  ZmRef<AuthRouteLoad_> load = new AuthRouteLoad_{
    m_context, m_config.dbKey, m_config.issuer, appID,
    ZuMv(login), ZuMv(complete)};
  load->start();
}

bool Daemon::loadKey_()
{
  ServerConfig config;
  int64_t now = Zm::now().sec();
  SignKey key = ZmBlock<SignKey>{}([this, now, &config](auto wake) mutable {
    m_context->signKeys->run(0, [this, now, expires = now + config.accessLifetime,
      maxKeys = config.limits.jwks, wake = ZuMv(wake)]() mutable {
      signKeyLoad(m_context, m_config.issuer, now, expires, maxKeys, [
        wake = ZuMv(wake)](SignKey key) mutable { wake(ZuMv(key)); });
    });
  });
  if (!key.id || key.state != State::Active || !key.privateMaterial) return false;
  Bytes privateKey;
  if (!serverSecretDecrypt(m_config.dbKey, m_config.issuer,
      "zum.sign_key", key.id, "privateMaterial", key.privateMaterial,
      privateKey)) return false;
  try {
    m_key = new Ztls::PK::SK_EC{m_rng,
      Ztls::PK::OIDs::EC_GRP_SECP256R1, privateKey};
    if (!signKeyMatch(m_rng, key, privateKey)) {
      ZuClear(privateKey.data(), privateKey.length());
      m_key = nullptr;
      return false;
    }
  } catch (...) {
    ZuClear(privateKey.data(), privateKey.length());
    return false;
  }
  ZuClear(privateKey.data(), privateKey.length());
  m_signKeyID = ZuMv(key.id);
  return true;
}

bool Daemon::init(
    DB *db, DBContext *context, Requests *requests, ZiMultiplex *mx,
    DaemonConfig config)
{
  Zhttp::URL parsed{config.issuer};
  auto authorizationBase = parsed.url();
  if (!parsed.ok() || !authorizationBase.host ||
      authorizationBase.hasQuery || authorizationBase.hasFragment ||
      (authorizationBase.path && authorizationBase.path != "/") ||
      (authorizationBase.scheme != Zhttp::Scheme::https &&
       !(authorizationBase.scheme == Zhttp::Scheme::http &&
         (authorizationBase.host == "localhost" ||
          authorizationBase.host == "127.0.0.1" ||
          authorizationBase.host == "::1")))) return false;
  config.issuer.null();
  config.issuer << authorizationBase.origin();
  m_db = db;
  m_context = context;
  m_requests = requests;
  m_mx = mx;
  m_config = ZuMv(config);
  ServerConfig provider{
    .issuer = m_config.issuer, .rpID = m_config.rpID,
    .rpName = m_config.rpName, .requestTimeout = m_config.requestTimeout,
    .authMethod = AuthMethod::LocalFirst,
    .refreshRevoke = RefreshRevokeFn{[this](AppID appID, RefreshID refreshID,
        int64_t expires) mutable {
      if (m_ssf) {
        if (m_config.refreshRevoke)
          m_config.refreshRevoke(appID, refreshID, expires);
        m_ssf->revoke(appID, ZuMv(refreshID), expires);
      } else if (m_config.refreshRevoke)
        m_config.refreshRevoke(appID, ZuMv(refreshID), expires);
    }}};
  if (!m_db || !m_context || !m_requests || !m_mx || !m_config.issuer ||
      !m_config.rpID || !m_config.admin || !m_config.requestTimeout ||
      !m_rng.init())
    return false;
  OIDCHTTPFn oidcHTTP = m_config.oidcHTTP;
  if (!oidcHTTP) oidcHTTP = OIDCHTTPFn{[]( 
      OIDCHTTPRequest, OIDCHTTPDoneFn complete) {
    complete(503, String{});
  }};
  m_config.oidcHTTP = oidcHTTP;
  m_sign = SignFn{[this](const SignKey &record, ZuBSpan digest,
      SignatureFn complete) {
    Bytes signature;
    auto sign = [this, digest, &signature](auto &key) {
      auto result = key.sign(m_rng, digest, [&signature](ZuBSpan der) {
        signature = Bytes{der};
      });
      if (result.template is<ZeException>()) signature.null();
    };
    if (!record.providerRef && record.issuer && record.id == m_signKeyID &&
        (record.issuer == m_config.issuer ||
         (m_config.ssfIssuer && record.issuer == m_config.ssfIssuer))) {
      if (m_key) sign(*m_key);
    } else if (!record.providerRef && record.issuer &&
        record.privateMaterial) {
      Bytes privateKey;
      if (serverSecretDecrypt(m_config.dbKey, record.issuer,
          "zum.sign_key", record.id, "privateMaterial",
          record.privateMaterial, privateKey)) {
        try {
          Ztls::PK::SK_EC key{m_rng, Ztls::PK::OIDs::EC_GRP_SECP256R1,
            privateKey};
          ZuClear(privateKey.data(), privateKey.length());
          sign(key);
        } catch (...) { signature.null(); }
      }
      if (privateKey) ZuClear(privateKey.data(), privateKey.length());
    }
    complete(ZuMv(signature));
  }};
  if (!m_provider.init(m_db, m_context, m_requests, ZuMv(provider),
      []() { return Zm::now().sec(); },
      [](AppID appID, Bytes id, String options) {
        return page_(appID, ZuMv(id), ZuMv(options));
      },
      [](const User &, const Client &, const ScopeSelection &,
          const ZtBitmap &allowed, PolicyDoneFn complete) {
        complete(true, ZtBitmap{allowed});
      },
      [this](PasskeyStart start, AdmitDoneFn complete) {
        PasskeyAdmission admission;
        if (start.type == PasskeyStartType::Enrollment && start.capability) {
          admission.allowed = true;
        } else if (start.type == PasskeyStartType::Recovery &&
            start.capability) {
          admission.allowed = true;
          admission.recovery.displayName = "Zum user";
          admission.recovery.label = "Recovered Zum passkey";
        } else if (start.type == PasskeyStartType::Bootstrap) {
          admission.allowed = true;
          admission.enrollment.name = m_config.admin;
          admission.enrollment.displayName = m_config.admin;
          admission.enrollment.label = "Zum administrator passkey";
          admission.enrollment.userID = m_config.bootstrap.adminUserID;
        }
        complete(ZuMv(admission));
      },
      m_sign, ZuMv(oidcHTTP), [this](
          AppID appID, String login, AuthRouteDoneFn complete) {
        authRoute_(appID, ZuMv(login), ZuMv(complete));
      })) return false;
  HTTP<Daemon>::init(m_provider);
  DaemonParser parser;
  parser.init(*this);
  auto http = Zhttp::ServerConfig().localIP(ZiIP(m_config.addr))
    .port(m_config.port).idleTimeout(30)
    .retainedBodyMax(ServerLimitMax::JSON).tcp();
  m_httpInited = m_http.init(
    Zhttp::HubConfig{m_mx, "rx", "tx"}, ZuMv(http), this);
  return m_httpInited;
}

bool Daemon::prepare(ServerBootstrapResult bootstrap)
{
  if (!loadKey_()) return false;
  m_config.bootstrap = ZuMv(bootstrap);
  if (m_ssf) {
    m_ssf->stop();
    m_ssf = nullptr;
  }
  if (m_config.ssfSecret) {
    if (!m_config.ssfIssuer) return false;
    m_ssf = new SSFTransmitter{};
    if (!m_ssf->init(SSFTransmitterConfig{
        .db = m_db, .context = m_context, .requests = m_requests,
        .issuer = m_config.ssfIssuer,
        .key = SignKey{.id = m_signKeyID,
          .issuer = m_config.ssfIssuer},
        .keyLoad = SSFKeyFn{[this](String issuer, SignKeyFn complete) mutable {
          int64_t now = Zm::now().sec();
          signKeyLoad(m_context, ZuMv(issuer), now,
              now + m_config.requestTimeout, 1,
              ZuMv(complete));
        }},
        .sign = m_sign, .http = m_config.oidcHTTP,
        .secret = m_config.ssfSecret,
        .receivers = ZuMv(m_config.ssfReceivers)})) {
      m_ssf = nullptr;
      return false;
    }
    if (m_httpInited) m_ssf->start();
  }
  return true;
}

bool Daemon::start()
{
  if (!m_httpInited || !m_http.start()) return false;
  if (m_ssf) m_ssf->start();
  return true;
}

void Daemon::stop()
{
  if (m_ssf) m_ssf->stop();
  if (m_requests) m_requests->deactivate();
  m_provider.stop();
  if (m_httpInited) (void)m_http.stop();
}

void Daemon::final()
{
  if (m_httpInited) {
    m_http.final();
    m_httpInited = false;
  }
  m_provider.final();
  m_ssf = nullptr;
  m_sign = SignFn{};
  m_key = nullptr;
  m_signKeyID.null();
  if (m_config.dbKey && m_config.dbKey.mutable_())
    ZuClear(m_config.dbKey.data(), m_config.dbKey.length());
}

void Daemon::listening(int, unsigned) { }
void Daemon::listenFailed(int, bool) { }

} // namespace Zum
