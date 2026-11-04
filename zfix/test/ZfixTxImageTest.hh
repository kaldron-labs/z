//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixTxImageTest_HH
#define ZfixTxImageTest_HH

#include <zlib/ZfixWire.hh>

// Diagnostic materialization for existing contiguous decoder assertions only.
// Production admission and transport consume the image's original ranges.
inline ZmRef<ZiIOBuf> testBytes(const Zfix::TxImage &image) {
  if (!image) return {};
  if (image.metadata->skip == image.origin &&
      image.metadata->length == image.length()) return image.metadata;
  using Buffer = Zi::IOBufAlloc<ZiIOBuf, ZiIOBuf_DefltSize,
    ZiIOBuf_DefltMaxSize, ZuStringT<"Zfix.Test.Image">>;
  ZmRef<ZiIOBuf> output = new Buffer{nullptr, 0};
  if (!output->ensure(image.length())) return {};
  image.each([&](ZuCSpan part) { output->append(part); });
  return output;
}
struct TestTxRecord {
  ZmRef<ZiIOBuf> buf;
  unsigned offset = 0, length = 0;
  uint64_t token = 0;
  unsigned part = 0;
  ZuCSpan cspan() const { return {buf->data_() + offset, length}; }
};
template <typename Fn> bool testParts(const Zfix::WireQueue::Node *node, Fn fn) {
  const auto &frame = node->data();
  unsigned index = 0;
  auto part = [&](const ZiIOBuf *buf, unsigned offset, unsigned length) {
    return fn(TestTxRecord{const_cast<ZiIOBuf *>(buf), offset, length, frame.token, ++index});
  };
  return part(frame.frame.metadata.ptr(), frame.frame.origin, frame.frame.length());
}
template <typename Signer = Zfix::NoSign>
ZmRef<ZiIOBuf> testFrame(const Zfix::TxBody &body, const Zfix::TxConfig &config,
    Zfix::TxContext context, Zfix::Fmt::Error &error, Zfix::Fmt::Limits limits = {},
    const Signer &signer = {}, ZmRef<ZiIOBuf> destination = {}, unsigned tail = 0) {
  return testBytes(Zfix::txFrame(body, config, context, error, limits, signer,
    ZuMv(destination), tail));
}

#endif /* ZfixTxImageTest_HH */
