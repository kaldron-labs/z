//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtlsCreds.hh>

#include <zlib/ZtlsPK.hh>

namespace Ztls {

ClientCreds::ClientCreds() = default;

ClientCreds::ClientCreds(ZmRef<PK::AnyPK> key, Certs certs) :
    m_key{ZuMv(key)} {
  static_cast<Certs::Base &>(m_certs) =
    ZuMv(static_cast<Certs::Base &>(certs));
}

ClientCreds::~ClientCreds() { }

ClientCreds::ClientCreds(ClientCreds &&o) : m_key{ZuMv(o.m_key)} {
  static_cast<Certs::Base &>(m_certs) =
    ZuMv(static_cast<Certs::Base &>(o.m_certs));
}

ClientCreds &ClientCreds::operator =(ClientCreds &&o) {
  if (this == &o) return *this;
  static_cast<Certs::Base &>(m_certs) =
    ZuMv(static_cast<Certs::Base &>(o.m_certs));
  m_key = ZuMv(o.m_key);
  return *this;
}

ClientCreds::operator bool() const { return !!m_key; }
const Certs &ClientCreds::certs() const { return m_certs; }

Backend::PKey *ClientCreds::pkey_() const {
  return m_key ? m_key->key : nullptr;
}

void ClientCreds::clearCerts_() { m_certs.null(); }

} // namespace Ztls
