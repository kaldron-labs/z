//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS Library - zpicotls/OpenSSL wrapper

#include <zlib/ZtlsLib.hh>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <zpicotls.h>

#include <stdio.h>
#include <string.h>

#include <zlib/ZtlsPico.hh>
#include <zlib/ZePlatform.hh>

ZtlsExtern const char ZtlsLib[] = "@(#) Z TLS Library v" Z_VERNAME;

namespace Ztls {

bool cryptoInit()
{
  static const bool done =
    OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr) == 1;
  return done;
}

static const char *alert_string(int alert)
{
  switch (alert) {
    case PTLS_ALERT_CLOSE_NOTIFY: return "close_notify";
    case PTLS_ALERT_UNEXPECTED_MESSAGE: return "unexpected_message";
    case PTLS_ALERT_BAD_RECORD_MAC: return "bad_record_mac";
    case PTLS_ALERT_HANDSHAKE_FAILURE: return "handshake_failure";
    case PTLS_ALERT_BAD_CERTIFICATE: return "bad_certificate";
    case PTLS_ALERT_UNSUPPORTED_CERTIFICATE: return "unsupported_certificate";
    case PTLS_ALERT_CERTIFICATE_REVOKED: return "certificate_revoked";
    case PTLS_ALERT_CERTIFICATE_EXPIRED: return "certificate_expired";
    case PTLS_ALERT_CERTIFICATE_UNKNOWN: return "certificate_unknown";
    case PTLS_ALERT_ILLEGAL_PARAMETER: return "illegal_parameter";
    case PTLS_ALERT_UNKNOWN_CA: return "unknown_ca";
    case PTLS_ALERT_ACCESS_DENIED: return "access_denied";
    case PTLS_ALERT_DECODE_ERROR: return "decode_error";
    case PTLS_ALERT_DECRYPT_ERROR: return "decrypt_error";
    case PTLS_ALERT_PROTOCOL_VERSION: return "protocol_version";
    case PTLS_ALERT_INTERNAL_ERROR: return "internal_error";
    case PTLS_ALERT_USER_CANCELED: return "user_canceled";
    case PTLS_ALERT_MISSING_EXTENSION: return "missing_extension";
    case PTLS_ALERT_UNSUPPORTED_EXTENSION: return "unsupported_extension";
    case PTLS_ALERT_UNRECOGNIZED_NAME: return "unrecognized_name";
    case PTLS_ALERT_UNKNOWN_PSK_IDENTITY: return "unknown_psk_identity";
    case PTLS_ALERT_CERTIFICATE_REQUIRED: return "certificate_required";
    case PTLS_ALERT_NO_APPLICATION_PROTOCOL: return "no_application_protocol";
    case PTLS_ALERT_ECH_REQUIRED: return "ech_required";
  }
  return nullptr;
}

static const char *ptls_error_string(int err)
{
  switch (err) {
    case PTLS_ERROR_NO_MEMORY: return "no_memory";
    case PTLS_ERROR_IN_PROGRESS: return "in_progress";
    case PTLS_ERROR_LIBRARY: return "library_error";
    case PTLS_ERROR_INCOMPATIBLE_KEY: return "incompatible_key";
    case PTLS_ERROR_SESSION_NOT_FOUND: return "session_not_found";
    case PTLS_ERROR_STATELESS_RETRY: return "stateless_retry";
    case PTLS_ERROR_NOT_AVAILABLE: return "not_available";
    case PTLS_ERROR_COMPRESSION_FAILURE: return "compression_failure";
    case PTLS_ERROR_REJECT_EARLY_DATA: return "reject_early_data";
    case PTLS_ERROR_DELEGATE: return "delegate";
    case PTLS_ERROR_ASYNC_OPERATION: return "async_operation";
    case PTLS_ERROR_BLOCK_OVERFLOW: return "block_overflow";
  }
  return nullptr;
}

size_t format_error(int err, char *buf, size_t len)
{
  if (!buf || !len) return 0;
  buf[0] = '\0';
  unsigned long oe = ERR_peek_last_error();
  if (oe) {
    ERR_error_string_n(oe, buf, len);
    return strlen(buf);
  }
  if (PTLS_ERROR_GET_CLASS(err) == PTLS_ERROR_CLASS_SELF_ALERT ||
      PTLS_ERROR_GET_CLASS(err) == PTLS_ERROR_CLASS_PEER_ALERT) {
    if (auto alert = alert_string(PTLS_ERROR_TO_ALERT(err))) {
      return snprintf(buf, len, "alert(%s)", alert);
    }
  }
  if (auto ptls_err = ptls_error_string(err)) {
    return snprintf(buf, len, "zpicotls(%s)", ptls_err);
  }
  return snprintf(buf, len, "error(%d)", err);
}

ZtlsExtern void init()
{
  if (!Pico::init())
    throw ZeEXCEPT(Error, "Ztls", "crypto initialization failed");
}

} // namespace Ztls
