//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/reflection_generated.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZmTime.hh>
#include <zlib/Zfb.hh>
#include <zlib/ZdbBuf.hh>
#include <zlib/ZdbSaga.hh>

#include "dbmsg_generated.h"
#include "../../zfb/interop/iobaseline.hh"
#include <zlib/ZfbReflect.hh>

using namespace ZuTestUtil;
namespace M = Zdb_::Msg;
namespace C = Zdb_::fbs;

struct Row { unsigned id; };
ZfStruct(, (Row, FB), (id, (Deflt<0>), UInt32));
Zfb::AsTable<"test.Row"> Zfb_Fmt(Row *);
enum { RowAlign = Zfb::BufferAlign<Row>::N > Zfb::BufferAlign<M::Root>::N ?
  Zfb::BufferAlign<Row>::N : Zfb::BufferAlign<M::Root>::N };
ZuDerive(Text, (ZtString<ZtStringHeapID<"zdbmsginterop.Text">>));

static void schema()
{
  ZuTestScope(schema);
  auto bytes = ZtScratch(Text, 0);
  Zfb::saveSchema<M::Root>(bytes);
  ZuBSpan span{bytes};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  ZuCheck(reflection::VerifySchemaBuffer(verifier));
  ZuCheck(Zfb::Reflect::verifySchema(span));
  auto root = reflection::GetSchema(span.data());
  ZuCheck(root->root_table()->name()->string_view() == "Zdb_.fbs.Msg");
  auto body = root->enums()->LookupByKey("Zdb_.fbs.Body");
  ZuCheck(body && body->values()->size() == 5);
  if (!body) return;
  auto repl = body->values()->Get(M::Tag::Replication);
  auto recovery = body->values()->Get(M::Tag::Recovery);
  ZuCheck(repl->name()->string_view() == "Replication" && repl->value() == 2);
  ZuCheck(recovery->name()->string_view() == "Recovery" && recovery->value() == 3);
  ZuCheck(repl->union_type()->index() == recovery->union_type()->index());
  auto source = ZtScratch(Text, 0);
  Zfb::saveFBS<M::Root>(source);
  flatbuffers::Parser parser;
  parser.opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
  bool parsed = parser.Parse(source.data());
  ZuCheck(parsed);
  if (!parsed) log(parser.error_);
}

static void records()
{
  ZuTestScope(records);
  auto row = ZtScratch(Text, 0);
  ZuCheck(Zfb::save(row, Row{42}) == Zfb::Error::OK);
  // Match the erased database writer: a nested row and envelope share one
  // pooled downward arena, including storage for forced row alignment.
  Zfb::IOBuilder<Zfb::Wire::MaxAlign> builder{new Zdb_::IOBufAlloc<>, RowAlign};
  auto nested = builder.startNested();
  auto root = Zfb::save(builder, Row{42});
  auto data = builder.endNested(nested, root);
  auto payload = Zdb_::saveRecord(builder, "rows", 7, 123, -3, 1, data);
  builder.finish(Zdb_::saveMsg(builder, payload, M::Tag::Replication));
  ZmRef<Zdb_::IOBuf> buf = Zdb_::saveHdr(builder, &builder);
  ZuCheck(buf && buf->owner == &builder);
  if (!buf) return;
  ZuCheck(Zdb_::loadHdr(buf.ptr()) == buf->length);
  auto hdr = buf->hdr();
  flatbuffers::Verifier verifier{hdr->data(), hdr->length};
  ZuCheck(C::VerifyMsgBuffer(verifier));
  auto canonical = C::GetMsg(hdr->data());
  ZuCheck(canonical->body_type() == C::Body::Replication);
  auto record = canonical->body_as_Replication();
  ZuCheck(record && record->table()->string_view() == "rows" && record->un() == 7);
  ZuCheck(record->sn()->h() == 0 && record->sn()->l() == 123 && record->vn() == -3);
  ZuCheck(buf->fbo<Row>().get<ZfField(Row, id)>() == 42);
  ZuCheck(buf->fbo_<Row>().get<ZfField(Row, id)>() == 42);

  // Canonical producer, including independently framed nested row bytes.
  flatbuffers::FlatBufferBuilder peer;
  auto name = peer.CreateString("peer");
  ZuBSpan rowBytes{row};
  auto peerData = peer.CreateVector(rowBytes.data(), rowBytes.length());
  Zf::UInt128 sn{1, 9};
  auto rec = C::CreateRecord(peer, name, 11, &sn, -2, 3, peerData);
  peer.Finish(C::CreateMsg(peer, C::Body::Recovery, rec.Union()));
  ZuBSpan incoming{peer.GetBufferPointer(), peer.GetSize()};
  ZuCheck(Zfb::verify<M::Root>(incoming));
  auto view = Zfb::view<M::Root>(incoming);
  ZuCheck(Zdb_::recovery(view));
  auto r = Zdb_::record(view);
  ZuCheck(r.get<M::ZfField(Record, table)>() == "peer");
  ZuCheck(r.get<M::ZfField(Record, sn)>() == ((uint128_t(1) << 64) | 9));
  ZuCheck(Zdb_::data<Row>(r).get<ZfField(Row, id)>() == 42);
  ZuCheck(!Zdb_::hb(view) && !Zdb_::commit(view));

  // The envelope verifies independently; ingress must also verify nested data.
  auto nestedData = const_cast<uint8_t *>(r.get<M::ZfField(Record, data)>().data());
  Zfb::write<Zfb::Offset>(nestedData, 0);
  ZuCheck(Zfb::verify<M::Root>(incoming));
  ZuCheck(!Zdb_::data<Row>(r));
  hdr->length = UINT32_MAX;
  ZuCheck(Zdb_::loadHdr(buf.ptr()) == -1);
  ZuCheck(Zdb_::verifyHdr(buf, [](const auto *, auto) { return 0; }) == -1);
}

static void heartbeat()
{
  ZuTestScope(heartbeat);
  M::TableState table{"rows", 8, 2};
  const M::TableState *tables[] = {&table};
  M::DBState state{123, tables};
  M::Root message;
  message.body.p<M::Tag::Heartbeat>(M::Heartbeat{"host", 3, 4, &state});
  auto bytes = ZtScratch(Text, 0);
  ZuCheck(Zfb::save(bytes, message) == Zfb::Error::OK);
  ZuBSpan span{bytes};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  ZuCheck(C::VerifyMsgBuffer(verifier));
  auto hb = C::GetMsg(span.data())->body_as_Heartbeat();
  ZuCheck(hb && hb->host()->string_view() == "host" && hb->n_shards() == 4);
  ZuCheck(hb->db_state()->table_states()->Get(0)->un() == 8);
  ZuCheck(Zfb::verify<M::Root>(span));
  auto view = Zfb::view<M::Root>(span);
  ZuCheck(Zdb_::hb(view).get<M::ZfField(Heartbeat, state)>() == 3);
  ZuCheck(!Zdb_::record(view) && !Zdb_::commit(view));

  flatbuffers::FlatBufferBuilder peer;
  auto host = peer.CreateString("peer");
  auto body = C::CreateHeartbeat(peer, host, 2, 5);
  peer.Finish(C::CreateMsg(peer, C::Body::Heartbeat, body.Union()));
  ZuBSpan incoming{peer.GetBufferPointer(), peer.GetSize()};
  ZuCheck(Zfb::verify<M::Root>(incoming));
  auto in = Zdb_::hb(Zfb::view<M::Root>(incoming));
  ZuCheck(in.get<M::ZfField(Heartbeat, host)>() == "peer");
  ZuCheck(in.get<M::ZfField(Heartbeat, nShards)>() == 5);
  ZuCheck(!in.get<M::ZfField(Heartbeat, dbState)>());
}

static void commit()
{
  ZuTestScope(commit);
  Zfb::IOBuilder<> builder{new ZiIOBufAlloc<>};
  auto payload = Zdb_::saveCommit(builder, "rows", 17, 3);
  builder.finish(Zdb_::saveMsg(builder, payload, M::Tag::Commit));
  auto owner = builder.buf();
  ZuBSpan span{owner->cspan()};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  ZuCheck(C::VerifyMsgBuffer(verifier));
  auto body = C::GetMsg(span.data())->body_as_Commit();
  ZuCheck(body && body->un() == 17 && body->shard() == 3);

  flatbuffers::FlatBufferBuilder peer;
  auto name = peer.CreateString("peer");
  auto value = C::CreateCommit(peer, name, 19, 2);
  peer.Finish(C::CreateMsg(peer, C::Body::Commit, value.Union()));
  ZuBSpan incoming{peer.GetBufferPointer(), peer.GetSize()};
  // Equal-length names and present scalar fields must not add native padding.
  ZuCheck(span.length() == incoming.length());
  ZuCheck(Zfb::verify<M::Root>(incoming));
  auto view = Zfb::view<M::Root>(incoming);
  auto in = Zdb_::commit(view);
  ZuCheck(in.get<M::ZfField(Commit, table)>() == "peer");
  ZuCheck(in.get<M::ZfField(Commit, un)>() == 19);
  ZuCheck(!Zdb_::record(view) && !Zdb_::hb(view));

  peer.Clear();
  peer.Finish(C::CreateMsg(peer));
  incoming = {peer.GetBufferPointer(), peer.GetSize()};
  ZuCheck(Zfb::verify<M::Root>(incoming));
  view = Zfb::view<M::Root>(incoming);
  ZuCheck(!Zdb_::record(view) && !Zdb_::hb(view) && !Zdb_::commit(view));

  Zfb::IOBuilder<> empty{new ZiIOBufAlloc<>};
  empty.finish(Zdb_::saveMsg(empty, 0, M::Tag::None));
  auto emptyOwner = empty.buf();
  ZuCheck(Zfb::verify<M::Root>(emptyOwner->cspan()));
  flatbuffers::Verifier emptyVerifier{emptyOwner->data(), emptyOwner->length};
  ZuCheck(C::VerifyMsgBuffer(emptyVerifier));
  ZuCheck(C::GetMsg(emptyOwner->data())->body_type() == C::Body::NONE);
}

namespace Bench {

// An owning consumer with the Record wire slots and SagaData's retained
// payload storage. This is a materialization control, not a store schema.
struct ReadRecord {
  Text table;
  Zdb_::UN un;
  Zdb_::SN sn;
  Zdb_::VN vn;
  Zdb_::Shard shard;
  Zdb_::SagaPayload data;
};
ZfStruct(, (ReadRecord, FB),
  (table,	(Ctor<0>),		String),
  (un,		(Ctor<1>, Deflt<0>),	UInt64),
  (sn,		(Ctor<2>),		UInt128),
  (vn,		(Ctor<3>, Deflt<0>),	Int64),
  (shard,	(Ctor<4>, Deflt<0>),	UInt8),
  (data,	(Ctor<5>),		Bytes));
Zfb::AsTable<"bench.ReadRecord"> Zfb_Fmt(ReadRecord *);

static uint64_t allocations()
{
  // This diagnostic runs on one thread. A noncapturing observer avoids
  // counting a newly allocated ZmFn closure in every snapshot.
  static uint64_t total;
  total = 0;
  Ztc::HeapMgr::all([](Ztc::Heap *heap) {
    Ztc::HeapTelemetry data;
    heap->telemetry(data);
    total += data.cacheAllocs + data.heapAllocs;
  });
  return total;
}

template <unsigned Mode>
static uint64_t read(const Zdb_::Hdr *hdr)
{
  if constexpr (Mode == 0) {
    auto r = C::GetMsg(hdr->data())->body_as_Replication();
    auto data = r->data();
    return r->un() + r->sn()->l() + uint64_t(r->vn()) + r->shard() +
      r->table()->size() + data->size() + data->Get(data->size() - 1);
  } else {
    auto r = Zdb_::record_(Zdb_::msg_(hdr));
    if constexpr (Mode == 1) {
      auto data = r.get<M::ZfField(Record, data)>();
      return r.get<M::ZfField(Record, un)>() +
	uint64_t(r.get<M::ZfField(Record, sn)>()) +
	uint64_t(r.get<M::ZfField(Record, vn)>()) +
	r.get<M::ZfField(Record, shard)>() +
	r.get<M::ZfField(Record, table)>().length() + data.length() +
	data[data.length() - 1];
    } else {
      auto row = Zfb::handler(Zfb::View<ReadRecord>{r.data()}).ctor();
      __asm__ __volatile__("" : : "g"(row.data.data()) : "memory");
      return row.un + uint64_t(row.sn) + uint64_t(row.vn) + row.shard +
	row.table.length() + row.data.length() + row.data[row.data.length() - 1];
    }
  }
}

template <unsigned Mode>
static void readSample(ZuCSpan name, const Zdb_::Hdr *hdr, unsigned count,
    unsigned payload)
{
  auto expected = read<0>(hdr);
  if (read<Mode>(hdr) != expected) Zm::exit(1);
  auto before = allocations();
  uint64_t sum = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    __asm__ __volatile__("" : : "g"(hdr) : "memory");
    sum += read<Mode>(hdr);
  }
  auto elapsed = Zm::now() - begin;
  auto allocs = allocations() - before;
  if (sum != expected * count) Zm::exit(1);
  std::cout << name << ',' << payload << ',' << count << ','
    << elapsed.as_decimal() << ',' << allocs << ','
    << (Mode == 2 ? uint64_t(payload) * count : 0) << ',' << sum << '\n';
}

static int reads(unsigned count, unsigned rounds, unsigned payload)
{
  if (!count || !rounds || !payload || payload >= Zdb_::MaxBufSize) return 1;
  auto data = ZtScratch(Zfb::Buffer, payload, payload);
  for (unsigned i = 0; i < payload; ++i) data[i] = uint8_t(i);
  IOBuilder b{new Zdb_::IOBufAlloc<>};
  auto bytes = b.CreateVector(data.data(), data.length());
  auto name = b.CreateString("Zdb.Saga");
  Zf::UInt128 sn{0, 123};
  auto record = C::CreateRecord(b, name, 7, &sn, -3, 1, bytes);
  b.Finish(C::CreateMsg(b, C::Body::Replication, record.Union()));
  unsigned length = b.GetSize();
  auto owner = b.buf();
  new (owner->prepend(sizeof(Zdb_::Hdr))) Zdb_::Hdr{length};
  auto hdr = reinterpret_cast<const Zdb_::Hdr *>(owner->data());
  flatbuffers::Verifier verifier{hdr->data(), hdr->length};
  if (!C::VerifyMsgBuffer(verifier) ||
      !Zfb::verify<M::Root>({hdr->data(), hdr->length})) return 1;
  std::cout << "reader,payload,count,seconds,z_heap_allocs,materialized_bytes,checksum\n";
  for (unsigned r = 0; r < rounds; ++r) {
    if (r & 1) {
      readSample<2>("native-owning", hdr, count, payload);
      readSample<1>("native-view", hdr, count, payload);
      readSample<0>("canonical-view", hdr, count, payload);
    } else {
      readSample<0>("canonical-view", hdr, count, payload);
      readSample<1>("native-view", hdr, count, payload);
      readSample<2>("native-owning", hdr, count, payload);
    }
  }
  return 0;
}

struct Canonical {
  class Builder : public IOBuilder {
  public:
    using IOBuilder::IOBuilder;
    unsigned alignment() const { return minalign_; }
  };
  static void save(Builder &b, uint64_t seq) {
    // Preserve the old nested-root path in the same downward arena.
    auto begin = b.GetSize();
    auto row = C::CreateBenchRow(b, 42);
    b.PreAlign(sizeof(flatbuffers::uoffset_t), b.alignment());
    b.PushElement(b.ReferTo(row.o));
    auto data = flatbuffers::Offset<flatbuffers::Vector<uint8_t>>{
      b.PushElement(b.GetSize() - begin)};
    auto name = b.CreateString("rows");
    Zf::UInt128 sn{0, 123};
    C::RecordBuilder record{b};
    record.add_table(name);
    record.add_un(seq);
    record.add_sn(&sn);
    record.add_vn(-3);
    record.add_shard(1);
    record.add_data(data);
    auto value = record.Finish();
    C::MsgBuilder message{b};
    message.add_body_type(C::Body::Replication);
    message.add_body(value.Union());
    b.Finish(message.Finish());
  }
};

struct Native {
  using Builder = Zfb::IOBuilder<Zfb::Wire::MaxAlign>;
  static Builder make() { return Builder{new Zdb_::IOBufAlloc<>, RowAlign}; }
  static void save(Builder &b, uint64_t seq) {
    auto nested = b.startNested();
    auto root = Zfb::save(b, Row{42});
    auto data = b.endNested(nested, root);
    auto record = Zdb_::saveRecord(b, "rows", seq, 123, -3, 1, data);
    b.finish(Zdb_::saveMsg(b, record, M::Tag::Replication));
  }
};

template <typename Codec>
static auto save(typename Codec::Builder &b, uint64_t seq)
{
  Codec::save(b, seq);
  auto owner = b.buf();
  if (!owner) Zm::exit(1);
  unsigned length = owner->length;
  // Same application framing as saveHdr, applied equally to both builders.
  auto ptr = owner->prepend(sizeof(Zdb_::Hdr));
  new (ptr) Zdb_::Hdr{length};
  return owner;
}

template <typename Codec>
static void measure(ZuCSpan name, unsigned count, bool fresh)
{
  auto builder = []() {
    if constexpr (ZuIsSame<Codec, Native>{}) return Codec::make();
    else return typename Codec::Builder{new Zdb_::IOBufAlloc<>};
  }();
  auto owner = save<Codec>(builder, 1);
  auto hdr = reinterpret_cast<const Zdb_::Hdr *>(owner->data());
  ZuBSpan bytes{hdr->data(), hdr->length};
  flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
  if (owner->length != bytes.length() + sizeof(Zdb_::Hdr) ||
      !C::VerifyMsgBuffer(verifier) || !Zfb::verify<M::Root>(bytes)) Zm::exit(1);
  auto msg = C::GetMsg(bytes.data());
  auto record = msg->body_as_Replication();
  if (!record || record->table()->string_view() != "rows" || record->un() != 1 ||
      record->sn()->h() || record->sn()->l() != 123 || record->vn() != -3 ||
      record->shard() != 1 || !record->data()) Zm::exit(1);
  auto data = record->data();
  flatbuffers::Verifier inner{data->data(), data->size()};
  if (!inner.VerifyBuffer<C::BenchRow>(nullptr) ||
      !Zfb::verify<Row>({data->data(), data->size()}) ||
      flatbuffers::GetRoot<C::BenchRow>(data->data())->id() != 42 ||
      Zfb::view<Row>({data->data(), data->size()}).get<ZfField(Row, id)>() != 42)
    Zm::exit(1);
  owner->clear();
  builder.buf(owner);
  uint64_t sum = 0, total = 0;
  unsigned capacity = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    owner = save<Codec>(builder, i + 1);
    __asm__ __volatile__("" : : "g"(owner->data()), "g"(owner->length) : "memory");
    auto hdr = reinterpret_cast<const Zdb_::Hdr *>(owner->data());
    sum += C::GetMsg(hdr->data())->body_as_Replication()->un();
    total += owner->length;
    capacity = owner->size;
    if (fresh) owner = new Zdb_::IOBufAlloc<>;
    else owner->clear();
    builder.buf(owner);
  }
  auto elapsed = Zm::now() - begin;
  if (sum != ((uint64_t(count) * (uint64_t(count) + 1))>>1)) Zm::exit(1);
  std::cout << name << ',' << (fresh ? "fresh" : "reuse") << ',' << count
    << ',' << total << ',' << capacity << ',' << elapsed.as_decimal()
    << ',' << sum << '\n';
}

static int run(unsigned count, unsigned rounds)
{
  if (!count || !rounds) return 1;
  std::cout << "codec,mode,count,total_bytes,capacity,seconds,checksum\n";
  for (unsigned r = 0; r < rounds; ++r)
    for (unsigned mode = 0; mode < 2; ++mode) {
      if (r & 1) {
	measure<Native>("native", count, mode);
	measure<Canonical>("canonical", count, mode);
      } else {
	measure<Canonical>("canonical", count, mode);
	measure<Native>("native", count, mode);
      }
    }
  return 0;
}

// Growth observers are local to this diagnostic. They count actual backing
// allocation and relocation paths, including jumbo allocations not in telemetry.
struct Growth {
  uint64_t calls = 0;
  uint64_t allocs = 0;
  uint64_t head = 0;
  uint64_t tail = 0;
  uint64_t inplace = 0;

  void record(const ZiIOBuf *buf, unsigned next, unsigned h, unsigned t) {
    ++calls;
    if (uint64_t(next) + buf->skip > buf->size) {
      ++allocs;
      head += h;
      tail += t;
    } else
      inplace += t;
  }
};

struct SagaCanonical : public IOBuilder {
  Growth growth;

  using IOBuilder::IOBuilder;

  void save(const Zdb_::SagaData &row, uint64_t seq) {
    auto begin = GetSize();
    auto type = CreateString(row.type.data(), row.type.length());
    auto payload = CreateVector(row.data.data(), row.data.length());
    Zf::UInt128 id{uint64_t(row.id>>64), uint64_t(row.id)};
    Zf::Time deadline{row.deadline.sec(), row.deadline.nsec()};
    C::BenchSagaDataBuilder r{*this};
    r.add_type(type);
    r.add_id(&id);
    r.add_shard(row.shard);
    r.add_data(payload);
    r.add_deadline(&deadline);
    auto root = r.Finish();
    PreAlign(sizeof(flatbuffers::uoffset_t), minalign_);
    PushElement(ReferTo(root.o));
    auto data = flatbuffers::Offset<flatbuffers::Vector<uint8_t>>{
      PushElement(GetSize() - begin)};
    auto name = CreateString("saga");
    Zf::UInt128 sn{0, 123};
    C::RecordBuilder record{*this};
    record.add_table(name);
    record.add_un(seq);
    record.add_sn(&sn);
    record.add_vn(-3);
    record.add_shard(1);
    record.add_data(data);
    auto value = record.Finish();
    C::MsgBuilder message{*this};
    message.add_body_type(C::Body::Replication);
    message.add_body(value.Union());
    Finish(message.Finish());
  }

protected:
  uint8_t *reallocate_downward(uint8_t *old, size_t size, size_t next,
      size_t tail, size_t head) override {
    growth.record(cspan(), next, head, tail);
    return IOBuilder::reallocate_downward(old, size, next, tail, head);
  }
};

struct SagaStorage : Zfb::IOStorage<Zfb::Wire::MaxAlign> {
  using Base = Zfb::IOStorage<Zfb::Wire::MaxAlign>;
  Growth growth;

  using Base::Base;

  uint8_t *grow(unsigned size, unsigned next, unsigned head, unsigned tail) {
    growth.record(buffer(), next, head, tail);
    return Base::grow(size, next, head, tail);
  }
};

class SagaNative : public SagaStorage, public Zfb::Builder<SagaStorage> {
  using Base = Zfb::Builder<SagaStorage>;
public:
  explicit SagaNative(ZmRef<ZiIOBuf> owner) :
    SagaStorage{ZuMv(owner), Zfb::BufferAlign<Zdb_::SagaData>::N},
    Base{*this, Zfb::BufferAlign<Zdb_::SagaData>::N} { }

  void save(const Zdb_::SagaData &row, uint64_t seq) {
    auto nested = startNested();
    auto data = endNested(nested, Zfb::save(*this, row));
    auto record = Zdb_::saveRecord(*this, "saga", seq, 123, -3, 1, data);
    finish(Zdb_::saveMsg(*this, record, M::Tag::Replication));
    if (failed()) Zm::exit(1);
  }
  ZmRef<ZiIOBuf> buf() {
    auto owner = detach(Base::capacity(), Base::size());
    Base::reset();
    return owner;
  }
  void buf(ZmRef<ZiIOBuf> owner) {
    attach(ZuMv(owner));
    Base::reset();
  }
};

template <typename B>
static void sagaBuffer(ZuCSpan name, const Zdb_::SagaData &row,
    unsigned count, bool fresh)
{
  B b{new Zdb_::IOBufAlloc<Zdb_::SagaData>};
  b.save(row, 1);
  auto owner = b.buf();
  flatbuffers::Verifier v{owner->data(), owner->length};
  if (!C::VerifyMsgBuffer(v) || !Zfb::verify<M::Root>(owner->cspan())) Zm::exit(1);
  auto record = C::GetMsg(owner->data())->body_as_Replication();
  auto data = record->data();
  flatbuffers::Verifier inner{data->data(), data->size()};
  auto value = flatbuffers::GetRoot<C::BenchSagaData>(data->data());
  if (!inner.VerifyBuffer<C::BenchSagaData>(nullptr) ||
      !Zfb::verify<Zdb_::SagaData>({data->data(), data->size()}) ||
      value->type()->string_view() != "liveSaga" || value->id()->l() != 42 ||
      value->shard() != 0 || value->deadline()->sec() != 123 ||
      value->deadline()->nsec() != 456 || value->data()->size() != row.data.length() ||
      memcmp(value->data()->data(), row.data.data(), row.data.length())) Zm::exit(1);
  if (fresh) owner = new Zdb_::IOBufAlloc<Zdb_::SagaData>;
  else owner->clear();
  b.buf(owner);
  b.growth = {};
  uint64_t bytes = 0, sum = 0;
  unsigned capacity = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    b.save(row, i + 1);
    owner = b.buf();
    __asm__ __volatile__("" : : "g"(owner->data()), "g"(owner->length) : "memory");
    bytes += owner->length;
    sum += C::GetMsg(owner->data())->body_as_Replication()->un();
    capacity = owner->size;
    if (fresh) owner = new Zdb_::IOBufAlloc<Zdb_::SagaData>;
    else owner->clear();
    b.buf(owner);
  }
  auto elapsed = Zm::now() - begin;
  if (sum != ((uint64_t(count) * (uint64_t(count) + 1))>>1)) Zm::exit(1);
  auto &g = b.growth;
  std::cout << name << ',' << (fresh ? "fresh" : "reuse") << ','
    << row.data.length() << ',' << count << ',' << bytes << ',' << capacity << ','
    << g.calls << ',' << g.allocs << ',' << g.head << ',' << g.tail << ','
    << g.inplace << ',' << elapsed.as_decimal() << ',' << sum << '\n';
}

static int sagaBuffers(unsigned count, unsigned rounds, unsigned payload)
{
  if (!count || !rounds || !payload || payload >= Zdb_::MaxBufSize) return 1;
  Zdb_::SagaData row{"liveSaga", 42, 0, {}, ZuTime{123, 456}};
  row.data.length(payload);
  for (unsigned i = 0; i < payload; ++i) row.data[i] = uint8_t(i);
  std::cout << "codec,mode,payload,count,total_bytes,capacity,growth_calls,"
    "backing_allocs,head_bytes,tail_bytes,inplace_bytes,seconds,checksum\n";
  for (unsigned r = 0; r < rounds; ++r)
    for (unsigned mode = 0; mode < 2; ++mode) {
      if (r & 1) {
	sagaBuffer<SagaNative>("native", row, count, mode);
	sagaBuffer<SagaCanonical>("canonical", row, count, mode);
      } else {
	sagaBuffer<SagaCanonical>("canonical", row, count, mode);
	sagaBuffer<SagaNative>("native", row, count, mode);
      }
    }
  return 0;
}

} // Bench

int main(int argc, char **argv)
{
  if (argc == 5 && !strcmp(argv[1], "--saga-buffers")) {
#ifndef NDEBUG
    if (!getenv("ZFB_BENCH_CURRENT")) {
      std::cerr << "Set ZFB_BENCH_CURRENT for a current-configuration comparison\n";
      return 1;
    }
#endif
    return Bench::sagaBuffers(ZuBox<unsigned>{argv[2]}, ZuBox<unsigned>{argv[3]},
      ZuBox<unsigned>{argv[4]});
  }
  if (argc == 5 && !strcmp(argv[1], "--reads")) {
#ifndef NDEBUG
    if (!getenv("ZFB_BENCH_CURRENT")) {
      std::cerr << "Set ZFB_BENCH_CURRENT for a current-configuration comparison\n";
      return 1;
    }
#endif
    return Bench::reads(ZuBox<unsigned>{argv[2]}, ZuBox<unsigned>{argv[3]},
      ZuBox<unsigned>{argv[4]});
  }
  if (argc == 4 && !strcmp(argv[1], "--bench")) {
#ifndef NDEBUG
    if (!getenv("ZFB_BENCH_CURRENT")) {
      std::cerr << "Set ZFB_BENCH_CURRENT for a current-configuration comparison\n";
      return 1;
    }
#endif
    return Bench::run(ZuBox<unsigned>{argv[2]}, ZuBox<unsigned>{argv[3]});
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(schema);
  ZuTestCall(records);
  ZuTestCall(heartbeat);
  ZuTestCall(commit);
}
