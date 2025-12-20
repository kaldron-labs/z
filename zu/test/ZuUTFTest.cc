//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <stdlib.h>
#include <string.h>

#include <iostream>

#include <zlib/ZuUTF.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuArray.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

int main()
{
  {
    ZuCArray<64> s;
    s.length(ZuUTF<char, uint16_t>::cvt(s.span(), 
	  { (const uint16_t *)"H\0e\0l\0l\0o\0 \0W\0o\0r\0l\0d\0", 11}));
    CHECK(s == "Hello World");
    s.length(ZuUTF<char, wchar_t>::cvt(s.span(),
	  ZuWSpan(L"Hello World", 11)));
    CHECK(s == "Hello World");
    s = L"Hello World";
    CHECK(s == "Hello World");
    {
      ZuWArray<64> w;
      w.length(ZuUTF<wchar_t, char>::cvt(w.span(), s));
      CHECK(w == L"Hello World");
      w = "Hello World";
      CHECK(w == L"Hello World");
    }
  }
  {
    uint32_t u[3] = { 0x1f404, (uint32_t)'x', (uint32_t)'y' }; // cow
    {
      ZuArray<uint16_t, 8> j;
      j.length(ZuUTF<uint16_t, uint32_t>::cvt(j.span(),
	    ZuSpan<const uint32_t>(&u[0], 3)));
      CHECK(j.length() == 4);
      CHECK(j[0] == 0xd83d && j[1] == 0xdc04 && j[2] == 'x' && j[3] == 'y');
      CHECK(ZuUTF32::width(u[0]) == 2);
      CHECK(ZuUTF32::width(u[1]) == 1);
    }
    {
      ZuArray<char, 16> j;
      j.length(ZuUTF<char, uint32_t>::cvt(j.span(), u));
      CHECK(j.length() == 6);
      CHECK(j.equals("\xf0\x9f\x90\x84xy"));
      std::cout << j << '\n';
      ZuArray<uint32_t, 4> k;
      k.length(ZuUTF<uint32_t, char>::cvt(k.span(), j));
      CHECK(k.equals(u));
    }
  }
}
