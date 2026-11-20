//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// Copyright (c) 2018 Fastly
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
#ifndef ZtlsCertCompression_HH
#define ZtlsCertCompression_HH

#ifndef ZtlsLib_HH
#include <zlib/ZtlsLib.hh>
#endif

#include <zlib/ZtlsEngine.hh>

namespace Ztls_ {

#define Ztls_CertCompAlgoGzip 1
#define Ztls_CertCompAlgoBrotli 2

ZuDerive(CompressedCertificate, (ZtBArray<ZtArrayHeapID<"Ztls.Engine.CompressedCertificate">>));

struct CompressedCertEntry {
  uint32_t		uncompressedLength;
  CompressedCertificate	bytes;
};

struct Ztls_API EmitCompressedCert : EmitCertificate {
  uint16_t		algo = 0;
  CompressedCertEntry	withOcsp{}, withoutOcsp{};

  int call(
    Ztls::Cxn *tls, MessageEmitter *emitter, KeySchedule *keySched, Bytes context, int pushStatus,
    ZuSpan<const uint16_t> algos) override;
};

Ztls_Extern DecompressCertificate &decompressCertificate;

// initializes a certificate emitter that precompresses a certificate chain (and ocsp status)
Ztls_Extern int
initCompressedCert(EmitCompressedCert *ecc, ZuSpan<const Bytes> certificates, Bytes ocspStatus);

Ztls_Extern void disposeCompressedCert(EmitCompressedCert *ecc);

} // namespace Ztls_

#endif
