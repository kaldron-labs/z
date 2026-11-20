//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z TLS library

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <stdio.h>
#include <string.h>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtlsEngine.hh>

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif
Ztls_Extern const char ZtlsLib[] = "@(#) Z TLS Library v" Z_VERNAME;

namespace Ztls {

bool cryptoInit()
{
  static const bool done = OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG, nullptr) == 1;
  return done;
}

static const char *alert_string(int alert)
{
  switch (alert) {
  case Ztls_AlertCloseNotify:
    return "close_notify";
  case Ztls_AlertUnwaitedMsg:
    return "unexpected_message";
  case Ztls_AlertBadRecordMac:
    return "bad_record_mac";
  case Ztls_AlertHSFailure:
    return "handshake_failure";
  case Ztls_AlertBadCert:
    return "bad_certificate";
  case Ztls_AlertUnsupportedCert:
    return "unsupported_certificate";
  case Ztls_AlertCertRevoked:
    return "certificate_revoked";
  case Ztls_AlertCertExpired:
    return "certificate_expired";
  case Ztls_AlertCertUnknown:
    return "certificate_unknown";
  case Ztls_AlertIllegalParameter:
    return "illegal_parameter";
  case Ztls_AlertUnknownCa:
    return "unknown_ca";
  case Ztls_AlertAccessDenied:
    return "access_denied";
  case Ztls_AlertDecodeError:
    return "decode_error";
  case Ztls_AlertDecryptError:
    return "decrypt_error";
  case Ztls_AlertProtoVer:
    return "protocol_version";
  case Ztls_AlertInternalError:
    return "internal_error";
  case Ztls_AlertUserCanceled:
    return "user_canceled";
  case Ztls_AlertMissingExt:
    return "missing_extension";
  case Ztls_AlertUnsupportedExt:
    return "unsupported_extension";
  case Ztls_AlertUnrecognizedName:
    return "unrecognized_name";
  case Ztls_AlertUnknownPSKId:
    return "unknown_psk_identity";
  case Ztls_AlertCertRequired:
    return "certificate_required";
  case Ztls_AlertNoAppProto:
    return "no_application_protocol";
  case Ztls_AlertECHRequired:
    return "ech_required";
  }
  return nullptr;
}

static const char *engine_error_string(int err)
{
  switch (err) {
  case Ztls_ErrorNoMemory:
    return "no_memory";
  case Ztls_ErrorInProgress:
    return "in_progress";
  case Ztls_ErrorLibrary:
    return "library_error";
  case Ztls_ErrorIncompatibleKey:
    return "incompatible_key";
  case Ztls_ErrorSessionNotFound:
    return "session_not_found";
  case Ztls_ErrorStatelessRetry:
    return "stateless_retry";
  case Ztls_ErrorNotAvailable:
    return "not_available";
  case Ztls_ErrorCompFailure:
    return "compression_failure";
  case Ztls_ErrorRejectEarlyData:
    return "reject_early_data";
  case Ztls_ErrorDelegate:
    return "delegate";
  case Ztls_ErrorAsyncOperation:
    return "async_operation";
  case Ztls_ErrorBlockOverflow:
    return "block_overflow";
  }
  return nullptr;
}

void clearTLSError() { ERR_clear_error(); }

ZeException
tlsError(const char *operation, int code, const char *file, unsigned line, const char *function)
{
  auto provider = code == Ztls_ErrorLibrary ? ERR_peek_last_error() : 0UL;
  return ZeMkException(
    Ze::Error, file, line, function, "Ztls",
    [operation = ZeString{operation}, code, provider](auto &s)
  {
    s << operation << ": " << code;
    auto name = Ztls_ErrorGetClass(code) == Ztls_ErrorClassSelfAlert ||
	Ztls_ErrorGetClass(code) == Ztls_ErrorClassPeerAlert ?
      alert_string(Ztls_ErrorToAlert(code)) :
      engine_error_string(code);
    if (name) s << " (" << name << ')';
    if (provider) {
      char detail[256];
      ERR_error_string_n(provider, detail, sizeof(detail));
      s << ": " << detail;
    }
  });
}

Ztls_Extern void init()
{
  if (!cryptoInit()) throw ZeEXCEPT(Error, "Ztls", "crypto initialization failed");
}

} // namespace Ztls
