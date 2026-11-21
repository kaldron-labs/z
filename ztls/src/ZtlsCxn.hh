//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Protocol state for one TLS connection

#ifndef ZtlsCxn_HH
#define ZtlsCxn_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <openssl/async.h>
#include <openssl/types.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmLib.hh>

#include <zlib/ZtlsBuf.hh>

namespace Ztls_ {

struct TrafficProtection {
  uint8_t	secret[Ztls_MaxDigestSize];
  size_t	epoch;
  // The following fields are unused when updateTrafficKey is installed.
  AeadContext	*aead;
  uint64_t	seq;
  unsigned	failed:1;
};

ZuDerive(
  SignatureAlgorithms,
  (ZtBuiltin<
    ZtArray<uint16_t, ZtArrayHeapID<"Ztls.Engine.SignatureAlgorithms">>, ZtlsMaxSigAlgos>));

struct CertificateRequest {
  // context.base becomes non-NULL when a CertificateRequest is pending for processing
  Bytes			context;
  SignatureAlgorithms	signatureAlgorithms;
};

// Properties for ECH. Iff ECH is used and not rejected, aead is non-null.
ZtEnumNS(Ztls_API, EchState, int8_t, None, Offered, Accepted, Grease);

ZuDerive(EchPublicName, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.ECH.PublicName">>));

struct Ech {
  // ECH state for this connection. Offered and Accepted are used on both client and server;
  // Grease is client-only (server cannot distinguish GREASE from a config mismatch, both are
  // simply ECH that fails to decrypt).
  Ztls_::EchState::T	state;
  uint8_t		configId;
  const HpkeKem		*kem;
  const HpkeSuite	*cipher;
  AeadContext		*aead;
  uint8_t		innerClientRandom[Ztls_HelloRandomSize];
  struct Client {
    KeyExchangeBytes	enc;
    uint8_t		maxNameLength;
    EchPublicName	publicName;

    // retains a copy of entire ECH extension so that it can be replayed in the 2nd CH when ECH is
    // rejected via HRR
    EchBytes		firstEch;
  } client;
};

// One receive operation, owned by the connection's serial receive caller.
struct StreamRecord {
  uint8_t	header[5], tag[16];
  size_t	headerOff, tagOff, tagSize;
  size_t	length, textLength, written;
  uint8_t	*base;
  unsigned	encrypted:1, begun:1, failed:1;
};

ZtEnumNS(
  Ztls_API, State, int8_t, CliHSStart, CliWaitSrvHello, CliWait2ndSrvHello, CliWaitEncExts,
  CliWaitCertReq, CliExpectCert, CliWaitCertSig, CliWaitFinished, SrvWaitCliHello,
  SrvWait2ndCliHello, SrvBuildCertSig, SrvExpectCert, SrvWaitCertSig, SrvWaitEarlyEnd,
  SrvWaitFinished, CliPostHS, SrvPostHS);
namespace State {
  enum { PostHSMin = CliPostHS };
}

ZuDerive(ServerName, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.SNI">>));
ZuDerive(NegotiatedProtocol, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.ALPN">>));
ZuDerive(CertificateRequestBytes, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CertificateRequest">>));
ZuDerive(
  HandshakeSecret, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.HandshakeSecret">>>));
ZuDerive(
  ExporterSecret, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.ExporterSecret">>>));

} // namespace Ztls_

namespace Ztls {

ZuDerive(AsyncSignature, (ZtBArray<ZtArrayHeapID<"Ztls.Handshake.Signature">>));

struct Ztls_API Cxn {
  using KeyExchange = const Ztls_::KeyExchangeAlgo;
  using KeyExchangeCtx = Ztls_::KeyExchangeContext;
  using SignatureResume =
    ZmFn<int(Ztls_::Buffer *, uint16_t *), ZmFnHeapID<"Ztls.Handshake.SignatureResume">>;
  using SignatureRetire = ZmFn<bool(), ZmFnHeapID<"Ztls.Handshake.SignatureRetire">>;

  // Published before use and stable throughout active transport.
  Ztls_::Context			*ctx = nullptr;
  void					*dataPtr = nullptr;
  bool					isServer = false;

  // Selected by Rx during handshake and read by Tx after handoff.
  const Ztls_::CipherSuite		*cipherSuite = nullptr;

  // Rx owns handshake, receive state, and optional exporter secrets.
  alignas(Zm::CacheLineSize)
  Ztls_::State::T			state{};
  Ztls_::TrafficProtection		rxTraffic{};
  struct RxBuffers {
    Ztls_::Buffer	rec, mess;
  } recvbuf{};
  Ztls_::StreamRecord			streamRec{};
  Ztls_::KeySchedule			*keySchedule = nullptr;
  Ztls_::ServerName			serverName;
  Ztls_::NegotiatedProtocol		negotiatedProtocol;
  KeyExchange				*keyShare = nullptr;
  uint8_t				clientRandom[Ztls_HelloRandomSize]{};
  struct ExporterSecrets {
    Ztls_::ExporterSecret	early, oneRtt;
  } exporterMasterSecret;
  Ztls_::Ech				ech{};
  bool					rxActive = false;
  bool					isPskHandshake = false;
  bool					sendChangeCipherSpec = false;
  bool					txHandedOff = false;
  uint32_t				keyUpdateRequests = 0;
  Ztls_::Bytes				legacySessionId{};
  uint8_t				legacySessionIdBuf[32]{};
  KeyExchangeCtx			*keyShareCtx = nullptr;
  bool					offeredPsk = false;
  bool					usingEarlyData = false;
  Ztls_::CertificateRequest		certificateRequest{};
  uint8_t				pendingTrafficSecret[Ztls_MaxDigestSize]{};
  uint32_t				earlyDataSkippedBytes = UINT32_MAX;
  uint8_t				numTicketsToSend = 0;
  SignatureResume			signatureResume;
  SignatureRetire			signatureRetire;
  int					signatureFd = -1;
  EVP_MD_CTX				*signatureDigest = nullptr;
  ASYNC_WAIT_CTX			*signatureWait = nullptr;
  ASYNC_JOB				*signatureJob = nullptr;
  AsyncSignature			signature;
  size_t				signatureLength = 0;
  uint16_t				signatureAlgorithm = 0;
  Ztls_::VerifySignature		*certificateVerify = nullptr;
  Ztls_::HandshakeSecret		pendingHandshakeSecret;
  Ztls_::CertificateRequestBytes	certificateRequestBytes;
  Ztls_::HandshakeProps			handshakeProps{};
  ZmRef<ZiIOBuf>			rxInput;
  PlaintextSink				plaintext;
  bool					rxPaused = false;

  // Rx uses these members while negotiating. The handoff assigns them to Tx.
  alignas(Zm::CacheLineSize)
  Ztls_::TrafficProtection		txTraffic{};
  bool					needsKeyUpdate = false;
  bool					keyUpdateSendRequest = false;
  bool					txClosing = false;
  uint64_t				txGeneration = 0;

  Cxn(const Cxn &) = delete;
  Cxn &operator=(const Cxn &) = delete;
  Cxn() = default;
  Cxn(Ztls_::Context *ctx, bool server) { initialize(ctx, server); }
  ~Cxn();

  // Initialize before publishing the connection. Reinitialization requires
  // both directions to be idle.
  void initialize(Ztls_::Context *, bool server, void *owner = nullptr);
  void clearVerifier()
  {
    delete certificateVerify;
    certificateVerify = nullptr;
  }
  void retireRx();
  void retireTx();
  bool retireSignature();
};

} // namespace Ztls

// ZtlsCxn_HH
#endif
