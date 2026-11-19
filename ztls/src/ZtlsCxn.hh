//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Protocol state for one TLS connection

#ifndef ZtlsCxn_HH
#define ZtlsCxn_HH

#include <openssl/types.h>

#include <zlib/ZmFn.hh>
#include <zlib/ZmLib.hh>
#include <zlib/ZtlsBuf.hh>

struct async_job_st;
struct async_wait_ctx_st;

namespace Ztls_ {

struct TrafficProtection {
    uint8_t secret[Ztls_MAX_DIGEST_SIZE];
    size_t epoch;
    /* the following fields are not used if the key_change callback is set */
    aead_context_t *aead;
    uint64_t seq;
    unsigned failed : 1;
};

struct SignatureAlgorithms {
    uint16_t list[Ztls_MAX_SIGNATURE_ALGORITHMS];
    size_t count;
};

struct CertificateRequest {
    /**
     * context.base becomes non-NULL when a CertificateRequest is pending for processing
     */
    Bytes context;
    SignatureAlgorithms signature_algorithms;
};

/**
 * Properties for ECH. Iff ECH is used and not rejected, `aead` is non-NULL.
 */
enum en_ech_state_t {
    Ztls_ECH_STATE_NONE = 0,
    Ztls_ECH_STATE_OFFERED,
    Ztls_ECH_STATE_ACCEPTED,
    Ztls_ECH_STATE_GREASE
};

ZuDerive(EchPublicName, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.ECH.PublicName">>));

struct Ech {
    /**
     * ECH state for this connection. `OFFERED` and `ACCEPTED` are used on both client and server; `GREASE` is client-only (server
     * cannot distinguish GREASE from a config mismatch, both are simply ECH that fails to decrypt).
     */
    en_ech_state_t state;
    uint8_t config_id;
    hpke_kem_t *kem;
    hpke_cipher_suite_t *cipher;
    aead_context_t *aead;
    uint8_t inner_client_random[Ztls_HELLO_RANDOM_SIZE];
    struct {
        KeyExchangeBytes enc;
        uint8_t max_name_length;
        EchPublicName public_name;
        /**
         * retains a copy of entire ECH extension so that it can be replayed in the 2nd CH when ECH is rejected via HRR
         */
        EchBytes first_ech;
    } client;
};

/* One receive operation, owned by the connection's serial receive caller. */
struct StreamRecord {
    uint8_t header[5], tag[16];
    size_t header_off, tag_off, tag_size;
    size_t length, text_length, written;
    uint8_t *base;
    size_t capacity;
    unsigned encrypted : 1;
    unsigned begun : 1;
    unsigned reserved : 1;
    unsigned failed : 1;
};

enum en_state_t {
    Ztls_STATE_CLIENT_HANDSHAKE_START,
    Ztls_STATE_CLIENT_EXPECT_SERVER_HELLO,
    Ztls_STATE_CLIENT_EXPECT_SECOND_SERVER_HELLO,
    Ztls_STATE_CLIENT_EXPECT_ENCRYPTED_EXTENSIONS,
    Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_REQUEST_OR_CERTIFICATE,
    Ztls_STATE_CLIENT_EXPECT_CERTIFICATE,
    Ztls_STATE_CLIENT_EXPECT_CERTIFICATE_VERIFY,
    Ztls_STATE_CLIENT_EXPECT_FINISHED,
    Ztls_STATE_SERVER_EXPECT_CLIENT_HELLO,
    Ztls_STATE_SERVER_EXPECT_SECOND_CLIENT_HELLO,
    Ztls_STATE_SERVER_GENERATING_CERTIFICATE_VERIFY,
    Ztls_STATE_SERVER_EXPECT_CERTIFICATE,
    Ztls_STATE_SERVER_EXPECT_CERTIFICATE_VERIFY,
    /* send can be called if the state is below here */
    Ztls_STATE_SERVER_EXPECT_END_OF_EARLY_DATA,
    Ztls_STATE_SERVER_EXPECT_FINISHED,
    Ztls_STATE_POST_HANDSHAKE_MIN,
    Ztls_STATE_CLIENT_POST_HANDSHAKE = Ztls_STATE_POST_HANDSHAKE_MIN,
    Ztls_STATE_SERVER_POST_HANDSHAKE
};

ZuDerive(ServerName, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.SNI">>));
ZuDerive(NegotiatedProtocol, (ZtCArray<ZtArrayHeapID<"Ztls.Engine.ALPN">>));
ZuDerive(CertificateRequestBytes, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CertificateRequest">>));
ZuDerive(HandshakeSecret, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.HandshakeSecret">>>));
ZuDerive(ExporterSecret, (ZtBArray<ZtArraySecret<true, ZtArrayHeapID<"Ztls.Engine.ExporterSecret">>>));

} // namespace Ztls_

namespace Ztls {

ZuDerive(AsyncSignature, (ZtBArray<ZtArrayHeapID<"Ztls.Handshake.Signature">>));

class ZtlsAPI Cxn {
  Cxn(const Cxn &) = delete;
  Cxn &operator =(const Cxn &) = delete;

public:
  Cxn() = default;
  Cxn(Ztls_::context_t *ctx, bool server) { initialize(ctx, server); }
  ~Cxn();

  // Initialize before publishing the connection. Reinitialization requires
  // both directions to be idle.
  void initialize(Ztls_::context_t *, bool server, void *owner = nullptr);
  void retireRx();
  void retireTx();
  void retireSignature();

  using SignatureResume = ZmFn<int(Ztls_::buffer_t *, uint16_t *),
    ZmFnHeapID<"Ztls.Handshake.SignatureResume">>;
  using SignatureRetire = ZmFn<void(), ZmFnHeapID<"Ztls.Handshake.SignatureRetire">>;

  // Published before use and stable throughout active transport.
  Ztls_::context_t	*ctx = nullptr;
  void			*data_ptr = nullptr;
  bool			is_server = false;

  // Selected by Rx during handshake and read by Tx after handoff.
  Ztls_::cipher_suite_t	*cipher_suite = nullptr;

  // Rx owns handshake, receive state, and optional exporter secrets.
  alignas(Zm::CacheLineSize)
  Ztls_::en_state_t	state{};
  Ztls_::TrafficProtection rx_traffic{};
  struct {
    Ztls_::buffer_t	rec, mess;
  } recvbuf{};
  Ztls_::StreamRecord stream_rec{};
  Ztls_::key_schedule_t	*key_schedule = nullptr;
  Ztls_::ServerName	server_name;
  Ztls_::NegotiatedProtocol negotiated_protocol;
  Ztls_::key_exchange_algorithm_t *key_share = nullptr;
  uint8_t		client_random[Ztls_HELLO_RANDOM_SIZE]{};
  struct {
    Ztls_::ExporterSecret early, one_rtt;
  } exporter_master_secret;
  Ztls_::Ech	ech{};
  bool			rx_active = false;
  bool			is_psk_handshake = false;
  bool			send_change_cipher_spec = false;
  bool			tx_handed_off = false;
  uint32_t		key_update_requests = 0;
  Ztls_::Bytes		legacy_session_id{};
  uint8_t		legacy_session_id_buf[32]{};
  Ztls_::key_exchange_context_t *key_share_ctx = nullptr;
  bool			offered_psk = false;
  bool			using_early_data = false;
  Ztls_::CertificateRequest certificate_request{};
  uint8_t		pending_traffic_secret[Ztls_MAX_DIGEST_SIZE]{};
  uint32_t		early_data_skipped_bytes = UINT32_MAX;
  uint8_t		num_tickets_to_send = 0;
  SignatureResume	signature_resume;
  SignatureRetire	signature_retire;
  int			signature_fd = -1;
  EVP_MD_CTX		*signature_digest = nullptr;
  async_wait_ctx_st	*signature_wait = nullptr;
  async_job_st		*signature_job = nullptr;
  AsyncSignature	signature;
  size_t		signature_length = 0;
  uint16_t		signature_algorithm = 0;
  struct {
    int (*cb)(void *, uint16_t, Ztls_::Bytes, Ztls_::Bytes);
    void *verify_ctx;
  } certificate_verify{};
  Ztls_::HandshakeSecret pending_handshake_secret;
  Ztls_::CertificateRequestBytes certificate_request_bytes;
  Ztls_::handshake_properties_t handshake_props{};
  ZmRef<ZiIOBuf>	rx_input;
  PlaintextSink		plaintext;
  bool			rx_paused = false;

  // Rx uses these members while negotiating. The handoff assigns them to Tx.
  alignas(Zm::CacheLineSize)
  Ztls_::TrafficProtection tx_traffic{};
  bool			needs_key_update = false;
  bool			key_update_send_request = false;
  bool			tx_closing = false;
  uint64_t		tx_generation = 0;
};

} // namespace Ztls

#endif /* ZtlsCxn_HH */
