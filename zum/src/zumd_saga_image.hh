//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// schema-checked immutable row images for administrative saga payloads

#ifndef zumd_saga_image_HH
#define zumd_saga_image_HH

#ifndef ZumLib_HH
#include <zlib/ZumLib.hh>
#endif

#include <zlib/ZumTypes.hh>

#include <zlib/Zfb.hh>
#include <zlib/ZfbStruct.hh>

#include <zlib/ZiIOBuf.hh>

namespace Zum {

struct SagaImage {
  template <typename T>
  static Bytes save(const T &item)
  {
    // The builder is bounded by the caller's maintenance batch limit; use
    // the dedicated saga-image heap for the retained copy.
    Zfb::IOBuilder fbb{new ZiIOBufAlloc<ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, "Zum.Saga.Image">{}};
    fbb.Finish(ZfbStruct::save(fbb, item));
    return Bytes{ZuBSpan{fbb.GetBufferPointer(), fbb.GetSize()}};
  }

  template <typename T>
  static bool load(ZuBSpan bytes, T &item)
  {
    auto fbo = ZfbStruct::verify<T>(bytes);
    if (!fbo) return false;
    item = ZfbStruct::ctor<T>(fbo);
    return true;
  }
};

} // namespace Zum

#endif /* zumd_saga_image_HH */
