//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/zumd_webauthn.hh>

#include <zlib/ZuArray.hh>
#include <zlib/ZuBase64URL.hh>
#include <zlib/ZuDerive.hh>

#include <zlib/ZfJSON.hh>

#include <zlib/ZtScratch.hh>

#include <zlib/ZtlsMD.hh>
#include <zlib/ZtlsCOSE.hh>
#include <zlib/ZtlsRandom.hh>
#include <zlib/ZtlsSec.hh>

namespace Zum {

// WebAuthn authenticator data has a fixed SHA-256 RP hash, flags byte,
// big-endian signature counter, and (for registration) attested-data header.
enum {
  AuthDataHashSize = 32,
  AuthDataFlags = AuthDataHashSize,
  AuthDataCounter = AuthDataFlags + 1,
  AssertionAuthDataSize = AuthDataCounter + 4,
  CredentialLength = AssertionAuthDataSize + 16,
  AttestedHeaderSize = CredentialLength + 2
};

static bool decode(ZuCSpan encoded, unsigned limit, Bytes &decoded)
{
  if (!encoded || encoded.length() > ZuBase64URL::enclen(limit)) return false;
  Bytes next;
  next.length(ZuBase64URL::declen(encoded.length()), false);
  if (ZuBase64URL::decode(next, ZuBSpan{encoded}) != next.length())
    return false;
  decoded = ZuMv(next);
  return true;
}

struct AssertionResponse {
  String authenticatorData;
  String clientDataJSON;
  String signature;
  String userHandle;
};
ZfStruct(, (AssertionResponse, JSON),
  (((authenticatorData),	(Required)),	String),
  (((clientDataJSON),	(Required)),		String),
  (((signature),		(Required)),	String),
  (((userHandle),	(Required)),		String));

struct RegistrationResponse {
  String attestationObject;
  String clientDataJSON;
};
ZfStruct(, (RegistrationResponse, JSON),
  (((attestationObject),	(Required)),	String),
  (((clientDataJSON),	(Required)),		String));

struct AssertionCredential {
  String rawID;
  AssertionResponse response;
  String type;
};
ZfStruct(, (AssertionCredential, JSON),
  (((rawID),	(JSON::ID<"rawId">, Required)),	String),
  (((response),	(Required)),			UDT),
  (((type),	(Required)),			String));

struct RegistrationCredential {
  String rawID;
  RegistrationResponse response;
  String type;
};
ZfStruct(, (RegistrationCredential, JSON),
  (((rawID),	(JSON::ID<"rawId">, Required)),	String),
  (((response),	(Required)),			UDT),
  (((type),	(Required)),			String));

template <typename T>
static bool jsonLoad(ZuSpan<char> json, T &value)
{
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() != int(json.length()) || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>()) return false;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (roots.length() != 1) return false;
  auto handler = ZfJSON::handler<T>(roots[0]);
  if (!handler.valid) return false;
  value = handler.ctor();
  return true;
}

int parseAssertion(ZuSpan<char> json,
    const WebAuthnInputLimits &limits, AssertionInput &input)
{
  AssertionCredential wire;
  if (!jsonLoad(json, wire)) return WebAuthnError::JSON;
  AssertionInput next;
  if (wire.type != "public-key" ||
      !decode(wire.rawID, limits.credentialID, next.credentialID) ||
      !decode(wire.response.clientDataJSON,
        limits.clientDataJSON, next.clientDataJSON) ||
      !decode(wire.response.authenticatorData,
        limits.authenticatorData, next.authenticatorData) ||
      !decode(wire.response.signature,
        limits.signature, next.signature) ||
      !decode(wire.response.userHandle,
        limits.userHandle, next.userHandle)) return WebAuthnError::Fields;
  input = ZuMv(next);
  return WebAuthnError::OK;
}

int parseRegistration(ZuSpan<char> json,
    const WebAuthnInputLimits &limits, RegistrationInput &input)
{
  RegistrationCredential wire;
  if (!jsonLoad(json, wire)) return WebAuthnError::JSON;
  RegistrationInput next;
  if (wire.type != "public-key" ||
      !decode(wire.rawID, limits.credentialID, next.credentialID) ||
      !decode(wire.response.clientDataJSON,
        limits.clientDataJSON, next.clientDataJSON) ||
      !decode(wire.response.attestationObject,
        limits.attestationObject, next.attestationObject))
    return WebAuthnError::Fields;
  input = ZuMv(next);
  return WebAuthnError::OK;
}

static String encode(ZuBSpan data)
{
  String encoded;
  encoded.length(ZuBase64URL::enclen(data.length()));
  encoded.length(ZuBase64URL::encode(encoded.span(), data));
  return encoded;
}

bool webAuthnChallenge(Ztls::Random &rng, Bytes &challenge)
{
  Bytes next;
  next.length(WebAuthnChallengeSize, false);
  if (!rng.random(next)) return false;
  challenge = ZuMv(next);
  return true;
}

struct AssertionPublicKey {
  String challenge;
  String rpID;
  uint64_t timeout = 0;
  String userVerification;
};
ZfStruct(, (AssertionPublicKey, JSON),
  (((challenge),		(Required)),		String),
  (((rpID),		(JSON::ID<"rpId">, Required)),	String),
  (((timeout),		(Required)),			UInt64),
  (((userVerification),	(Required)),			String));

struct AssertionOptions { AssertionPublicKey publicKey; };
ZfStruct(, (AssertionOptions, JSON),
  (((publicKey),	(Required)),	UDT));

struct RelyingParty { String id; String name; };
ZfStruct(, (RelyingParty, JSON),
  (((id),		(Required)),	String),
  (((name),		(Required)),	String));

struct RegistrationUser { String id; String name; String displayName; };
ZfStruct(, (RegistrationUser, JSON),
  (((id),		(Required)),	String),
  (((name),		(Required)),	String),
  (((displayName),	(Required)),	String));

struct CredentialParam { String type; int32_t alg = 0; };
ZfStruct(, (CredentialParam, JSON),
  (((type),		(Required)),	String),
  (((alg),		(Required)),	Int32));
ZuDerive(CredentialParamArray, (ZtArray<CredentialParam,
  ZtArrayHeapID<"Zum.WebAuthn.Params">>));
struct CredentialParamVec : public CredentialParamArray {
  ZuDerive_(CredentialParamVec, CredentialParamArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(CredentialParamVec *);
};

struct AuthenticatorSelection {
  String residentKey;
  bool requireResidentKey = false;
  String userVerification;
};
ZfStruct(, (AuthenticatorSelection, JSON),
  (((residentKey),	(Required)),	String),
  (((requireResidentKey),(Required)),	Bool),
  (((userVerification),	(Required)),	String));

struct RegistrationPublicKey {
  String challenge;
  RelyingParty rp;
  RegistrationUser user;
  CredentialParamVec pubKeyCredParams;
  uint64_t timeout = 0;
  AuthenticatorSelection authenticatorSelection;
  String attestation;
};
ZfStruct(, (RegistrationPublicKey, JSON),
  (((challenge),		(Required)),	String),
  (((rp),		(Required)),		UDT),
  (((user),		(Required)),		UDT),
  (((pubKeyCredParams),	(Required)),		UDT),
  (((timeout),		(Required)),		UInt64),
  (((authenticatorSelection),(Required)),	UDT),
  (((attestation),	(Required)),		String));

struct RegistrationOptions { RegistrationPublicKey publicKey; };
ZfStruct(, (RegistrationOptions, JSON),
  (((publicKey),	(Required)),	UDT));

String assertionOptions(
    ZuBSpan challenge, ZuCSpan rpID, uint64_t timeoutMS)
{
  String json;
  ZfJSON::save(json, AssertionOptions{AssertionPublicKey{
    .challenge = encode(challenge), .rpID = rpID, .timeout = timeoutMS,
    .userVerification = "required"}});
  return json;
}

String registrationOptions(
    ZuBSpan challenge, ZuCSpan rpID, ZuCSpan rpName,
    ZuBSpan userHandle, ZuCSpan userName, ZuCSpan displayName,
    uint64_t timeoutMS)
{
  String json;
  ZfJSON::save(json, RegistrationOptions{RegistrationPublicKey{
    .challenge = encode(challenge), .rp = {rpID, rpName},
    .user = {encode(userHandle), userName, displayName},
    .pubKeyCredParams = {{.type = "public-key", .alg = -7}},
    .timeout = timeoutMS,
    .authenticatorSelection = {"required", true, "required"},
    .attestation = "none"}});
  return json;
}

struct ClientData {
  String	type;
  String	challenge;
  String	origin;
  bool		crossOrigin = false;
};
ZfStruct(, (ClientData, JSON),
  (((type),		(Required)),			String),
  (((challenge),	(Required)),			String),
  (((origin),		(Required)),			String),
  (((crossOrigin),	(JSON::Opt, Deflt<false>)),	Bool));

static int clientData(
    ZuSpan<char> json, ZuCSpan type, ZuBSpan challenge,
    ZuCSpan origin)
{
  ClientData client;
  if (!jsonLoad(json, client)) return WebAuthnError::JSON;
  if (!client.type || !client.challenge || !client.origin || client.crossOrigin)
    return WebAuthnError::Fields;
  if (client.type != type) return WebAuthnError::Type;
  unsigned length = ZuBase64URL::enclen(challenge.length());
  auto encoded = ZtScratch(TextScratch, 0, length + 1);
  encoded.length(ZuBase64URL::encode(encoded.span(), challenge));
  if (client.challenge != encoded) return WebAuthnError::Challenge;
  if (client.origin != origin) return WebAuthnError::Origin;
  return WebAuthnError::OK;
}

int verifyAssertion(
    AssertionInput &input, const AssertionState &state,
    AssertionResult &result)
{
  if (!input.credentialID || !input.clientDataJSON ||
      !input.authenticatorData || !input.signature || !input.userHandle ||
      input.userHandle != state.userHandle)
    return WebAuthnError::Fields;

  // ZfJSON parses in place; hash the exact browser bytes before parsing.
  ZuBArray<Ztls::MD<>::Size> clientHash(Ztls::MD<>::Size, false);
  {
    Ztls::MD<> md;
    md.update(input.clientDataJSON);
    md.finish(clientHash);
  }
  int error = clientData(input.clientDataJSON, "webauthn.get",
    state.challenge, state.origin);
  if (error) return error;

  auto authenticatorData = ZuBSpan{input.authenticatorData};
  if (authenticatorData.length() < AssertionAuthDataSize)
    return WebAuthnError::AuthData;
  ZuBArray<Ztls::MD<>::Size> rpIDHash(Ztls::MD<>::Size, false);
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{state.rpID});
    md.finish(rpIDHash);
  }
  if (!Ztls::ctEqual(
      {authenticatorData.data(), rpIDHash.length()}, rpIDHash))
    return WebAuthnError::RPID;

  uint8_t flags = authenticatorData[AuthDataFlags];
  constexpr uint8_t UP = 1U, UV = 1U<<2, BE = 1U<<3, BS = 1U<<4;
  if ((flags & (UP | UV)) != (UP | UV) ||
      bool(flags & BE) != state.backupEligible ||
      ((flags & BS) && !(flags & BE))) return WebAuthnError::Flags;
  uint32_t signCount =
    (uint32_t(authenticatorData[AuthDataCounter])<<24) |
    (uint32_t(authenticatorData[AuthDataCounter + 1])<<16) |
    (uint32_t(authenticatorData[AuthDataCounter + 2])<<8) |
    uint32_t(authenticatorData[AuthDataCounter + 3]);

  if (!Ztls::COSE::ES256::verify(state.publicKey, authenticatorData,
      clientHash, input.signature)) return WebAuthnError::Signature;

  AssertionResult next{
    .signCount = signCount,
    .counter = CounterState::None,
    .backedUp = bool(flags & BS)
  };
  if (state.signCount && signCount) {
    if (signCount > state.signCount)
      next.counter = CounterState::Advanced;
    else if (state.backupEligible)
      next.counter = CounterState::Regression;
    else
      return WebAuthnError::Counter;
  }
  result = next;
  return WebAuthnError::OK;
}

using namespace ZuFieldProp;

struct Attestation {
  ZuCSpan	fmt;
  ZuBSpan	authData;
};

ZfStruct(, Attestation,
  (((fmt), (Mutable)),		String),
  (((authData), (Mutable)),	Bytes));

ZfStructRender(, Attestation, CBOR, fmt, authData);

int verifyRegistration(
    RegistrationInput &input, const RegistrationState &state,
    unsigned credentialIDMax,
    RegistrationResult &result)
{
  if (!input.credentialID || !input.clientDataJSON ||
      !input.attestationObject || !credentialIDMax)
    return WebAuthnError::Fields;
  int error = clientData(input.clientDataJSON, "webauthn.create",
    state.challenge, state.origin);
  if (error) return error;

  Attestation attestation{};
  ZfCBOR::handler<Attestation>(input.attestationObject).load(attestation);
  if (attestation.fmt != "none" || !attestation.authData)
    return WebAuthnError::Attestation;
  auto authData = attestation.authData;
  if (authData.length() < AttestedHeaderSize) return WebAuthnError::AuthData;
  ZuBArray<Ztls::MD<>::Size> rpIDHash(Ztls::MD<>::Size, false);
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{state.rpID});
    md.finish(rpIDHash);
  }
  if (!Ztls::ctEqual({authData.data(), rpIDHash.length()}, rpIDHash))
    return WebAuthnError::RPID;
  uint8_t flags = authData[AuthDataFlags];
  constexpr uint8_t UP = 1U, UV = 1U<<2, BE = 1U<<3, BS = 1U<<4,
    AT = 1U<<6;
  if ((flags & (UP | UV | AT)) != (UP | UV | AT) ||
      ((flags & BS) && !(flags & BE))) return WebAuthnError::Flags;
  uint32_t signCount =
    (uint32_t(authData[AuthDataCounter])<<24) |
    (uint32_t(authData[AuthDataCounter + 1])<<16) |
    (uint32_t(authData[AuthDataCounter + 2])<<8) |
    uint32_t(authData[AuthDataCounter + 3]);
  unsigned credentialLength =
    (unsigned(authData[CredentialLength])<<8) |
    unsigned(authData[CredentialLength + 1]);
  if (!credentialLength || credentialLength > credentialIDMax ||
      AttestedHeaderSize + credentialLength >= authData.length())
    return WebAuthnError::Credential;
  ZuBSpan credentialID{
    authData.data() + AttestedHeaderSize, credentialLength};
  if (credentialID != input.credentialID) return WebAuthnError::Credential;
  ZuBSpan cose{authData.data() + AttestedHeaderSize + credentialLength,
    authData.length() - AttestedHeaderSize - credentialLength};
  ZuBArray<Ztls::COSE::ES256::PublicKeySize> publicKey(
    Ztls::COSE::ES256::PublicKeySize, false);
  if (!Ztls::COSE::ES256::loadPK(cose, publicKey))
    return WebAuthnError::PublicKey;

  RegistrationResult next;
  next.credentialID = Bytes{credentialID};
  next.publicKey = Bytes{publicKey};
  next.signCount = signCount;
  next.backupEligible = flags & BE;
  next.backedUp = flags & BS;
  result = ZuMv(next);
  return WebAuthnError::OK;
}

} // namespace Zum
