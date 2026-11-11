//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_oidc.hh>
#include <zlib/zumd_db.hh>
#include <zlib/zumd_identity_db.hh>
#include <zlib/zumd_provider_db.hh>
#include <zlib/zumd_saga_external_projection.hh>

#include <limits.h>
#include <string.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZfJSON.hh>
#include <zlib/ZhttpDiscovery.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmPolymorph.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmPlatform.hh>
#include <zlib/ZmPQueue.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmScheduler.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiRx.hh>
#include <zlib/ZiMultiplex.hh>
#include <zlib/ZiResolver.hh>

#include <zlib/ZhttpClient.hh>
#include <zlib/ZhttpNative.hh>
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
  (issuer,		(JSON::ID<"iss">, Required),		String),
  (subject,		(JSON::ID<"sub">, Required),		String),
  (audience,		(JSON::ID<"aud">, Required),		UDT),
  (authorizedParty, (JSON::ID<"azp">, JSON::Opt),		String),
  (nonce,		(Required),				String),
  (iat,		(Required),					Int64),
  (expires,		(JSON::ID<"exp">, Required),		Int64),
  (authTime,		(JSON::ID<"auth_time">, Required),	Int64));

struct OIDCToken { String idToken; String accessToken; };
ZfStruct(, (OIDCToken, JSON),
  (idToken,		(JSON::ID<"id_token">, Required),	String),
  (accessToken,	(JSON::ID<"access_token">, JSON::Opt),		String));
struct OIDCUserInfo { String subject; };
ZfStruct(, (OIDCUserInfo, JSON),
  (subject,		(JSON::ID<"sub">, Required),	String));
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
  (kid,		(Required),		String),
  (kty,		(Required),		String),
  (crv,		(Required),		String),
  (use,		(JSON::Opt),		String),
  (alg,		(JSON::Opt),		String),
  (x,		(Required),		String),
  (y,		(Required),		String));
ZuDerive(OIDCJWKWireArray, (ZtArray<OIDCJWK,
  ZtArrayHeapID<"Zum.OIDC.JWKs">>));
struct OIDCJWKWireVec : public OIDCJWKWireArray {
  ZuDerive_(OIDCJWKWireVec, OIDCJWKWireArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(OIDCJWKWireVec *);
};
struct OIDCJWKSResponse { OIDCJWKWireVec keys; };
ZfStruct(, (OIDCJWKSResponse, JSON),
  (keys,		(Required),	UDT));
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
  (issuer,		(Required),						String),
  (authorize,	(JSON::ID<"authorization_endpoint">, Required),			String),
  (token,		(JSON::ID<"token_endpoint">, Required),			String),
  (jwks,		(JSON::ID<"jwks_uri">, Required),			String),
  (responses,	(JSON::ID<"response_types_supported">, Required),		StringVec),
  (algorithms,	(JSON::ID<"id_token_signing_alg_values_supported">,
    Required),									StringVec),
  (methods,		(JSON::ID<"token_endpoint_auth_methods_supported">,
    Required),									StringVec),
  (userinfo,		(JSON::ID<"userinfo_endpoint">, JSON::Opt),		String));
struct OIDCEssential { bool essential = true; };
ZfStruct(, (OIDCEssential, JSON),
  (essential,	(Required),		Bool));
struct OIDCAuthTime { OIDCEssential authTime; };
ZfStruct(, (OIDCAuthTime, JSON),
  (authTime,		(JSON::ID<"auth_time">, Required),	UDT));
struct OIDCAuthorizeClaims { OIDCAuthTime idToken; };
ZfStruct(, (OIDCAuthorizeClaims, JSON),
  (idToken,		(JSON::ID<"id_token">, Required),	UDT));

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

// Map-load only the configured claims; names are selected at run time.
struct OIDCClaim {
  ZuCSpan name;
  StringVec &values;
  bool present = false;

  bool load(ZuCSpan key, const ZfJSON::AnyNode *node, unsigned limit) {
    if (!name || key != name) return true;
    if (present) return false;
    present = true;
    return stringsValue(node, limit, values);
  }
};
struct OIDCClaimMap {
  using Key = ZuCSpan;
  using Val = ZfJSON::Union<>;

  OIDCClaim roles;
  OIDCClaim eligibility;
  unsigned limit;
  bool valid = true;

  friend ZfJSON::AsMap<ZfFieldTC::UDT> ZfJSON_Fmt(OIDCClaimMap *);

  void clean() {
    roles.present = eligibility.present = false;
    roles.values.null();
    eligibility.values.null();
    valid = true;
  }
  void add(Key key, Val value) {
    if (!valid) return;
    auto node = value.p<const ZfJSON::AnyNode *>();
    valid = roles.load(key, node, limit) &&
      eligibility.load(key, node, limit);
  }
};

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
  OIDCClaimMap extra{
    {config.roles == OIDCRoles::Mapped ? ZuCSpan{config.roleClaim} : ZuCSpan{},
      next.roleValues},
    {config.eligibilityMode == EligibilityMode::ClaimValues ?
      ZuCSpan{config.eligibilityClaim} : ZuCSpan{}, next.eligibilityValues},
    limits.roleValues};
  if (config.claimSource == ClaimSource::IDToken &&
      (extra.roles.name || extra.eligibility.name)) {
    auto handler = ZfJSON::handler<OIDCClaimMap>(root);
    if (!handler.valid) return false;
    handler.load(extra);
    if (!extra.valid) return false;
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
       !extra.eligibility.present)) return false;
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

class OIDCUserLoad : public ZmNHeap<OIDCUserLoad>, public ZmObject  {
friend inline ZuStringT<"Zum.zumd.oidc.OIDCUserLoad"> ZmNHeapID(OIDCUserLoad *);

public:
  OIDCUserLoad(DB *db, String subject,
      const OIDCConfig &config, StringVec roleValues,
      StringVec eligibilityValues, int64_t now, OIDCUserFn complete,
      ZmRef<ZmPolymorph> work) :
    m_work{ZuMv(work)}, m_db{db}, m_subject{ZuMv(subject)}, m_appID{config.appID},
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
    if (!m_db || !m_subject || !m_appID || !m_providerID ||
        !m_assignmentMaxAge || m_now <= 0 || !m_complete || !m_rng.init()) {
      load_4(false);
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
        load_4(false);
        return;
    }
    load_0();
  }

private:
  void load_0()
  {
    auto identities = m_db->extIdentities;
    identities->run(0, [self = ZmRef<OIDCUserLoad>{this},
        identities]() mutable {
      identities->find<0>(0, ZuFwdTuple(self->m_providerID,
          self->m_issuer, self->m_subject), [self = ZuMv(self)](
            ZdbRowRef<ExtIdentity> row) mutable {
        if (row && !row->data().owner) {
          self->load_2(row->data().userID);
          return;
        }
        if (row) { self->load_4(false); return; }
        self->load_1();
      });
    });
  }

  void load_1()
  {
    uint64_t userID = 0;
    do {
      if (!m_rng.random({reinterpret_cast<uint8_t *>(&userID),
          sizeof(userID)})) { load_4(false); return; }
    } while (!userID);
    Bytes random;
    random.length(sizeof(ZdbSagaID), false);
    if (!m_rng.random(random)) { load_4(false); return; }
    ZdbSagaID sagaID;
    memcpy(&sagaID, random.data(), sizeof(sagaID));
    String name{"oidc:"};
    name << m_providerID << ':' << userID;
    Bytes handle;
    handle.length(32, false);
    if (!m_rng.random(handle)) { load_4(false); return; }
    ExternalProjectionArgs projection{.providerID = m_providerID,
      .issuer = m_issuer, .subject = m_subject, .userID = userID,
      .name = ZuMv(name), .handle = ZuMv(handle), .created = m_now};
    auto db = static_cast<DB *>(m_db->users->db());
    if (!sagaSubmit(db, sagaID, externalProjection(ZuMv(projection)),
        [self = ZmRef<OIDCUserLoad>{this}](bool ok) mutable {
          if (!ok) self->load_4(false);
        }, [self = ZmRef<OIDCUserLoad>{this}](bool ok) mutable {
          if (!ok) { self->load_4(false); return; }
          self->load_0();
        })) load_4(false);
  }

  void load_2(UserID userID)
  {
    auto users = m_db->users;
    users->run(0, [self = ZmRef<OIDCUserLoad>{this}, users, userID]() mutable {
      users->find<0>(0, ZuFwdTuple(userID), [
          self = ZuMv(self)](ZdbRowRef<User> row) mutable {
	if (!row || row->data().source != UserSource::External ||
	    row->data().state != State::Active || row->data().owner) {
	  self->load_4(false);
	  return;
	}
	self->m_user = row->data();
	self->load_3();
      });
    });
  }

  Evidence evidenceRecord() const
  {
    int64_t deadline = m_now > INT64_MAX - int64_t(m_assignmentMaxAge) ?
      INT64_MAX : m_now + int64_t(m_assignmentMaxAge);
    return Evidence{.appID = m_appID, .userID = m_user.id,
      .providerID = m_providerID, .roleValues = m_roleValues,
      .eligible = m_eligible, .observed = m_now, .deadline = deadline,
      .source = m_claimSource, .policyVersion = m_policyVersion,
      .created = m_now, .updated = m_now};
  }

  void load_3()
  {
    auto evidence = m_db->evidence;
    evidence->run(0, [self = ZmRef<OIDCUserLoad>{this}, evidence]() mutable {
      auto key = ZuFwdTuple(self->m_appID, self->m_user.id, self->m_providerID);
      evidence->findUpd<0, ZuSeq<1>>(0, ZuMv(key), [self = ZuMv(self), evidence](
          ZdbRow<Evidence> *row) mutable {
	if (row) {
	  if (row->data().owner) { self->load_4(false); return; }
	  auto next = self->evidenceRecord();
	  next.version = row->data().version + 1;
	  next.created = row->data().created;
	  // No new provider refresh token was returned; preserve ciphertext.
	  next.protectedRefreshToken = row->data().protectedRefreshToken;
	  row->data() = ZuMv(next);
	  self->m_evidence = row->data();
	  self->load_4(row->commit() && self->m_eligible);
	  return;
	}
	ZdbRowRef<Evidence> created = new ZdbRow<Evidence>{
	  evidence, ZdbShard{0}};
	evidence->insert(ZuMv(created), [self = ZuMv(self)](
	    ZdbRow<Evidence> *row) mutable {
	  if (!row) { self->load_4(false); return; }
	  new (row->ptr()) Evidence{self->evidenceRecord()};
	  self->m_evidence = row->data();
	  self->load_4(row->commit() && self->m_eligible);
	});
      });
    });
  }

  void load_4(bool ok)
  {
    if (m_done) return;
    m_done = true;
    auto complete = ZuMv(m_complete);
    complete(ok, ok ? ZuMv(m_user) : User{},
      ok ? ZuMv(m_roles) : IDVec{}, ok ? ZuMv(m_evidence) : Evidence{});
  }

  // The last member destroyed retains the whole DB helper graph, including
  // saga callbacks that can outlive an early application failure result.
  ZmRef<ZmPolymorph>	m_work;
  DB		*m_db = nullptr;
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
    DB *db, String subject, const OIDCConfig &config,
    StringVec roleValues, StringVec eligibilityValues,
    int64_t now, OIDCUserFn complete)
{
  ZmRef<OIDCUserLoad> load = new OIDCUserLoad{db, ZuMv(subject),
    config, ZuMv(roleValues), ZuMv(eligibilityValues), now, ZuMv(complete), {}};
  load->start();
}

using OIDCKeyIDs = ZmRBTree<String,
  ZmRBTreeUnique<true, ZmRBTreeHeapID<"Zum.OIDC.KeyIDs">>>;

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

// The hash owns each request. Shadow links give cancellation a bounded
// traversal without retaining an iterator across application callbacks.
ZmListDerive(OIDCReqList, OIDCReqData,
  ZmListNode<OIDCReqData, ZmListShadow<>>);
ZmHashDerive(OIDCReqHash, OIDCReqList::Node,
  (ZmHashNode<OIDCReqList::Node,
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
  OIDCClaimMap extra{
    {config.roles == OIDCRoles::Mapped ? ZuCSpan{config.roleClaim} : ZuCSpan{},
      nextRoles},
    {config.eligibilityMode == EligibilityMode::ClaimValues ?
      ZuCSpan{config.eligibilityClaim} : ZuCSpan{}, nextEligibility},
    limits.roleValues};
  if (extra.roles.name || extra.eligibility.name) {
    auto handler = ZfJSON::handler<OIDCClaimMap>(root);
    if (!handler.valid) return false;
    handler.load(extra);
  }
  if (wire.subject != subject || !extra.valid ||
      (config.roles == OIDCRoles::Mapped && !extra.roles.present) ||
      (config.eligibilityMode == EligibilityMode::ClaimValues &&
       !extra.eligibility.present)) return false;
  roleValues = ZuMv(nextRoles);
  eligibilityValues = ZuMv(nextEligibility);
  return true;
}

static bool jwksResponse(
    String &json, const OIDCLimits &limits, ZuCSpan requested,
    Bytes &publicKey)
{
  if (!json || json.length() > limits.response) return false;
  if (!json.mutable_()) json.length(json.length());
  ZuPtr<ZfJSON::AnyNode> root;
  OIDCJWKSResponse response;
  if (!jsonLoad(json, root, response) || !response.keys ||
      response.keys.length() > limits.keys) return false;
  OIDCKeyIDs ids;
  Bytes selected;
  for (auto &wire: response.keys) {
    if (!wire.kid || wire.kty != "EC" || wire.crv != "P-256" ||
        !wire.x || !wire.y || (wire.use && wire.use != "sig") ||
        (wire.alg && wire.alg != "ES256")) continue;
    Bytes key;
    key.length(Ztls::COSE::ES256::PublicKeySize, false);
    key[0] = 4;
    if (ZuBase64URL::decode({key.data() + 1,
          Ztls::COSE::ES256::CoordinateSize}, ZuBSpan{wire.x}) !=
          Ztls::COSE::ES256::CoordinateSize ||
        ZuBase64URL::decode({key.data() + 1 +
          Ztls::COSE::ES256::CoordinateSize, Ztls::COSE::ES256::CoordinateSize},
          ZuBSpan{wire.y}) != Ztls::COSE::ES256::CoordinateSize ||
        !Ztls::COSE::ES256::validPK(key))
      continue;
    if (ids.find(wire.kid)) return false;
    if (wire.kid == requested) selected = ZuMv(key);
    ids.add(ZuMv(wire.kid));
  }
  if (!selected) return false;
  publicKey = ZuMv(selected);
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

namespace OIDCCredit { enum { Closed = 1LL<<62, Count = Closed - 1 }; }
namespace OIDCWorkLimit { enum { Batch = 64 }; }

// One credit covers an accepted call and every HTTP/DB descendant. Foreign
// callbacks consume their capture when called; retaining a fired callback must
// not retain the owner. Variable data crosses shards in this handle.
struct OIDCWorkReturn {
  OIDCState *owner;
  ~OIDCWorkReturn();
};

struct OIDCBeginWork : public ZmNHeap<OIDCBeginWork>, public ZmPolymorph, OIDCWorkReturn {
friend inline ZuStringT<"Zum.OIDC.Begin"> ZmNHeapID(OIDCBeginWork *);

  Bytes grantID;
  OIDCConfig config;
  OIDCBeginFn begin;
  String body;
  unsigned status = 0;

  explicit OIDCBeginWork(OIDCState *owner_) : OIDCWorkReturn{owner_} { }
  ~OIDCBeginWork() {
    if (config.clientSecret.mutable_()) ZuClear(config.clientSecret);
    config.clientSecret.null();
  }
};

struct OIDCFinishWork : public ZmNHeap<OIDCFinishWork>, public ZmPolymorph, OIDCWorkReturn {
friend inline ZuStringT<"Zum.OIDC.Finish"> ZmNHeapID(OIDCFinishWork *);

  OIDCFinishFn finish;
  AppID appID = 0;
  String query;
  ZmRef<OIDCReq> request;
  String body;
  String subject;
  User user;
  IDVec roleIDs;
  Evidence evidence;
  int64_t authTime = 0;
  int64_t now = 0;
  unsigned status = 0;
  bool ok = false;

  explicit OIDCFinishWork(OIDCState *owner_) : OIDCWorkReturn{owner_} { }
};

class OIDCState : public ZmNHeap<OIDCState>, public ZmObject,
    private ZmEngine<OIDCState> {
friend inline ZuStringT<"Zum.zumd.oidc.OIDCState"> ZmNHeapID(OIDCState *);

  using Engine = ZmEngine<OIDCState>;
  using BeginWork = ZmRef<OIDCBeginWork>;
  using FinishWork = ZmRef<OIDCFinishWork>;
  friend Engine;
  friend OIDCWorkReturn;
public:
  using Engine::start;
  using Engine::stop;
  using Engine::state;

  ~OIDCState() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!m_scheduler);
  }

  bool init(
      ZmScheduler *scheduler, unsigned sid, DB *db,
      OIDCLimits limits, unsigned pendingLimit,
      uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
  {
    if (!scheduler || !sid || sid > scheduler->params().nThreads() ||
        !db || !limits.response ||
        !limits.keys || !pendingLimit || !timeout || !clock || !http ||
        !m_rng.init())
      return false;
    m_scheduler = scheduler;
    m_sid = sid;
    m_db = db;
    m_limits = limits;
    m_pendingLimit = pendingLimit;
    m_timeout = timeout;
    m_clock = ZuMv(clock);
    m_http = ZuMv(http);
    return true;
  }

  void final() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!(m_credit.load_() & OIDCCredit::Count));
    ZmAssert_(!m_pending.count_() && m_pendingAll.empty_() && !m_discovering);
    ZmAssert_(!m_draining && !m_drainPosted);
    m_http = {};
    m_clock = {};
    m_db = nullptr;
    m_scheduler = nullptr;
  }

  bool begin(Bytes grantID, OIDCConfig config, OIDCBeginFn complete) {
    if (!grantID || !oidcConfigValid(config) || !complete || !admit_()) {
      clearSecret(config);
      return false;
    }
    BeginWork work = new OIDCBeginWork{this};
    work->grantID = ZuMv(grantID);
    work->config = ZuMv(config);
    work->begin = ZuMv(complete);
    invoke_([work = ZuMv(work)]() mutable {
      auto self = work->owner;
      self->begin_0(ZuMv(work));
    });
    return true;
  }

  bool finish(AppID appID, String query, OIDCFinishFn complete) {
    if (!appID || !query || !complete || !admit_()) return false;
    FinishWork work = new OIDCFinishWork{this};
    work->appID = appID;
    work->query = ZuMv(query);
    work->finish = ZuMv(complete);
    invoke_([work = ZuMv(work)]() mutable {
      auto self = work->owner;
      self->finish_(ZuMv(work));
    });
    return true;
  }

private:
  template <typename L>
  void invoke_(L &&l)
  {
    m_scheduler->run(ZuFwd<L>(l), m_sid);
  }

  bool active_() const { return Engine::state() == ZmEngineState::Running; }

  bool admit_() {
    if (!active_()) return false;
    uint64_t count;
    do {
      count = m_credit.load_();
      if (count & OIDCCredit::Closed) return false;
    } while (m_credit.cmpXch(count + 1, count) != count);
    return true;
  }

  void start_() {
    if (!m_scheduler) { Engine::started(false); return; }
    invoke_([this]() {
      ZmAssert_(m_credit.load_() == OIDCCredit::Closed);
      ZmAssert_(!m_pending.count_() && m_pendingAll.empty_() && !m_discovering);
      ZmAssert_(!m_draining && !m_drainPosted);
      m_credit = 0;
      Engine::started(true);
    });
  }

  void stop_() {
    m_credit |= OIDCCredit::Closed;
    invoke_([this]() { cancel_(); });
  }

  void cancel_() {
    for (unsigned n = 0; n < OIDCWorkLimit::Batch; ++n) {
      ZmRef<OIDCReq> request = static_cast<OIDCReq *>(m_pendingAll.headPtr());
      if (!request) {
        m_draining = true;
        stop_0();
        return;
      }
      complete_(ZuMv(request), false, {}, {}, {}, {}, 0);
    }
    invoke_([this]() { cancel_(); });
  }

  void workDone_() {
    invoke_([this]() {
      --m_credit;
      stop_0();
    });
  }

  void stop_0() {
    if (!m_draining || m_drainPosted || (m_credit.load_() & OIDCCredit::Count)) return;
    m_drainPosted = true;
    // Retire cancelled timer callbacks on their own scheduler shard before
    // exposing Stopped. A work destructor also reaches this owner barrier.
    invoke_([this]() {
      m_drainPosted = false;
      ZmAssert_(!m_pending.count_() && m_pendingAll.empty_() && !m_discovering);
      m_draining = false;
      Engine::stopped(true);
    });
  }

  template <auto Method, typename Work>
  void send_(Work work, OIDCHTTPRequest request) {
    m_http(ZuMv(request), [credit = ZuMv(work)](unsigned status, String body) mutable {
      auto work = ZuMv(credit);
      auto self = work->owner;
      work->status = status;
      work->body = ZuMv(body);
      self->invoke_([work = ZuMv(work)]() mutable {
        auto self = work->owner;
        (self->*Method)(ZuMv(work));
      });
    });
  }

  static void clearSecret(OIDCConfig &config)
  {
    if (config.clientSecret.mutable_())
      ZuClear(config.clientSecret);
    config.clientSecret.null();
  }

  void begin_0(BeginWork work) {
    auto &config = work->config;
    if (!active_() || m_pending.count_() + m_discovering >= m_pendingLimit) {
      clearSecret(config);
      work->begin(false, String{});
      return;
    }
    if (config.authorizeEndpoint) {
      begin_2(ZuMv(work->grantID), ZuMv(config), ZuMv(work->begin));
      return;
    }
    ++m_discovering;
    // OIDC Discovery appends to the issuer path, unlike RFC 8414's rule.
    String url{config.issuer};
    if (url[url.length() - 1] == '/') url.length(url.length() - 1);
    url << "/.well-known/openid-configuration";
    send_<&OIDCState::begin_1>(ZuMv(work), OIDCHTTPRequest{.url = ZuMv(url)});
  }

  void begin_1(BeginWork work) {
    --m_discovering;
    if (!active_() || work->status != 200 ||
        !discoveryResponse(work->body, m_limits, work->config)) {
      clearSecret(work->config);
      work->begin(false, String{});
      return;
    }
    begin_2(ZuMv(work->grantID), ZuMv(work->config), ZuMv(work->begin));
  }

  void begin_2(Bytes grantID, OIDCConfig config, OIDCBeginFn complete)
  {
    if (!active_() || m_pending.count_() >= m_pendingLimit) {
      clearSecret(config);
      complete(false, String{});
      return;
    }
    ZmRef<OIDCReq> request = new OIDCReq{static_cast<OIDCState *>(this)};
    if (!randomText(m_rng, request->stateID) ||
        !randomText(m_rng, request->nonce) ||
        !randomText(m_rng, request->verifier)) {
      clearSecret(config);
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
      clearRequest(*request);
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
    m_pendingAll.pushNode(request.ptr());
    m_scheduler->add(&request->timer, Zm::now() + ZuTime{double(m_timeout)},
      ZmScheduler::Update, [this, request](auto &&arm) {
        return arm([this, request]() mutable {
          if (request->state) fail_(ZuMv(request));
        });
      }, m_sid);
    complete(true, ZuMv(location));
  }

  void finish_(FinishWork work)
  {
    auto appID = work->appID;
    auto query = ZuMv(work->query);
    auto complete = ZuMv(work->finish);
    if (!active_() || query.length() > m_limits.callback) {
      complete(false, Bytes{}, User{}, IDVec{}, Evidence{}, 0);
      return;
    }
    if (!query.mutable_()) query.length(query.length());
    if (query[0] == '?') query.splice(0, 1);
    String code, stateID, error;
    unsigned seen = 0;
    bool valid = true;
    constexpr auto matcher = ZuMatcher<OIDCCallbackFields>();
    valid = formEach(query,
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
        SecretString plain;
        ZfURI::PathQuote::quote(plain, request->config.clientID);
        plain << ':';
        ZfURI::PathQuote::quote(plain, request->config.clientSecret);
        SecretString encoded;
        encoded.length(ZuBase64::enclen(plain.length()));
        encoded.length(ZuBase64::encode(encoded.span(), ZuBSpan{plain}));
        plain.null();
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
    work->request = ZuMv(request);
    send_<&OIDCState::token_>(ZuMv(work), ZuMv(http));
  }

  void token_(FinishWork work)
  {
    auto request = work->request;
    if (!active_(request) || work->status != 200 ||
        !tokenResponse(work->body, m_limits,
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
    OIDCHTTPRequest http{.url = request->config.jwksEndpoint};
    send_<&OIDCState::keys_>(ZuMv(work), ZuMv(http));
  }

  void keys_(FinishWork work)
  {
    auto request = work->request;
    Bytes publicKey;
    if (!active_(request) || work->status != 200 ||
        !jwksResponse(work->body, m_limits, request->keyID, publicKey)) {
      fail_(request);
      return;
    }
    verify_(ZuMv(work), publicKey);
  }

  void verify_(FinishWork work, ZuBSpan publicKey)
  {
    auto request = work->request;
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
        ZuClear(request->accessToken);
      request->accessToken.null();
      work->subject = ZuMv(claims.subject);
      work->authTime = authTime;
      work->now = now;
      send_<&OIDCState::userinfo_>(ZuMv(work), ZuMv(http));
      return;
    }
    load_(ZuMv(work), ZuMv(claims.subject), ZuMv(claims.roleValues),
      ZuMv(claims.eligibilityValues), authTime, now);
  }

  void userinfo_(FinishWork work) {
    auto request = work->request;
    StringVec roleValues, eligibilityValues;
    if (!active_(request) || work->status != 200 ||
        !userinfoResponse(work->body, m_limits, request->config, work->subject,
          roleValues, eligibilityValues)) {
      fail_(request);
      return;
    }
    auto subject = ZuMv(work->subject);
    auto authTime = work->authTime, now = work->now;
    load_(ZuMv(work), ZuMv(subject), ZuMv(roleValues),
      ZuMv(eligibilityValues), authTime, now);
  }

  void load_(FinishWork work, String subject, StringVec roleValues,
      StringVec eligibilityValues, int64_t authTime, int64_t now) {
    work->authTime = authTime;
    auto &config = work->request->config;
    auto credit = work;
    ZmRef<OIDCUserLoad> load = new OIDCUserLoad{m_db, ZuMv(subject), config,
      ZuMv(roleValues), ZuMv(eligibilityValues), now,
      [credit = ZuMv(work)](bool ok, User user, IDVec roleIDs, Evidence evidence) mutable {
        auto work = ZuMv(credit);
        auto self = work->owner;
        work->ok = ok;
        work->user = ZuMv(user);
        work->roleIDs = ZuMv(roleIDs);
        work->evidence = ZuMv(evidence);
        self->invoke_([work = ZuMv(work)]() mutable {
          auto self = work->owner;
          auto request = work->request;
          if (!work->ok || !self->active_(request)) { self->fail_(request); return; }
          auto grantID = request->grantID;
          self->complete_(request, true, ZuMv(grantID), ZuMv(work->user),
            ZuMv(work->roleIDs), ZuMv(work->evidence), work->authTime);
        });
      }, ZuMv(credit)};
    load->start();
  }

  bool active_(const ZmRef<OIDCReq> &request)
  {
    return active_() && request && request->state == this &&
      m_pending.find(request->stateID) == request;
  }

  void fail_(ZmRef<OIDCReq> request)
  {
    if (!request || request->state != this) return;
    complete_(request, false, {}, {}, {}, {}, 0);
  }

  static void clearRequest(OIDCReq &request)
  {
    if (request.verifier.mutable_())
      ZuClear(request.verifier);
    request.verifier.null();
    if (request.idToken.mutable_())
      ZuClear(request.idToken);
    request.idToken.null();
    if (request.accessToken.mutable_())
      ZuClear(request.accessToken);
    request.accessToken.null();
    clearSecret(request.config);
    request.config = {};
  }

  void complete_(
      ZmRef<OIDCReq> request, bool ok, Bytes grantID, User user,
      IDVec roleIDs, Evidence evidence, int64_t authTime)
  {
    m_scheduler->del(&request->timer);
    m_pendingAll.delNode(request.ptr());
    m_pending.del(request->stateID);
    request->state = nullptr;
    clearRequest(*request);
    auto complete = ZuMv(request->complete);
    if (complete)
      complete(ok, ZuMv(grantID), ZuMv(user), ZuMv(roleIDs),
        ZuMv(evidence), authTime);
  }

  ZmScheduler	*m_scheduler = nullptr;
  unsigned	m_sid = 0;
  DB	*m_db = nullptr;
  OIDCLimits	m_limits;
  unsigned	m_pendingLimit = 0;
  uint64_t	m_timeout = 0;
  OIDCHTTPFn	m_http;
  OIDCClockFn	m_clock;
  Ztls::Random	m_rng;
  OIDCReqHash	m_pending;
  OIDCReqList	m_pendingAll;
  unsigned	m_discovering = 0;
  bool m_draining = false;
  bool m_drainPosted = false;

  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t> m_credit = OIDCCredit::Closed;
};

OIDCWorkReturn::~OIDCWorkReturn() {
  // Base destruction follows every payload/callback member destructor. Its
  // owner post can therefore safely release the last graph credit on any
  // shard, even if that post runs before this destructor itself returns.
  owner->workDone_();
}

OIDC::OIDC() = default;
OIDC::~OIDC() { final(); }

bool OIDC::init(
    ZmScheduler *scheduler, unsigned sid, DB *db,
    OIDCLimits limits, unsigned pendingLimit,
    uint64_t timeout, OIDCClockFn clock, OIDCHTTPFn http)
{
  if (m_state) return false;
  ZmRef<OIDCState> state = new OIDCState;
  if (!state->init(scheduler, sid, db, limits,
      pendingLimit, timeout, ZuMv(clock), ZuMv(http))) return false;
  m_state = ZuMv(state);
  return true;
}

bool OIDC::start() { return m_state && m_state->start(); }
void OIDC::start(OIDCCtrlFn complete) {
  if (!m_state) { complete(false); return; }
  m_state->start(ZuMv(complete));
}
bool OIDC::stop() { return !m_state || m_state->stop(); }
void OIDC::stop(OIDCCtrlFn complete) {
  if (!m_state) { complete(true); return; }
  m_state->stop(ZuMv(complete));
}
int OIDC::state() const {
  return m_state ? m_state->state() : ZmEngineState::Stopped;
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

template <unsigned Status_>
struct ResponseData : public ZmNHeap<ResponseData<Status_>>, public ZmObject  {
friend inline ZuStringT<"Zum.zumd.oidc.ResponseData"> ZmNHeapID(ResponseData *);

  enum { Status = Status_ };
  String body;
  ResponseData &operator =(ZuSpan<uint8_t> data) {
    body = data;
    return *this;
  }
};

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

struct Call : public ZmNHeap<Call>, public ZmObject  {
friend inline ZuStringT<"Zum.zumd.oidc.Call"> ZmNHeapID(Call *);

  mutable String	contentType;
  mutable String	authorization;
  mutable String	target;
  mutable String	body;
  mutable OIDCHTTPDoneFn complete;
  mutable ZmAtomic<unsigned> done = 0;

  void finish(unsigned status, String value) const {
    if (done.cmpXch(1, 0)) return;
    if (authorization.mutable_())
      ZuClear(authorization);
    authorization.null();
    if (body.mutable_()) ZuClear(body);
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
class Pool;
ZmPQueueDerive(ReqBuilderQ, ReqBuilder_,
  ZmPQueueOverlap<false, ZmPQueueNode<ReqBuilder_,
    ZmPQueueHeapID<"Zum.OIDC.HTTP.Request">>>);
using ReqBuilder = ReqBuilderQ::Node;
ZuDerive(TxQ, (ZmPQTx<Pool, ReqBuilderQ, ZmPQTxOrdered<false>>));

class Pool : public ZmNHeap<Pool>, public Zhttp::Pool<OIDCClient, TxQ, ResParser,
  Zhttp::Profiles<Zhttp::H1TLS, Zhttp::H2TLS>> {
friend inline ZuStringT<"Zum.OIDC.HTTP.Pool"> ZmNHeapID(Pool *);

  using Base = Zhttp::Pool<OIDCClient, TxQ, ResParser, Zhttp::Profiles<Zhttp::H1TLS,
    Zhttp::H2TLS>>;
public:
  Pool(OIDCClient *client) : Base{client} { }
  ReqBuilderQ *txQueue() { return &m_requests; }
  void archive_(ReqBuilder *) { }
  ZmRef<ReqBuilder> retrieve_(ReqBuilderQ::Key, ReqBuilderQ::Key) {
    return {};
  }
private:
  ReqBuilderQ m_requests;
};

class OIDCClient : public ZmNHeap<OIDCClient>, public ZmObject,
    public Zhttp::Client<OIDCClient, Pool> {
friend inline ZuStringT<"Zum.zumd.oidc.Client"> ZmNHeapID(OIDCClient *);

public:
  using Base = Zhttp::Client<OIDCClient, Pool>;

  bool init(ZiMultiplex *mx, const Zhttp::URLView &url, ZuCSpan caPath,
      const Zhttp::TCPConfig &tcp, const Zhttp::TLSConfig &tlsPolicy) {
    m_host = url.host;
    m_port = url.port;
    m_ipv6Literal = url.ipv6Literal;
    auto config = Zhttp::Config().links(2).concurrency(Concurrency)
      .linkMax(Concurrency).requestTimeout(RequestTimeout)
      .maxRetries(1).retainedBodyMax(ResponseMax)
      .protocol(Zhttp::ProtoPolicy::DisableH3)
      .secure(true).tcp(true).tls(true).quic(false);
    if (!Base::init(Zhttp::HubConfig{mx, "rx", "tx"}, 1,
        config, tcp, Zhttp::H2Config{}.tls([&caPath, &tlsPolicy](auto &tls) {
          tls = tlsPolicy;
          tls.caPath(caPath);
        }),
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

struct Entry {
  ZmRef<OIDCClient> client;
  unsigned pending = 0;
  bool retiring = false;
};
ZuDerive(Clients,
  (ZtArray<Entry, ZtArrayHeapID<"Zum.OIDC.HTTP.Clients">>));

} // namespace OIDCHTTP_

// Credits include requests between admission, origin eviction, native startup,
// native response and the final owner callback. Closed admission and credits
// share a word so a reserved-but-not-yet-posted request cannot escape stop.
namespace OIDCHTTPCredit { enum { Closed = 1LL<<62, Count = Closed - 1 }; }
// A turn releases/stops at most this many origin clients, independently of
// the configured origin capacity.
namespace OIDCHTTPWork { enum { Batch = 64 }; }

struct OIDCHTTPAction : public ZmNHeap<OIDCHTTPAction>, public ZmObject {
friend inline ZuStringT<"Zum.OIDC.HTTP.Action"> ZmNHeapID(OIDCHTTPAction *);

  OIDCHTTPRequest request;
  OIDCHTTPDoneFn complete;
  String body;
  unsigned status = 0;

  OIDCHTTPAction(OIDCHTTPRequest request_, OIDCHTTPDoneFn complete_) :
    request{ZuMv(request_)}, complete{ZuMv(complete_)} { }
};

class OIDCHTTPState : public ZmNHeap<OIDCHTTPState>, public ZmObject,
    private ZmEngine<OIDCHTTPState> {
friend inline ZuStringT<"Zum.zumd.oidc.OIDCHTTPState"> ZmNHeapID(OIDCHTTPState *);
  using Engine = ZmEngine<OIDCHTTPState>;
  using Action = ZmRef<OIDCHTTPAction>;
  friend Engine;
public:
  using Engine::start;
  using Engine::stop;
  using Engine::state;

  ~OIDCHTTPState() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!m_mx && !m_clients.length());
  }

  bool init(ZiMultiplex *mx, unsigned sid, unsigned origins, ZuCSpan caPath) {
    if (!mx || m_mx || !sid || !origins || sid > mx->params().nThreads() ||
        sid == mx->rxThread() || sid == mx->txThread()) return false;
    m_mx = mx;
    m_sid = sid;
    m_origins = origins;
    m_caPath = caPath;
    using TCP = Zhttp::NativeTraits<Zhttp::H1TCP>::Transport;
    using TLS = Zhttp::NativeTraits<Zhttp::H1TLS>::Transport;
    m_tcp.rxAllocator(TCP::registerRx(mx))
      .txAllocator(TCP::registerTx(mx));
    m_tls.rxAllocator(TLS::registerRx(mx))
      .txAllocator(TLS::registerTx(mx))
      .txPacketAllocator(TLS::registerTxPacket(mx))
      .socketRxAllocator(mx->allocator<Zi::IOBufType::RxTCP, ZiIOBuf,
        Ztls_::RxPacketCapacity>());
    m_resolverOwned = !ZiResolver::instance()->initialized();
    if (m_resolverOwned)
      ZiResolver::init(ZiResolverParams{}.timeoutMS(2000).tries(2));
    return true;
  }

  void send(OIDCHTTPRequest request, OIDCHTTPDoneFn complete) {
    if (!complete) return;
    if (Engine::state() != ZmEngineState::Running || !admit_()) {
      complete(0, String{});
      return;
    }
    Action action = new OIDCHTTPAction{ZuMv(request), ZuMv(complete)};
    post_([this, action = ZuMv(action)]() mutable { send_(ZuMv(action)); });
  }

  void final() {
    ZmAssert_(Engine::state() == ZmEngineState::Stopped);
    ZmAssert_(!(m_credit.load_() & OIDCHTTPCredit::Count));
    ZmAssert_(!m_clients.length() && !m_children && !m_draining && !m_drainPosted);
    m_clients.init();
    if (m_resolverOwned) {
      ZiResolver::final();
      m_resolverOwned = false;
    }
    m_caPath.null();
    m_mx = nullptr;
  }

private:
  template <typename L>
  void post_(L &&l) { m_mx->run(ZuFwd<L>(l), m_sid); }

  bool active_() const { return Engine::state() == ZmEngineState::Running; }

  bool admit_() {
    uint64_t count;
    do {
      count = m_credit.load_();
      if (count & OIDCHTTPCredit::Closed) return false;
    } while (m_credit.cmpXch(count + 1, count) != count);
    return true;
  }

  void start_() {
    if (!m_mx) { Engine::started(false); return; }
    if (m_resolverOwned) {
      ZiResolver::start([this](bool ok) {
        if (!ok) { Engine::started(false); return; }
        post_([this]() { start_0(); });
      });
      return;
    }
    if (ZiResolver::instance()->state() != ZmEngineState::Running) {
      Engine::started(false);
      return;
    }
    post_([this]() { start_0(); });
  }

  void start_0() {
    ZmAssert_(m_credit.load_() == OIDCHTTPCredit::Closed);
    ZmAssert_(!m_clients.length() && !m_children && !m_draining && !m_drainPosted);
    m_credit = 0;
    Engine::started(true);
  }

  void stop_() {
    m_credit |= OIDCHTTPCredit::Closed;
    post_([this]() {
      m_stopOK = true;
      m_children = m_clients.length();
      m_release = 0;
      m_draining = true;
      stop_0(0);
    });
  }

  void stop_0(unsigned offset) {
    unsigned end = offset + OIDCHTTPWork::Batch;
    unsigned count = m_clients.length();
    if (end > count) end = count;
    while (offset < end) {
      auto &entry = m_clients[offset++];
      if (!entry.client) { stop_1(); continue; }
      // A retirement already in progress joins the same native engine stop.
      // Its request keeps its own admission credit until its owner callback.
      entry.client->stop([this](bool ok) {
        post_([this, ok]() {
          m_stopOK &= ok;
          stop_1();
        });
      });
    }
    if (end < count) {
      post_([this, end]() { stop_0(end); });
      return;
    }
    stop_2();
  }

  void stop_1() {
    --m_children;
    stop_2();
  }

  void finish_(Action action, unsigned status = 0, String body = {}) {
    auto complete = ZuMv(action->complete);
    complete(status, ZuMv(body));
    complete = {};
    action = nullptr;
    --m_credit;
    stop_2();
  }

  void stop_2() {
    if (!m_draining || m_drainPosted || m_children ||
        (m_credit.load_() & OIDCHTTPCredit::Count)) return;
    m_drainPosted = true;
    post_([this]() { stop_3(); });
  }

  void stop_3() {
    m_drainPosted = false;
    unsigned end = m_release + OIDCHTTPWork::Batch;
    unsigned count = m_clients.length();
    if (end > count) end = count;
    while (m_release < end) {
      auto &entry = m_clients[m_release++];
      ZmAssert_(!entry.pending && !entry.retiring);
      if (entry.client) entry.client->final();
      entry.client = nullptr;
    }
    if (end < count) { stop_2(); return; }
    m_clients.length(0);
    m_release = 0;
    m_draining = false;
    if (m_resolverOwned) {
      ZiResolver::stop([this](bool ok) {
        post_([this, ok]() { Engine::stopped(m_stopOK && ok); });
      });
      return;
    }
    Engine::stopped(m_stopOK);
  }

  void send_(Action action) {
    if (!active_() || !action->request.url) { finish_(ZuMv(action)); return; }
    Zhttp::URL parsed{action->request.url};
    auto url = parsed.url();
    if (!parsed.ok() || url.scheme != Zhttp::Scheme::https || !url.host ||
        url.hasFragment) { finish_(ZuMv(action)); return; }
    unsigned count = m_clients.length(), idle = count;
    for (unsigned i = 0; i < count; ++i) {
      const auto &entry = m_clients[i];
      if (entry.retiring) continue;
      if (entry.client && entry.client->origin(url)) {
        dispatch_(i, ZuMv(action), url);
        return;
      }
      if (!entry.pending && idle == count) idle = i;
    }
    if (count < m_origins) {
      m_clients.push(OIDCHTTP_::Entry{});
      dispatch_(count, ZuMv(action), url);
      return;
    }
    if (idle == count) { finish_(ZuMv(action), 503); return; }
    auto &entry = m_clients[idle];
    if (!entry.client) { dispatch_(idle, ZuMv(action), url); return; }
    entry.retiring = true;
    entry.client->stop([this, idle, action = ZuMv(action)](bool ok) mutable {
      post_([this, idle, ok, action = ZuMv(action)]() mutable {
        auto &entry = m_clients[idle];
        entry.retiring = false;
        if (!active_()) { finish_(ZuMv(action)); return; }
        entry.client->final();
        entry.client = nullptr;
        if (!ok) { finish_(ZuMv(action)); return; }
        Zhttp::URL parsed{action->request.url};
        dispatch_(idle, ZuMv(action), parsed.url());
      });
    });
  }

  void dispatch_(unsigned slot, Action action, const Zhttp::URLView &url) {
    auto &entry = m_clients[slot];
    if (!entry.client) {
      ZmRef<OIDCHTTP_::OIDCClient> candidate = new OIDCHTTP_::OIDCClient{};
      if (!candidate->init(m_mx, url, m_caPath, m_tcp, m_tls)) {
        candidate->final();
        finish_(ZuMv(action));
        return;
      }
      entry.client = ZuMv(candidate);
    }
    ++entry.pending;
    entry.client->start([this, slot, action = ZuMv(action)](bool ok) mutable {
      post_([this, slot, ok, action = ZuMv(action)]() mutable {
        if (!ok || !active_()) {
          --m_clients[slot].pending;
          finish_(ZuMv(action));
          return;
        }
        Zhttp::URL parsed{action->request.url};
        auto request = ZuMv(action->request);
        m_clients[slot].client->send(ZuMv(request),
          [this, slot, action = ZuMv(action)](unsigned status, String body) mutable {
            // Exclusive handoff: only this response producer writes these
            // fields until the action reaches its owner continuation.
            action->status = status;
            action->body = ZuMv(body);
            post_([this, slot, action = ZuMv(action)]() mutable {
              --m_clients[slot].pending;
              unsigned status = action->status;
              auto body = ZuMv(action->body);
              finish_(ZuMv(action), status, ZuMv(body));
            });
          }, parsed.url());
      });
    });
  }

  Zhttp::TCPConfig m_tcp;
  Zhttp::TLSConfig m_tls;
  ZiMultiplex *m_mx = nullptr;
  String m_caPath;
  unsigned m_sid = 0;
  unsigned m_origins = 0;
  bool m_resolverOwned = false;

  alignas(Zm::CacheLineSize)
  ZmAtomic<uint64_t> m_credit = OIDCHTTPCredit::Closed;
  alignas(Zm::CacheLineSize)
  OIDCHTTP_::Clients m_clients;
  unsigned m_children = 0;
  unsigned m_release = 0;
  bool m_stopOK = true;
  bool m_draining = false;
  bool m_drainPosted = false;
};

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

bool OIDCHTTP::start()
{
  return m_state && m_state->start();
}

void OIDCHTTP::start(OIDCCtrlFn complete)
{
  if (!m_state) { complete(false); return; }
  m_state->start(ZuMv(complete));
}

bool OIDCHTTP::stop()
{
  return !m_state || m_state->stop();
}

void OIDCHTTP::stop(OIDCCtrlFn complete)
{
  if (!m_state) { complete(true); return; }
  m_state->stop(ZuMv(complete));
}

int OIDCHTTP::state() const
{
  return m_state ? m_state->state() : ZmEngineState::Stopped;
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
