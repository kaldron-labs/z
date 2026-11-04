//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>

#include <zlib/Zdb.hh>

using namespace ZuTestUtil;
using namespace Zdb_;

namespace SchemaTest {

struct Row { unsigned id; int value; };
ZfStruct(, Row, (id, (Keys<0>), UInt32), (value,, Int32));
ZfStructRender(, Row, FB, id, (value, FB::ID<"a">));
Zfb::AsTable<"test.FacetRow"> Zfb_Fmt(Row *);

static void run()
{
  ZuTestScope(schema);
  Zfb::Buffer bytes;
  Zfb::saveSchema<Row>(bytes);
  auto schema = Schema::from<Row>(bytes);
  namespace R = Zfb::Reflect;
  ZuCheck(schema.field("id").get<R::ZfField(Field, name)>() == "id");
  ZuCheck(schema.field("value").get<R::ZfField(Field, name)>() == "a");
  ZuCheck(!schema.field("a"));
}

} // SchemaTest

namespace BufTest {

struct Small {
  friend ZuUnsigned<256> ZdbBufSize(Small *);
  friend ZuStringT<"Zdb.Test.Small"> ZdbBufHeapID(Small *);
};
struct Large {
  friend ZuUnsigned<1024> ZdbBufSize(Large *);
  friend ZuStringT<"Zdb.Test.Large"> ZdbBufHeapID(Large *);
};

static void run()
{
  ZuTestScope(buffers);
  static_assert(ZdbBuf_Size<Small>{} == 256);
  static_assert(ZdbBuf_Size<Large>{} == 1024);
  static_assert(ZuIsSame<ZdbBuf_HeapID<Small>, ZuStringT<"Zdb.Test.Small">>{});
  static_assert(ZuIsSame<ZdbBuf_HeapID<Large>, ZuStringT<"Zdb.Test.Large">>{});
  auto first = tableBufAllocFn<Small>();
  auto second = tableBufAllocFn<Small>();
  auto large = tableBufAllocFn<Large>();
  ZuCheck(first.allocator() && second.allocator() && large.allocator());
  ZuCheck(first.allocator() != second.allocator());
  auto copied = first;
  ZuCheck(copied.allocator() == first.allocator());
  auto moved = ZuMv(copied);
  ZuCheck(moved.allocator() == first.allocator());
  IOBufAllocFn assigned;
  assigned = moved;
  ZuCheck(assigned.allocator() == first.allocator());
  assigned = IOBufAllocFn{};
  ZuCheck(!assigned && !assigned.allocator());
  auto local = localBufAllocFn<Small>();
  ZuCheck(local && !local.allocator());
  auto a = first();
  auto b = second();
  auto c = large();
  ZuCheck(a && b && c);
  ZuCheck(a->size >= 256 && c->size >= 1024);
  ZuCheck(a->skip >= sizeof(Hdr));
  ZuCheck(!a->rep && !b->rep && !c->rep);
  ZuCheck(!a->ring() || a->ring() != b->ring());
  auto ptr = a->data();
  Builder builder{ZuMv(a), Zfb::Wire::Align};
  builder.finish(saveMsg(builder,
    saveCommit(builder, "test", 42, 0), Msg::Tag::Commit));
  ZmRef<IOBuf> frame = saveFrame(builder);
  ZuCheck(frame && frame->data_() + Zfb::Wire::MaxAlign == ptr);
  ZuCheck(frame->hdr()->length == frame->length - sizeof(Hdr));
  auto refs = frame->refCount();
  auto original = frame->data();
  auto hdr = frame->hdr();
  auto length = frame->length;
  auto range = Zi::RxRange{frame, 0, frame->length};
  frame->advance(sizeof(Hdr));
  auto view = ZmRef<IOBuf>{new RxFrame{ZuMv(range)}};
  ZuCheck(frame->refCount() == refs + 1);
  ZuCheck(view->data() == original && view->length == length);
  ZuCheck(view->hdr()->length == hdr->length);
  frame->advance(frame->length);
  ZuCheck(!frame->length && view->data() == original &&
    view->hdr()->length == length - sizeof(Hdr));
  ZuCheck(view->ensure(view->size + 1024));
  ZuCheck(view->jumbo());
  ZuCheck(frame->refCount() == refs);
  ZuCheck(hdr->length == length - sizeof(Hdr));
}

static void frameOrigin()
{
  ZuTestScope(frameOrigin);
  auto alloc = tableBufAllocFn<Small>();
  Builder builder{alloc()};
  auto nested = builder.startNested();
  auto data = builder.endNested(nested, Zfb::save(builder, SchemaTest::Row{7, 11}));
  auto record = saveRecord(builder, "frame", 17, 23, 0, 1, data);
  builder.finish(saveMsg(builder, record, Msg::Tag::Replication));
  auto packet = saveFrame(builder);
  ZuCheck(packet && packet->framePos == packet->skip && packet->framePos);
  auto hdr = packet->hdr();
  auto span = packet->frameSpan();
  auto length = packet->length;
  ZmRef<RepBuf<SchemaTest::Row>> cached = new RepBuf<SchemaTest::Row>{packet};
  packet->advance(length >> 1);
  ZuCheck(packet->data() == span.data() + (length >> 1));
  ZuCheck(packet->hdr() == hdr && packet->frameSpan() == span);
  ZuCheck(IOBuf_UNAxor(*packet) == 17 &&
    cached->fbo_().get<SchemaTest::ZfField(Row, id)>() == 7 &&
    cached->fbo_().get<SchemaTest::ZfField(Row, value)>() == 11);
  packet->advance(packet->length);
  ZuCheck(!packet->length && packet->data() == span.data() + length);
  ZuCheck(packet->hdr() == hdr && packet->frameSpan() == span &&
    packet->rep == cached.ptr());
  auto committed = record_(msg_(packet->hdr()));
  ZuCheck(committed.get<Msg::ZfField(Record, shard)>() == 1 &&
    committed.get<Msg::ZfField(Record, un)>() == 17 &&
    cached->fbo_().get<SchemaTest::ZfField(Row, value)>() == 11);
}

} // BufTest

static void heartbeat()
{
  ZuTestScope(heartbeat);
  DBState state{4};
  state.sn = (SN{1}<<80) + 17;
  state.add("orders", 0, 7);
  state.add("orders", 1, 11);
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder builder{storage};
  auto body = Zfb::save(builder, Heartbeat{"self", 3, 2, &state});
  auto bytes = builder.finish(saveMsg(builder, body, Msg::Tag::Heartbeat));
  bool valid = !builder.failed() && Zfb::verify<Msg::Root>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto hb = Zdb_::hb(Zfb::view<Msg::Root>(bytes));
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
  auto load = Zfb::handler(Zfb::View<DBState>{wire.data()});
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
  bytes = builder.finish(Zfb::save(builder, Msg::DBState{23, rows}));
  valid = !builder.failed() && Zfb::verify<DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  Zfb::handler<DBState>(bytes).load(peer);
  first = peer.find("orders", 0);
  ZuCheck(peer.sn == 23 && peer.count_() == 3 && first && first->p<1>() == 15);
  buf.null();
  ZuCheck(peer.find("orders", 1) && peer.find("other", 1));
}

static void heartbeatPrint()
{
  ZuTestScope(heartbeatPrint);
  Msg::TableState orders{"orders", 11, 1}, other{"other", 7, 0};
  ZtArray<const Msg::TableState *> rows{&orders, &other};
  Msg::DBState state{23, rows};
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder builder{storage};
  auto bytes = builder.finish(Zfb::save(builder,
    Msg::Heartbeat{"self", HostState::Active, 2, &state}));
  ZtString text;
  text << HB_Print{Zfb::view<Msg::Heartbeat>(bytes)};
  ZuCheck(text == "{host=self state=Active dbState={sn=23 "
    "dbs=[{orders.1,11},{other.0,7}]}}");

  builder.clear();
  state.tableStates = {};
  bytes = builder.finish(Zfb::save(builder,
    Msg::Heartbeat{"self", HostState::Active, 2, &state}));
  text = ZtString{};
  text << HB_Print{Zfb::view<Msg::Heartbeat>(bytes)};
  ZuCheck(text == "{host=self state=Active dbState={sn=23 dbs=[]}}");
}

static void empty()
{
  ZuTestScope(empty);
  DBState state{4};
  state.sn = 19;
  Zfb::Buffer buf;
  Zfb::ArrayStorage<> storage{buf};
  Zfb::Builder builder{storage};
  auto bytes = builder.finish(Zfb::save(builder, state));
  bool valid = !builder.failed() && Zfb::verify<Msg::DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto wire = Zfb::view<Msg::DBState>(bytes);
  ZuCheck(wire.get<Msg::ZfField(DBState, sn)>() == 19);
  ZuCheck(!wire.get<Msg::ZfField(DBState, tableStates)>().length());

  builder.clear();
  bytes = builder.finish(Zfb::save<ZuFacet::FB, ZuAlwaysFalse>(builder, state));
  valid = !builder.failed() && Zfb::verify<Msg::DBState>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  wire = Zfb::view<Msg::DBState>(bytes);
  ZuCheck(!wire.has<Msg::ZfField(DBState, sn)>());
  ZuCheck(!wire.has<Msg::ZfField(DBState, tableStates)>());

  builder.clear();
  bytes = builder.finish(Zfb::save(builder, Heartbeat{"self", 3, 2, nullptr}));
  valid = !builder.failed() && Zfb::verify<Msg::Heartbeat>(bytes);
  ZuCheck(valid);
  if (!valid) return;
  auto hb = Zfb::view<Msg::Heartbeat>(bytes);
  auto absent = hb.get<Msg::ZfField(Heartbeat, dbState)>();
  ZuCheck(!absent);
  state.add("orders", 0, 7);
  Zfb::handler(Zfb::View<DBState>{absent.data()}).load(state);
  ZuCheck(state.sn == ZuCmp<SN>::null() && state.count_() == 1);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(SchemaTest::run);
  ZuTestCall(BufTest::run);
  ZuTestCall(BufTest::frameOrigin);
  ZuTestCall(heartbeat);
  ZuTestCall(heartbeatPrint);
  ZuTestCall(empty);
}
