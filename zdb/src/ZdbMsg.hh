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

#include <zlib/ZuByteSwap.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmRef.hh>

#include <zlib/Zfb.hh>

#include <zlib/ZdbTypes.hh>

#include <zlib/zdb__fbs.h>

namespace Zdb_ {

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

// call following Finish() to push header and detach buffer
template <typename Builder, typename Owner>
inline auto saveHdr(Builder &fbb, Owner *owner) {
  unsigned length = fbb.GetSize();
  auto buf = fbb.buf();
  buf->owner = owner;
  auto ptr = buf->prepend(sizeof(Hdr));
  if (ZuUnlikely(!ptr)) return decltype(buf){};
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
  return sizeof(Hdr) + uint32_t(hdr->length);
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

inline const fbs::Msg *msg(const Hdr *hdr) {
  if (ZuUnlikely(!hdr)) return nullptr;
  auto data = hdr->data();
  if (ZuUnlikely((!Zfb::Verifier{data, hdr->length}.VerifyBuffer<fbs::Msg>())))
    return nullptr;
  return Zfb::GetRoot<fbs::Msg>(data);
}
inline const fbs::Msg *msg_(const Hdr *hdr) {
  return Zfb::GetRoot<fbs::Msg>(hdr->data());
}
inline const fbs::Heartbeat *hb_(const fbs::Msg *msg) {
  return static_cast<const fbs::Heartbeat *>(msg->body());
}
inline const fbs::Heartbeat *hb(const fbs::Msg *msg) {
  if (ZuUnlikely(!msg)) return nullptr;
  switch (msg->body_type()) {
    default:
      return nullptr;
    case fbs::Body::Heartbeat:
      return hb_(msg);
  }
}
inline bool recovery(const fbs::Msg *msg) {
  if (ZuUnlikely(!msg)) return false;
  return msg->body_type() == fbs::Body::Recovery;
}
inline bool recovery_(const fbs::Msg *msg) {
  return msg->body_type() == fbs::Body::Recovery;
}
inline const fbs::Record *record_(const fbs::Msg *msg) {
  return static_cast<const fbs::Record *>(msg->body());
}
inline const fbs::Record *record(const fbs::Msg *msg) {
  if (ZuUnlikely(!msg)) return nullptr;
  switch (msg->body_type()) {
    default:
      return nullptr;
    case fbs::Body::Replication:
    case fbs::Body::Recovery:
      return record_(msg);
  }
}
template <typename T>
inline const T *data(const fbs::Record *record) {
  if (ZuUnlikely(!record)) return nullptr;
  auto data = Zfb::Load::bytes(record->data());
  if (ZuUnlikely(!data)) return nullptr;
  if (ZuUnlikely((
	!Zfb::Verifier{data.data(), data.length()}.VerifyBuffer<T>())))
    return nullptr;
  return Zfb::GetRoot<T>(data.data());
}
template <typename T>
inline const T *data_(const fbs::Record *record) {
  auto data = Zfb::Load::bytes(record->data());
  if (ZuUnlikely(!data)) return nullptr;
  return Zfb::GetRoot<T>(data.data());
}
inline const fbs::Commit *commit_(const fbs::Msg *msg) {
  return static_cast<const fbs::Commit *>(msg->body());
}
inline const fbs::Commit *commit(const fbs::Msg *msg) {
  if (ZuUnlikely(!msg)) return nullptr;
  switch (msg->body_type()) {
    default:
      return nullptr;
    case fbs::Body::Commit:
      return commit_(msg);
  }
}

} // Zdb_

#endif /* ZdbMsg_HH */
