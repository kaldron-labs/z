//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZumWebAuthn.hh>

#include <zlib/ZuBase64URL.hh>

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

static ZfJSON::AnyNode *rootObject(
    ZuSpan<char> json, ZuPtr<ZfJSON::AnyNode> &tree)
{
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() < 0) return nullptr;
  tree = ZuMv(parsed.p<1>());
  if (!tree || !tree->has<ZfJSON::AnyNode::Array>()) return nullptr;
  auto &roots = tree->data<ZfJSON::AnyNode::Array>();
  if (!roots || !roots[0]->has<ZfJSON::AnyNode::Object>()) return nullptr;
  return roots[0];
}

static ZuCSpan string(ZfJSON::AnyNode *node)
{
  if (!node || !node->has<ZfJSON::AnyNode::String>()) return {};
  return node->data<ZfJSON::AnyNode::String>();
}

static ZfJSON::AnyNode *responseObject(ZfJSON::AnyNode *root)
{
  ZfJSON::AnyNode *response = nullptr;
  for (auto &field: root->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == "response") {
      auto node = field.p<1>().ptr();
      response = node->has<ZfJSON::AnyNode::Object>() ? node : nullptr;
    }
  return response;
}

static ZuCSpan fieldString(ZfJSON::AnyNode *object, ZuCSpan id)
{
  ZuCSpan value;
  for (auto &field: object->data<ZfJSON::AnyNode::Object>())
    if (field.p<0>() == id) value = string(field.p<1>().ptr());
  return value;
}

static bool credential(
    ZfJSON::AnyNode *root, unsigned limit, Bytes &credentialID)
{
  ZuCSpan type, rawID;
  for (auto &field: root->data<ZfJSON::AnyNode::Object>()) {
    if (field.p<0>() == "type") type = string(field.p<1>().ptr());
    else if (field.p<0>() == "rawId") rawID = string(field.p<1>().ptr());
  }
  return type == "public-key" && decode(rawID, limit, credentialID);
}

int parseAssertion(ZuSpan<char> json,
    const WebAuthnInputLimits &limits, AssertionInput &input)
{
  ZuPtr<ZfJSON::AnyNode> tree;
  auto root = rootObject(json, tree);
  if (!root) return WebAuthnError::JSON;
  auto response = responseObject(root);
  AssertionInput next;
  if (!response || !credential(root, limits.credentialID, next.credentialID) ||
      !decode(fieldString(response, "clientDataJSON"),
        limits.clientDataJSON, next.clientDataJSON) ||
      !decode(fieldString(response, "authenticatorData"),
        limits.authenticatorData, next.authenticatorData) ||
      !decode(fieldString(response, "signature"),
        limits.signature, next.signature) ||
      !decode(fieldString(response, "userHandle"),
        limits.userHandle, next.userHandle)) return WebAuthnError::Fields;
  input = ZuMv(next);
  return WebAuthnError::OK;
}

int parseRegistration(ZuSpan<char> json,
    const WebAuthnInputLimits &limits, RegistrationInput &input)
{
  ZuPtr<ZfJSON::AnyNode> tree;
  auto root = rootObject(json, tree);
  if (!root) return WebAuthnError::JSON;
  auto response = responseObject(root);
  RegistrationInput next;
  if (!response || !credential(root, limits.credentialID, next.credentialID) ||
      !decode(fieldString(response, "clientDataJSON"),
        limits.clientDataJSON, next.clientDataJSON) ||
      !decode(fieldString(response, "attestationObject"),
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

String assertionOptions(
    ZuBSpan challenge, ZuCSpan rpID, uint64_t timeoutMS)
{
  String json{"{\"publicKey\":{\"challenge\":"};
  ZfJSON::quote(json, encode(challenge));
  json << ",\"rpId\":";
  ZfJSON::quote(json, rpID);
  json << ",\"timeout\":" << timeoutMS <<
    ",\"userVerification\":\"required\"}}";
  return json;
}

String registrationOptions(
    ZuBSpan challenge, ZuCSpan rpID, ZuCSpan rpName,
    ZuBSpan userHandle, ZuCSpan userName, ZuCSpan displayName,
    uint64_t timeoutMS)
{
  String json{"{\"publicKey\":{\"challenge\":"};
  ZfJSON::quote(json, encode(challenge));
  json << ",\"rp\":{\"id\":";
  ZfJSON::quote(json, rpID);
  json << ",\"name\":";
  ZfJSON::quote(json, rpName);
  json << "},\"user\":{\"id\":";
  ZfJSON::quote(json, encode(userHandle));
  json << ",\"name\":";
  ZfJSON::quote(json, userName);
  json << ",\"displayName\":";
  ZfJSON::quote(json, displayName);
  json << "},\"pubKeyCredParams\":[{\"type\":\"public-key\",\"alg\":-7}],"
    "\"timeout\":" << timeoutMS <<
    ",\"authenticatorSelection\":{\"residentKey\":\"required\","
    "\"requireResidentKey\":true,\"userVerification\":\"required\"},"
    "\"attestation\":\"none\"}}";
  return json;
}

struct ClientData {
  ZuCSpan	type;
  ZuCSpan	challenge;
  ZuCSpan	origin;
  bool		crossOrigin = false;
};

static bool clientData(
    ZfJSON::AnyNode *node, ClientData &data)
{
  if (!node || !node->has<ZfJSON::AnyNode::Object>()) return false;
  auto &fields = node->data<ZfJSON::AnyNode::Object>();
  unsigned seen = 0;
  for (auto &field: fields) {
    auto id = field.p<0>();
    auto value = field.p<1>().ptr();
    unsigned bit;
    if (id == "type") {
      bit = 1U;
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      data.type = value->data<ZfJSON::AnyNode::String>();
    } else if (id == "challenge") {
      bit = 2U;
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      data.challenge = value->data<ZfJSON::AnyNode::String>();
    } else if (id == "origin") {
      bit = 4U;
      if (!value->has<ZfJSON::AnyNode::String>()) return false;
      data.origin = value->data<ZfJSON::AnyNode::String>();
    } else if (id == "crossOrigin") {
      bit = 8U;
      if (value->has<ZfJSON::AnyNode::True>()) data.crossOrigin = true;
      else if (!value->has<ZfJSON::AnyNode::False>()) return false;
    } else {
      continue;
    }
    seen |= bit;
  }
  return (seen & 7U) == 7U;
}

static int clientData(
    ZuSpan<char> json, ZuCSpan type, ZuBSpan challenge,
    ZuCSpan origin)
{
  auto parsed = ZfJSON::scan(json);
  if (parsed.p<0>() < 0 || !parsed.p<1>() ||
      !parsed.p<1>()->has<ZfJSON::AnyNode::Array>())
    return WebAuthnError::JSON;
  auto &roots = parsed.p<1>()->data<ZfJSON::AnyNode::Array>();
  if (!roots) return WebAuthnError::JSON;
  ClientData client;
  if (!clientData(roots[0], client) || client.crossOrigin)
    return WebAuthnError::Fields;
  if (client.type != type) return WebAuthnError::Type;
  unsigned length = ZuBase64URL::enclen(challenge.length());
  auto encoded = ZtScratch(String, length, length);
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
  uint8_t clientHash[Ztls::MD<>::Size];
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
  uint8_t rpIDHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{state.rpID});
    md.finish(rpIDHash);
  }
  if (!Ztls::ctEqual(
      {authenticatorData.data(), sizeof(rpIDHash)}, rpIDHash))
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

ZfStruct(Attestation,
  (((fmt), (Mutable)), (String)),
  (((authData), (Mutable)), (Bytes)));

ZfStructRender(Attestation, CBOR, fmt, authData);

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
  uint8_t rpIDHash[Ztls::MD<>::Size];
  {
    Ztls::MD<> md;
    md.update(ZuBSpan{state.rpID});
    md.finish(rpIDHash);
  }
  if (!Ztls::ctEqual({authData.data(), sizeof(rpIDHash)}, rpIDHash))
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
  uint8_t publicKey[Ztls::COSE::ES256::PublicKeySize];
  if (!Ztls::COSE::ES256::loadPK(cose, publicKey))
    return WebAuthnError::PublicKey;

  RegistrationResult next;
  next.credentialID = Bytes{credentialID};
  next.publicKey = Bytes{ZuBSpan{publicKey}};
  next.signCount = signCount;
  next.backupEligible = flags & BE;
  next.backedUp = flags & BS;
  result = ZuMv(next);
  return WebAuthnError::OK;
}

} // namespace Zum
