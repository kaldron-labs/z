//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Independent parser/dictionary; intentionally no native FIX headers

#include <stdio.h>
#include <iostream>
#include <limits.h>
#include <string>
#include <quickfix/config.h>
#include <quickfix/DataDictionary.h>
#include <quickfix/Group.h>
#include <quickfix/Message.h>
#include <zlib/ZuTestUtil.hh>
#include "../util/load.hh"
#ifdef ZfixFmtLib_HH
#error QuickFIX fixtures must not include the Zfix parser
#endif
#ifdef ZfixLib_HH
#error QuickFIX fixtures must not include the Zfix session engine
#endif
using namespace ZuTestUtil;
using namespace ZfixLoad;
static Text read(const char *path) {
  auto file = fopen(path, "rb"); if (!file) return {};
  if (fseek(file, 0, SEEK_END)) { fclose(file); return {}; }
  auto length = ftell(file);
  if (length <= 0 || uint64_t(length) > UINT_MAX || fseek(file, 0, SEEK_SET)) { fclose(file);
    return {}; }
  auto size = uint64_t(length);
  Text bytes(size, size + 1);
  bool ok = fread(bytes.data(), 1, bytes.length(), file) == bytes.length();
  fclose(file); return ok ? ZuMv(bytes) : Text{};
}
static void validate(const char *dict, int argc, char **argv) {
  ZuTestScope(validate);
  FIX::DataDictionary dictionary{dict};
  ZuCheck(dictionary.getVersion() == "FIX.4.4");
  for (int i = 2; i < argc; ++i) {
    auto bytes = read(argv[i]); ZuCheck(bytes.length());
    try {
      FIX::Message message{std::string{bytes.data(), bytes.length()}, dictionary, true};
      dictionary.validate(message); // all upstream defaults; no validation switches
      auto type = message.getHeader().getField(35);
      ZuCheck(type == "D" || type == "8" || type == "F" || type == "G");
      ZuCheck(message.getField(11).length() && message.getField(55) == "ZFIX");
      if (message.isSetField(355)) {
	const auto &data = message.getField(355);
	ZuCheck((data.length() == 300 || data.length() == 4096) && !data[0] && data[1] == 1
	  && data[61] == '=');
      }
      if (message.isSetField(453) && message.getField(453) == "2") {
	FIX::Group party{453, 448}; message.getGroup(2, party);
	ZuCheck(party.getField(448) == "PARTY" && party.getField(452) == "2");
	if (party.getField(802) == "2") {
	  FIX::Group sub{802, 523}; party.getGroup(2, sub);
	  ZuCheck(sub.getField(523) == "DESK" && sub.getField(803) == "2");
	} else ZuCheck(party.getField(802) == "0" && !party.groupCount(802));
      }
      if (i == 2) {
	FIX::Message unknown{message}; unknown.setField(9999, "extension");
	bool rejected = false;
	try { dictionary.validate(unknown); }
	catch (const FIX::InvalidTagNumber &) { rejected = true; }
	ZuCheck(rejected); // QuickFIX's default directional expectation
	auto extra = unknown.toString(); Text path; path << argv[i] << ".unknown";
	auto file = fopen(path, "wb"); ZuCheck(file);
	if (file) {
	  bool ok = fwrite(extra.data(), 1, extra.length(), file) == extra.length();
	  int closed = fclose(file); ZuCheck(ok && !closed);
	}
	auto corrupt = std::string{bytes.data(), bytes.length()};
	corrupt[corrupt.length() - 2] = corrupt[corrupt.length() - 2] == '0' ? '1' : '0';
	rejected = false;
	try { FIX::Message invalid{corrupt, dictionary, true}; }
	catch (const FIX::InvalidMessage &) { rejected = true; }
	ZuCheck(rejected);
      }
      // Rebuild framing/order with QuickFIX and feed its output back to Zfix.
      auto output = message.toString(); Text path; path << argv[i] << ".peer";
      auto file = fopen(path, "wb"); ZuCheck(file);
      if (file) {
	bool ok = fwrite(output.data(), 1, output.length(), file) == output.length();
	int closed = fclose(file); ZuCheck(ok && !closed);
      }
    } catch (const FIX::Exception &error) {
      log(argv[i], ": ", error.what()); ZuCheck(false);
    }
  }
}
int main(int argc, char **argv) {
  if (argc < 3) { std::cerr << "usage: codec_test FIX44.xml FRAME...\n"; return 2; }
  parse(1, argv); ZuTestMain(); ZuTestCall(validate, argv[1], argc, argv); return 0;
}
