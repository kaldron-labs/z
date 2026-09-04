//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZtlsCreds_HH
#define ZtlsCreds_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuDerive.hh>

#include <zlib/ZmRef.hh>

#include <zlib/ZtArray.hh>

namespace Ztls {

namespace Backend { struct PKey; }
namespace PK { struct AnyPK; }

ZuDerive(Cert, (ZtArray<uint8_t, ZtArrayHeapID<"Ztls.Cert">>));
ZuDerive(Certs, (ZtArray<Cert, ZtArrayHeapID<"Ztls.Certs">>));

class ZtlsAPI ClientCreds {
public:
  ClientCreds();
  ClientCreds(ZmRef<PK::AnyPK> key, Certs certs);
  ~ClientCreds();

  ClientCreds(const ClientCreds &) = delete;
  ClientCreds &operator =(const ClientCreds &) = delete;
  ClientCreds(ClientCreds &&);
  ClientCreds &operator =(ClientCreds &&);

  explicit operator bool() const;
  const Certs &certs() const;

private:
  template <typename> friend class Client;

  Backend::PKey *pkey_() const;
  void clearCerts_();

  Certs			m_certs;
  ZmRef<PK::AnyPK>	m_key;
};

} // namespace Ztls

#endif /* ZtlsCreds_HH */
