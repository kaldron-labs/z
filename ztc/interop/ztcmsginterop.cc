//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <flatbuffers/idl.h>
#include <flatbuffers/reflection.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuBox.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtcMsg.hh>

#include "telemetry_generated.h"
#include "../../zi/interop/iobaseline.hh"
#include <zlib/ZiFBReflect.hh>

using namespace ZuTestUtil;
using Text = ZtString<ZtStringHeapID<"ztcmsginterop.Text">>;

template <typename O>
static void schema()
{
  ZuTestScope(schema);
  auto bytes = ZtScratch(Text, ZiFB::SchemaOut::Schema<O>::Size);
  ZiFB::saveSchema<O>(bytes);
  ZuBSpan span{bytes};
  flatbuffers::Verifier verifier{span.data(), span.length()};
  bool valid = reflection::VerifySchemaBuffer(verifier);
  ZuCheck(valid);
  if (!valid) return;
  flatbuffers::Parser imported;
  bool loaded = imported.Deserialize(span.data(), span.length());
  ZuCheck(loaded);
  if (!loaded) { log(imported.error_); return; }
  auto source = ZtScratch(Text, 0);
  ZiFB::saveFBS<O>(source);
  flatbuffers::Parser parsed;
  parsed.opts.lang_to_generate = flatbuffers::IDLOptions::kCpp;
  bool compiled = parsed.Parse(source.data());
  ZuCheck(compiled);
  if (!compiled) { log(parsed.error_); return; }
  parsed.Serialize();
  auto binary = reflection::GetSchema(span.data());
  auto text = reflection::GetSchema(parsed.builder_.GetBufferPointer());
  ZuCheck(binary->root_table()->name()->str() == text->root_table()->name()->str());
  ZuCheck(binary->objects()->size() == text->objects()->size());
  for (auto object : *binary->objects()) {
    auto other = text->objects()->LookupByKey(object->name()->c_str());
    ZuCheck(other);
    if (!other) continue;
    ZuCheck(object->fields()->size() == other->fields()->size());
    for (auto field : *object->fields()) {
      auto peer = other->fields()->LookupByKey(field->name()->c_str());
      ZuCheck(peer);
      if (!peer) continue;
      ZuCheck(field->id() == peer->id());
      ZuCheck(field->offset() == peer->offset());
      ZuCheck(field->type()->base_type() == peer->type()->base_type());
      ZuCheck(field->type()->element() == peer->type()->element());
      auto deflt = peer->default_integer();
      if (field->type()->base_type() == reflection::ULong) {
	// Canonical 25.12.19 FieldDef::Serialize uses StringToInt for
	// unsigned defaults too, losing values above INT64_MAX. Compare
	// against the parsed constant before that lossy serialization.
	auto objectDef = parsed.structs_.Lookup(object->name()->c_str());
	auto fieldDef = objectDef->fields.Lookup(field->name()->c_str());
	uint64_t value = 0;
	ZuCheck(flatbuffers::StringToNumber(fieldDef->value.constant.c_str(), &value));
	deflt = int64_t(value);
      }
      ZuCheck(field->default_integer() == deflt);
      if (field->default_integer() != deflt)
	log(object->name()->c_str(), '.', field->name()->c_str(),
	  ": BFBS ", field->default_integer(), ", FBS ", deflt);
    }
  }
}

static void request()
{
  ZuTestScope(request);
  Ztc::Builder builder{new ZiIOBufAlloc<>};
  auto nested = builder.startNested();
  Ztc::Request request;
  request.filter = "heap*";
  request.id = "publisher";
  request.seqNo = 31;
  request.group = Ztc::Group::Heap;
  request.interval = 7;
  request.subscribe = true;
  auto payload = ZiFB::save(builder, request);
  builder.finish(Ztc::saveMsg(builder, nested, Ztc::Body::Request,
    payload, 19, "device", 23));
  auto owner = builder.buf();
  ZuCheck(owner);
  if (!owner) return;
  flatbuffers::Verifier verifier{owner->data(), owner->length};
  ZuCheck(Peer::VerifyMsgBuffer(verifier));
  auto envelope = Peer::GetMsg(owner->data());
  auto data = envelope->data();
  flatbuffers::Verifier bodyVerifier{data->data(), data->size()};
  ZuCheck(bodyVerifier.VerifyBuffer<Peer::Payload>(nullptr));
  auto body = flatbuffers::GetRoot<Peer::Payload>(data->data());
  auto value = body->body_as_Request();
  ZuCheck(value);
  if (!value) return;
  ZuCheck(value->seq_no() == 31 && value->group() == 0);
  ZuCheck(value->filter()->string_view() == "heap*");
  ZuCheck(value->id()->string_view() == "publisher");
  ZuCheck(value->interval() == 7 && value->subscribe());
  ZuCheck(value->alert_date() == 0 && value->alert_seq_no() == 0);
  ZuCheck(envelope->sub_id() == 19 && envelope->agent_gen() == 23);
  ZuCheck(envelope->device_id()->string_view() == "device");

  flatbuffers::FlatBufferBuilder peer;
  auto filter = peer.CreateString("");
  auto req = Peer::CreateRequest(peer, 41, 8, filter, 11, false, 20260101, 9);
  peer.Finish(Peer::CreatePayload(peer, Peer::Body::Request, req.Union()));
  flatbuffers::FlatBufferBuilder outer;
  auto bytes = outer.CreateVector(peer.GetBufferPointer(), peer.GetSize());
  outer.Finish(Peer::CreateMsg(outer, bytes, 53));
  auto msg = Ztc::msg(ZuBSpan{outer.GetBufferPointer(), outer.GetSize()});
  ZuCheck(msg);
  if (!msg) return;
  auto view = Ztc::msgBody(msg).get<Ztc::Body::Request>();
  ZuCheck(view.get<Ztc::ZfField(Request, seqNo)>() == 41);
  ZuCheck(view.get<Ztc::ZfField(Request, group)>() == Ztc::Group::Alert);
  ZuCheck(view.has<Ztc::ZfField(Request, filter)>());
  ZuCheck(!view.get<Ztc::ZfField(Request, filter)>());
  ZuCheck(!view.has<Ztc::ZfField(Request, id)>());
  ZuCheck(view.get<Ztc::ZfField(Request, alertDate)>() == 20260101);
  ZuCheck(view.get<Ztc::ZfField(Request, alertSeqNo)>() == 9);
}

static void copy()
{
  ZuTestScope(copy);
  flatbuffers::FlatBufferBuilder peer;
  peer.ForceDefaults(true);
  auto shutdown = Peer::CreateShutdown(peer);
  auto publisher = peer.CreateString("publisher");
  auto telemetry = Peer::CreateTelemetry(peer, publisher, 0,
    Peer::TelemetryBody::Shutdown, shutdown.Union());
  peer.Finish(Peer::CreatePayload(peer, Peer::Body::Telemetry, telemetry.Union()));
  ZuBSpan original{peer.GetBufferPointer(), peer.GetSize()};
  ZuCheck(ZiFB::verify<Ztc::Payload>(original));
  Ztc::Builder builder{new ZiIOBufAlloc<>};
  auto data = builder.copyRoot(original, Ztc::Builder::Align);
  builder.finish(Ztc::saveMsg(builder, data, 59, "routed", 61));
  auto owner = builder.buf();
  ZuCheck(owner);
  if (!owner) return;
  auto msg = Ztc::msg(owner->cspan());
  ZuCheck(msg);
  if (!msg) return;
  auto bytes = msg.get<Ztc::ZfField(Msg, data)>();
  ZuCheck(bytes.length() == original.length());
  ZuCheck(!memcmp(bytes.data(), original.data(), bytes.length()));
  peer.Reset(); // The copy must have no reference to the source arena.
  flatbuffers::Verifier verifier{bytes.data(), bytes.length()};
  ZuCheck(verifier.VerifyBuffer<Peer::Payload>(nullptr));
  auto value = Ztc::msgBody(msg).get<Ztc::Body::Telemetry>();
  auto seq = value.field<Ztc::ZfField(Telemetry, seqNo)>();
  ZuCheck(seq); // Explicitly present zero survives the routing copy.
  if (!seq) return;
  ZiFB::write<uint64_t>(const_cast<uint8_t *>(seq), 67);
  auto canonical = flatbuffers::GetRoot<Peer::Payload>(bytes.data());
  ZuCheck(canonical->body_as_Telemetry()->seq_no() == 67);
  ZuCheck(canonical->body_as_Telemetry()->value_type() == Peer::TelemetryBody::Shutdown);
  ZiFB::write<uint32_t>(const_cast<uint8_t *>(bytes.data()), UINT32_MAX);
  ZuCheck(ZiFB::verify<Ztc::Msg>(owner->cspan()));
  ZuCheck(!Ztc::msg(owner->cspan())); // Ingress also verifies the nested root.
}

namespace Bench {

struct Canonical {
  class Builder : public IOBuilder {
  public:
    using IOBuilder::IOBuilder;
    unsigned alignment() const { return minalign_; }
  };

  template <bool Copy>
  static void save(Builder &b, const Ztc::Request &row, ZuBSpan original, uint64_t seq)
  {
    flatbuffers::Offset<flatbuffers::Vector<uint8_t>> data;
    if constexpr (Copy) {
      b.ForceVectorAlignment(original.length(), 1, IOBuilder::Align);
      data = b.CreateVector(original.data(), original.length());
    } else {
      // The old Zfb::nest path: finish a root directly inside the byte vector.
      auto begin = b.GetSize();
      auto filter = b.CreateString(row.filter.data(), row.filter.length());
      ZuCSpan publisher = row.id;
      auto id = b.CreateString(publisher.data(), publisher.length());
      Peer::RequestBuilder request{b};
      request.add_seq_no(row.seqNo);
      request.add_group(row.group);
      request.add_filter(filter);
      request.add_interval(row.interval);
      request.add_subscribe(row.subscribe);
      request.add_alert_date(row.alertDate);
      request.add_alert_seq_no(row.alertSeqNo);
      request.add_id(id);
      auto value = request.Finish();
      Peer::PayloadBuilder payload{b};
      payload.add_body_type(Peer::Body::Request);
      payload.add_body(value.Union());
      auto root = payload.Finish();
      b.PreAlign(sizeof(flatbuffers::uoffset_t), b.alignment());
      b.PushElement(b.ReferTo(root.o));
      data = flatbuffers::Offset<flatbuffers::Vector<uint8_t>>{
	b.PushElement(b.GetSize() - begin)};
    }
    auto device = b.CreateString("device");
    Peer::MsgBuilder msg{b};
    msg.add_data(data);
    msg.add_sub_id(seq);
    msg.add_device_id(device);
    msg.add_agent_gen(23);
    b.Finish(msg.Finish());
  }
};

struct Native {
  using Builder = Ztc::Builder;

  template <bool Copy>
  static void save(Builder &b, const Ztc::Request &row, ZuBSpan original, uint64_t seq)
  {
    if constexpr (Copy) {
      auto data = b.copyRoot(original, IOBuilder::Align);
      b.finish(Ztc::saveMsg(b, data, seq, "device", 23));
    } else {
      auto nested = b.startNested();
      auto value = ZiFB::save(b, row);
      b.finish(Ztc::saveMsg(b, nested, Ztc::Body::Request,
	value, seq, "device", 23));
    }
  }
};

template <typename Codec, bool Copy>
static void measure(ZuCSpan name, const Ztc::Request &row, ZuBSpan original,
    unsigned count, bool fresh)
{
  ZmRef<ZiIOBuf> owner = new ZiIOBufAlloc<>;
  typename Codec::Builder builder{owner};
  // Verify both roots and their independent field expectations before timing.
  Codec::template save<Copy>(builder, row, original, 1);
  owner = builder.buf();
  if (!owner || !Ztc::msg(owner->cspan())) Zm::exit(1);
  flatbuffers::Verifier outer{owner->data(), owner->length};
  if (!Peer::VerifyMsgBuffer(outer)) Zm::exit(1);
  auto msg = Peer::GetMsg(owner->data());
  auto data = msg->data();
  flatbuffers::Verifier inner{data->data(), data->size()};
  if (!inner.VerifyBuffer<Peer::Payload>(nullptr)) Zm::exit(1);
  auto request = flatbuffers::GetRoot<Peer::Payload>(data->data())->body_as_Request();
  if (!request || request->seq_no() != 31 || request->group() != 0 ||
      request->filter()->string_view() != "heap*" ||
      request->id()->string_view() != "publisher" ||
      request->interval() != 7 || !request->subscribe() ||
      request->alert_date() || request->alert_seq_no() ||
      msg->sub_id() != 1 || msg->device_id()->string_view() != "device" ||
      msg->agent_gen() != 23) Zm::exit(1);
  if constexpr (Copy)
    if (data->size() != original.length() ||
	memcmp(data->data(), original.data(), original.length())) Zm::exit(1);
  owner->clear();
  builder.buf(owner);
  uint64_t sum = 0, bytes = 0;
  auto begin = Zm::now();
  for (unsigned i = 0; i < count; ++i) {
    Codec::template save<Copy>(builder, row, original, i + 1);
    owner = builder.buf();
    if (!owner) Zm::exit(1);
    __asm__ __volatile__("" : : "g"(owner->data()), "g"(owner->length) : "memory");
    sum += Peer::GetMsg(owner->data())->sub_id();
    bytes += owner->length;
    if (fresh) owner = new ZiIOBufAlloc<>;
    else owner->clear();
    builder.buf(owner);
  }
  auto elapsed = Zm::now() - begin;
  if (sum != ((uint64_t(count) * (uint64_t(count) + 1))>>1)) Zm::exit(1);
  std::cout << name << ',' << (Copy ? "copy" : "nested") << ',' <<
    (fresh ? "fresh" : "reuse") << ',' << count << ',' << bytes << ',' <<
    elapsed.as_decimal() << ',' << sum << '\n';
}

static int run(unsigned count, unsigned rounds)
{
  if (!count || !rounds) return 1;
  Ztc::Request row;
  row.filter = "heap*";
  row.id = "publisher";
  row.seqNo = 31;
  row.group = Ztc::Group::Heap;
  row.interval = 7;
  row.subscribe = true;
  Canonical::Builder builder{new ZiIOBufAlloc<>};
  Canonical::save<false>(builder, row, {}, 1);
  auto original = builder.buf();
  auto data = Peer::GetMsg(original->data())->data();
  ZuBSpan bytes{data->data(), data->size()};
  std::cout << "codec,operation,mode,count,total_bytes,seconds,checksum\n";
  for (unsigned r = 0; r < rounds; ++r)
    ZuUnroll::all<2>([r, &row, bytes, count](auto copy) {
      for (unsigned mode = 0; mode < 2; ++mode) {
	if (r & 1) {
	  measure<Native, copy>("native", row, bytes, count, mode);
	  measure<Canonical, copy>("canonical", row, bytes, count, mode);
	} else {
	  measure<Canonical, copy>("canonical", row, bytes, count, mode);
	  measure<Native, copy>("native", row, bytes, count, mode);
	}
      }
    });
  return 0;
}

} // Bench

int main(int argc, char **argv)
{
  if (argc == 4 && !strcmp(argv[1], "--bench")) {
#ifdef NDEBUG
    return Bench::run(ZuBox<unsigned>{argv[2]}, ZuBox<unsigned>{argv[3]});
#else
    std::cerr << "Performance measurements require a release build\n";
    return 1;
#endif
  }
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(schema<Ztc::Msg>);
  ZuTestCall(schema<Ztc::Payload>);
  ZuTestCall(request);
  ZuTestCall(copy);
  return 0;
}
