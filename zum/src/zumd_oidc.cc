//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_db.hh>

#include <string.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpURL.hh>

#include <zlib/ZrestClient.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

struct OIDCID {
  String issuer;
  String subject;
  ZfJSON::Union<String, StringVec> audience;
  String authorizedParty;
  String nonce;
  int64_t iat = 0;
  int64_t expires = 0;
  int64_t authTime = 0;
};
ZfStruct(, (OIDCID, JSON),
  (((issuer),		(JSON::ID<"iss">, Required)),	(String)),
  (((subject),		(JSON::ID<"sub">, Required)),	(String)),
  (((audience),		(JSON::ID<"aud">, Required)),	(UDT)),
  (((authorizedParty), (JSON::ID<"azp">, JSON::Opt)),	(String)),
  (((nonce),		(Required)),	(String)),
  (((iat),		(Required)),	(Int64)),
  (((expires),		(JSON::ID<"exp">, Required)),	(Int64)),
  (((authTime),		(JSON::ID<"auth_time">, Required)), (Int64)));

struct OIDCToken { String idToken; String accessToken; };
ZfStruct(, (OIDCToken, JSON),
  (((idToken),		(JSON::ID<"id_token">, Required)),	(String)),
  (((accessToken),	(JSON::ID<"access_token">, JSON::Opt)), (String)));
struct OIDCUserInfo { String subject; };
ZfStruct(, (OIDCUserInfo, JSON),
  (((subject),		(JSON::ID<"sub">, Required)),	(String)));
struct OIDCJWK {
  String kid;
  String kty;
  String crv;
  String use;
  String alg;
  String x;
  String y;
};
ZfStruct(, (OIDCJWK, JSON),
  (((kid),		(Required)),	(String)),
  (((kty),		(Required)),	(String)),
  (((crv),		(Required)),	(String)),
  (((use),		(JSON::Opt)),	(String)),
  (((alg),		(JSON::Opt)),	(String)),
  (((x),		(Required)),	(String)),
  (((y),		(Required)),	(String)));
ZuDerive(OIDCJWKWireArray, (ZtArray<OIDCJWK,
  ZtArrayHeapID<"Zum.OIDC.JWKs">>));
struct OIDCJWKWireVec : public OIDCJWKWireArray {
  ZuDerive_(OIDCJWKWireVec, OIDCJWKWireArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(OIDCJWKWireVec *);
};
struct OIDCJWKSResponse { OIDCJWKWireVec keys; };
ZfStruct(, (OIDCJWKSResponse, JSON),
  (((keys),		(Required)),	(UDT)));
struct OIDCDiscovery {
  String issuer;
  String authorize;
  String token;
  String jwks;
  StringVec responses;
  StringVec algorithms;
  StringVec methods;
  String userinfo;
};
ZfStruct(, (OIDCDiscovery, JSON),
  (((issuer),		(Required)),	(String)),
  (((authorize),	(JSON::ID<"authorization_endpoint">, Required)), (String)),
  (((token),		(JSON::ID<"token_endpoint">, Required)), (String)),
  (((jwks),		(JSON::ID<"jwks_uri">, Required)), (String)),
  (((responses),	(JSON::ID<"response_types_supported">, Required)), (StringVec)),
  (((algorithms),	(JSON::ID<"id_token_signing_alg_values_supported">, Required)), (StringVec)),
  (((methods),		(JSON::ID<"token_endpoint_auth_methods_supported">, Required)), (StringVec)),
  (((userinfo),		(JSON::ID<"userinfo_endpoint">, JSON::Opt)), (String)));
struct OIDCEssential { bool essential = true; };
ZfStruct(, (OIDCEssential, JSON),
  (((essential),	(Required)),	(Bool)));
struct OIDCAuthTime { OIDCEssential authTime; };
ZfStruct(, (OIDCAuthTime, JSON),
  (((authTime),		(JSON::ID<"auth_time">, Required)), (UDT)));
struct OIDCAuthorizeClaims { OIDCAuthTime idToken; };
ZfStruct(, (OIDCAuthorizeClaims, JSON),
  (((idToken),		(JSON::ID<"id_token">, Required)), (UDT)));

struct OIDCCallbackFields {
  using Keys = ZuStringTL<"code", "state", "error">;
};
struct ReservedClaims {
  using Keys = ZuStringTL<"iss", "sub", "aud", "azp", "nonce", "iat", "exp">;
};

template <typename T>
static bool jsonLoad(String &json, ZuPtr<ZfJSON::AnyNode> &root, T &value)
{
  auto parsed = ZfJSON::scan(json.span());
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
    return false;
  root = ZuMv(roots[0]);
  auto handler = ZfJSON::handler<T>(root);
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

static ZfJSON::AnyNode *jsonField(
    const ZfJSON::AnyNode *object, ZuCSpan name)
{
  if (!object || !object->has<ZfJSON::AnyNode::Object>()) return nullptr;
  for (auto &field: object->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == name) return field.p<1>();
  return nullptr;
}

static bool hasScope(const StringVec &scopes, ZuCSpan name)
{
  for (auto &scope: scopes) if (scope == name) return true;
  return false;
}

static bool reservedClaim(ZuCSpan name)
{
  constexpr auto matcher = ZuMatcher<ReservedClaims>();
  return matcher.exact(name) >= 0;
}

static bool hasRole(const IDVec &roles, RoleID roleID)
{
  for (auto id: roles) if (id == roleID) return true;
  return false;
}

bool oidcConfigValid(const OIDCConfig &config)
{
  bool authorize = bool(config.authorizeEndpoint);
  bool token = bool(config.tokenEndpoint);
  bool jwks = bool(config.jwksEndpoint);
  if (!config.issuer || (authorize != token) || (authorize != jwks) ||
      !config.clientID ||
      !config.redirectURI || !hasScope(config.oidcScopes, "openid") ||
      (config.claimSource != ClaimSource::IDToken &&
       config.claimSource != ClaimSource::UserInfo) ||
      (config.claimSource == ClaimSource::UserInfo &&
       !config.userinfoEndpoint && authorize) ||
      (config.eligibilityMode == EligibilityMode::ClaimValues &&
       (!config.eligibilityClaim || !config.eligibilityValues ||
        reservedClaim(config.eligibilityClaim))) ||
      (config.eligibilityMode != EligibilityMode::ClaimValues &&
       config.eligibilityMode != EligibilityMode::MappedRole) ||
      (config.clientAuth != OIDCClientAuth::Basic &&
       config.clientAuth != OIDCClientAuth::Post &&
       config.clientAuth != OIDCClientAuth::None) ||
      (config.clientAuth != OIDCClientAuth::None && !config.clientSecret))
    return false;
  auto validURL = [](ZuCSpan value, bool issuer) {
    Zhttp::URL parsed{value};
    auto url = parsed.url();
    return parsed.error().ok() && url.scheme == Zhttp::Scheme::https &&
      url.host && !url.hasFragment &&
      (!issuer || !url.hasQuery);
  };
  if (!validURL(config.issuer, true) ||
      !validURL(config.redirectURI, false) ||
      (authorize && (!validURL(config.authorizeEndpoint, false) ||
       !validURL(config.tokenEndpoint, false) ||
       !validURL(config.jwksEndpoint, false) ||
       (config.claimSource == ClaimSource::UserInfo &&
	!validURL(config.userinfoEndpoint, false))))) return false;
  switch (config.roles) {
    case OIDCRoles::Local:
      return !config.roleClaim && !config.roleMap;
    case OIDCRoles::Mapped:
      return config.roleClaim && !reservedClaim(config.roleClaim);
    default:
      return false;
  }
}

static bool stringValue(const ZfJSON::AnyNode *node, String &value)
{
  if (!node || !node->has<ZfJSON::AnyNode::String>()) return false;
  value = node->data<ZfJSON::AnyNode::String>();
  return true;
}

static bool stringsValue(
    const ZfJSON::AnyNode *node, unsigned limit, StringVec &values)
{
  StringVec next;
  if (node && node->has<ZfJSON::AnyNode::String>()) {
    String value;
    if (!stringValue(node, value)) return false;
    next.push(ZuMv(value));
  } else if (node && node->has<ZfJSON::AnyNode::Array>()) {
    auto &array = node->data<ZfJSON::AnyNode::Array>();
    if (array.length() > limit) return false;
    for (auto &item: array) {
      String value;
      if (!stringValue(item, value)) return false;
      next.push(ZuMv(value));
    }
  } else {
    return false;
  }
  values = ZuMv(next);
  return true;
}

static bool hasAudience(const StringVec &audiences, ZuCSpan audience)
{
  for (auto &value: audiences) if (value == audience) return true;
  return false;
}

bool oidcVerifyIDToken(
    ZuCSpan token, ZuBSpan publicKey, ZuCSpan nonce,
    const OIDCConfig &config, int64_t now, const OIDCLimits &limits,
    OIDCClaims &claims)
{
  if (!nonce || !config.issuer || !config.clientID || now <= 0 ||
      !limits.audiences || !limits.roleValues || limits.clockSkew < 0)
    return false;
  JWTHeader header;
  String json;
  if (!jwtES256(token, publicKey, limits.jwt, header, json) ||
      (header.type && header.type != "JWT")) return false;
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCID wire;
  if (!jsonLoad(json, root, wire)) return false;
  OIDCClaims next;
  next.issuer = ZuMv(wire.issuer);
  next.subject = ZuMv(wire.subject);
  next.nonce = ZuMv(wire.nonce);
  next.iat = wire.iat;
  next.expires = wire.expires;
  next.authTime = wire.authTime;
  if (!wire.audience.template is<const ZfJSON::AnyNode *>()) return false;
  auto audience = wire.audience.template p<const ZfJSON::AnyNode *>();
  if (audience->has<ZfJSON::AnyNode::String>()) {
    String value;
    if (!stringValue(audience, value) || !value) return false;
    wire.audience = ZuMv(value);
    next.audience.push(ZuMv(wire.audience.template p<String>()));
  } else if (audience->has<ZfJSON::AnyNode::Array>()) {
    StringVec values;
    if (!stringsValue(audience, limits.audiences, values)) return false;
    wire.audience = ZuMv(values);
    next.audience = ZuMv(wire.audience.template p<StringVec>());
  } else return false;
  bool rolesPresent = false, eligibilityPresent = false;
  if (config.claimSource == ClaimSource::IDToken) {
    if (config.roles == OIDCRoles::Mapped) {
      auto value = jsonField(root, config.roleClaim);
      rolesPresent = value &&
        stringsValue(value, limits.roleValues, next.roleValues);
      if (value && !rolesPresent) return false;
    }
    if (config.eligibilityMode == EligibilityMode::ClaimValues) {
      auto value = jsonField(root, config.eligibilityClaim);
      eligibilityPresent = value &&
        stringsValue(value, limits.roleValues, next.eligibilityValues);
      if (value && !eligibilityPresent) return false;
    }
  }
  if (next.issuer != config.issuer ||
      !next.subject || next.nonce != nonce ||
      !hasAudience(next.audience, config.clientID) ||
      next.iat <= 0 || next.expires <= next.iat ||
      next.authTime <= 0 || next.authTime > next.iat ||
      next.iat > now + limits.clockSkew ||
      now - limits.clockSkew >= next.expires ||
      (config.claimSource == ClaimSource::IDToken &&
       config.eligibilityMode == EligibilityMode::ClaimValues &&
       !eligibilityPresent)) return false;
  if (config.maxAgePresent && now > next.authTime &&
      uint64_t(now - next.authTime) > config.maxAge &&
      uint64_t(now - next.authTime) - config.maxAge > uint64_t(limits.clockSkew))
    return false;
  if ((next.audience.length() > 1 || wire.authorizedParty) &&
      wire.authorizedParty != config.clientID) return false;
  claims = ZuMv(next);
  return true;
}

IDVec oidcMapRoles(
    ZuSpan<const String> values, ZuSpan<const OIDCRoleMap> map)
{
  IDVec roles;
  for (auto &value: values) {
    if (!value) continue;
    for (auto &entry: map)
      if (entry.value == value) {
	if (entry.roleID && !hasRole(roles, entry.roleID))
	  roles.push(entry.roleID);
	break;
      }
  }
  return roles;
}

template <typename Heap = ZuVoid>
class OIDCUserLoad__ : public Heap, public ZmObject  {
public:
  OIDCUserLoad__(DBContext *context, String subject,
      const OIDCConfig &config, StringVec roleValues,
      StringVec eligibilityValues, int64_t now, OIDCUserFn complete) :
    m_context{context}, m_subject{ZuMv(subject)}, m_appID{config.appID},
    m_providerID{config.providerID}, m_issuer{config.issuer},
    m_roleValues(ZuMv(roleValues)),
    m_eligibilityValues(ZuMv(eligibilityValues)),
    m_allowedEligibility(config.eligibilityValues),
    m_eligibilityMode{config.eligibilityMode},
    m_claimSource{config.claimSource}, m_policyVersion{config.policyVersion},
    m_assignmentMaxAge{config.assignmentMaxAge}, m_now{now},
    m_roles(oidcMapRoles(m_roleValues, config.roleMap)),
    m_complete{ZuMv(complete)} { }

  void start()
  {
    if (!m_context || !m_subject || !m_appID || !m_providerID ||
        !m_assignmentMaxAge || m_now <= 0 || !m_complete || !m_rng.init()) {
      finish_(false);
      return;
    }
    switch (m_eligibilityMode) {
      case EligibilityMode::MappedRole:
        m_eligible = bool(m_roles);
        break;
      case EligibilityMode::ClaimValues:
        for (auto &value: m_eligibilityValues)
          for (auto &allowed: m_allowedEligibility)
            if (value == allowed) { m_eligible = true; break; }
        break;
      default:
        finish_(false);
        return;
    }
    identity_();
  }

private:
  void identity_()
  {
    auto identities = m_context->extIdentities;
    identities->run(0, [self = ZmRef<OIDCUserLoad__>{this},
        identities]() mutable {
      identities->find<0>(0, ZuFwdTuple(self->m_providerID,
          self->m_issuer, self->m_subject), [self = ZuMv(self)](
            ZdbRowRef<ExtIdentity> row) mutable {
        if (row && !row->data().owner) {
          self->user_(row->data().userID);
          return;
        }
        if (row) { self->finish_(false); return; }
        self->project_();
      });
    });
  }

  void project_()
  {
    uint64_t userID = 0;
    do {
      if (!m_rng.random({reinterpret_cast<uint8_t *>(&userID),
          sizeof(userID)})) { finish_(false); return; }
    } while (!userID);
    Bytes random;
    random.length(sizeof(ZdbSagaID), false);
    if (!m_rng.random(random)) { finish_(false); return; }
    ZdbSagaID sagaID;
    memcpy(&sagaID, random.data(), sizeof(sagaID));
    String name{"oidc:"};
    name << m_providerID << ':' << userID;
    Bytes handle;
    handle.length(32, false);
    if (!m_rng.random(handle)) { finish_(false); return; }
    ExternalProjection projection{.providerID = m_providerID,
      .issuer = m_issuer, .subject = m_subject, .userID = userID,
      .name = ZuMv(name), .handle = ZuMv(handle), .created = m_now};
    ZmRef<MSaga> saga = new MSaga{};
    saga->init(ZuMv(projection));
    auto db = static_cast<DB *>(m_context->users->db());
    if (!sagaSubmit(db, sagaID, ZuMv(saga),
        [self = ZmRef<OIDCUserLoad__>{this}](bool ok) mutable {
          if (!ok) self->finish_(false);
        }, [self = ZmRef<OIDCUserLoad__>{this}](bool ok) mutable {
          if (!ok) { self->finish_(false); return; }
          self->identity_();
        })) finish_(false);
  }

  void user_(UserID userID)
  {
    auto users = m_context->users;
    users->run(0, [self = ZmRef<OIDCUserLoad__>{this}, users, userID]() mutable {
      users->find<0>(0, ZuFwdTuple(userID), [
          self = ZuMv(self)](ZdbRowRef<User> row) mutable {
	if (!row || row->data().source != UserSource::External ||
	    row->data().state != State::Active || row->data().owner) {
	  self->finish_(false);
	  return;
	}
	self->m_user = row->data();
	self->evidence_();
      });
    });
  }

  Evidence evidenceRecord_() const
  {
    int64_t deadline = m_now > INT64_MAX - int64_t(m_assignmentMaxAge) ?
      INT64_MAX : m_now + int64_t(m_assignmentMaxAge);
    return Evidence{.appID = m_appID, .userID = m_user.id,
      .providerID = m_providerID, .roleValues = m_roleValues,
      .eligible = m_eligible, .observed = m_now, .deadline = deadline,
      .source = m_claimSource, .policyVersion = m_policyVersion,
      .created = m_now, .updated = m_now};
  }

  void evidence_()
  {
    auto evidence = m_context->evidence;
    evidence->run(0, [self = ZmRef<OIDCUserLoad__>{this}, evidence]() mutable {
      auto key = ZuFwdTuple(self->m_appID, self->m_user.id, self->m_providerID);
      evidence->findUpd<0, ZuSeq<1>>(0, ZuMv(key), [self = ZuMv(self), evidence](
          ZdbRow<Evidence> *row) mutable {
	if (row) {
	  if (row->data().owner) { self->finish_(false); return; }
	  auto next = self->evidenceRecord_();
	  next.version = row->data().version + 1;
	  next.created = row->data().created;
	  // No new provider refresh token was returned; preserve ciphertext.
	  next.protectedRefreshToken = row->data().protectedRefreshToken;
	  row->data() = ZuMv(next);
	  self->m_evidence = row->data();
	  self->finish_(row->commit() && self->m_eligible);
	  return;
	}
	ZdbRowRef<Evidence> created = new ZdbRow<Evidence>{
	  evidence, ZdbShard{0}};
	evidence->insert(ZuMv(created), [self = ZuMv(self)](
	    ZdbRow<Evidence> *row) mutable {
	  if (!row) { self->finish_(false); return; }
	  new (row->ptr()) Evidence{self->evidenceRecord_()};
	  self->m_evidence = row->data();
	  self->finish_(row->commit() && self->m_eligible);
	});
      });
    });
  }

  void finish_(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ok, ok ? ZuMv(m_user) : User{},
      ok ? ZuMv(m_roles) : IDVec{}, ok ? ZuMv(m_evidence) : Evidence{});
  }

  DBContext		*m_context = nullptr;
  String		m_subject;
  AppID			m_appID = 0;
  ProviderID		m_providerID = 0;
  String		m_issuer;
  StringVec		m_roleValues;
  StringVec		m_eligibilityValues;
  StringVec		m_allowedEligibility;
  EligibilityMode::T	m_eligibilityMode = EligibilityMode::MappedRole;
  ClaimSource::T	m_claimSource = ClaimSource::IDToken;
  uint64_t		m_policyVersion = 0;
  uint32_t		m_assignmentMaxAge = 0;
  int64_t		m_now = 0;
  IDVec			m_roles;
  User			m_user;
  Evidence		m_evidence;
  OIDCUserFn		m_complete;
  Ztls::Random		m_rng;
  bool			m_eligible = false;
  bool			m_done = false;
};
using OIDCUserLoadHeap =
  ZmHeap<"Zum.zumd.oidc.OIDCUserLoad", OIDCUserLoad__<>>;
ZuDerive(OIDCUserLoad_, (OIDCUserLoad__<OIDCUserLoadHeap>));

void oidcLoadUser(
    DBContext *context, String subject, const OIDCConfig &config,
    StringVec roleValues, StringVec eligibilityValues,
    int64_t now, OIDCUserFn complete)
{
  ZmRef<OIDCUserLoad_> load = new OIDCUserLoad_{context, ZuMv(subject),
    config, ZuMv(roleValues), ZuMv(eligibilityValues), now, ZuMv(complete)};
  load->start();
}

struct OIDCKey {
  String	id;
  Bytes		publicKey;
};

ZuDerive(OIDCKeyVec,
  (ZtArray<OIDCKey, ZtArrayHeapID<"Zum.OIDC.KeySet">>));

class OIDCReqData : public ZmObject {
public:
  OIDCReqData(OIDCState *state_) : state{state_} { }

  OIDCState		*state = nullptr;
  String		stateID;
  String		nonce;
  String		verifier;
  String		keyID;
  String		idToken;
  String		accessToken;
  Bytes			grantID;
  OIDCConfig		config;
  ZmScheduler::Timer	timer;
  OIDCFinishFn		complete;
  bool			consumed = false;
};

static const String &oidcReqID(const OIDCReqData &request)
{
  return request.stateID;
}

ZmHashDerive(OIDCReqHash, OIDCReqData,
  (ZmHashNode<OIDCReqData,
    ZmHashKey<oidcReqID,
      ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.OIDC.Pending">>>>));
using OIDCReq = OIDCReqHash::Node;
static bool randomText(Ztls::Random &rng, String &value)
{
  ZuBArray<OIDCRandomSize> random(OIDCRandomSize, false);
  if (!rng.random(random)) return false;
  value.length(ZuBase64URL::enclen(random.length()));
  return ZuBase64URL::encode(value.span(), random) ==
    value.length();
}

static void formField(String &out, bool &first, ZuCSpan name, ZuCSpan value)
{
  if (!first) out << '&';
  first = false;
  out << name << '=';
  ZfURI::PathQuote::quote(out, value);
}

static bool tokenResponse(
    String &json, const OIDCLimits &limits, bool needAccessToken,
    String &idToken, String &accessToken)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCToken wire;
  if (!jsonLoad(json, root, wire) || !wire.idToken ||
      wire.idToken.length() > limits.jwt.token ||
      (needAccessToken && (!wire.accessToken ||
        wire.accessToken.length() > limits.response)))
    return false;
  idToken = ZuMv(wire.idToken);
  accessToken = ZuMv(wire.accessToken);
  return true;
}

static bool userinfoResponse(
    String &json, const OIDCLimits &limits, const OIDCConfig &config,
    ZuCSpan subject, StringVec &roleValues, StringVec &eligibilityValues)
{
  if (!json || json.length() > limits.response || !subject) return false;
  if (!json.mutable_()) json.length(json.length());
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCUserInfo wire;
  if (!jsonLoad(json, root, wire)) return false;
  StringVec nextRoles, nextEligibility;
  bool roles = config.roles != OIDCRoles::Mapped ||
    stringsValue(jsonField(root, config.roleClaim), limits.roleValues, nextRoles);
  bool eligibility = config.eligibilityMode != EligibilityMode::ClaimValues ||
    stringsValue(jsonField(root, config.eligibilityClaim),
      limits.roleValues, nextEligibility);
  if (wire.subject != subject || !roles || !eligibility) return false;
  roleValues = ZuMv(nextRoles);
  eligibilityValues = ZuMv(nextEligibility);
  return true;
}

static bool jwksResponse(
    String &json, const OIDCLimits &limits, OIDCKeyVec &keys)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCJWKSResponse response;
  if (!jsonLoad(json, root, response) || !response.keys ||
      response.keys.length() > limits.keys) return false;
  OIDCKeyVec next;
  for (auto &wire: response.keys) {
    if (!wire.kid || wire.kty != "EC" || wire.crv != "P-256" ||
        !wire.x || !wire.y || (wire.use && wire.use != "sig") ||
        (wire.alg && wire.alg != "ES256")) continue;
    OIDCKey key{.id = ZuMv(wire.kid)};
    key.publicKey.length(Ztls::COSE::ES256::PublicKeySize, false);
    key.publicKey[0] = 4;
    if (ZuBase64URL::decode({key.publicKey.data() + 1,
          Ztls::COSE::ES256::CoordinateSize}, ZuBSpan{wire.x}) !=
          Ztls::COSE::ES256::CoordinateSize ||
        ZuBase64URL::decode({key.publicKey.data() + 1 +
          Ztls::COSE::ES256::CoordinateSize, Ztls::COSE::ES256::CoordinateSize},
          ZuBSpan{wire.y}) != Ztls::COSE::ES256::CoordinateSize ||
        !Ztls::COSE::ES256::validPK(key.publicKey))
      continue;
    for (auto &existing: next)
      if (existing.id == key.id) return false;
    next.push(ZuMv(key));
  }
  if (!next) return false;
  keys = ZuMv(next);
  return true;
}

static bool discoveryResponse(
    String &json, const OIDCLimits &limits, OIDCConfig &config)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCDiscovery wire;
  if (!jsonLoad(json, root, wire)) return false;
  auto contains = [](const StringVec &values, ZuCSpan value) {
    for (auto &candidate: values) if (candidate == value) return true;
    return false;
  };
  ZuCSpan method = config.clientAuth == OIDCClientAuth::Basic ?
    ZuCSpan{"client_secret_basic"} :
    config.clientAuth == OIDCClientAuth::Post ?
      ZuCSpan{"client_secret_post"} : ZuCSpan{"none"};
  if (wire.issuer != config.issuer || !wire.authorize || !wire.token ||
      !wire.jwks || !contains(wire.responses, "code") ||
      !contains(wire.algorithms, "ES256") ||
      !contains(wire.methods, method) ||
      (config.claimSource == ClaimSource::UserInfo && !wire.userinfo))
    return false;
  config.authorizeEndpoint = ZuMv(wire.authorize);
  config.tokenEndpoint = ZuMv(wire.token);
  config.jwksEndpoint = ZuMv(wire.jwks);
  config.userinfoEndpoint = ZuMv(wire.userinfo);
  return oidcConfigValid(config);
}

template <typename Heap = ZuVoid>
class OIDCState_ : public Heap, public ZmObject  {
public:
  bool init(
      ZmScheduler *scheduler, unsigned sid, DBContext *context,
      OIDCLimits limits, unsigned pendingLimit,
      uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
  {
    if (!scheduler || !sid || sid > scheduler->params().nThreads() ||
        !context || !limits.response ||
        !limits.keys || !pendingLimit || !timeout || !clock || !http ||
        !m_rng.init())
      return false;
    m_scheduler = scheduler;
    m_sid = sid;
    m_context = context;
    m_limits = limits;
    m_pendingLimit = pendingLimit;
    m_timeout = timeout;
    m_clock = ZuMv(clock);
    m_http = ZuMv(http);
    m_up = 1;
    return true;
  }

  void final()
  {
    m_up = 0;
    invoke_([self = ZmRef<OIDCState_>{this}]() mutable {
      {
	auto i = self->m_pending.iter();
	while (auto request = i()) {
		  self->m_scheduler->del(&request->timer);
		  auto complete = ZuMv(request->complete);
		  request->state = nullptr;
		  self->clearRequest_(*request);
		  i.del();
		  if (complete)
		    complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
		}
      }
      self->m_http = OIDCHTTPFn{};
      self->m_clock = OIDCClockFn{};
    });
  }

  bool begin(Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
  {
    if (!m_up || !grantID || !oidcConfigValid(config) || !complete) {
      clearSecret_(config);
      return false;
    }
    invoke_([self = ZmRef<OIDCState_>{this}, grantID = ZuMv(grantID),
        config = ZuMv(config), complete = ZuMv(complete)]() mutable {
      self->beginSelect_(ZuMv(grantID), ZuMv(config), ZuMv(complete));
    });
    return true;
  }

  bool finish(AppID appID, String query, OIDCFinishFn complete)
  {
    if (!m_up || !appID || !query || !complete) return false;
    invoke_([self = ZmRef<OIDCState_>{this}, appID, query = ZuMv(query),
        complete = ZuMv(complete)]() mutable {
      self->finish_(appID, ZuMv(query), ZuMv(complete));
    });
    return true;
  }

private:
  void invoke_(ZmFn<void()> fn)
  {
    m_scheduler->run([fn = ZuMv(fn)]() mutable { fn(); }, m_sid);
  }

  static void clearSecret_(OIDCConfig &config)
  {
    if (config.clientSecret.mutable_())
      ZuClear(config.clientSecret.data(), config.clientSecret.length());
    config.clientSecret.null();
  }

  void beginSelect_(
      Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
  {
    if (!m_up || m_pending.count_() + m_discovering >= m_pendingLimit) {
      clearSecret_(config);
      complete(false, String{});
      return;
    }
    if (config.authorizeEndpoint) {
      begin_(ZuMv(grantID), ZuMv(config), ZuMv(complete));
      return;
    }
    ++m_discovering;
    // OIDC Discovery appends to the issuer path, unlike RFC 8414's rule.
    String url{config.issuer};
    if (url[url.length() - 1] == '/') url.length(url.length() - 1);
    url << "/.well-known/openid-configuration";
    auto send = m_http;
    send(OIDCHTTPRequest{.url = ZuMv(url)}, [
      self = ZmRef<OIDCState_>{this}, grantID = ZuMv(grantID),
      config = ZuMv(config), complete = ZuMv(complete)
    ](unsigned status, String body) mutable {
      self->invoke_([self, grantID = ZuMv(grantID),
          config = ZuMv(config), complete = ZuMv(complete), status,
          body = ZuMv(body)]() mutable {
        self->discovered_(ZuMv(grantID), ZuMv(config),
          ZuMv(complete), status, ZuMv(body));
      });
    });
  }

  void discovered_(Bytes grantID, OIDCConfig config, OIDCBeginFn complete,
      unsigned status, String body)
  {
    if (m_discovering) --m_discovering;
    if (!m_up || status != 200 ||
        !discoveryResponse(body, m_limits, config)) {
      clearSecret_(config);
      complete(false, String{});
      return;
    }
    begin_(ZuMv(grantID), ZuMv(config), ZuMv(complete));
  }

  void begin_(Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
  {
    if (!m_up || m_pending.count_() >= m_pendingLimit) {
      clearSecret_(config);
      complete(false, String{});
      return;
    }
    ZmRef<OIDCReq> request = new OIDCReq{static_cast<OIDCState *>(this)};
    if (!randomText(m_rng, request->stateID) ||
        !randomText(m_rng, request->nonce) ||
        !randomText(m_rng, request->verifier)) {
      clearSecret_(config);
      complete(false, String{});
      return;
    }
    request->grantID = ZuMv(grantID);
    request->config = ZuMv(config);
    ZuBArray<Ztls::MD<>::Size> digest(Ztls::MD<>::Size, false);
    Ztls::MD<> md;
    md.update(ZuBSpan{request->verifier});
    md.finish(digest);
    String challenge;
    challenge.length(ZuBase64URL::enclen(digest.length()));
    challenge.length(ZuBase64URL::encode(challenge.span(), digest));
    if (!challenge || m_pending.find(request->stateID)) {
      clearSecret_(config);
      complete(false, String{});
      return;
    }
    String scope;
    for (auto &value: request->config.oidcScopes) {
      if (!value) continue;
      if (scope) scope << ' ';
      scope << value;
    }
    String location{request->config.authorizeEndpoint};
    location << (location.find<"?">() >= 0 ? '&' : '?');
    bool first = true;
    formField(location, first, "response_type", "code");
    formField(location, first, "client_id", request->config.clientID);
    formField(location, first, "redirect_uri", request->config.redirectURI);
    formField(location, first, "scope", scope);
    if (request->config.loginHint)
      formField(location, first, "login_hint", request->config.loginHint);
    formField(location, first, "state", request->stateID);
    formField(location, first, "nonce", request->nonce);
    formField(location, first, "code_challenge", challenge);
    formField(location, first, "code_challenge_method", "S256");
    String claims;
    ZfJSON::save(claims, OIDCAuthorizeClaims{});
    formField(location, first, "claims", claims);
    if (request->config.prompt)
      formField(location, first, "prompt", request->config.prompt);
    if (request->config.maxAgePresent) {
      String age;
      age << request->config.maxAge;
      formField(location, first, "max_age", age);
    }
    m_pending.addNode(request);
    m_scheduler->add(&request->timer, Zm::now() + ZuTime{double(m_timeout)},
      ZmScheduler::Update, [this, request](auto &&arm) {
        return arm([this, request]() mutable {
          if (request->state) fail_(ZuMv(request));
        });
      }, m_sid);
    complete(true, ZuMv(location));
  }

  void finish_(AppID appID, String query, OIDCFinishFn complete)
  {
    if (!m_up || query.length() > m_limits.callback) {
      complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
      return;
    }
    if (!query.mutable_()) query.length(query.length());
    if (query[0] == '?') query.splice(0, 1);
    String code, stateID, error;
    unsigned seen = 0;
    bool valid = true;
    constexpr auto matcher = ZuMatcher<OIDCCallbackFields>();
    valid = formEach({query.data(), query.length()},
      [&code, &stateID, &error, &seen, &valid, &matcher](
          ZuCSpan name, ZuCSpan value) {
        String *target;
        unsigned bit;
        switch (matcher.exact(name)) {
          case 0: target = &code; bit = 1U; break;
          case 1: target = &stateID; bit = 2U; break;
          case 2: target = &error; bit = 4U; break;
          default: return;
        }
        if (seen & bit) { valid = false; return; }
        seen |= bit;
        *target = value;
      }) && valid;
    ZmRef<OIDCReq> request;
    if (stateID) request = m_pending.find(stateID);
    if (!valid || !request || request->config.appID != appID ||
	request->consumed || !(seen & 2U) ||
        bool(code) == bool(error)) {
      complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
      return;
    }
    request->consumed = true;
    request->complete = ZuMv(complete);
    if (error) { fail_(request); return; }
    String body;
    bool first = true;
    formField(body, first, "grant_type", "authorization_code");
    formField(body, first, "code", code);
    formField(body, first, "redirect_uri", request->config.redirectURI);
    formField(body, first, "code_verifier", request->verifier);
    OIDCHTTPRequest http{
      .url = request->config.tokenEndpoint,
      .contentType = "application/x-www-form-urlencoded",
      .body = ZuMv(body), .method = OIDCHTTPMethod::POST};
    switch (request->config.clientAuth) {
      case OIDCClientAuth::Basic: {
        String plain;
        ZfURI::PathQuote::quote(plain, request->config.clientID);
        plain << ':';
        ZfURI::PathQuote::quote(plain, request->config.clientSecret);
        String encoded;
        encoded.length(ZuBase64::enclen(plain.length()));
        encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
        if (plain.mutable_()) ZuClear(plain.data(), plain.length());
        http.authorization << "Basic " << encoded;
        if (encoded.mutable_()) ZuClear(encoded.data(), encoded.length());
      } break;
      case OIDCClientAuth::Post:
        formField(http.body, first, "client_id", request->config.clientID);
        formField(http.body, first, "client_secret",
          request->config.clientSecret);
        break;
      case OIDCClientAuth::None:
        formField(http.body, first, "client_id", request->config.clientID);
        break;
    }
    auto send = m_http;
    send(ZuMv(http), [self = ZmRef<OIDCState_>{this}, request](
        unsigned status, String body) mutable {
      self->invoke_([self, request, status, body = ZuMv(body)]() mutable {
        self->token_(request, status, ZuMv(body));
      });
    });
  }

  void token_(ZmRef<OIDCReq> request, unsigned status, String body)
  {
    if (!active_(request) || status != 200 ||
        !tokenResponse(body, m_limits,
          request->config.claimSource == ClaimSource::UserInfo,
          request->idToken, request->accessToken)) {
      fail_(request);
      return;
    }
    JWTHeader header;
    if (!jwtHeader(request->idToken, m_limits.jwt, header) ||
        header.algorithm != "ES256" || !header.keyID) {
      fail_(request);
      return;
    }
    request->keyID = ZuMv(header.keyID);
    // Re-fetch the bounded issuer key set for each SSO exchange. No key
    // survives provider removal merely because an earlier login used it.
    auto send = m_http;
    send(OIDCHTTPRequest{.url = request->config.jwksEndpoint},
      [self = ZmRef<OIDCState_>{this}, request](
          unsigned status, String body) mutable {
        self->invoke_([self, request, status, body = ZuMv(body)]() mutable {
          self->keys_(request, status, ZuMv(body));
        });
      });
  }

  void keys_(ZmRef<OIDCReq> request, unsigned status, String body)
  {
    OIDCKeyVec keys;
    if (!active_(request) || status != 200 ||
        !jwksResponse(body, m_limits, keys)) {
      fail_(request);
      return;
    }
    for (const auto &key: keys)
      if (key.id == request->keyID) {
	verify_(request, key.publicKey);
	return;
      }
    fail_(request);
  }

  void verify_(ZmRef<OIDCReq> request, ZuBSpan publicKey)
  {
    OIDCClaims claims;
    int64_t now = m_clock ? m_clock() : 0;
    if (!active_(request) || !oidcVerifyIDToken(request->idToken, publicKey,
        request->nonce, request->config, now, m_limits, claims)) {
      fail_(request);
      return;
    }
    int64_t authTime = claims.authTime;
    if (request->config.claimSource == ClaimSource::UserInfo) {
      OIDCHTTPRequest http{
        .url = request->config.userinfoEndpoint,
        .method = OIDCHTTPMethod::GET};
      http.authorization << "Bearer " << request->accessToken;
      if (request->accessToken.mutable_())
        ZuClear(request->accessToken.data(), request->accessToken.length());
      request->accessToken.null();
      auto subject = claims.subject;
      auto send = m_http;
      send(ZuMv(http), [self = ZmRef<OIDCState_>{this}, request,
          subject = ZuMv(subject), authTime, now](
          unsigned status, String body) mutable {
        self->invoke_([self, request, subject = ZuMv(subject), authTime, now,
            status, body = ZuMv(body)]() mutable {
          self->userinfo_(request, ZuMv(subject), authTime, now,
            status, ZuMv(body));
        });
      });
      return;
    }
    load_(request, ZuMv(claims.subject), ZuMv(claims.roleValues),
      ZuMv(claims.eligibilityValues), authTime, now);
  }

  void userinfo_(ZmRef<OIDCReq> request, String subject,
      int64_t authTime, int64_t now, unsigned status, String body)
  {
    StringVec roleValues, eligibilityValues;
    if (!active_(request) || status != 200 ||
        !userinfoResponse(body, m_limits, request->config, subject,
          roleValues, eligibilityValues)) {
      fail_(request);
      return;
    }
    load_(request, ZuMv(subject), ZuMv(roleValues),
      ZuMv(eligibilityValues), authTime, now);
  }

  void load_(ZmRef<OIDCReq> request, String subject, StringVec roleValues,
      StringVec eligibilityValues, int64_t authTime, int64_t now)
  {
    oidcLoadUser(m_context, ZuMv(subject), request->config,
      ZuMv(roleValues), ZuMv(eligibilityValues), now,
      [self = ZmRef<OIDCState_>{this}, request, authTime](
          bool ok, User user, IDVec roleIDs, Evidence evidence) mutable {
        self->invoke_([self, request, ok, user = ZuMv(user),
            roleIDs = ZuMv(roleIDs), evidence = ZuMv(evidence),
            authTime]() mutable {
          if (!ok || !self->active_(request)) { self->fail_(request); return; }
          auto grantID = request->grantID;
          self->complete_(request, true, ZuMv(grantID), ZuMv(user),
            ZuMv(roleIDs), ZuMv(evidence), authTime);
        });
      });
  }

  bool active_(const ZmRef<OIDCReq> &request)
  {
    return m_up && request && request->state == this &&
      m_pending.find(request->stateID) == request;
  }

  void fail_(ZmRef<OIDCReq> request)
  {
    if (!active_(request)) return;
    complete_(request, false, {}, {}, {}, {}, 0);
  }

  static void clearRequest_(OIDCReq &request)
  {
    if (request.verifier.mutable_())
      ZuClear(request.verifier.data(), request.verifier.length());
    request.verifier.null();
    if (request.idToken.mutable_())
      ZuClear(request.idToken.data(), request.idToken.length());
    request.idToken.null();
    if (request.accessToken.mutable_())
      ZuClear(request.accessToken.data(), request.accessToken.length());
    request.accessToken.null();
    clearSecret_(request.config);
    request.config = {};
  }

  void complete_(
      ZmRef<OIDCReq> request, bool ok, Bytes grantID, User user,
      IDVec roleIDs, Evidence evidence, int64_t authTime)
  {
    m_scheduler->del(&request->timer);
    m_pending.del(request->stateID);
    request->state = nullptr;
    clearRequest_(*request);
    auto complete = ZuMv(request->complete);
    if (complete)
      complete(ok, ZuMv(grantID), ZuMv(user), ZuMv(roleIDs),
        ZuMv(evidence), authTime);
  }

  ZmScheduler	*m_scheduler = nullptr;
  unsigned	m_sid = 0;
  DBContext	*m_context = nullptr;
  OIDCLimits	m_limits;
  unsigned	m_pendingLimit = 0;
  uint64_t	m_timeout = 0;
  OIDCHTTPFn	m_http;
  OIDCClockFn	m_clock;
  Ztls::Random	m_rng;
  OIDCReqHash	m_pending;
  unsigned	m_discovering = 0;
  ZmAtomic<uint32_t> m_up = 0;
};
using OIDCStateHeap = ZmHeap<"Zum.zumd.oidc.OIDCState", OIDCState_<>>;
ZuDerive(OIDCState, (OIDCState_<OIDCStateHeap>));
OIDC::OIDC() = default;
OIDC::~OIDC() { final(); }

bool OIDC::init(
    ZmScheduler *scheduler, unsigned sid, DBContext *context,
    OIDCLimits limits, unsigned pendingLimit,
    uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
{
  if (m_state) return false;
  ZmRef<OIDCState> state = new OIDCState;
  if (!state->init(scheduler, sid, context, limits,
      pendingLimit, timeout, ZuMv(clock), ZuMv(http))) return false;
  m_state = ZuMv(state);
  return true;
}

void OIDC::final()
{
  if (!m_state) return;
  auto state = ZuMv(m_state);
  state->final();
}

bool OIDC::begin(Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
{
  return m_state && m_state->begin(
    ZuMv(grantID), ZuMv(config), ZuMv(complete));
}

bool OIDC::finish(AppID appID, String query, OIDCFinishFn complete)
{
  return m_state && m_state->finish(appID, ZuMv(query), ZuMv(complete));
}

namespace OIDCHTTP_ {

enum {
  RequestTimeout = 15,
  ResponseMax = OIDCLimitMax::Response,
  Concurrency = 32
};

template <unsigned Status_, typename Heap = ZuVoid>
struct ResponseData_ : public Heap, public ZmObject  {
  enum { Status = Status_ };
  String body;
  ResponseData_ &operator =(ZuSpan<uint8_t> data) {
    body = data;
    return *this;
  }
};
template <unsigned Status_>
using ResponseData = ResponseData_<Status_,
  ZmHeap<"Zum.zumd.oidc.ResponseData", ResponseData_<Status_>>>;

template <unsigned Status_>
struct Response : public Zrest::ResParser<Response<Status_>,
    ResponseData<Status_>> {
  enum { Status = Status_, Body = Zrest::BodyPolicy::Raw };

  bool bodyInfo(Zhttp::BodyType::T type, uint64_t length) {
    if (type == Zhttp::BodyType::Fixed && length > ResponseMax) return false;
    m_fixed = type == Zhttp::BodyType::Fixed;
    m_remaining = length;
    return true;
  }

  template <typename Rx> bool body(Rx &rx) {
    while (rx) {
      int64_t consumed = rx.consume(
	[this](ZuSpan<uint8_t> span) -> int64_t {
	  uint64_t n = span.length();
	  if (m_fixed && n > m_remaining) n = m_remaining;
	  if (this->object->body.length() + n > ResponseMax) return -1;
	  return n;
	},
	[this](ZuSpan<uint8_t> span) {
	  this->object->body << span;
	  if (m_fixed) m_remaining -= span.length();
	});
      if (consumed < 0) return false;
      if (!consumed) break;
    }
    return true;
  }

private:
  uint64_t m_remaining = 0;
  bool m_fixed = false;
};

using Responses = ZuTypeList<Response<200>, Response<201>, Response<204>,
  Response<400>, Response<401>, Response<403>, Response<404>, Response<405>,
  Response<409>, Response<415>, Response<422>, Response<429>, Response<500>,
  Response<501>, Response<502>, Response<503>, Response<504>>;

template <typename Heap = ZuVoid>
struct Call_ : public Heap, public ZmObject  {
  mutable String	contentType;
  mutable String	authorization;
  mutable String	target;
  mutable String	body;
  mutable OIDCHTTPDoneFn complete;
  mutable ZmAtomic<unsigned> done = 0;

  void finish(unsigned status, String value) const {
    if (done.cmpXch(1, 0)) return;
    if (authorization.mutable_())
      ZuClear(authorization.data(), authorization.length());
    authorization.null();
    if (body.mutable_()) ZuClear(body.data(), body.length());
    body.null();
    auto fn = ZuMv(complete);
    if (fn) fn(status, ZuMv(value));
  }

  template <typename Link, typename Value>
  void process(Link *, const Value *value) const {
    finish(Value::Status, value->body);
  }
  template <typename Link> void failed(Link *) const {
    finish(0, {});
  }
};
using CallHeap = ZmHeap<"Zum.zumd.oidc.Call", Call_<>>;
ZuDerive(Call, (Call_<CallHeap>));

template <typename Impl, Zhttp::Method::T Method_, unsigned Body_>
struct Request : public Zrest::ReqBuilder<Impl, Call> {
  using Base = Zrest::ReqBuilder<Impl, Call>;
  using Base::header;
  enum { Method = Method_, Exact = 1, Query = Zrest::QueryPolicy::Raw,
    Body = Body_ };
  using Path = ZuStringT<"/">;
  using Headers = ZhttpHeaders(
    "content-type", "authorization", "content-length");
  using Responses = OIDCHTTP_::Responses;

  const String &queryObject(const Call *call) const { return call->target; }
  const String &bodyObject(const Call *call) const { return call->body; }
  template <typename Key, typename L> void header(L &&l) const {
    if constexpr (Key{}() == "content-type")
      l(this->object->contentType);
    else if constexpr (Key{}() == "authorization")
      l(this->object->authorization);
    else Base::template header<Key>(ZuFwd<L>(l));
  }
};

struct GET : public Request<GET, Zhttp::Method::GET,
    Zrest::BodyPolicy::None> { };
struct POST : public Request<POST, Zhttp::Method::POST,
    Zrest::BodyPolicy::Raw> { };
using Requests = ZuTypeList<GET, POST>;
ZrestCatalogDerive(Catalog, Requests);
ZrestCatalogImpl(Catalog)

struct ReqBuilder_ : public ZmObject, public Zrest::MReqBuilder<Catalog> {
  using Reqs = Requests;
  uint64_t id = 0;
  uint64_t key() const { return id; }
  uint64_t length() const { return 1; }

  void selected(const Zhttp::Endpoint *endpoint, uint64_t, uint64_t,
      unsigned, unsigned, Zhttp::Transport::T transport,
      Zhttp::Version::T version) {
    ZiLOG(Debug, "zumd", ([ip = endpoint->ip, port = endpoint->port,
        transport, version](auto &s) {
      s << "OIDC HTTP selected " << ip << ':' << port
	<< " transport=" << Zhttp::Transport{}.name(transport)
	<< " HTTP=" << Zhttp::Version{}.name(version);
    }));
  }

  void completed(const Zhttp::Result &result) {
    u.cdispatch([&result](auto, const auto &request) {
      if (!request.object->done.load_())
        request.object->finish(result.status, {});
    });
  }
};

struct ResParser : public Zrest::MResParser<Catalog, ReqBuilder_> { };

class OIDCClient;
struct Pool;
template <typename Heap> class Pool_;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zum.OIDC.HTTP.Request">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

template <typename Heap = ZuVoid>
class Pool_ : public Heap, public Zhttp::Pool<OIDCClient, TxQ, ResParser> {
  using Base = Zhttp::Pool<OIDCClient, TxQ, ResParser>;
public:
  Pool_(OIDCClient *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) {
    return {};
  }
private:
  ReqBuilderQ m_requests;
};

using PoolHeap = ZmHeap<"Zum.OIDC.HTTP.Pool", Pool_<>>;
ZuDerive(Pool, (Pool_<PoolHeap>));

template <typename Heap = ZuVoid>
class Client_ : public Heap, public ZmObject,
    public Zhttp::Client<OIDCClient, Pool> {
public:
  using Base = Zhttp::Client<OIDCClient, Pool>;

  bool init(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath) {
    m_host = url.host;
    m_port = url.port;
    m_ipv6Literal = url.ipv6Literal;
    auto config = Zhttp::Config().links(2).concurrency(Concurrency)
      .linkMax(Concurrency).requestTimeout(RequestTimeout)
      .maxRetries(1).retainedBodyMax(ResponseMax)
      .protocol(Zhttp::ProtoPolicy::DisableH3)
      .secure(true).tcp(true).tls(true).quic(false);
    if (!Base::init(Zhttp::HubConfig{mx, "rx", "tx"}, 1,
        config, Zhttp::TCPConfig{}, Zhttp::H2Config{}.caPath(caPath),
        Zhttp::QUICConfig{})) return false;
    Base::txErrorFn(ZiTxErrorFn{[](ZeException &e) {
      ZiLOG(Error, "zumd", ([e](auto &s) {
	s << "OIDC HTTP transmit error: " << e;
      }));
      return false;
    }});
    return Base::pool(0, Zhttp::Destination{url.origin()});
  }

  bool origin(const Zhttp::URLView &url) const {
    return url.scheme == Zhttp::Scheme::https && url.host == m_host &&
      url.port == m_port && url.ipv6Literal == m_ipv6Literal;
  }

  void send(OIDCHTTPRequest request, OIDCHTTPDoneFn complete,
      const Zhttp::URLView &url) {
    ZmRef<Call> call = new Call{};
    call->contentType = ZuMv(request.contentType);
    call->authorization = ZuMv(request.authorization);
    call->body = ZuMv(request.body);
    call->complete = ZuMv(complete);
    if (url.path) {
      ZuBSpan path = url.path;
      if (path[0] == '/') path.offset(1);
      call->target = path;
    }
    if (url.hasQuery) call->target << '?' << url.query;
    ZmRef<ReqBuilder> builder = new ReqBuilder{};
    builder->id = ++m_id;
    switch (request.method) {
      case OIDCHTTPMethod::GET:
        builder->template init<GET>(call.ptr());
        break;
      case OIDCHTTPMethod::POST:
        builder->template init<POST>(call.ptr());
        break;
      default:
        call->finish(0, {});
        return;
    }
    if (!Base::send(0, ZuMv(builder))) call->finish(0, {});
  }

private:
  String	m_host;
  uint16_t	m_port = 0;
  uint64_t	m_id = 0;
  bool		m_ipv6Literal = false;
};
using ClientHeap = ZmHeap<"Zum.zumd.oidc.Client", Client_<>>;
class OIDCClient : public Client_<ClientHeap> {
public:
  using Client_<ClientHeap>::Client_;
};

struct Entry {
  ZmRef<OIDCClient> client;
  unsigned pending = 0;
  bool retiring = false;
};
ZuDerive(Clients,
  (ZtArray<Entry, ZtArrayHeapID<"Zum.OIDC.HTTP.Clients">>));

} // namespace OIDCHTTP_

template <typename Heap = ZuVoid>
class OIDCHTTPState_ : public Heap, public ZmObject  {
public:
  bool init(ZiMultiplex *mx, unsigned sid, unsigned origins, ZuCSpan caPath) {
    if (!mx || m_up || !sid || !origins || sid > mx->params().nThreads() ||
        sid == mx->rxThread() || sid == mx->txThread()) return false;
    m_mx = mx;
    m_sid = sid;
    m_origins = origins;
    m_caPath = caPath;
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (m_resolverOwned)
      ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
    ZiResolver::start();
    m_up = true;
    return true;
  }

  void send(OIDCHTTPRequest request, OIDCHTTPDoneFn complete) {
    m_mx->run([this, request = ZuMv(request),
        complete = ZuMv(complete)]() mutable {
      send_(ZuMv(request), ZuMv(complete));
    }, m_sid);
  }

  void final() {
    if (!m_mx) return;
    ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	m_up = false;
	m_stopDone = ZuMv(wake);
	m_stopping = m_clients.length();
	if (!m_stopping) { stopped_(); return; }
	for (auto &entry: m_clients) {
	  if (entry.retiring) continue;
	  if (!entry.client) { stopOne_(); continue; }
	  entry.client->stop([this](bool) {
	    m_mx->run([this]() {
	      stopOne_();
	    }, m_sid);
	  });
	}
      }, m_sid);
    });
    for (auto &entry: m_clients) if (entry.client) entry.client->final();
    m_clients.init();
    if (m_resolverOwned) {
      ZiResolver::stop();
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_mx = nullptr;
  }

private:
  void send_(OIDCHTTPRequest request, OIDCHTTPDoneFn complete) {
    if (!m_up || !complete || !request.url) {
      if (complete) complete(0, String{});
      return;
    }
    Zhttp::URL parsed{request.url};
    auto url = parsed.url();
    if (!parsed.ok() || url.scheme != Zhttp::Scheme::https || !url.host ||
        url.hasFragment) {
      complete(0, String{});
      return;
    }
    unsigned count = m_clients.length(), idle = count;
    for (unsigned i = 0; i < count; ++i) {
      const auto &entry = m_clients[i];
      if (entry.retiring) continue;
      if (entry.client && entry.client->origin(url)) {
	dispatch_(i, ZuMv(request), ZuMv(complete), url);
	return;
      }
      if (!entry.pending && idle == count) idle = i;
    }
    if (count < m_origins) {
      m_clients.push(OIDCHTTP_::Entry{});
      dispatch_(count, ZuMv(request), ZuMv(complete), url);
      return;
    }
    if (idle == count) { complete(503, String{}); return; }
    auto &entry = m_clients[idle];
    if (!entry.client) {
      dispatch_(idle, ZuMv(request), ZuMv(complete), url);
      return;
    }
    entry.retiring = true;
    entry.client->stop([this, idle, request = ZuMv(request),
        complete = ZuMv(complete)](bool ok) mutable {
      m_mx->run([this, idle, ok, request = ZuMv(request),
          complete = ZuMv(complete)]() mutable {
	auto &entry = m_clients[idle];
	entry.retiring = false;
	if (!m_up) {
	  complete(0, String{});
	  stopOne_();
	  return;
	}
	// The native stop continuation has drained Rx/Tx; finalization does not wait.
	entry.client->final();
	entry.client = nullptr;
	if (!ok) { complete(0, String{}); return; }
	Zhttp::URL parsed{request.url};
	dispatch_(idle, ZuMv(request), ZuMv(complete), parsed.url());
      }, m_sid);
    });
  }

  void dispatch_(unsigned slot, OIDCHTTPRequest request,
      OIDCHTTPDoneFn complete, const Zhttp::URLView &url) {
    auto &entry = m_clients[slot];
    if (!entry.client) {
      ZmRef<OIDCHTTP_::OIDCClient> candidate = new OIDCHTTP_::OIDCClient{};
      if (!candidate->init(m_mx, url, m_caPath)) {
        candidate->final();
        complete(0, String{});
        return;
      }
      entry.client = ZuMv(candidate);
    }
    ++entry.pending;
    entry.client->start([this, slot, request = ZuMv(request),
        complete = ZuMv(complete)](bool ok) mutable {
      m_mx->run([this, slot, ok, request = ZuMv(request),
          complete = ZuMv(complete)]() mutable {
	if (!ok || !m_up) {
	  --m_clients[slot].pending;
	  complete(0, String{});
	  return;
	}
	Zhttp::URL parsed{request.url};
	m_clients[slot].client->send(ZuMv(request), [this, slot,
	    complete = ZuMv(complete)](unsigned status, String body) mutable {
	  m_mx->run([this, slot, status, body = ZuMv(body),
	      complete = ZuMv(complete)]() mutable {
	    --m_clients[slot].pending;
	    complete(status, ZuMv(body));
	  }, m_sid);
	}, parsed.url());
      }, m_sid);
    });
  }

  void stopOne_() {
    if (!--m_stopping) stopped_();
  }

  void stopped_() {
    auto complete = ZuMv(m_stopDone);
    complete(true);
  }

  ZiMultiplex		*m_mx = nullptr;
  OIDCHTTP_::Clients	m_clients;
  String		m_caPath;
  ZmFn<void(bool)>	m_stopDone;
  unsigned		m_sid = 0;
  unsigned		m_origins = 0;
  unsigned		m_stopping = 0;
  bool			m_resolverOwned = false;
  bool			m_up = false;
};
using OIDCHTTPStateHeap =
  ZmHeap<"Zum.zumd.oidc.OIDCHTTPState", OIDCHTTPState_<>>;
ZuDerive(OIDCHTTPState, (OIDCHTTPState_<OIDCHTTPStateHeap>));
OIDCHTTP::OIDCHTTP() = default;
OIDCHTTP::~OIDCHTTP() { final(); }

bool OIDCHTTP::init(ZiMultiplex *mx, unsigned sid, unsigned origins,
    ZuCSpan caPath)
{
  if (m_state) return false;
  ZmRef<OIDCHTTPState> state = new OIDCHTTPState{};
  if (!state->init(mx, sid, origins, caPath)) return false;
  m_state = ZuMv(state);
  return true;
}

OIDCHTTPFn OIDCHTTP::fn() const
{
  if (!m_state) return {};
  return OIDCHTTPFn{[state = m_state](
      OIDCHTTPRequest request, OIDCHTTPDoneFn complete) mutable {
    state->send(ZuMv(request), ZuMv(complete));
  }};
}

void OIDCHTTP::final()
{
  if (!m_state) return;
  auto state = ZuMv(m_state);
  state->final();
}

} // namespace Zum
