//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpH1.hh>

namespace Zhttp { namespace H1 {

ZtEnumImplStruct(ParserState);

FieldOutput headerOutput(HeaderScratch &block)
{
  static constexpr WriterOps ops{
    [](void *ctx, ZuBSpan span) { *static_cast<HeaderScratch *>(ctx) << span; },
    [](void *ctx, const BufferPrint &value) {
      *static_cast<HeaderScratch *>(ctx) << value;
    },
    [](void *ctx, ZmRef<ZiIOBuf> buf) {
      *static_cast<HeaderScratch *>(ctx) << ZuBSpan{buf->data(), buf->length};
    },
    [](void *) { return true; },
    [](const void *) { return false; },
    [](const void *ctx) -> uint64_t {
      return static_cast<const HeaderScratch *>(ctx)->length();
    },
    [](const void *) { return unsigned(UINT_MAX); },
    [](const void *) { return 0U; },
    [](const void *) { return 0U; },
    [](void *, unsigned head) -> ZmRef<ZiIOBuf> {
      using Alloc = Zi::IOBufAlloc<ZiRxQueue::Node,
	ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize, ZuStringT<"Zhttp.Header.Print">>;
      auto buf = ZmRef<ZiIOBuf>{new Alloc{}};
      buf->skip = head;
      return buf;
    },
    [](void *ctx, unsigned length) -> ZuSpan<uint8_t> {
      auto &out = *static_cast<HeaderScratch *>(ctx);
      auto offset = out.length();
      return {out.ensure(offset + length) + offset, length};
    },
    [](void *ctx, unsigned length) {
      auto &out = *static_cast<HeaderScratch *>(ctx);
      out.length(out.length() + length);
    }};
  return {&block, [](void *ctx, FieldForm::T, int,
      const FieldClass *, PrintView key, PrintView value, bool) {
    auto &block = *static_cast<HeaderScratch *>(ctx);
    Writer out{ctx, ops};
    key.write(out);
    out << ": ";
    uint64_t offset = block.length();
    value.write(out);
    FieldOffset span{offset, unsigned(block.length() - offset)};
    out << "\r\n";
    return span;
  }};
}


} } // namespace Zhttp::H1
