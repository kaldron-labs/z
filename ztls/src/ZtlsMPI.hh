//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// mbedtls C++ wrapper - mbedtls MPI (aka bignum)

#ifndef ZtlsMPI_HH
#define ZtlsMPI_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <mbedtls/bignum.h>

namespace Ztls {

struct MPI {
  mbedtls_mpi	mpi_;
  int		error = 0;

  MPI(ZuBSpan data) {
    mbedtls_mpi_init(&mpi_);
    error = mbedtls_mpi_read_binary(&mpi_, &data[0], data.length());
  }
  ~MPI() {
    mbedtls_mpi_free(&mpi_);
  }

  bool valid() const { return !error; }
  mbedtls_mpi *mpi() const { return valid() ? &mpi_ : nullptr; }
};

} // Ztls

#endif /* ZtlsMPI_HH */
