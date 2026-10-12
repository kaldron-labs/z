//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Z Database message format

#ifndef ZdbMsg_HH
#define ZdbMsg_HH

#ifndef ZdbLib_HH
#include <zlib/ZdbLib.hh>
#endif

#include <limits.h>

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuUnion.hh>

#include <zlib/ZmRef.hh>

#include <zlib/ZiFB.hh>

#include <zlib/ZdbTypes.hh>

namespace Zdb_ {

// Borrowed message data. Variable-length members retain their source owner.
// These declarations also generate the canonical schema directly in memory.
namespace Msg {

struct TableState { ZuCSpan table; UN un; Shard shard; };
ZfStruct(ZdbAPI, (TableState, FB),
  (table,,			String),
  (un,		(Deflt<0>),	UInt64),
  (shard,	(Deflt<0>),	UInt8));
ZiFB::AsTable<"Zdb_.fbs.TableState"> ZiFB_Fmt(TableState *);

struct DBState { SN sn; ZuSpan<const TableState *> tableStates; };
ZfStruct(ZdbAPI, (DBState, FB),
  (sn,,			UInt128),
  (tableStates,,	UDT));
ZiFB::AsTable<"Zdb_.fbs.DBState"> ZiFB_Fmt(DBState *);

struct Heartbeat {
  ZuCSpan host;
  uint32_t state, nShards;
  const DBState *dbState;
};
ZfStruct(ZdbAPI, (Heartbeat, FB),
  (host,,			String),
  (state,	(Deflt<0>),	UInt32),
  (nShards,	(Deflt<0>),	UInt32),
  (dbState,,			UDT));
ZiFB::AsTable<"Zdb_.fbs.Heartbeat"> ZiFB_Fmt(Heartbeat *);

struct Record {
  ZuCSpan table;
  UN un;
  SN sn;
  VN vn;
  Shard shard;
  ZuBSpan data;
};
ZfStruct(ZdbAPI, (Record, FB),
  (table,,			String),
  (un,		(Deflt<0>),	UInt64),
  (sn,,				UInt128),
  (vn,		(Deflt<0>),	Int64),
  (shard,	(Deflt<0>),	UInt8),
  (data,,			Bytes));
ZiFB::AsTable<"Zdb_.fbs.Record"> ZiFB_Fmt(Record *);

struct Commit { ZuCSpan table; UN un; Shard shard; };
ZfStruct(ZdbAPI, (Commit, FB),
  (table,,			String),
  (un,		(Deflt<0>),	UInt64),
  (shard,	(Deflt<0>),	UInt8));
ZiFB::AsTable<"Zdb_.fbs.Commit"> ZiFB_Fmt(Commit *);

// Positional tags preserve distinct replication/recovery alternatives with
// the same payload type. The independent interop fixture checks these IDs.
namespace Tag { enum { None, Heartbeat, Replication, Recovery, Commit }; }
ZuDerive(Body, (ZuUnion<void, Heartbeat, Record, Record, Commit>));
ZiFB::AsUnion<"Zdb_.fbs.Body",
  ZiFB::Alt<"Heartbeat", Heartbeat>, ZiFB::Alt<"Replication", Record>,
  ZiFB::Alt<"Recovery", Record>, ZiFB::Alt<"Commit", Commit>> ZiFB_Fmt(Body *);
struct Root { Body body; };
ZfStruct(ZdbAPI, (Root, FB), (body,, UDT));
ZiFB::AsTable<"Zdb_.fbs.Msg"> ZiFB_Fmt(Root *);

using View = ZiFB::View<Root>;
using RecordView = ZiFB::View<Record>;
using HeartbeatView = ZiFB::View<Heartbeat>;
using CommitView = ZiFB::View<Commit>;

} // Msg

// Compose a message around a payload already written into the same arena.
template <typename B>
ZiFB::Offset saveMsg(B &fbb, ZiFB::Offset payload, unsigned tag) {
  using H = ZiFB::As<Msg::Root>::template Handler<Msg::Root>;
  return ZiFB::As<Msg::Root>::template withOffsets<H::N>(
    [&fbb, payload, tag](auto &fields) {
      auto table = fbb.startTable(fields.data());
      H::template fieldUnion<Msg::ZfField(Root, body)>(fbb, table,
	ZiFB::UnionOffset<Msg::Body>{payload, uint8_t(tag)});
      return fbb.endTable(table);
    });
}

// Keep the wide UN before the table offset to avoid commit-message padding.
template <typename B>
ZiFB::Offset saveCommit(B &fbb, ZuCSpan id, UN un, Shard shard) {
  auto name = fbb.string(id);
  using H = ZiFB::As<Msg::Commit>::template Handler<Msg::Commit>;
  return ZiFB::As<Msg::Commit>::template withOffsets<H::N>(
    [&fbb, name, un, shard](auto &fields) {
      auto table = fbb.startTable(fields.data());
      H::template field<Msg::ZfField(Commit, un)>(fbb, table, un);
      H::template fieldOffset<Msg::ZfField(Commit, table)>(fbb, table, name);
      H::template field<Msg::ZfField(Commit, shard)>(fbb, table, shard);
      return fbb.endTable(table);
    });
}

// The nested row has its own root and is already in the arena. Attach its
// byte-vector offset directly; no intermediate buffer or payload copy.
template <typename B>
ZiFB::Offset saveRecord(B &fbb, ZuCSpan id, UN un, SN sn, VN vn,
    Shard shard, ZiFB::Offset data) {
  auto name = fbb.string(id);
  using H = ZiFB::As<Msg::Record>::template Handler<Msg::Record>;
  return ZiFB::As<Msg::Record>::template withOffsets<H::N>(
    [&fbb, name, un, sn, vn, shard, data](auto &fields) {
      auto table = fbb.startTable(fields.data());
      H::template fieldOffset<Msg::ZfField(Record, table)>(fbb, table, name);
      H::template field<Msg::ZfField(Record, un)>(fbb, table, un);
      H::template field<Msg::ZfField(Record, sn)>(fbb, table, sn);
      H::template field<Msg::ZfField(Record, vn)>(fbb, table, vn);
      H::template field<Msg::ZfField(Record, shard)>(fbb, table, shard);
      H::template fieldOffset<Msg::ZfField(Record, data)>(fbb, table, data);
      return fbb.endTable(table);
    });
}

// --- message format - used for both file and network

// custom header with a little-endian uint32 length
#pragma pack(push, 4)
struct Hdr {
  ZuLittleEndian<uint32_t>	length;	// length of body

  const uint8_t *data() const {
    return reinterpret_cast<const uint8_t *>(this) + sizeof(Hdr);
  }
};
#pragma pack(pop)

// Call after finish() to prepend the application header and detach the owner.
template <typename Builder, typename Owner>
inline auto saveHdr(Builder &fbb, Owner *owner) {
  unsigned length = fbb.size();
  auto buf = fbb.buf();
  if (ZuUnlikely(!buf)) return buf;
  buf->owner = owner;
  auto ptr = buf->prepend(sizeof(Hdr));
  new (ptr) Hdr{length};
  return buf;
}
template <typename Builder>
inline auto saveHdr(Builder &fbb) {
  return saveHdr(fbb, static_cast<void *>(nullptr));
}

// returns the total length of the message including the header,
// INT_MAX if not enough bytes have been read yet, -1 if corrupt
template <typename Buf>
inline int loadHdr(const Buf *buf) {
  if (ZuUnlikely(buf->length < sizeof(Hdr))) return INT_MAX;
  auto hdr = buf->template ptr<Hdr>();
  unsigned length = hdr->length;
  if (ZuUnlikely(length > INT_MAX - sizeof(Hdr))) return -1;
  return sizeof(Hdr) + length;
}

// returns -1 if the header is invalid/corrupted, or lambda return
template <typename Buf, typename L>
inline int verifyHdr(ZmRef<Buf> buf, L &&l) {
  if (ZuUnlikely(buf->length < sizeof(Hdr))) return -1;
  auto hdr = buf->template ptr<Hdr>();
  unsigned length = hdr->length;
  if (length > (buf->length - sizeof(Hdr))) return -1;
  int i = ZuFwd<L>(l)(hdr, ZuMv(buf));
  if (i < 0) return i;
  return sizeof(Hdr) + i;
}

// payload data containing a single whole message
inline ZuBSpan msgData(const Hdr *hdr) {
  if (ZuUnlikely(!hdr)) return {};
  return {
    reinterpret_cast<const uint8_t *>(hdr),
    static_cast<unsigned>(sizeof(Hdr) + hdr->length)};
}

inline Msg::View msg_(const Hdr *hdr) {
  return ZiFB::view<Msg::Root>({hdr->data(), hdr->length});
}
inline Msg::View msg(const Hdr *hdr) {
  if (ZuUnlikely(!hdr)) return {};
  ZuBSpan bytes{hdr->data(), hdr->length};
  if (ZuUnlikely(!ZiFB::verify<Msg::Root>(bytes))) return {};
  return ZiFB::view<Msg::Root>(bytes);
}
// Trusted helpers assume the caller has established the message kind.
inline Msg::HeartbeatView hb_(Msg::View msg) {
  return Msg::HeartbeatView{msg.get<Msg::ZfField(Root, body)>().data()};
}
inline Msg::HeartbeatView hb(Msg::View msg) {
  if (!msg || msg.get<Msg::ZfField(Root, body)>().tag() != Msg::Tag::Heartbeat) return {};
  return hb_(msg);
}
inline bool recovery_(Msg::View msg) {
  return msg.get<Msg::ZfField(Root, body)>().tag() == Msg::Tag::Recovery;
}
inline bool recovery(Msg::View msg) { return msg && recovery_(msg); }
inline Msg::RecordView record_(Msg::View msg) {
  return Msg::RecordView{msg.get<Msg::ZfField(Root, body)>().data()};
}
inline Msg::RecordView record(Msg::View msg) {
  if (!msg) return {};
  switch (msg.get<Msg::ZfField(Root, body)>().tag()) {
    case Msg::Tag::Replication: case Msg::Tag::Recovery: return record_(msg);
    default: return {};
  }
}
template <typename T>
inline ZiFB::View<T> data_(Msg::RecordView record) {
  auto bytes = record.get<Msg::ZfField(Record, data)>();
  return bytes ? ZiFB::view<T>(bytes) : ZiFB::View<T>{};
}
template <typename T>
inline ZiFB::View<T> data(Msg::RecordView record) {
  if (!record) return {};
  auto bytes = record.get<Msg::ZfField(Record, data)>();
  if (!bytes) return {};
  auto vn = record.get<Msg::ZfField(Record, vn)>();
  bool valid = vn < 0 ?
    ZiFB::verify<T, ZuFacet::FB, ZfFieldFilter::Del>(bytes) :
    vn > 0 ? ZiFB::verify<T, ZuFacet::FB, ZfFieldFilter::Upd>(bytes) :
    ZiFB::verify<T>(bytes);
  if (!valid) return {};
  return ZiFB::view<T>(bytes);
}
inline Msg::CommitView commit_(Msg::View msg) {
  return Msg::CommitView{msg.get<Msg::ZfField(Root, body)>().data()};
}
inline Msg::CommitView commit(Msg::View msg) {
  if (!msg || msg.get<Msg::ZfField(Root, body)>().tag() != Msg::Tag::Commit) return {};
  return commit_(msg);
}

} // Zdb_

#endif /* ZdbMsg_HH */
