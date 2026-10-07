//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef zrest_example_HH
#define zrest_example_HH

#include <zlib/ZuDerive.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfJSON.hh>
#include <zlib/ZfURI.hh>

enum {
  OAuthAppIDMax = 64,
  OAuthClientIDMax = 128,
  OAuthURLMax = 2048,
  OAuthScopeMax = 512,
  OAuthStateMax = 256,
  OAuthCodeMax = 256,
  OAuthVerifierMax = 128,
  OAuthTokenMax = 16U<<10,
  OAuthErrorDescMax = 512
};

ZuDerive(OAuthString, (ZtString<ZtStringHeapID<"zrest.OAuthString">>));
ZuDerive(OAuthSecret, (ZtString<ZtStringSecret<true,
  ZtStringHeapID<"zrest.OAuthString">>>));
ZuDerive(OAuthStringArray, (ZtArray<OAuthString,
  ZtArrayHeapID<"zrest.OAuthStrings">>));
struct OAuthStringVec : public OAuthStringArray {
  ZuDerive_(OAuthStringVec, OAuthStringArray);
  friend ZfJSON::AsArray<ZfFieldTC::String> ZfJSON_Fmt(OAuthStringVec *);
};

// Path components are deliberately represented as URI fields.  Callers load
// and save these objects, then validate the fixed profile components.
struct OAuthIssuerPath {
  OAuthString oauth2{"oauth2"};
  OAuthString app;
};
ZfStruct(, (OAuthIssuerPath, URI),
  (oauth2, (URI::PathIndex<0>, Required),	String),
  (app,    (URI::PathIndex<1>, Required),	String));

struct OAuthEndpointPath {
  OAuthString oauth2{"oauth2"};
  OAuthString app;
  OAuthString version{"v1"};
  OAuthString endpoint;
};
ZfStruct(, (OAuthEndpointPath, URI),
  (oauth2,   (URI::PathIndex<0>, Required),	String),
  (app,      (URI::PathIndex<1>, Required),	String),
  (version,  (URI::PathIndex<2>, Required),	String),
  (endpoint, (URI::PathIndex<3>, Required),	String));

struct OAuthMetadataPath {
  OAuthString wellKnown{".well-known"};
  OAuthString metadata{"oauth-authorization-server"};
  OAuthString oauth2{"oauth2"};
  OAuthString app;
};
ZfStruct(, (OAuthMetadataPath, URI),
  (wellKnown, (URI::PathIndex<0>, Required),		String),
  (metadata,  (URI::PathIndex<1>, Required),		String),
  (oauth2,    (URI::PathIndex<2>, Required),		String),
  (app,       (URI::PathIndex<3>, Required),		String));

template <typename Heap = ZuVoid>
struct OAuthAuthorizeReq_ : public Heap, public ZmObject {
  OAuthString responseType;
  OAuthString clientID;
  OAuthString redirectURI;
  OAuthString scope;
  OAuthString state;
  OAuthString codeChallenge;
  OAuthString codeChallengeMethod;
};
ZuDerive(OAuthAuthorizeReqHeap,
  (ZmHeap<"zrest.OAuthAuthorizeReq", OAuthAuthorizeReq_<>>));
ZuDerive(OAuthAuthorizeReq,
  (OAuthAuthorizeReq_<OAuthAuthorizeReqHeap>));
ZfStruct(, (OAuthAuthorizeReq, URI),
  (responseType, (URI::ID<"response_type">, Required),		String),
  (clientID, (URI::ID<"client_id">, Required),			String),
  (redirectURI, (URI::ID<"redirect_uri">, Required),		String),
  (scope, (Required),						String),
  (state, (Required),						String),
  (codeChallenge, (URI::ID<"code_challenge">, Required),	String),
  (codeChallengeMethod,
    (URI::ID<"code_challenge_method">, Required),		String));

template <typename Heap = ZuVoid>
struct OAuthCodeTokenReq_ : public Heap, public ZmObject {
  OAuthString grantType;
  OAuthString code;
  OAuthString redirectURI;
  OAuthString clientID;
  OAuthString codeVerifier;
};
ZuDerive(OAuthCodeTokenReqHeap,
  (ZmHeap<"zrest.OAuthCodeTokenReq", OAuthCodeTokenReq_<>>));
ZuDerive(OAuthCodeTokenReq, (OAuthCodeTokenReq_<OAuthCodeTokenReqHeap>));
ZfStruct(, (OAuthCodeTokenReq, URI),
  (grantType, (URI::ID<"grant_type">, Required),		String),
  (code, (Required),						String),
  (redirectURI, (URI::ID<"redirect_uri">, Required),		String),
  (clientID, (URI::ID<"client_id">, Required),			String),
  (codeVerifier, (URI::ID<"code_verifier">, Required),		String));

template <typename Heap = ZuVoid>
struct OAuthRefreshTokenReq_ : public Heap, public ZmObject {
  OAuthString grantType;
  OAuthString refreshToken;
  OAuthString scope;
  OAuthString clientID;
};
ZuDerive(OAuthRefreshTokenReqHeap,
  (ZmHeap<"zrest.OAuthRefreshTokenReq", OAuthRefreshTokenReq_<>>));
ZuDerive(OAuthRefreshTokenReq,
  (OAuthRefreshTokenReq_<OAuthRefreshTokenReqHeap>));
ZfStruct(, (OAuthRefreshTokenReq, URI),
  (grantType, (URI::ID<"grant_type">, Required),		String),
  (refreshToken, (URI::ID<"refresh_token">, Required),		String),
  (scope, (JSON::Opt),						String),
  (clientID, (URI::ID<"client_id">, Required),			String));

template <typename Heap = ZuVoid>
struct OAuthRevokeReq_ : public Heap, public ZmObject {
  OAuthString token;
  OAuthString tokenTypeHint;
  OAuthString clientID;
};
ZuDerive(OAuthRevokeReqHeap,
  (ZmHeap<"zrest.OAuthRevokeReq", OAuthRevokeReq_<>>));
ZuDerive(OAuthRevokeReq, (OAuthRevokeReq_<OAuthRevokeReqHeap>));
ZfStruct(, (OAuthRevokeReq, URI),
  (token, (Required),						String),
  (tokenTypeHint, (URI::ID<"token_type_hint">, JSON::Opt),	String),
  (clientID, (URI::ID<"client_id">, Required),			String));

struct OAuthAuthorizeCodeRes {
  OAuthString code;
  OAuthString state;
  OAuthString issuerURL;
};
ZfStruct(, (OAuthAuthorizeCodeRes, URI),
  (code, (Required),				String),
  (state, (Required),				String),
  (issuerURL, (URI::ID<"iss">, Required),	String));

struct OAuthAuthorizeErrorRes {
  OAuthString error;
  OAuthString errorDescription;
  OAuthString state;
  OAuthString issuerURL;
};
ZfStruct(, (OAuthAuthorizeErrorRes, URI),
  (error, (Required),				String),
  (errorDescription,
    (URI::ID<"error_description">, JSON::Opt),	String),
  (state, (JSON::Opt),				String),
  (issuerURL, (URI::ID<"iss">, Required),	String));

template <typename Heap = ZuVoid>
struct OAuthTokenRes_ : public Heap, public ZmObject {
  OAuthSecret accessToken;
  OAuthString tokenType;
  uint64_t expiresIn = 0;
  OAuthSecret refreshToken;
  OAuthString scope;
};
ZuDerive(OAuthTokenResHeap, (ZmHeap<"zrest.OAuthTokenRes", OAuthTokenRes_<>>));
ZuDerive(OAuthTokenRes, (OAuthTokenRes_<OAuthTokenResHeap>));
ZfStruct(, (OAuthTokenRes, JSON),
  (accessToken, (JSON::ID<"access_token">, Required),		String),
  (tokenType, (JSON::ID<"token_type">, Required),		String),
  (expiresIn, (JSON::ID<"expires_in">, Required),		UInt64),
  (refreshToken, (JSON::ID<"refresh_token">, JSON::Opt),	String),
  (scope, (Required),						String));

template <typename Heap = ZuVoid>
struct OAuthError_ : public Heap, public ZmObject {
  OAuthString error;
  OAuthString errorDescription;
};
ZuDerive(OAuthErrorHeap, (ZmHeap<"zrest.OAuthError", OAuthError_<>>));
ZuDerive(OAuthError, (OAuthError_<OAuthErrorHeap>));
ZfStruct(, (OAuthError, JSON),
  (error, (Required),					String),
  (errorDescription,
    (JSON::ID<"error_description">, JSON::Opt),		String));

template <typename Heap = ZuVoid>
struct OAuthMetadata_ : public Heap, public ZmObject {
  OAuthString issuerURL;
  OAuthString authorizationEndpoint;
  OAuthString tokenEndpoint;
  OAuthString revocationEndpoint;
  OAuthString jwksURI;
  OAuthStringVec responseTypes;
  OAuthStringVec grantTypes;
  OAuthStringVec codeChallengeMethods;
  OAuthStringVec scopes;
  OAuthStringVec tokenAuthMethods;
  OAuthStringVec revokeAuthMethods;
};
ZuDerive(OAuthMetadataHeap, (ZmHeap<"zrest.OAuthMetadata", OAuthMetadata_<>>));
ZuDerive(OAuthMetadata, (OAuthMetadata_<OAuthMetadataHeap>));
ZfStruct(, (OAuthMetadata, JSON),
  (issuerURL, (JSON::ID<"issuer">, Required),					String),
  (authorizationEndpoint,
    (JSON::ID<"authorization_endpoint">, Required),				String),
  (tokenEndpoint,
    (JSON::ID<"token_endpoint">, Required),					String),
  (revocationEndpoint,
    (JSON::ID<"revocation_endpoint">, Required),				String),
  (jwksURI, (JSON::ID<"jwks_uri">, Required),					String),
  (responseTypes,
    (JSON::ID<"response_types_supported">, Required),				StringVec),
  (grantTypes,
    (JSON::ID<"grant_types_supported">, Required),				StringVec),
  (codeChallengeMethods,
    (JSON::ID<"code_challenge_methods_supported">, Required),			StringVec),
  (scopes, (JSON::ID<"scopes_supported">, Required),				StringVec),
  (tokenAuthMethods,
    (JSON::ID<"token_endpoint_auth_methods_supported">, Required),		StringVec),
  (revokeAuthMethods,
    (JSON::ID<"revocation_endpoint_auth_methods_supported">, Required),		StringVec));

struct OAuthAccessClaims {
  OAuthString issuerURL;
  OAuthString audience;
  OAuthString subject;
  OAuthString clientID;
  OAuthString scope;
  int64_t issuedAt = 0;
  int64_t notBefore = 0;
  int64_t expiry = 0;
  OAuthString tokenID;
};
ZfStruct(, (OAuthAccessClaims, JSON),
  (issuerURL, (JSON::ID<"iss">, Required),		String),
  (audience, (JSON::ID<"aud">, Required),		String),
  (subject, (JSON::ID<"sub">, Required),		String),
  (clientID, (JSON::ID<"client_id">, Required),		String),
  (scope, (Required),					String),
  (issuedAt, (JSON::ID<"iat">, Required),		Int64),
  (notBefore, (JSON::ID<"nbf">, Required),		Int64),
  (expiry, (JSON::ID<"exp">, Required),			Int64),
  (tokenID, (JSON::ID<"jti">, Required),		String));

struct OAuthJWK {
  OAuthString keyID;
  OAuthString keyType;
  OAuthString curve;
  OAuthString use;
  OAuthString algorithm;
  OAuthString x;
  OAuthString y;
};
ZfStruct(, (OAuthJWK, JSON),
  (keyID, (JSON::ID<"kid">, Required),		String),
  (keyType, (JSON::ID<"kty">, Required),	String),
  (curve, (JSON::ID<"crv">, Required),		String),
  (use, (Required),				String),
  (algorithm, (JSON::ID<"alg">, Required),	String),
  (x, (Required),				String),
  (y, (Required),				String));
ZuDerive(OAuthJWKArray, (ZtArray<OAuthJWK,
  ZtArrayHeapID<"zrest.OAuthJWKs">>));
struct OAuthJWKVec : public OAuthJWKArray {
  ZuDerive_(OAuthJWKVec, OAuthJWKArray);
  friend ZfJSON::AsArray<ZfFieldTC::UDT> ZfJSON_Fmt(OAuthJWKVec *);
};
template <typename Heap = ZuVoid>
struct OAuthJWKS_ : public Heap, public ZmObject { OAuthJWKVec keys; };
ZuDerive(OAuthJWKSHeap, (ZmHeap<"zrest.OAuthJWKS", OAuthJWKS_<>>));
ZuDerive(OAuthJWKS, (OAuthJWKS_<OAuthJWKSHeap>));
ZfStruct(, (OAuthJWKS, JSON), (keys, (Required), UDT));

using PingPath = ZuStringT<"/api/ping">;

template <typename Heap = ZuVoid>
struct Ping_ : public Heap, public ZmObject { bool ping = false; };
ZuDerive(PingHeap, (ZmHeap<"zrest.Ping", Ping_<>>));
ZuDerive(Ping, (Ping_<PingHeap>));

template <typename Heap = ZuVoid>
struct Pong_ : public Heap, public ZmObject { bool pong = false; };
ZuDerive(PongHeap, (ZmHeap<"zrest.Pong", Pong_<>>));
ZuDerive(Pong, (Pong_<PongHeap>));

ZfStruct(, (Ping, URI), (ping, (Required), Bool));
ZfStruct(, (Pong, JSON), (pong, (Required), Bool));

#endif /* zrest_example_HH */
