//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/zumd_db.hh>

using namespace ZuTestUtil;

static void catalog()
{
  ZuTestScope(catalog);
  auto defs = Zum::sagaDefs();
  ZuCheck(defs.length() == 41);
  bool metadata = true;
  bool unique = true;
  bool phaseDefs = true;
  bool rowFields = true;
  bool bounds = true;
  bool loadsReject = true;
  for (unsigned i = 0; i < defs.length(); ++i) {
    const auto &def = defs[i];
    metadata &= def.type && def.nPhases && def.phaseDef && def.load;
    for (unsigned j = 0; j < i; ++j)
      unique &= def.type != defs[j].type;
    Zdb_::SagaTypeStep row;
    for (unsigned phase = 0; phase < def.nPhases; ++phase) {
      bool defined = def.phaseDef(phase, row);
      phaseDefs &= defined;
      rowFields &= defined && row.step == phase && row.table;
    }
    bounds &= !def.phaseDef(def.nPhases, row);
    bool rejected = false;
    try { (void)def.load(ZuBSpan{}); }
    catch (...) { rejected = true; }
    loadsReject &= rejected;
  }
  ZuCheck(metadata);
  ZuCheck(unique);
  ZuCheck(phaseDefs);
  ZuCheck(rowFields);
  ZuCheck(bounds);
  ZuCheck(loadsReject);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(catalog);
  return 0;
}
