//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <string.h>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZgrpcCall.hh>
#include <zlib/ZgrpcFrame.hh>
#include <zlib/ZgrpcMeta.hh>

using namespace ZuTestUtil;
using namespace Zgrpc;

// Wire IDs match interop/zgrpc.proto. Views must retain decoded backing.
struct Packet {
  uint64_t seq = 0;
  ZuBSpan payload;
  uint32_t count = 0;
  uint32_t code = 0;
  bool waitCancel = false;
};
ZfStruct(, (Packet, PB),
  (seq, (Ctor<0>, PB::FieldID<1>), UInt64),
  (payload, (Ctor<1>, PB::FieldID<2>), Bytes),
  (count, (Ctor<2>, PB::FieldID<3>), UInt32),
  (code, (Ctor<3>, PB::FieldID<4>), UInt32),
  (waitCancel, (Ctor<4>, PB::FieldID<5>), Bool));
Zpb::AsMessage<"zgrpc.interop.Packet"> Zpb_Fmt(Packet *);

template <int K> struct Contract : Method {
  using Request = Packet;
  using Response = Packet;
  enum { Kind = K, ReqBorrowed = 1, ResBorrowed = 1 };
};

using Pool = Zi::IOBufBuiltin<
  ZiIOBuf,
  32,
  ZiIOBuf_DefltAlign,
  ZuStringT<"Zgrpc.ITest">,
  1U << 20>;

// Component integration before the transport adapter is available. This is
// deliberately not counted as HTTP or independent-peer qualification.
template <int K> class Pipe {
  enum {
    Rx = 1, Tx = 2, Fragment = 4096, WorkBatch = 2,
    Streamed = K == Kind::ClientStream || K == Kind::Duplex
  };
  using Call = ClientCall<Contract<K>>;
  using Ops = CallOps<Packet, Packet>;

  static inline const Ops ops{
    [](void *ptr, uint64_t id, uint64_t gen, Message<Packet> message) {
      auto self = static_cast<Pipe *>(ptr);
      self->m_txOK &= id == 1 && gen == 1;
      if (!self->m_txDir.message()) {
	self->m_txOK = false;
	self->m_messageDone.post();
	return false;
      }
      FrameError::T error;
      ZmRef<ZiIOBuf> initial = new Pool{ZmSingleton<typename Pool::Allocator>::instance()};
      initial->skip = 8;
      auto buf = encodeFrame(ZuMv(initial), 4, Default::MaxMessageBytes,
	message.object(), error);
      self->m_txOK &= buf && error == FrameError::OK;
      if (!buf) { self->m_messageDone.post(); return false; }
      self->m_txOK &= buf->skip >= 8 && buf->avail() >= 4;
      self->m_scheduler.run([self, buf = ZuMv(buf)]() mutable {
	self->receive_(ZuMv(buf), 0);
      }, Rx);
      return true;
    },
    [](void *ptr, uint64_t id, uint64_t gen) {
      auto self = static_cast<Pipe *>(ptr);
      self->m_txOK &= id == 1 && gen == 1 && self->m_txDir.end();
      self->m_call->invalidateTx_();
      self->m_scheduler.run([self]() {
	self->m_rxOK &= self->m_frame.end() && self->m_meta.end() &&
	  self->m_rxDir.end();
	++self->m_completions;
	self->m_call->invalidateRx_();
	self->m_endDone.post();
      }, Rx);
      return self->m_txOK;
    },
    nullptr, nullptr,
    [](void *ptr, uint64_t, uint64_t) {
      auto self = static_cast<Pipe *>(ptr);
      self->m_rxOK = false;
      self->m_call->invalidateRx_(true);
    }
  };

  void receive_(ZmRef<ZiIOBuf> buf, unsigned offset) {
    ++m_turns;
    for (unsigned work = 0; work < WorkBatch && offset < buf->length; ++work) {
      unsigned left = buf->length - offset;
      unsigned length = left < Fragment ? left : Fragment;
      unsigned used = 0;
      Frame frame;
      auto result = m_frame.feed({buf->data() + offset, length}, nullptr,
	used, frame, Fragment);
      offset += used;
      if (result == FrameResult::Failed || !used) {
	m_rxOK = false;
	m_messageDone.post();
	return;
      }
      if (result == FrameResult::Message) {
	auto decoded = decodeFrame<Packet, ZuFacet::PB, true>(frame);
	m_rxOK &= decoded.template is<Message<Packet>>() && m_rxDir.message() &&
	  offset == buf->length;
	if (decoded.template is<Message<Packet>>()) {
	  m_last = ZuMv(decoded);
	  auto &message = m_last.template p<Message<Packet>>();
	  // Drop transport storage before examining the borrowed native value.
	  buf = nullptr;
	  frame = {};
	  auto &packet = message.object();
	  m_rxOK &= packet.seq == (m_empty ? 0 : m_rxDir.count()) &&
	    packet.payload == m_payload && !packet.count && !packet.code &&
	    !packet.waitCancel;
	  if (!m_empty) m_rxOK &= bool(message.backing);
	}
	m_messageDone.post();
	return;
      }
    }
    if (offset < buf->length) {
      m_scheduler.run([this, buf = ZuMv(buf), offset]() mutable {
	receive_(ZuMv(buf), offset);
      }, Rx);
      return;
    }
    m_rxOK = false; // each test input must contain one complete envelope
    m_messageDone.post();
  }

public:
  Pipe(ZuBSpan payload, bool empty) :
    m_scheduler{ZmSchedParams{}.nThreads(2)
      .thread(Rx, [](auto &thread) { thread.isolated(1); })
      .thread(Tx, [](auto &thread) { thread.isolated(1); })},
    m_payload{payload}, m_empty{empty},
    m_call{new Call{1, 1, {&m_scheduler, Rx}, this,
      {&m_scheduler, Tx}, this, Default::MaxQueue, ops}} { }

  void run() {
    ZuTestScope(run);
    ZmSemaphore ready;
    m_scheduler.run([this, &ready]() {
      namespace S = Zhttp::FieldSection;
      m_rxOK &= m_meta.header(S::Final, "content-type", "application/grpc") &&
	m_meta.header(S::Final, "te", "trailers") &&
	m_meta.header(S::Final, "x-test-bin", "ACz/,c2Vjb25k") && m_meta.initial();
      auto fields = m_meta.metadata().fields();
      m_rxOK &= fields.length() == 2;
      if (fields.length() == 2) {
	const uint8_t binary[] = {0, ',', 255};
	m_rxOK &= m_meta.metadata().value(fields[0]) == ZuBSpan{binary} &&
	  m_meta.metadata().value(fields[1]) == ZuBSpan{"second"};
      }
      ready.post();
    }, Rx);
    ZuCheck(m_scheduler.start(), (return));
    ready.wait();
    enum { Count = Streamed ? 3 : 1 };
    {
      ZuTestRepeat(message, Count);
      for (unsigned i = 0; i < Count; ++i) {
	bool admitted = m_call->send(Packet{m_empty ? 0 : i + 1, m_payload});
	ZuCheck(admitted);
	if (!admitted) break;
	m_messageDone.wait();
      }
    }
    bool ended = m_call->end();
    ZuCheck(ended);
    if (ended) m_endDone.wait();
    ZuCheck(m_scheduler.stop());
    ZuCheck(m_rxOK && m_txOK && m_completions == 1);
    ZuCheck(m_txDir.count() == Count && m_rxDir.count() == Count);
    ZuCheck(!m_call->live() && !m_call->send(Packet{}) && !m_call->end());
    ZuCheck(m_turns >= (m_empty ? 1 : Count<<5));
  }

private:
  ZmScheduler m_scheduler;
  const ZuBSpan m_payload;
  const bool m_empty;
  ZmRef<Call> m_call;
  ZmSemaphore m_messageDone, m_endDone;

  alignas(Zm::CacheLineSize)
  Direction m_rxDir{Streamed};
  FrameRx m_frame;
  MetaRx m_meta;
  Decoded<Packet> m_last;
  unsigned m_turns = 0;
  unsigned m_completions = 0;
  bool m_rxOK = true;

  alignas(Zm::CacheLineSize)
  Direction m_txDir{Streamed};
  bool m_txOK = true;
};

template <int K> static void pipeline()
{
  ZuTestScope(pipeline);
  Bytes payload;
  payload.length((256U << 10) + 7);
  memset(payload.data(), 0x71, payload.length());
  Pipe<K> pipe{payload, false};
  ZuTestCall(pipe.run);
}

static void emptyMessage()
{
  ZuTestScope(emptyMessage);
  Pipe<Kind::Unary> pipe{{}, true};
  ZuTestCall(pipe.run);
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall((pipeline<Kind::Unary>));
  ZuTestCall((pipeline<Kind::ServerStream>));
  ZuTestCall((pipeline<Kind::ClientStream>));
  ZuTestCall((pipeline<Kind::Duplex>));
  ZuTestCall(emptyMessage);
}
