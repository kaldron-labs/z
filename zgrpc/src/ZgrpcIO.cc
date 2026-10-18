//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZgrpcIO.hh>

namespace Zgrpc {

RxInput::RxInput(bool response, bool streamed, Limits limits, unsigned headerMax) :
  m_limits{limits}, m_response{response}, m_meta{response, headerMax},
  m_frames{limits.maxMessageBytes}, m_direction{streamed},
  m_retainedMax{limits.maxQueueBytes}
{
  if (!valid(limits)) { m_failed = true; m_code = Code::RESOURCE_EXHAUSTED; }
}

void RxInput::bind_(Zhttp::Channel *channel)
{
  if (!channel || m_channel) return;
  m_channel = channel;
  m_route = {channel->mx(), channel->rxThread()};
}

bool RxInput::accept(Zhttp::Channel *channel)
{
  if (m_failed || m_finished || m_accepted) return false;
  bind_(channel);
  if (!m_meta.initial()) { fail(m_meta.status().code); return false; }
  m_accepted = true;
  try { if (m_onAccept) m_onAccept(channel, *this); }
  catch (...) { fail(Code::INTERNAL); }
  m_onAccept = {};
  return !m_failed;
}

bool RxInput::enqueue(ZmRef<ZiIOBuf> buf)
{
  if (!buf || m_failed || m_finished || m_peerEnd || !m_accepted) return false;
  if (m_chunks.count_() >= m_limits.maxQueue ||
      m_retainedBytes > m_retainedMax || buf->length > m_retainedMax - m_retainedBytes) {
    fail(Code::RESOURCE_EXHAUSTED);
    return false;
  }
  if (!buf->length) return true;
  m_retainedBytes += buf->length;
  m_chunks.push(RxChunk{ZuMv(buf)});
  kick_();
  return true;
}

void RxInput::complete(Zhttp::Channel *channel, bool ok)
{
  if (m_finished || m_peerEnd) return;
  bind_(channel);
  m_peerEnd = true;
  m_httpOK = ok;
  kick_();
}

void RxInput::fail(Code::T code)
{
  if (m_finished || m_failed) return;
  m_failed = true;
  m_code = code;
  if (m_channel) m_channel->disconnect();
  kick_();
}

void RxInput::kick_()
{
  if (m_scheduled || m_finished) return;
  m_scheduled = true;
  ZmRef<RxInput> self{this};
  if (m_route.scheduler)
    m_route.run([self = ZuMv(self)]() { self->pump_(); });
  else while (m_scheduled && !m_finished) pump_();
}

void RxInput::pump_()
{
  ZiAssert(m_route.invoked(), "Zgrpc", (), "receive outside Rx", return);
  // Input is already detached. Process bounded bytes and messages this turn.
  unsigned bytes = ZiIOBuf_DefltSize;
  unsigned work = m_limits.workBatch;
  while (!m_failed && bytes && work && m_chunks.count_()) {
    auto &chunk = m_chunks.headPtr()->data();
    unsigned used = 0;
    Frame frame;
    auto span = chunk.buf->cspan().offset(chunk.offset);
    auto result = m_frames.feed(span, chunk.buf, used, frame, bytes);
    chunk.offset += used;
    m_retainedBytes -= used;
    bytes -= used;
    if (chunk.offset == chunk.buf->length) m_chunks.shift();
    if (result == FrameResult::Failed) {
      Code::T code = Code::INTERNAL;
      switch (m_frames.error()) {
	case FrameError::Compression: code = Code::UNIMPLEMENTED; break;
	case FrameError::Limit: code = Code::RESOURCE_EXHAUSTED; break;
	default: break;
      }
      fail(code);
      break;
    }
    if (result == FrameResult::Message) {
      --work;
      bool accepted = m_direction.message();
      try { if (accepted && m_onFrame) accepted = m_onFrame(ZuMv(frame)); }
      catch (...) { accepted = false; }
      // A rejected protobuf payload follows the same stream failure path.
      if (!accepted) fail(Code::INTERNAL);
    } else if (!used) { fail(Code::INTERNAL); break; }
  }
  m_scheduled = false;
  if (m_failed || (!m_chunks.count_() && m_peerEnd)) { finish_(); return; }
  if (m_chunks.count_()) {
    if (m_route.scheduler) kick_();
    else m_scheduled = true;
  }
}

void RxInput::finish_()
{
  if (m_finished) return;
  if (!m_failed) {
    if (!m_httpOK) {
      m_httpFailed = true;
      // A metadata diagnostic takes precedence over a resulting HTTP reset.
      m_code = m_meta.failed() ? m_meta.status().code : Code::UNAVAILABLE;
      m_failed = true;
    } else if (!m_frames.end()) { m_code = Code::INTERNAL; m_failed = true; }
    else if (!m_meta.end()) { m_code = m_meta.status().code; m_failed = true; }
    else if (!m_direction.end(!m_response || m_meta.status().code == Code::OK)) {
      m_code = Code::INTERNAL; m_failed = true;
    } else m_code = m_response ? m_meta.status().code : Code::OK;
    // HTTP has already terminated failed input, including local disposal after
    // an early response. Only a new RPC error on successful HTTP needs aborting.
    if (m_failed && m_httpOK && m_channel) m_channel->disconnect();
  }
  m_finished = true;
  m_chunks.clean();
  m_retainedBytes = 0;
  m_onFrame = {};
  m_onAccept = {};
  auto done = ZuMv(m_onEnd);
  // The terminal callback may release the endpoint's last registered work.
  // Drop transport ownership before permitting that drain to finish.
  m_channel.reset();
  try { if (done) done(*this, !m_failed); } catch (...) { }
}

bool TxFrames::push(ZmRef<ZiIOBuf> buf, const Limits &limits)
{
  if (!buf || m_frames.count_() >= limits.maxQueue ||
      m_bytes > limits.maxQueueBytes || buf->length > limits.maxQueueBytes - m_bytes)
    return false;
  m_bytes += buf->length;
  m_frames.push(ZuMv(buf));
  return true;
}

ZmRef<ZiIOBuf> TxFrames::shift()
{
  auto node = m_frames.shift();
  if (!node) return nullptr;
  auto buf = ZuMv(node->data());
  m_bytes -= buf->length;
  return buf;
}

bool TxBody::attach(Zhttp::BodyEmitter emit)
{
  if (m_failed || m_direction.ended() || m_emit.is<Zhttp::BodyEmitter>()) return false;
  m_emit = emit;
  bool written = false;
  bool admitted = emit([&written](auto &out) {
    written = out.flush() && !out.failed();
    return written ? Zhttp::WriteOutcome::Stream : Zhttp::WriteOutcome::Failed;
  });
  if (!admitted || !written) return failed_(Code::INTERNAL);
  return true;
}

bool TxBody::end()
{
  if (!ready() || !m_direction.end()) return abort(Code::INTERNAL);
  bool written = false;
  auto emit = m_emit.p<Zhttp::BodyEmitter>();
  bool admitted = emit([&written](auto &out) {
    written = out.flush() && !out.failed();
    return written ? Zhttp::WriteOutcome::End : Zhttp::WriteOutcome::Failed;
  });
  m_emit = {};
  return admitted && written ? true : failed_(Code::INTERNAL);
}

bool TxBody::abort(Code::T code)
{
  if (ready()) {
    auto emit = m_emit.p<Zhttp::BodyEmitter>();
    (void)emit([](auto &) {
      return Zhttp::WriteOutcome::Failed;
    });
  }
  m_emit = {};
  return failed_(code);
}

} // Zgrpc
