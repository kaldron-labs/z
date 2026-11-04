//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <quickfix/DataDictionary.h>
#include "../interop/quickfixwire.hh"
#include "codec.hh"

struct QuickfixCodec {
  static constexpr auto	Name = "quickfix"_z;
  FIX::DataDictionary	dictionary{QUICKFIX_DICT_PATH};
  std::string		storage;

  ZuCSpan encode(const ZfixLoad::Order &o) {
    FIX::Message message;
    QfLoad::encode(message, o);
    auto &h = message.getHeader();
    h.setField(8, "FIX.4.4"); h.setField(35, "D"); h.setField(49, "ZFIX-C");
    h.setField(56, "ZFIX-S"); h.setField(34, "2");
    QfLoad::TimeText time;
    time << o.time.fmt(ZuDateTimeFmt::FIX<9>{});
    h.setField(52, std::string{time.data(), time.length()});
    storage = message.toString();
    return storage;
  }

  uint64_t decode(ZuCSpan bytes, unsigned qty) {
    FIX::Message message{std::string{bytes.data(), bytes.length()}, dictionary, true};
    dictionary.validate(message);
    QfLoad::Input<ZfixLoad::Order> value{unsigned(bytes.length())};
    QfLoad::decode(message, value);
    return value.value.qty == qty ? value.value.key : 0;
  }
};

int main(int argc, char **argv) {
  try { QuickfixCodec engine; return ZfixBench::codec(engine, argc, argv); }
  catch (const FIX::Exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
