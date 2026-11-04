//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZfixLabel_HH
#define ZfixLabel_HH
#include <zlib/ZtString.hh>
#include <zlib/ZfixFmt.hh>

struct Label {
  ZtString<ZtStringHeapID<"Zfix.Test">>	value;
  Label() = default;

  explicit Label(ZuCSpan s) : value{s} { }
};

struct LabelFormat {
  inline static unsigned saves = 0;
  template <typename O> struct Handler {
    static O load(ZuCSpan value, Zfix::Fmt::Error &error) {
      if (value == "valid") return O(value);
      error.code = Zfix::Fmt::ErrorCode::Value;
      return O{};
    }

    template <typename Out> static void save(Out &out, const O &o) {
      ++saves; out << o.value;
    }
  };

};

LabelFormat Zfix_StringFmt(Label *);

#endif /* ZfixLabel_HH */
