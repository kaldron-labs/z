//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// In-memory TLS 1.3 handshake for record and provider fixtures.

#ifndef ZtlsTestHandshake_HH
#define ZtlsTestHandshake_HH

#include <zlib/ZuLib.hh>

#include <zlib/ZtlsCxn.hh>
#include <zlib/ZtlsEngineOpenSSL.hh>

namespace ZtlsTest {

inline void useTestPSK(Ztls_::Context &ctx, const Ztls_::CipherSuite *suite)
{
  // Reuse the TLS 1.3 credentials from zpicotls's exact receive test.
  ctx.preSharedKey.identity = Ztls_::wire_bytes("inplace", 7);
  ctx.preSharedKey.secret = Ztls_::wire_bytes("exact receive capacity", 22);
  ctx.preSharedKey.hash = suite->hash;
}

inline bool
handshake(Ztls::Cxn *client, Ztls::Cxn *server, Ztls_::HandshakeProps *properties = nullptr)
{
  if (!client || !server) return false;
  Ztls_::Buffer request, response;
  uint8_t	requestStorage[1024], responseStorage[1024];
  Ztls_::buffer_init_tx(&request, requestStorage, sizeof(requestStorage));
  Ztls_::buffer_init_tx(&response, responseStorage, sizeof(responseStorage));
  ZuGuard dispose{[&request, &response]()
  {
    Ztls_::buffer_dispose(&response);
    Ztls_::buffer_dispose(&request);
  }};
  if (Ztls_::handshake(client, &request, nullptr, nullptr, properties) != Ztls_ErrorInProgress)
    return false;
  size_t consumed = request.off;
  int	 result = Ztls_::handshake(server, &response, request.base, &consumed, nullptr);
  if ((result && result != Ztls_ErrorInProgress) || consumed != request.off) return false;
  request.off = 0;
  consumed = response.off;
  if (Ztls_::handshake(client, &request, response.base, &consumed, properties)) return false;
  if (consumed != response.off) {
    auto	  remaining = response.off - consumed;
    auto	  read = remaining;
    Ztls_::Buffer plaintext;
    uint8_t	  small;
    Ztls_::buffer_init_rx(&plaintext, &small, 0);
    auto status = Ztls_::receive(client, &plaintext, response.base + consumed, &read);
    bool received = !status && read == remaining && !plaintext.off;
    Ztls_::buffer_dispose(&plaintext);
    if (!received) return false;
  }
  response.off = 0;
  consumed = request.off;
  return !Ztls_::handshake(server, &response, request.base, &consumed, nullptr) &&
    consumed == request.off && Ztls_::handshake_is_complete(client) &&
    Ztls_::handshake_is_complete(server);
}

} // namespace ZtlsTest

#endif // ZtlsTestHandshake_HH
