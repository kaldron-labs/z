//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZhttpH1.hh>

namespace Zhttp { namespace H1 {

ZtEnumImplStruct(ParserState);

FieldOutput headerOutput(HeaderScratch &block)
{
  return {&block, [](void *ctx, FieldForm::T, int,
      const FieldClass *, PrintView key, PrintView value, bool) {
    auto &block = *static_cast<HeaderScratch *>(ctx);
    Writer out{ctx, Native_::bufferWriterOps<HeaderScratch, true>()};
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
