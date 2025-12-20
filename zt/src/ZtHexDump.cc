//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// hex dumper

#include <zlib/ZtHexDump.hh>

void ZtHexDump_::print(ZuVStream &s) const
{
  if (ZuUnlikely(!m_data)) return;

  s << '\n';

  ZuCArray<64> hex;
  ZuCArray<20> ascii;
  ZuAssert(ZuTraits<char>::IsPrimitive);
  ZuCArray<48> pad;
  memset(pad.data(), ' ', 45);
  pad.length(45);

  unsigned offset, col;

  for (offset = 0; offset < m_length; offset += 16) {
    hex.null();
    ascii.null();
    using namespace ZuFmt;
    hex << ZuBoxed(offset).fmt<Hex<0, Alt<Right<8>>>>() << "  ";
    for (col = 0; col < 16; col++) {
      if (offset + col >= m_length) {
	hex << ZuCSpan(pad.data(), 3 * (16 - col));
	break;
      }
      ZuBox<uint8_t> byte = m_data[offset + col];
      hex << byte.fmt<Hex<0, Right<2>>>() << ' ';
      ascii << (((byte >= 0x20 && byte < 0x7f) || byte == '"') ?
	  static_cast<char>(byte.val()) : '.');
    }
    hex << ' ';
    s << hex << ascii << '\n';
  }
}
