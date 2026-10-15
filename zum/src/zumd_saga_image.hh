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

#include <zlib/ZiIOBuf.hh>

namespace Zum {

struct SagaImage {
  using Buf = ZiIOBufAlloc<ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize,
    "Zum.Saga.Image">;

  template <typename T>
  static Bytes save(const T &item)
  {
    Zfb::IOBuilder<Zfb::BufferAlign<T>::N> fbb{new Buf{}};
    fbb.finish(Zfb::save(fbb, item));
    return Bytes{fbb.span()};
  }

  template <typename T>
  static bool load(ZuBSpan bytes, T &item)
  {
    if (!Zfb::verify<T>(bytes)) return false;
    item = Zfb::load<T>(bytes);
    return true;
  }
};

} // namespace Zum

#endif /* zumd_saga_image_HH */
