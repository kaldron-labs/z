//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zdb.hh>

using namespace ZuTestUtil;
using namespace Zdb_;

namespace SchemaTest {

struct Row { unsigned id; int value; };
ZfStruct(, Row, (id, (Keys<0>), UInt32), (value,, Int32));
ZfStructRender(, Row, FB, id, (value, FB::ID<"a">));
ZiFB::AsTable<"test.FacetRow"> ZiFB_Fmt(Row *);

static void run()
{
  ZuTestScope(schema);
  ZiFB::Buffer bytes;
  ZiFB::saveSchema<Row>(bytes);
  auto schema = Schema::from<Row>(bytes);
  namespace R = ZiFB::Reflect;
  ZuCheck(schema.field("id").get<R::ZfField(Field, name)>() == "id");
  ZuCheck(schema.field("value").get<R::ZfField(Field, name)>() == "a");
  ZuCheck(!schema.field("a"));
}

} // SchemaTest

static void heartbeat()
{
  ZuTestScope(heartbeat);
  DBState state{4};
  state.sn = (SN{1}<<80) + 17;
  state.add("orders", 0, 7);
  state.add("orders", 1, 11);
  ZiFB::Buffer buf;
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto body = ZiFB::save(builder, Heartbeat{"self", 3, 2, &state});
  auto bytes = builder.finish(saveMsg(builder, body, Msg::Tag::Heartbeat));
  bool valid = !builder.failed() && ZiFB::verify<Msg::Root>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto hb = Zdb_::hb(ZiFB::view<Msg::Root>(bytes));
  ZuCheck(hb.get<Msg::ZfField(Heartbeat, host)>() == "self");
  ZuCheck(hb.get<Msg::ZfField(Heartbeat, state)>() == 3);
  ZuCheck(hb.get<Msg::ZfField(Heartbeat, nShards)>() == 2);
  auto wire = hb.get<Msg::ZfField(Heartbeat, dbState)>();
  ZuCheck(wire.get<Msg::ZfField(DBState, sn)>() == state.sn);
  ZuCheck(wire.get<Msg::ZfField(DBState, tableStates)>().length() == 2);

  DBState peer{4};
  peer.sn = state.sn + 1;
  peer.add("orders", 0, 9);
  peer.add("other", 1, 13);
  auto load = ZiFB::handler(ZiFB::View<DBState>{wire.data()});
  load.load(peer);
  ZuCheck(peer.sn == state.sn && peer.count_() == 3);
  auto first = peer.find("orders", 0);
  auto second = peer.find("orders", 1);
  auto other = peer.find("other", 1);
  ZuCheck(first && first->p<1>() == 9);
  ZuCheck(second && second->p<1>() == 11);
  ZuCheck(other && other->p<1>() == 13);
  load.load(peer);
  ZuCheck(peer.count_() == 3);

  // Ordinary borrowed message data can also load through the hash adapter.
  builder.clear();
  Msg::TableState row{"orders", 15, 0};
  ZuArray<const Msg::TableState *, 1> rows;
  rows.push(&row);
  bytes = builder.finish(ZiFB::save(builder, Msg::DBState{23, rows}));
  valid = !builder.failed() && ZiFB::verify<DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  ZiFB::handler<DBState>(bytes).load(peer);
  first = peer.find("orders", 0);
  ZuCheck(peer.sn == 23 && peer.count_() == 3 && first && first->p<1>() == 15);
  buf.null();
  ZuCheck(peer.find("orders", 1) && peer.find("other", 1));
}

static void empty()
{
  ZuTestScope(empty);
  DBState state{4};
  state.sn = 19;
  ZiFB::Buffer buf;
  ZiFB::ArrayStorage<> storage{buf};
  ZiFB::Builder<decltype(storage)> builder{storage};
  auto bytes = builder.finish(ZiFB::save(builder, state));
  bool valid = !builder.failed() && ZiFB::verify<Msg::DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto wire = ZiFB::view<Msg::DBState>(bytes);
  ZuCheck(wire.get<Msg::ZfField(DBState, sn)>() == 19);
  ZuCheck(!wire.get<Msg::ZfField(DBState, tableStates)>().length());

  builder.clear();
  bytes = builder.finish(ZiFB::save<ZuFacet::FB, ZuAlwaysFalse>(builder, state));
  valid = !builder.failed() && ZiFB::verify<Msg::DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  wire = ZiFB::view<Msg::DBState>(bytes);
  ZuCheck(!wire.has<Msg::ZfField(DBState, sn)>());
  ZuCheck(!wire.has<Msg::ZfField(DBState, tableStates)>());

  builder.clear();
  bytes = builder.finish(ZiFB::save(builder, Heartbeat{"self", 3, 2, nullptr}));
  valid = !builder.failed() && ZiFB::verify<Msg::Heartbeat>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto hb = ZiFB::view<Msg::Heartbeat>(bytes);
  auto absent = hb.get<Msg::ZfField(Heartbeat, dbState)>();
  ZuCheck(!absent);
  state.add("orders", 0, 7);
  ZiFB::handler(ZiFB::View<DBState>{absent.data()}).load(state);
  ZuCheck(state.sn == ZuCmp<SN>::null() && state.count_() == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(SchemaTest::run);
  ZuTestCall(heartbeat);
  ZuTestCall(empty);
}
