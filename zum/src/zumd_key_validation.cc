//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_key_db.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuMatcher.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsPK.hh>
#include <zlib/ZtlsSec.hh>
#include <zlib/ZfJSON.hh>

namespace Zum {

struct SignKeyJWK {
  String kty;
  String crv;
  String kid;
  String x;
  String y;
  String alg;
  String use;
  String d;
};
ZfStruct(, (SignKeyJWK, JSON),
  (kty,		(Required),		String),
  (crv,		(Required),		String),
  (kid,		(Required),		String),
  (x,		(Required),		String),
  (y,		(Required),		String),
  (alg,		(JSON::Opt),		String),
  (use,		(JSON::Opt),		String),
  (d,		(JSON::Opt),		String));

bool signKeyPublic(const SignKey &key, Bytes &output)
{
  // ZfJSON parses in place; the saved public JWK must remain unchanged.
  String json{key.publicJwk};
  if (!json.mutable_()) json.length(json.length());
  bool valid = [&json, &key, &output]() {
    auto parsed = ZfJSON::scan({json.data(), json.length()});
    if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
	!parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
    auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
    if (roots.length() != 1 || !roots[0]->has<ZfJSON::AnyNode::Object>())
      return false;
    auto handler = ZfJSON::handler<SignKeyJWK>(roots[0]);
    if (!handler.valid) return false;
    auto jwk = handler.ctor();
    if (jwk.d || jwk.kty != "EC" || jwk.crv != "P-256" ||
        jwk.kid != key.id || !jwk.x || !jwk.y ||
        (jwk.alg && jwk.alg != "ES256") ||
        (jwk.use && jwk.use != "sig")) return false;
    // SEC1 uncompressed P-256 point, fixed by the supported ES256 profile.
    output.length(Ztls::COSE::ES256::PublicKeySize, false);
    auto point = output.data();
    enum { Coord = Ztls::COSE::ES256::CoordinateSize };
    point[0] = 4;
    if (!(ZuBase64URL::decode({point + 1, Coord}, ZuBSpan{jwk.x}) == Coord &&
      ZuBase64URL::decode({point + 1 + Coord, Coord}, ZuBSpan{jwk.y}) == Coord &&
      Ztls::COSE::ES256::validPK(output))) return false;
    return true;
  }();
  ZuClear(json);
  if (!valid) output.null();
  return valid;
}

bool signKeyVerify(const SignKey &key, ZuCSpan token,
    ZuCSpan issuer, ZuCSpan audience, int64_t now,
    const JWTLimits &limits, Principal &principal)
{
  if (key.issuer != issuer || key.algorithm != "ES256" ||
      (key.state != State::Active && key.state != State::Suspended) ||
      key.notBefore > now || (key.retireAfter && key.retireAfter <= now))
    return false;
  Bytes publicKey;
  if (!signKeyPublic(key, publicKey)) return false;
  return audience ? jwtVerify(token, key.id, issuer, audience,
    publicKey, now, limits, principal) : jwtVerifyIssuer(token, key.id,
      issuer, publicKey, now, limits, principal);
}

static bool publicKeyValid(const SignKey &key, ZuBSpan expected = {})
{
  Bytes point;
  return signKeyPublic(key, point) &&
    (!expected || Ztls::ctEqual(point, expected));
}

bool signKeyMatch(Ztls::Random &rng, const SignKey &key, ZuBSpan privateKey)
{
  try {
    Ztls::PK::SK_EC signer{rng, Ztls::PK::OIDs::EC_GRP_SECP256R1, privateKey};
    ZuBArray<Ztls::COSE::ES256::PublicKeySize> point(
      Ztls::COSE::ES256::PublicKeySize, false);
    return Ztls::Backend::pkey_ec_export_public(signer.key, point) &&
      publicKeyValid(key, point);
  } catch (...) {
    return false;
  }
}

} // namespace Zum
