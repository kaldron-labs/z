//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZtlsCredentials.hh>
#include <zlib/ZtlsPKOps.hh>

#include "../src/engine/ZtlsCertCompression.hh"
#include "ZtlsTestHandshake.hh"

using namespace Ztls_;
using namespace ZuTestUtil;

namespace RawHandshakeTest {

enum { Initial = 0, EarlyData = 1, Handshake = 2, Application = 3, EpochCount = 4 };

ZuDerive(Ticket, (ZtBArray<ZtArrayHeapID<"Ztls.Test.RawHandshake.Ticket">>));

struct Peer {
  Ztls::Cxn	tls;
  Ticket	*ticket = nullptr;
  unsigned	traffic[2]{};

  static int trafficKey(Ztls::Cxn *tls, int transmit, size_t epoch, const void *secret)
  {
    auto peer = static_cast<Peer *>(tls->dataPtr);
    if (epoch >= EpochCount || !secret || !cipher(tls)) return Ztls_ErrorLibrary;
    peer->traffic[!!transmit] |= 1U << epoch;
    return 0;
  }

  static int saveTicket(Ztls::Cxn *tls, Bytes input)
  {
    auto peer = static_cast<Peer *>(tls->dataPtr);
    if (peer->ticket && !*peer->ticket) *peer->ticket = ZuBSpan{input.data(), input.length()};
    return 0;
  }
};

static void configure(Context &context)
{
  struct TrafficHook : UpdateTrafficKey {
    int call(Ztls::Cxn *tls, int tx, size_t epoch, const void *secret) override
    { return Peer::trafficKey(tls, tx, epoch, secret); }
  };
  static TrafficHook traffic;
  context.random = &opensslRandom;
  context.getTime = &Ztls_::getTime;
  context.keyExchanges = opensslKeyExchanges;
  context.cipherSuites = opensslCipherSuites;
  context.omitEndOfEarlyData = 1;
  context.updateTrafficKey = &traffic;
}

struct Flight {
  enum { Capacity = 4096, Sentinel = 0xa5, HandshakeHeaderSize = 4 };

  uint8_t		storage[Capacity];
  RawHandshakeResult	result{};

  int drive(
    Peer &peer, RawHandshakeOp::T operation, unsigned epoch = Initial, ZuBSpan input = {},
    unsigned capacity = Capacity)
  {
    memset(storage, Sentinel, sizeof(storage));
    result = rawHandshake(
      &peer.tls, &peer.tls.handshakeProps, operation, epoch, input, {storage, capacity});
    return result.status;
  }

  bool untouched(size_t from) const
  {
    for (size_t i = from; i < sizeof(storage); ++i)
      if (storage[i] != Sentinel) return false;
    return true;
  }

  bool empty() const
  {
    if (result.length || result.offsets.length() != EpochCount + 1) return false;
    for (auto offset : result.offsets)
      if (offset) return false;
    return true;
  }

  ZuBSpan bytes(unsigned epoch) const
  {
    auto from = result.offsets[epoch];
    return {storage + from, result.offsets[epoch + 1] - from};
  }

  bool valid(unsigned capacity = Capacity) const
  {
    if (result.length > capacity || result.offsets.length() != EpochCount + 1 ||
      result.offsets[0] || result.offsets[EpochCount] != result.length ||
      result.offsets[EarlyData] != result.offsets[EarlyData + 1] || !untouched(result.length))
      return false;

    for (unsigned epoch = 0; epoch < EpochCount; ++epoch) {
      if (result.offsets[epoch] > result.offsets[epoch + 1] ||
	result.offsets[epoch + 1] > result.length)
	return false;
      auto input = bytes(epoch);
      while (input.length()) {
	if (input.length() < HandshakeHeaderSize) return false;
	size_t length =
	  HandshakeHeaderSize + (unsigned(input[1]) << 16) + (unsigned(input[2]) << 8) + input[3];
	if (length > input.length()) return false;
	input.offset(length);
      }
    }
    return true;
  }

  bool startsWith(unsigned epoch, unsigned message) const
  {
    auto input = bytes(epoch);
    return input.length() && input[0] == message;
  }
};

static bool accepted(int result) { return !result || result == Ztls_ErrorInProgress; }

static void outputBounds()
{
  ZuTestScope(outputBounds);
  Context context{};
  configure(context);
  Peer	 client;
  Flight flight;
  client.tls.initialize(&context, false, &client);
  auto status = flight.drive(client, RawHandshakeOp::Start);
  bool started = accepted(status) && flight.valid() &&
    flight.startsWith(Initial, Ztls_HsTypeCliHello) &&
    flight.result.offsets[1] == flight.result.length;
  ZuCHECK(started, "ClientHello or Initial output range was invalid: ", status);
  if (!started) return;

  auto length = flight.result.length;
  client.tls.initialize(&context, false, &client);
  status = flight.drive(client, RawHandshakeOp::Start, Initial, {}, length);
  ZuCHECK(
    accepted(status) && flight.valid(length) && flight.result.length == length,
    "exact-fit output did not keep the caller buffer: ", status);

  client.tls.initialize(&context, false, &client);
  status = flight.drive(client, RawHandshakeOp::Start, Initial, {}, length - 1);
  ZuCHECK(
    status == Ztls_ErrorRawOutput && flight.empty() && flight.untouched(length - 1),
    "short output exposed ranges or wrote past its capacity: ", status);
}

static bool certificate(Context &server, const Ztls::PK::Key &key)
{
  auto cert = X509_new();
  if (!cert) return false;
  ZuGuard cleanup{[cert]() { X509_free(cert); }};
  auto	  name = X509_get_subject_name(cert);
  if (X509_set_version(cert, 2) != 1 || ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) != 1 ||
    !X509_gmtime_adj(X509_getm_notBefore(cert), -60) ||
    !X509_gmtime_adj(X509_getm_notAfter(cert), 3600) ||
    X509_NAME_add_entry_by_txt(
      name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char *>("localhost"), -1, -1,
      0) != 1 ||
    X509_set_issuer_name(cert, name) != 1 || X509_set_pubkey(cert, key.get()) != 1 ||
    X509_sign(cert, key.get(), EVP_sha256()) <= 0)
    return false;
  auto length = i2d_X509(cert, nullptr);
  if (length <= 0) return false;
  auto		 extent = unsigned(length);
  CertificateDER der(extent, extent);
  auto		 out = der.data();
  if (i2d_X509(cert, &out) != length) return false;
  server.certificates.push(ZuMv(der));
  return true;
}

static bool exchange(
  Peer &client, Peer &server, Flight &c, Flight &s, bool retry,
  const void *cookieKey = nullptr, bool rejectBinder = false)
{
  if (!accepted(c.drive(client, RawHandshakeOp::Start)) || !c.valid() ||
    rawReadEpoch(&server.tls) != Initial)
    return false;

  auto hello = c.bytes(Initial);
  if (hello.length() <= 3 ||
    s.drive(server, RawHandshakeOp::Input, Initial, {hello.data(), 3}) != Ztls_ErrorInProgress ||
    !s.valid() || s.result.length || rawReadEpoch(&server.tls) != Initial || server.traffic[0] ||
    server.traffic[1])
    return false;
  int status =
    s.drive(server, RawHandshakeOp::Input, Initial, {hello.data() + 3, hello.length() - 3});
  if (!(cookieKey ? status == Ztls_ErrorStatelessRetry : accepted(status)) || !s.valid())
    return false;

  if (retry) {
    if (!s.startsWith(Initial, Ztls_HsTypeSrvHello) || s.result.offsets[1] != s.result.length ||
      rawReadEpoch(&server.tls) != Initial ||
      !accepted(c.drive(client, RawHandshakeOp::Input, Initial, s.bytes(Initial))) ||
      !c.valid() || !c.startsWith(Initial, Ztls_HsTypeCliHello) ||
      c.result.offsets[1] != c.result.length || rawReadEpoch(&client.tls) != Initial)
      return false;
    if (cookieKey) {
      auto ctx = server.tls.ctx;
      server.tls.initialize(ctx, true, &server);
      server.tls.handshakeProps.server.cookie.key = cookieKey;
      server.tls.handshakeProps.server.retryUsesCookie = 1;
      server.tls.handshakeProps.server.enforceRetry = 1;
    }
    if (rejectBinder) {
      c.storage[c.result.offsets[Initial + 1] - 1] ^= 1;
      return Ztls_ErrorToAlert(
	s.drive(server, RawHandshakeOp::Input, Initial, c.bytes(Initial))) ==
	Ztls_AlertDecryptError;
    }
    if (!accepted(s.drive(server, RawHandshakeOp::Input, Initial, c.bytes(Initial))) ||
      !s.valid())
      return false;
  }

  return s.startsWith(Initial, Ztls_HsTypeSrvHello) &&
    s.startsWith(Handshake, Ztls_HsTypeEncExts) &&
    s.startsWith(Application, Ztls_HsTypeNewTicket) && rawReadEpoch(&server.tls) == Handshake &&
    !(server.traffic[0] & (1U << Application)) && (server.traffic[1] & (1U << Application)) &&
    accepted(c.drive(client, RawHandshakeOp::Input, Initial, s.bytes(Initial))) && c.valid() &&
    !c.result.length && rawReadEpoch(&client.tls) == Handshake &&
    !c.drive(client, RawHandshakeOp::Input, Handshake, s.bytes(Handshake)) && c.valid() &&
    c.startsWith(Handshake, Ztls_HSTypeFinished) && rawReadEpoch(&client.tls) == Application &&
    !rawHandshake(
      &client.tls, &client.tls.handshakeProps, RawHandshakeOp::Input, Application,
      s.bytes(Application), {})
       .status &&
    !s.drive(server, RawHandshakeOp::Input, Handshake, c.bytes(Handshake)) && s.valid() &&
    rawReadEpoch(&server.tls) == Application && handshakeComplete(&client.tls) &&
    handshakeComplete(&server.tls);
}

static void handshake(bool compressed = false)
{
  ZuTestScopeRT(handshake);
  Context clientContext{}, serverContext{};
  configure(clientContext);
  configure(serverContext);
  struct SaveSessionTicket : SaveTicket {
    int call(Ztls::Cxn *tls, Bytes input) override { return Peer::saveTicket(tls, input); }
  };
  static SaveSessionTicket save;
  clientContext.saveTicket = &save;
  Ztls::PK::Key key;
  bool		haveCert = Ztls::PK::rsaGenerate(key, 2048) && certificate(serverContext, key);
  ZuCheckRT(haveCert);
  if (!haveCert) return;
  auto	  signer = Ztls::signCertNew(key);
  auto	  tickets = Ztls::ticketKeyNew();
  ZuGuard cleanup{[signer, tickets]()
  {
    if (tickets) Ztls::ticketKeyFree(tickets);
    if (signer) Ztls::signCertFree(signer);
  }};
  ZuCheckRT(signer && tickets);
  if (!signer || !tickets) return;
  serverContext.signCertificate = Ztls::signCertHook(signer);
  serverContext.encryptTicket = Ztls::ticketEncryptHook(tickets);
  serverContext.ticketLifetime = 60;
  serverContext.maxEarlyDataSize = 1024;

#if ZtlsUseBrotli
  EmitCompressedCert emitter;
  ZuGuard	     dispose{[&emitter]() { disposeCompressedCert(&emitter); }};
  if (compressed) {
    auto &cert = serverContext.certificates[0];
    Bytes certificates[] = {
      {cert.data(), cert.length()}
	  };
    ZuCheckRT(!initCompressedCert(&emitter, certificates, {}));
    serverContext.emitCertificate = &emitter;
    clientContext.decompressCertificate = &decompressCertificate;
  }
#endif

  Ticket ticket;
  uint8_t cookieKey[Ztls_MaxDigestSize]{};
  for (unsigned pass = 0; pass < 6; ++pass) {
    bool retry = pass >= 2, cookie = pass == 3 || pass == 5, rejectBinder = pass >= 4;
    Peer client, server;
    client.ticket = &ticket;
    client.tls.initialize(&clientContext, false, &client);
    server.tls.initialize(&serverContext, true, &server);
    size_t earlyLimit = 0;
    if (pass) {
      client.tls.handshakeProps.client.sessionTicket = wireBytes(ticket.data(), ticket.length());
      client.tls.handshakeProps.client.maxEarlyDataSize = &earlyLimit;
    }
    server.tls.handshakeProps.server.enforceRetry = retry;
    if (cookie) {
      server.tls.handshakeProps.server.cookie.key = cookieKey;
      server.tls.handshakeProps.server.retryUsesCookie = 1;
    }

    Flight c, s;
    bool complete = exchange(
      client, server, c, s, retry, cookie ? cookieKey : nullptr, rejectBinder);
    ZuCheckRT(
      complete,
      ZuTestUtil::log_(
	"raw exchange pass ", pass, ": client status ", c.result.status, ", server status ",
	s.result.status));
    if (!complete) return;
    if (rejectBinder) {
      ZuCheckRT(!earlyLimit &&
	client.tls.handshakeProps.client.earlyDataAcceptance == EarlyDataAcceptance::Rejected);
      continue;
    }
    ZuCheckRT(
      ticket.length() && bool(isPskHandshake(&client.tls)) == bool(pass) &&
      bool(isPskHandshake(&server.tls)) == bool(pass));
    if (retry)
      ZuCheckRT(!earlyLimit &&
	client.tls.handshakeProps.client.earlyDataAcceptance == EarlyDataAcceptance::Rejected);

    unsigned active = (1U << Handshake) | (1U << Application);
    ZuCheckRT(
      client.traffic[0] == active &&
      client.traffic[1] == (active | (pass ? 1U << EarlyData : 0)) &&
      server.traffic[0] == (active | (pass == 1 ? 1U << EarlyData : 0)) &&
      server.traffic[1] == active);

    uint8_t secret[sizeof(client.tls.rxTraffic.secret)];
    memcpy(secret, client.tls.rxTraffic.secret, sizeof(secret));
    uint8_t update[] = {Ztls_HsTypeKeyUpdate, 0, 0, 1, 0};
    auto    status = c.drive(client, RawHandshakeOp::Input, Application, update);
    ZuCheckRT(
      Ztls_ErrorToAlert(status) == Ztls_AlertUnwaitedMsg && c.result.keyUpdateRejected &&
      c.empty() && !memcmp(secret, client.tls.rxTraffic.secret, sizeof(secret)));
    ZuClear(secret, sizeof(secret));
  }
}

#if ZtlsOpenSSLHaveX25519
static void echHandshake(bool accept)
{
  ZuTestScopeRT(echHandshake);
  Context clientContext{}, serverContext{};
  configure(clientContext);
  configure(serverContext);
  Ztls::PK::Key key;
  bool		haveCert = Ztls::PK::rsaGenerate(key, 2048) && certificate(serverContext, key);
  ZuCheckRT(haveCert);
  if (!haveCert) return;
  auto	  signer = Ztls::signCertNew(key);
  auto	  tickets = Ztls::ticketKeyNew();
  ZuGuard credentials{[signer, tickets]()
  {
    Ztls::ticketKeyFree(tickets);
    Ztls::signCertFree(signer);
  }};
  ZuCheckRT(signer && tickets);
  if (!signer || !tickets) return;
  serverContext.signCertificate = Ztls::signCertHook(signer);
  serverContext.encryptTicket = Ztls::ticketEncryptHook(tickets);
  serverContext.ticketLifetime = 60;

  const HpkeKem	  *kems[] = {&opensslHPKEX25519sha256, nullptr};
  const HpkeSuite *const choices[] = {&opensslHPKEAes128gcmsha256};
  const HpkeSuites ciphers{choices};
  clientContext.ech.client.kems = kems;
  clientContext.ech.client.ciphers = &ciphers;
  KeyExchangeContext *receiver = nullptr;
  ZuCheckRT(!kems[0]->keyex->create(&receiver) && receiver);
  if (!receiver) return;
  ZuGuard receiverKey{[receiver]() { delete receiver; }};
  uint8_t storage[1024];
  Buffer  config;
  bufferInitTx(&config, storage, sizeof(storage));
  ZuGuard configBytes{[&config]() { bufferDispose(&config); }};
  config.off = 2;
  enum { ConfigID = 42 };
  ZuCheckRT(!echEncodeConfig(
    &config, ConfigID, kems[0], receiver->pubkey, ciphers, 32, "public.example"));
  config.base[0] = uint8_t((config.off - 2) >> 8);
  config.base[1] = uint8_t(config.off - 2);

  struct Opener : EchCreateOpener {
    KeyExchangeContext	*receiver;
    Bytes		config;
    bool		accept;
    unsigned		calls = 0;
    int			result = Ztls_ErrorNotAvailable;

    Opener(KeyExchangeContext *key, Bytes bytes, bool enabled)
    :
      receiver{key}, config{bytes}, accept{enabled}
    { }
    AeadContext *call(
      const HpkeKem **kem, const HpkeSuite **cipher, Ztls::Cxn *, uint8_t id, HpkeSuiteID suite,
      Bytes enc, Bytes prefix) override
    {
      ++calls;
      if (!accept || id != ConfigID || suite.kdf != opensslHPKEAes128gcmsha256.id.kdf ||
	suite.aead != opensslHPKEAes128gcmsha256.id.aead)
	return nullptr;
      *kem = &opensslHPKEX25519sha256;
      *cipher = &opensslHPKEAes128gcmsha256;
      ZtBArray<ZtArrayHeapID<"Ztls.Test.EchInfo">> info;
      info.append(prefix.data(), prefix.length());
      info.append(config.data(), config.length());
      AeadContext *aead = nullptr;
      result = hpkeSetupBaseR(*kem, *cipher, receiver, &aead, enc, {info.data(), info.length()});
      return result ? nullptr : aead;
    }
  } opener{
    receiver, {config.base + 2, config.off - 2},
	 accept
  };
  struct ExtensionSpy : OnExtension {
    unsigned	calls = 0;
    int call(Ztls::Cxn *, uint8_t, uint16_t, Bytes) override
    {
      ++calls;
      return 0;
    }
  } extensions;
  struct HelloSpy : OnClientHello {
    unsigned	calls = 0;
    int call(Ztls::Cxn *tls, CliHelloParams *params) override
    {
      ++calls;
      return serverName(tls, params->serverName);
    }
  } hello;
  serverContext.ech.server.createOpener = &opener;
  serverContext.onExtension = &extensions;
  serverContext.onClientHello = &hello;
  Peer client, server;
  client.tls.initialize(&clientContext, false, &client);
  server.tls.initialize(&serverContext, true, &server);
  ZuCheckRT(!serverName(&client.tls, "private.example"));
  client.tls.handshakeProps.client.ech.configs = {config.base, config.off};
  Flight c, s;
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Start)));
  ZuCheckRT(accepted(s.drive(server, RawHandshakeOp::Input, Initial, c.bytes(Initial))));
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Input, Initial, s.bytes(Initial))));
  int  status = c.drive(client, RawHandshakeOp::Input, Handshake, s.bytes(Handshake));
  bool complete = false;
  if (accept && !status) {
    ZuCheckRT(!s.drive(server, RawHandshakeOp::Input, Handshake, c.bytes(Handshake)));
    complete = handshakeComplete(&client.tls) && handshakeComplete(&server.tls);
  }
  ZuCheckRT(opener.calls == 1 && extensions.calls && hello.calls == 1);
  if (accept) {
    ZuCheckRT(
      complete && !opener.result,
      log_(
	"ECH accept=", accept, " hpke=", opener.result, " cli=", c.result.status,
	" srv=", s.result.status, " cliECH=", int(client.tls.ech.state), " srvECH=",
	int(server.tls.ech.state), " serverName=", ZuCSpan{server.tls.serverName.data()}));
    ZuCheckRT(
      client.tls.ech.state == Ztls_::EchState::Accepted &&
      server.tls.ech.state == Ztls_::EchState::Accepted);
    ZuCheckRT(ZuCSpan{server.tls.serverName.data()} == "private.example");
  } else {
    ZuCheckRT(
      !complete && c.result.status == Ztls_AlertECHRequired && c.empty(),
      log_(
	"ECH reject hpke=", opener.result, " cli=", c.result.status, " srv=", s.result.status,
	" srvECH=", int(server.tls.ech.state),
	" serverName=", ZuCSpan{server.tls.serverName.data()}));
    ZuCheckRT(
      server.tls.ech.state != Ztls_::EchState::Accepted &&
      ZuCSpan{server.tls.serverName.data()} == "public.example");
  }
}
#endif

static void listGrowth(unsigned extra)
{
  ZuTestScopeRT(listGrowth);
  Context clientContext{}, serverContext{};
  configure(clientContext);
  configure(serverContext);
  Ztls::PK::Key key;
  if (!Ztls::PK::rsaGenerate(key, 2048)) {
    ZuCheckRT(false);
    return;
  }
  auto	  signer = Ztls::signCertNew(key);
  ZuGuard cleanup{[signer]() { Ztls::signCertFree(signer); }};
  ZuCheckRT(signer);
  if (!signer) return;
  serverContext.signCertificate = Ztls::signCertHook(signer);

  struct Verifier : VerifyCertificate {
    bool	seen = false;
    int call(Ztls::Cxn *, const char *, VerifySignature **, ZuSpan<const Bytes> certs) override
    {
      seen = certs.length() && certs[0].length() == 2 && certs[0][0] == certs.length();
      for (unsigned i = 0; seen && i < certs.length(); ++i)
	seen = certs[i].length() == 2 && certs[i][1] == i;
      return Ztls_AlertBadCert;
    }
  } verifier;
  uint16_t signatures[] = {Ztls_SigRSAPSSRSAESHA256};
  verifier.algos = signatures;
  clientContext.verifyCertificate = &verifier;
  unsigned count = ZtlsCertChainBuiltin + extra;
  for (unsigned i = 0; i < count; ++i) {
    uint8_t marker[] = {uint8_t(count), uint8_t(i)};
    serverContext.certificates.push(CertificateDER{marker, sizeof(marker)});
  }
  uint16_t ids[ZtlsCertCompBuiltin + 1];
  count = ZtlsCertCompBuiltin + extra;
  for (unsigned i = 0; i < count; ++i) ids[i] = 0xf000 + i;
  ids[count - 1] = Ztls_CertCompAlgoBrotli;
  struct AdvertisedCompression : DecompressCertificate {
    int call(Ztls::Cxn *, uint16_t, Bytes, Bytes) override { return Ztls_AlertBadCert; }
  } decompress;
  decompress.supportedAlgorithms = {ids, count};
  clientContext.decompressCertificate = &decompress;
  struct Emitter : EmitCertificate {
    ZuSpan<const uint16_t>	expected;
    bool			seen = false;
    int call(
      Ztls::Cxn *, MessageEmitter *, KeySchedule *, Bytes, int,
      ZuSpan<const uint16_t> offers) override
    {
      seen = offers == expected;
      return Ztls_ErrorDelegate;
    }
  } emitter;
  emitter.expected = decompress.supportedAlgorithms;
  serverContext.emitCertificate = &emitter;
  Peer	 client, server;
  Flight c, s;
  client.tls.initialize(&clientContext, false, &client);
  server.tls.initialize(&serverContext, true, &server);
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Start)));
  ZuCheckRT(accepted(s.drive(server, RawHandshakeOp::Input, Initial, c.bytes(Initial))));
  ZuCheckRT(emitter.seen);
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Input, Initial, s.bytes(Initial))));
  ZuCheckRT(
    c.drive(client, RawHandshakeOp::Input, Handshake, s.bytes(Handshake)) == Ztls_AlertBadCert);
  ZuCheckRT(verifier.seen);
}

struct VerifyLifetime {
  enum { Success, BadSignature, UnexpectedMessage, Retire, Reinitialize, RejectCertificate };
};

struct VerifyObservation {
  unsigned	releases = 0;
  unsigned	verifies = 0;
};

struct ObservedSignature : ZmNHeap<ObservedSignature>, VerifySignature {
friend inline ZuStringT<"Ztls.Test.VerifySignature"> ZmNHeapID(ObservedSignature *);
  VerifyObservation	&observation;

  explicit ObservedSignature(VerifyObservation &state) : observation{state} { }
  ~ObservedSignature() override { ++observation.releases; }
  int verify(uint16_t, Bytes data, Bytes signature) override
  {
    ++observation.verifies;
    return data.length() && signature.length() ? 0 : Ztls_AlertDecodeError;
  }
};

static void verifierLifetime(unsigned mode)
{
  ZuTestScopeRT(verifierLifetime);
  Context clientContext{}, serverContext{};
  configure(clientContext);
  configure(serverContext);
  Ztls::PK::Key key;
  bool		haveCert = Ztls::PK::rsaGenerate(key, 2048) && certificate(serverContext, key);
  ZuCheckRT(haveCert);
  if (!haveCert) return;
  auto	  signer = Ztls::signCertNew(key);
  ZuGuard cleanup{[signer]() { Ztls::signCertFree(signer); }};
  ZuCheckRT(signer);
  if (!signer) return;
  serverContext.signCertificate = Ztls::signCertHook(signer);
  VerifyObservation observation;
  struct Verifier : VerifyCertificate {
    VerifyObservation	&observation;
    bool		reject;
    Verifier(VerifyObservation &state, bool reject_) : observation{state}, reject{reject_} { }
    int call(Ztls::Cxn *, const char *, VerifySignature **signature, ZuSpan<const Bytes>) override
    {
      *signature = new ObservedSignature{observation};
      return reject ? Ztls_AlertBadCert : 0;
    }
  } verifier{observation, mode == VerifyLifetime::RejectCertificate};
  uint16_t algorithms[] = {Ztls_SigRSAPSSRSAESHA256};
  verifier.algos = algorithms;
  clientContext.verifyCertificate = &verifier;
  Peer	 client, server;
  Flight c, s;
  client.tls.initialize(&clientContext, false, &client);
  server.tls.initialize(&serverContext, true, &server);
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Start)));
  ZuCheckRT(accepted(s.drive(server, RawHandshakeOp::Input, Initial, c.bytes(Initial))));
  ZuCheckRT(accepted(c.drive(client, RawHandshakeOp::Input, Initial, s.bytes(Initial))));
  auto	 flight = s.bytes(Handshake);
  size_t certificateEnd = 0;
  while (certificateEnd + 4 <= flight.length()) {
    auto   message = flight.data() + certificateEnd;
    size_t length = 4 + (unsigned(message[1]) << 16) + (unsigned(message[2]) << 8) + message[3];
    if (length > flight.length() - certificateEnd) break;
    certificateEnd += length;
    if (*message == Ztls_HsTypeCert) break;
  }
  ZuCheckRT(certificateEnd && certificateEnd < flight.length());
  int status = c.drive(client, RawHandshakeOp::Input, Handshake, {flight.data(), certificateEnd});
  if (mode == VerifyLifetime::RejectCertificate) {
    ZuCheckRT(status == Ztls_AlertBadCert && observation.releases == 1);
  } else {
    ZuCheckRT(status == Ztls_ErrorInProgress && client.tls.certificateVerify);
    ZuCheckRT(!observation.releases && !observation.verifies);
    switch (mode) {
    case VerifyLifetime::Success:
      ZuCheckRT(accepted(c.drive(
	client, RawHandshakeOp::Input, Handshake,
	{flight.data() + certificateEnd, flight.length() - certificateEnd})));
      ZuCheckRT(handshakeComplete(&client.tls) && observation.verifies == 1);
      break;
    case VerifyLifetime::BadSignature: {
      uint8_t malformed[] = {Ztls_HsTypeCertVerify, 0, 0, 1, 0};
      ZuCheckRT(
	c.drive(client, RawHandshakeOp::Input, Handshake, malformed) == Ztls_AlertDecodeError);
    } break;
    case VerifyLifetime::UnexpectedMessage: {
      uint8_t unexpected[] = {Ztls_HSTypeFinished, 0, 0, 0};
      ZuCheckRT(
	c.drive(client, RawHandshakeOp::Input, Handshake, unexpected) == Ztls_AlertUnwaitedMsg);
    } break;
    case VerifyLifetime::Retire:
      client.tls.retireRx();
      break;
    case VerifyLifetime::Reinitialize:
      client.tls.initialize(&clientContext, false, &client);
      break;
    }
    ZuCheckRT(!client.tls.certificateVerify && observation.releases == 1);
  }
  client.tls.retireRx();
  ZuCheckRT(observation.releases == 1);
}

static void wrongEpoch()
{
  ZuTestScope(wrongEpoch);
  Context context{};
  configure(context);
  Peer client, server;
  client.tls.initialize(&context, false, &client);
  server.tls.initialize(&context, true, &server);
  Flight c, s;
  auto	 status = c.drive(client, RawHandshakeOp::Start);
  bool	 ready = accepted(status) && c.valid() && c.bytes(Initial).length();
  ZuCHECK(ready, "no raw ClientHello for epoch mismatch test: ", status);
  if (!ready) return;
  status = s.drive(server, RawHandshakeOp::Input, Handshake, c.bytes(Initial));
  ZuCHECK(
    Ztls_ErrorToAlert(status) == Ztls_AlertUnwaitedMsg && !s.result.keyUpdateRejected &&
      s.empty() && rawReadEpoch(&server.tls) == Initial && !server.tls.recvbuf.mess.off &&
      !server.traffic[0] && !server.traffic[1],
    "wrong-epoch input changed the receive state: ", status);
}

} // namespace RawHandshakeTest

int main(int argc, char **argv)
{
  ZuTestUtil::parse(argc, argv);
  ZuTestMain();
  bool initialized = Ztls::cryptoInit();
  ZuCHECK(initialized, "Ztls initialization failed");
  if (!initialized) return 1;
  using namespace RawHandshakeTest;
  ZuTestCall(listGrowth, 0);
  ZuTestCall(listGrowth, 1);
  ZuTestCall(outputBounds);
  ZuTestCall(wrongEpoch);
  ZuTestCall(verifierLifetime, VerifyLifetime::Success);
  ZuTestCall(verifierLifetime, VerifyLifetime::BadSignature);
  ZuTestCall(verifierLifetime, VerifyLifetime::UnexpectedMessage);
  ZuTestCall(verifierLifetime, VerifyLifetime::Retire);
  ZuTestCall(verifierLifetime, VerifyLifetime::Reinitialize);
  ZuTestCall(verifierLifetime, VerifyLifetime::RejectCertificate);
#if ZtlsOpenSSLHaveX25519
  ZuTestCall(echHandshake, true);
  ZuTestCall(echHandshake, false);
#endif
  ZuTestCall(handshake);
#if ZtlsUseBrotli
  ZuTestCall(handshake, true);
#endif
}
