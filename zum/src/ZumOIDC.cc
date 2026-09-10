//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumOIDC.hh>

#include <string.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZhttpURL.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsRandom.hh>

namespace Zum {

static bool hasScope(const StringVec &scopes, ZuCSpan name)
{
  for (auto &scope: scopes) if (scope == name) return true;
  return false;
}

static bool reservedClaim(ZuCSpan name)
{
  return name == "iss" || name == "sub" || name == "aud" ||
    name == "azp" || name == "nonce" || name == "iat" || name == "exp";
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

static bool stringValue(ZfJSON::AnyNode *node, String &value)
{
  if (!node || !node->has<ZfJSON::AnyNode::String>()) return false;
  value = node->data<ZfJSON::AnyNode::String>();
  return true;
}

static bool integerValue(ZfJSON::AnyNode *node, int64_t &value)
{
  if (!node || !node->has<ZfJSON::AnyNode::Number>()) return false;
  auto parsed = ZfJSON::eov_Decimal(node->data<ZfJSON::AnyNode::Number>());
  if (parsed.p<0>() < 0) return false;
  value = int64_t(parsed.p<1>().floor());
  return true;
}

static bool stringsValue(
    ZfJSON::AnyNode *node, unsigned limit, StringVec &values)
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
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;

  OIDCClaims next;
  String authorizedParty;
  unsigned seen = 0;
  auto &fields = roots[0]->data<ZfJSON::AnyNode::Object>();
  for (auto &field: fields) {
    auto name = field.p<0>();
    auto value = field.p<1>().ptr();
    unsigned bit = 0;
    bool valid = true;
    if (name == "iss") {
      bit = 1U<<0; valid = stringValue(value, next.issuer);
    } else if (name == "sub") {
      bit = 1U<<1; valid = stringValue(value, next.subject);
    } else if (name == "aud") {
      bit = 1U<<2;
      valid = stringsValue(value, limits.audiences, next.audience);
    } else if (name == "azp") {
      bit = 1U<<3; valid = stringValue(value, authorizedParty);
    } else if (name == "nonce") {
      bit = 1U<<4; valid = stringValue(value, next.nonce);
    } else if (name == "iat") {
      bit = 1U<<5; valid = integerValue(value, next.iat);
    } else if (name == "exp") {
      bit = 1U<<6; valid = integerValue(value, next.expires);
    } else if (config.claimSource == ClaimSource::IDToken &&
        ((config.roles == OIDCRoles::Mapped &&
	name == config.roleClaim) ||
	(config.eligibilityMode == EligibilityMode::ClaimValues &&
	 name == config.eligibilityClaim))) {
      StringVec values;
      valid = stringsValue(value, limits.roleValues, values);
      if (config.roles == OIDCRoles::Mapped && name == config.roleClaim) {
	bit |= 1U<<7;
	next.roleValues = values;
      }
      if (config.eligibilityMode == EligibilityMode::ClaimValues &&
	  name == config.eligibilityClaim) {
	bit |= 1U<<8;
	next.eligibilityValues = ZuMv(values);
      }
    } else {
      continue;
    }
    if (!valid || (seen & bit)) return false;
    seen |= bit;
  }
  constexpr unsigned required = (1U<<3) - 1 | (7U<<4);
  if ((seen & required) != required || next.issuer != config.issuer ||
      !next.subject || next.nonce != nonce ||
      !hasAudience(next.audience, config.clientID) ||
      next.iat <= 0 || next.expires <= next.iat ||
      next.iat > now + limits.clockSkew ||
      now - limits.clockSkew >= next.expires ||
      (config.claimSource == ClaimSource::IDToken &&
       config.eligibilityMode == EligibilityMode::ClaimValues &&
       !(seen & (1U<<8)))) return false;
  if ((next.audience.length() > 1 || (seen & (1U<<3))) &&
      authorizedParty != config.clientID) return false;
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

class OIDCUserLoad_ : public ZmObject {
public:
  OIDCUserLoad_(DBContext *context, String subject,
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
    identities->run(0, [self = ZmRef<OIDCUserLoad_>{this},
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
        [self = ZmRef<OIDCUserLoad_>{this}](bool ok) mutable {
          if (!ok) self->finish_(false);
        }, [self = ZmRef<OIDCUserLoad_>{this}](bool ok) mutable {
          if (!ok) { self->finish_(false); return; }
          self->identity_();
        })) finish_(false);
  }

  void user_(UserID userID)
  {
    auto users = m_context->users;
    users->find<0>(0, ZuFwdTuple(userID), [
        self = ZmRef<OIDCUserLoad_>{this}](ZdbRowRef<User> row) mutable {
      if (!row || row->data().source != UserSource::External ||
          row->data().state != State::Active || row->data().owner) {
        self->finish_(false);
        return;
      }
      self->m_user = row->data();
      self->evidence_();
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
    auto key = ZuFwdTuple(m_appID, m_user.id, m_providerID);
    evidence->findUpd<0>(0, ZuMv(key), [
        self = ZmRef<OIDCUserLoad_>{this}, evidence](
          ZdbRow<Evidence> *row) mutable {
      if (row) {
        if (row->data().owner) { self->finish_(false); return; }
        auto next = self->evidenceRecord_();
        next.version = row->data().version + 1;
        next.created = row->data().created;
        // Retained upstream tokens are opaque protected envelopes.  This path
        // does not receive a new upstream refresh token; preserve ciphertext.
        next.protectedRefreshToken = row->data().protectedRefreshToken;
        next.refreshOutcome = row->data().refreshOutcome;
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

static const String &oidcKeyID(const OIDCKey &key) { return key.id; }
using OIDCKeyHash = ZmHash<OIDCKey,
  ZmHashKey<oidcKeyID,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.OIDC.Keys">>>>;
using OIDCKeyVec = ZtArray<OIDCKey, ZtArrayHeapID<"Zum.OIDC.KeySet">>;

class OIDCState;

class OIDCReq : public ZumObject {
public:
  OIDCReq(OIDCState *state_) : state{state_} { }

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

static const String &oidcReqID(const ZmRef<OIDCReq> &request)
{
  return request->stateID;
}

using OIDCReqHash = ZmHash<ZmRef<OIDCReq>,
  ZmHashKey<oidcReqID,
    ZmHashLock<ZmNoLock, ZmHashHeapID<"Zum.OIDC.Pending">>>>;
static bool randomText(Ztls::Random &rng, String &value)
{
  uint8_t random[OIDCRandomSize];
  if (!rng.random(random)) return false;
  value.length(ZuBase64URL::enclen(sizeof(random)));
  return ZuBase64URL::encode(value.span(), random) ==
    value.length();
}

static void formField(String &out, bool &first, ZuCSpan name, ZuCSpan value)
{
  if (!first) out << '&';
  first = false;
  out << name << '=';
  ZfURI::URIQuote<true>::quote(out, value);
}

static bool tokenResponse(
    String &json, const OIDCLimits &limits, bool needAccessToken,
    String &idToken, String &accessToken)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  String nextID, nextAccess;
  unsigned seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    String *target;
    unsigned bit;
    if (field.p<0>() == "id_token") {
      target = &nextID;
      bit = 1U;
    } else if (field.p<0>() == "access_token") {
      target = &nextAccess;
      bit = 2U;
    } else {
      continue;
    }
    if ((seen & bit) || !stringValue(field.p<1>().ptr(), *target))
      return false;
    seen |= bit;
  }
  if (!nextID || nextID.length() > limits.jwt.token ||
      (needAccessToken && (!nextAccess || nextAccess.length() > limits.response)))
    return false;
  idToken = ZuMv(nextID);
  accessToken = ZuMv(nextAccess);
  return true;
}

static bool userinfoResponse(
    String &json, const OIDCLimits &limits, const OIDCConfig &config,
    ZuCSpan subject, StringVec &roleValues, StringVec &eligibilityValues)
{
  if (!json || json.length() > limits.response || !subject) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  String nextSubject;
  StringVec nextRoles, nextEligibility;
  unsigned seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    auto name = field.p<0>();
    auto value = field.p<1>().ptr();
    unsigned bit = 0;
    bool valid = true;
    if (name == "sub") {
      bit = 1U;
      valid = stringValue(value, nextSubject);
    } else if ((config.roles == OIDCRoles::Mapped &&
	name == config.roleClaim) ||
	(config.eligibilityMode == EligibilityMode::ClaimValues &&
	 name == config.eligibilityClaim)) {
      StringVec values;
      valid = stringsValue(value, limits.roleValues, values);
      if (config.roles == OIDCRoles::Mapped && name == config.roleClaim) {
	bit |= 2U;
	nextRoles = values;
      }
      if (config.eligibilityMode == EligibilityMode::ClaimValues &&
	  name == config.eligibilityClaim) {
	bit |= 4U;
	nextEligibility = ZuMv(values);
      }
    } else {
      continue;
    }
    if (!valid || (seen & bit)) return false;
    seen |= bit;
  }
  if (!(seen & 1U) || nextSubject != subject ||
      (config.roles == OIDCRoles::Mapped && !(seen & 2U)) ||
      (config.eligibilityMode == EligibilityMode::ClaimValues &&
       !(seen & 4U))) return false;
  roleValues = ZuMv(nextRoles);
  eligibilityValues = ZuMv(nextEligibility);
  return true;
}

static bool jwksResponse(
    String &json, const OIDCLimits &limits, OIDCKeyVec &keys)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  ZfJSON::AnyNode *keysNode = nullptr;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == "keys") keysNode = field.p<1>().ptr();
  if (!keysNode || !keysNode->has<ZfJSON::AnyNode::Array>()) return false;
  auto &array = keysNode->data<ZfJSON::AnyNode::Array>();
  if (!array || array.length() > limits.keys) return false;
  OIDCKeyVec next;
  for (auto &node: array) {
    if (!node->has<ZfJSON::AnyNode::Object>()) continue;
    String kid, kty, crv, use, alg, x, y;
    unsigned seen = 0;
    bool valid = true;
    for (auto &field: node->data<ZfJSON::AnyNode::Object>()) {
      String *value = nullptr;
      unsigned bit = 0;
      if (field.p<0>() == "kid") { bit = 1U<<0; value = &kid; }
      else if (field.p<0>() == "kty") { bit = 1U<<1; value = &kty; }
      else if (field.p<0>() == "crv") { bit = 1U<<2; value = &crv; }
      else if (field.p<0>() == "use") { bit = 1U<<3; value = &use; }
      else if (field.p<0>() == "alg") { bit = 1U<<4; value = &alg; }
      else if (field.p<0>() == "x") { bit = 1U<<5; value = &x; }
      else if (field.p<0>() == "y") { bit = 1U<<6; value = &y; }
      else continue;
      if (!stringValue(field.p<1>().ptr(), *value)) {
	valid = false;
	break;
      }
      seen |= bit;
    }
    constexpr unsigned required =
      (1U<<0) | (1U<<1) | (1U<<2) | (1U<<5) | (1U<<6);
    if (!valid || (seen & required) != required || !kid || kty != "EC" ||
        crv != "P-256" || (use && use != "sig") ||
        (alg && alg != "ES256")) continue;
    OIDCKey key{.id = ZuMv(kid)};
    key.publicKey.length(Ztls::COSE::ES256::PublicKeySize, false);
    key.publicKey[0] = 4;
    if (ZuBase64URL::decode({key.publicKey.data() + 1,
          Ztls::COSE::ES256::CoordinateSize}, ZuBSpan{x}) !=
          Ztls::COSE::ES256::CoordinateSize ||
        ZuBase64URL::decode({key.publicKey.data() + 1 +
          Ztls::COSE::ES256::CoordinateSize, Ztls::COSE::ES256::CoordinateSize},
          ZuBSpan{y}) != Ztls::COSE::ES256::CoordinateSize)
      continue;
    bool duplicate = false;
    for (auto &existing: next)
      if (existing.id == key.id) { duplicate = true; break; }
    if (duplicate) continue;
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
  auto parsed = ZfJSON::scan({json.data(), json.length()});
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return false;
  String issuer, authorize, token, jwks, userinfo;
  StringVec responses, algorithms, methods;
  unsigned seen = 0;
  for (auto &field: roots[0]->data<ZfJSON::AnyNode::Object>()) {
    auto name = field.p<0>();
    auto value = field.p<1>().ptr();
    bool valid = true;
    unsigned bit = 0;
    if (name == "issuer") {
      bit = 1U<<0; valid = stringValue(value, issuer);
    } else if (name == "authorization_endpoint") {
      bit = 1U<<1; valid = stringValue(value, authorize);
    } else if (name == "token_endpoint") {
      bit = 1U<<2; valid = stringValue(value, token);
    } else if (name == "jwks_uri") {
      bit = 1U<<3; valid = stringValue(value, jwks);
    } else if (name == "response_types_supported") {
      bit = 1U<<4; valid = stringsValue(value, 32, responses);
    } else if (name == "id_token_signing_alg_values_supported") {
      bit = 1U<<5; valid = stringsValue(value, 32, algorithms);
    } else if (name == "token_endpoint_auth_methods_supported") {
      bit = 1U<<6; valid = stringsValue(value, 32, methods);
    } else if (name == "userinfo_endpoint") {
      bit = 1U<<7; valid = stringValue(value, userinfo);
    } else {
      continue;
    }
    if (!valid || seen & bit) return false;
    seen |= bit;
  }
  constexpr unsigned required = (1U<<7) - 1;
  auto contains = [](const StringVec &values, ZuCSpan value) {
    for (auto &candidate: values) if (candidate == value) return true;
    return false;
  };
  ZuCSpan method = config.clientAuth == OIDCClientAuth::Basic ?
    ZuCSpan{"client_secret_basic"} :
    config.clientAuth == OIDCClientAuth::Post ?
      ZuCSpan{"client_secret_post"} : ZuCSpan{"none"};
  if ((seen & required) != required || issuer != config.issuer ||
      !contains(responses, "code") || !contains(algorithms, "ES256") ||
      !contains(methods, method) ||
      (config.claimSource == ClaimSource::UserInfo && !(seen & (1U<<7))))
    return false;
  config.authorizeEndpoint = ZuMv(authorize);
  config.tokenEndpoint = ZuMv(token);
  config.jwksEndpoint = ZuMv(jwks);
  config.userinfoEndpoint = ZuMv(userinfo);
  return oidcConfigValid(config);
}

class OIDCState : public ZumObject {
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
    invoke_([self = ZmRef<OIDCState>{this}]() mutable {
      auto i = self->m_pending.iter();
      while (auto request = i.val()) {
        self->m_scheduler->del(&request->timer);
        auto complete = ZuMv(request->complete);
        request->state = nullptr;
        i.del();
        if (complete)
          complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
      }
      self->m_keys.clean();
      self->m_http = OIDCHTTPFn{};
      self->m_clock = OIDCClockFn{};
    });
  }

  bool begin(Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
  {
    if (!m_up || !grantID || !oidcConfigValid(config) || !complete)
      return false;
    invoke_([self = ZmRef<OIDCState>{this}, grantID = ZuMv(grantID),
        config = ZuMv(config), complete = ZuMv(complete)]() mutable {
      self->beginSelect_(ZuMv(grantID), ZuMv(config), ZuMv(complete));
    });
    return true;
  }

  bool finish(String query, OIDCFinishFn complete)
  {
    if (!m_up || !query || !complete) return false;
    invoke_([self = ZmRef<OIDCState>{this}, query = ZuMv(query),
        complete = ZuMv(complete)]() mutable {
      self->finish_(ZuMv(query), ZuMv(complete));
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
    ZuCSpan issuer{config.issuer};
    auto scheme = issuer.find<"://">();
    unsigned authority = scheme >= 0 ? unsigned(scheme) + 3 : 0;
    ZuCSpan authoritySpan{
      issuer.data() + authority, issuer.length() - authority};
    auto slash = authoritySpan.find<"/">();
    String url;
    if (slash < 0 || unsigned(slash) + authority + 1 == issuer.length()) {
      url = issuer;
      if (url[url.length() - 1] == '/') url.length(url.length() - 1);
      url << "/.well-known/openid-configuration";
    } else {
      unsigned path = authority + unsigned(slash);
      url = ZuCSpan{issuer.data(), path};
      url << "/.well-known/openid-configuration" << issuer.offset(path);
    }
    auto send = m_http;
    send(OIDCHTTPRequest{.url = ZuMv(url)}, [
      self = ZmRef<OIDCState>{this}, grantID = ZuMv(grantID),
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
    ZmRef<OIDCReq> request = new OIDCReq{this};
    if (!randomText(m_rng, request->stateID) ||
        !randomText(m_rng, request->nonce) ||
        !randomText(m_rng, request->verifier)) {
      complete(false, String{});
      return;
    }
    request->grantID = ZuMv(grantID);
    request->config = ZuMv(config);
    uint8_t digest[Ztls::MD<>::Size];
    Ztls::MD<> md;
    md.update(ZuBSpan{request->verifier});
    md.finish(digest);
    String challenge;
    challenge.length(ZuBase64URL::enclen(sizeof(digest)));
    challenge.length(ZuBase64URL::encode(challenge.span(), digest));
    if (!challenge || m_pending.find(request->stateID)) {
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
    formField(location, first, "state", request->stateID);
    formField(location, first, "nonce", request->nonce);
    formField(location, first, "code_challenge", challenge);
    formField(location, first, "code_challenge_method", "S256");
    m_pending.add(request);
    m_scheduler->add(&request->timer, Zm::now() + ZuTime{double(m_timeout)},
      ZmScheduler::Update, [request](auto &&arm) {
        return arm([request]() { if (request->state) request->state->fail_(request); });
      }, m_sid);
    complete(true, ZuMv(location));
  }

  void finish_(String query, OIDCFinishFn complete)
  {
    if (!m_up || query.length() > m_limits.callback) {
      complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
      return;
    }
    if (!query.mutable_()) query.length(query.length());
    if (query[0] == '?') query.splice(0, 1);
    String code, stateID, error;
    unsigned seen = 0;
    formEach({query.data(), query.length()},
      [&code, &stateID, &error, &seen](ZuCSpan name, ZuCSpan value) {
        String *target;
        unsigned bit;
        if (name == "code") { target = &code; bit = 1U; }
        else if (name == "state") { target = &stateID; bit = 2U; }
        else if (name == "error") { target = &error; bit = 4U; }
        else return;
        seen |= bit;
        *target = value;
      });
    auto request = stateID ? m_pending.findVal(stateID) : ZmRef<OIDCReq>{};
    if (!request || request->consumed || !(seen & 2U) ||
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
        String plain{request->config.clientID};
        plain << ':' << request->config.clientSecret;
        String encoded;
        encoded.length(ZuBase64::enclen(plain.length()));
        encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
        if (plain.mutable_()) ZuClear(plain.data(), plain.length());
        http.authorization << "Basic " << encoded;
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
    send(ZuMv(http), [self = ZmRef<OIDCState>{this}, request](
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
    String cacheID{request->config.issuer};
    cacheID << '\n' << request->keyID;
    auto key = m_keys.findVal(cacheID);
    if (key.id) {
      verify_(request, key.publicKey);
      return;
    }
    auto send = m_http;
    send(OIDCHTTPRequest{.url = request->config.jwksEndpoint},
      [self = ZmRef<OIDCState>{this}, request](
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
    for (auto &key: keys) {
      String cacheID{request->config.issuer};
      cacheID << '\n' << key.id;
      key.id = ZuMv(cacheID);
      m_keys.del(key.id);
      m_keys.add(ZuMv(key));
    }
    String cacheID{request->config.issuer};
    cacheID << '\n' << request->keyID;
    auto key = m_keys.findVal(cacheID);
    if (!key.id) { fail_(request); return; }
    verify_(request, key.publicKey);
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
    int64_t authTime = claims.iat;
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
      send(ZuMv(http), [self = ZmRef<OIDCState>{this}, request,
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
      [self = ZmRef<OIDCState>{this}, request, authTime](
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
      m_pending.findVal(request->stateID) == request;
  }

  void fail_(ZmRef<OIDCReq> request)
  {
    if (!active_(request)) return;
    complete_(request, false, {}, {}, {}, {}, 0);
  }

  void complete_(
      ZmRef<OIDCReq> request, bool ok, Bytes grantID, User user,
      IDVec roleIDs, Evidence evidence, int64_t authTime)
  {
    m_scheduler->del(&request->timer);
    m_pending.del(request->stateID);
    request->state = nullptr;
    if (request->verifier.mutable_())
      ZuClear(request->verifier.data(), request->verifier.length());
    request->verifier.null();
    if (request->idToken.mutable_())
      ZuClear(request->idToken.data(), request->idToken.length());
    request->idToken.null();
    if (request->accessToken.mutable_())
      ZuClear(request->accessToken.data(), request->accessToken.length());
    request->accessToken.null();
    if (request->config.clientSecret.mutable_())
      ZuClear(request->config.clientSecret.data(),
        request->config.clientSecret.length());
    request->config = {};
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
  OIDCKeyHash	m_keys;
  unsigned	m_discovering = 0;
  ZmAtomic<uint32_t> m_up = 0;
};

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

bool OIDC::finish(String query, OIDCFinishFn complete)
{
  return m_state && m_state->finish(ZuMv(query), ZuMv(complete));
}

} // namespace Zum
