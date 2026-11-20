//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Independent temporary certificate paths for packet integration tests.

#ifndef ZtlsPacketCert_HH
#define ZtlsPacketCert_HH

#include <stdlib.h>
#include <unistd.h>

#include <zlib/ZtString.hh>

struct ZtlsPacketCert {
  using Path = ZtString<ZtStringHeapID<"Ztls.ITest.CertPath">>;
  using Command = ZtString<ZtStringHeapID<"Ztls.ITest.CertCommand">>;

  char	dir[sizeof("/tmp/ztlspackettest.XXXXXX")] = "/tmp/ztlspackettest.XXXXXX";
  Path	cert, key;
  bool	created = false;
  bool init()
  {
    if (!mkdtemp(dir)) return false;
    created = true;
    cert << static_cast<const char *>(dir) << "/cert.pem";
    key << static_cast<const char *>(dir) << "/key.pem";
    Command command;
    command << "openssl req -x509 -newkey rsa:2048 -nodes -days 1 "
	       "-subj /CN=localhost -addext basicConstraints=critical,CA:TRUE "
	       "-addext subjectAltName=DNS:localhost,IP:127.0.0.1 -keyout "
	    << key << " -out " << cert << " >/dev/null 2>&1";
    return !system(command.data());
  }
  ~ZtlsPacketCert()
  {
    if (!created) return;
    unlink(cert.data());
    unlink(key.data());
    rmdir(dir);
  }
};

#endif
