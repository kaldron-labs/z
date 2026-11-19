//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS library

#include <zlib/ZtlsLib.hh>

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <zlib/ZtlsEngine.hh>

#include <stdio.h>
#include <string.h>

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
    case Ztls_ALERT_CLOSE_NOTIFY: return "close_notify";
    case Ztls_ALERT_UNEXPECTED_MESSAGE: return "unexpected_message";
    case Ztls_ALERT_BAD_RECORD_MAC: return "bad_record_mac";
    case Ztls_ALERT_HANDSHAKE_FAILURE: return "handshake_failure";
    case Ztls_ALERT_BAD_CERTIFICATE: return "bad_certificate";
    case Ztls_ALERT_UNSUPPORTED_CERTIFICATE: return "unsupported_certificate";
    case Ztls_ALERT_CERTIFICATE_REVOKED: return "certificate_revoked";
    case Ztls_ALERT_CERTIFICATE_EXPIRED: return "certificate_expired";
    case Ztls_ALERT_CERTIFICATE_UNKNOWN: return "certificate_unknown";
    case Ztls_ALERT_ILLEGAL_PARAMETER: return "illegal_parameter";
    case Ztls_ALERT_UNKNOWN_CA: return "unknown_ca";
    case Ztls_ALERT_ACCESS_DENIED: return "access_denied";
    case Ztls_ALERT_DECODE_ERROR: return "decode_error";
    case Ztls_ALERT_DECRYPT_ERROR: return "decrypt_error";
    case Ztls_ALERT_PROTOCOL_VERSION: return "protocol_version";
    case Ztls_ALERT_INTERNAL_ERROR: return "internal_error";
    case Ztls_ALERT_USER_CANCELED: return "user_canceled";
    case Ztls_ALERT_MISSING_EXTENSION: return "missing_extension";
    case Ztls_ALERT_UNSUPPORTED_EXTENSION: return "unsupported_extension";
    case Ztls_ALERT_UNRECOGNIZED_NAME: return "unrecognized_name";
    case Ztls_ALERT_UNKNOWN_PSK_IDENTITY: return "unknown_psk_identity";
    case Ztls_ALERT_CERTIFICATE_REQUIRED: return "certificate_required";
    case Ztls_ALERT_NO_APPLICATION_PROTOCOL: return "no_application_protocol";
    case Ztls_ALERT_ECH_REQUIRED: return "ech_required";
  }
  return nullptr;
}

static const char *engine_error_string(int err)
{
  switch (err) {
    case Ztls_ERROR_NO_MEMORY: return "no_memory";
    case Ztls_ERROR_IN_PROGRESS: return "in_progress";
    case Ztls_ERROR_LIBRARY: return "library_error";
    case Ztls_ERROR_INCOMPATIBLE_KEY: return "incompatible_key";
    case Ztls_ERROR_SESSION_NOT_FOUND: return "session_not_found";
    case Ztls_ERROR_STATELESS_RETRY: return "stateless_retry";
    case Ztls_ERROR_NOT_AVAILABLE: return "not_available";
    case Ztls_ERROR_COMPRESSION_FAILURE: return "compression_failure";
    case Ztls_ERROR_REJECT_EARLY_DATA: return "reject_early_data";
    case Ztls_ERROR_DELEGATE: return "delegate";
    case Ztls_ERROR_ASYNC_OPERATION: return "async_operation";
    case Ztls_ERROR_BLOCK_OVERFLOW: return "block_overflow";
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
  if (Ztls_ERROR_GET_CLASS(err) == Ztls_ERROR_CLASS_SELF_ALERT ||
      Ztls_ERROR_GET_CLASS(err) == Ztls_ERROR_CLASS_PEER_ALERT) {
    if (auto alert = alert_string(Ztls_ERROR_TO_ALERT(err))) {
      return snprintf(buf, len, "alert(%s)", alert);
    }
  }
  if (auto engineErr = engine_error_string(err)) {
    return snprintf(buf, len, "tls(%s)", engineErr);
  }
  return snprintf(buf, len, "error(%d)", err);
}

ZtlsExtern void init()
{
  if (!cryptoInit())
    throw ZeEXCEPT(Error, "Ztls", "crypto initialization failed");
}

} // namespace Ztls
