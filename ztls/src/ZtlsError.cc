//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TLS engine diagnostics with a captured libcrypto error

#include <openssl/err.h>

#include <zlib/ZtlsEngine.hh>
#include <zlib/ZtlsError.hh>

namespace Ztls {

static const char *alertName(int alert)
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

static const char *errorName(int err)
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

Error::Error(ZuCSpan operation_, int code_)
:
  operation{operation_}, provider{code_ == Ztls_ErrorLibrary ? ERR_peek_last_error() : 0UL},
  code{code_}
{ }

void Error::operator()(ZeLogBuf &s) const
{
  s << operation << ": " << code;
  auto name = Ztls_ErrorGetClass(code) == Ztls_ErrorClassSelfAlert ||
	Ztls_ErrorGetClass(code) == Ztls_ErrorClassPeerAlert ?
    alertName(Ztls_ErrorToAlert(code)) :
    errorName(code);
  if (name) s << " (" << name << ')';
  if (provider) {
    char detail[256];
    ERR_error_string_n(provider, detail, sizeof(detail));
    s << ": " << detail;
  }
}

} // namespace Ztls
