//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZiPBReflect.hh>

#include <zprotoc_base.hh>
#include <zprotoc_user.hh>

using namespace ZuTestUtil;

ZuDerive(ImportOutput,
  (ZtArray<uint8_t, ZtArrayHeapID<"zprotocimport.Output">>));

static void exchange()
{
  ZuTestScope(exchange);
  demo::Imported input;
  input.record = new shared::Record;
  input.record->id = 17;
  input.records.push(new shared::Record);
  input.records[0]->id = 29;
  input.state = shared::State::STATE_ON;
  input.states.push(shared::State::STATE_ON);
  input.detail = new shared::Record_Detail;
  input.detail->value = 37;
  shared::Record named;
  named.id = 43;
  input.recordsByName.add("primary", ZuMv(named));
  ImportOutput output;
  ZiPB::ArrayStorage storage{output};
  ZiPB::Builder builder{storage};
  auto wire = ZiPB::save(builder, input);
  ZuCheck(!builder.failed() && ZiPB::verify<demo::Imported>(wire));
  auto loaded = ZiPB::load<demo::Imported>(wire);
  ZuCheck(loaded.record && loaded.record->id == 17);
  ZuCheck(loaded.records.length() == 1 && loaded.records[0]->id == 29);
  ZuCheck(loaded.state == shared::State::STATE_ON &&
    loaded.states.length() == 1 &&
    loaded.states[0] == shared::State::STATE_ON);
  ZuCheck(loaded.detail && loaded.detail->value == 37);
  ZuCheck(loaded.recordsByName.count_() == 1 &&
    loaded.recordsByName.findVal("primary").id == 43);
}

static void nativeCrossPackage()
{
  ZuTestScope(nativeCrossPackage);
  using namespace ZiPB::Reflect;
  DescSet descriptor;
  ZuCheck(!exportSchema<demo::EnumOnly>(descriptor,
    "enum-only.proto", "demo", Syntax::Proto2));
  ZuCheck(!exportSchema<demo::Imported>(descriptor,
    "imported.proto", "demo", Syntax::Proto2));
  ZuCheck(!exportSchema<demo::DetailOnly>(descriptor,
    "detail-only.proto", "demo", Syntax::Proto2));
  ZuCheck(!descriptor.files.length());
}

int main(int argc, char **argv)
{
  ZuTestMain();
  ZuTestCall(exchange);
  ZuTestCall(nativeCrossPackage);
}
