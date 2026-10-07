//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// JSON-RPC stdio framing and worker I/O

#ifndef ZjrpcStdio_HH
#define ZjrpcStdio_HH

#ifndef ZjrpcLib_HH
#include <zlib/ZjrpcLib.hh>
#endif

#include <zlib/ZuID.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmList.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmPLock.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>

#include <zlib/ZiAssert.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiIOBuf.hh>
#include <zlib/ZiMultiplex.hh>

#include <zlib/Zjrpc.hh>
#include <zlib/ZjrpcCompletion.hh>
#include <zlib/ZjrpcIO.hh>

namespace Zjrpc {

namespace Private {
  ZjrpcExtern uintptr_t stdioThread();
  ZjrpcExtern void interruptStdio(uintptr_t);
  ZjrpcExtern void closeStdioThread(uintptr_t);
}

class StdioConfig {
public:
  StdioConfig() :
    m_input{ZiFile::stdIn().handle()},
    m_output{ZiFile::stdOut().handle()} { }

  const Limits &limits() const { return m_limits; }
  ZuCSpan rxThread() const { return m_rxThread; }
  ZuCSpan txThread() const { return m_txThread; }
  Zi::Handle input() const { return m_input; }
  Zi::Handle output() const { return m_output; }

  StdioConfig &limits(Limits v) { m_limits = ZuMv(v); return *this; }
  StdioConfig &rxThread(ZuCSpan v) { m_rxThread = v; return *this; }
  StdioConfig &txThread(ZuCSpan v) { m_txThread = v; return *this; }
  StdioConfig &input(Zi::Handle v) { m_input = v; return *this; }
  StdioConfig &output(Zi::Handle v) { m_output = v; return *this; }

private:
  Limits	m_limits;
  ZuID		m_rxThread;
  ZuID		m_txThread;
  Zi::Handle	m_input = Zi::nullHandle();
  Zi::Handle	m_output = Zi::nullHandle();
};

using StdioBuf = ZiIOBufAlloc<ZiIOBuf_DefltSize,
  ZiIOBuf_DefltMaxSize, "Zjrpc.Stdio.Buf">;

class StdioFramer {
public:
  enum { Open, Closed };

  StdioFramer(unsigned maxLine) : m_maxLine{maxLine} { }

  int state() const { return m_state; }

  void close() {
    m_state = Closed;
    m_tail = nullptr;
  }

  template <typename L>
  bool feed(ZuCSpan input, L &&l) {
    if (m_state == Closed) return false;
    unsigned n = input.length();
    unsigned begin = 0;
    for (unsigned i = 0; i < n; ++i) {
      if (input[i] != '\n') continue;
      unsigned length = i - begin;
      unsigned tail = m_tail ? m_tail->length : 0;
      if (ZuUnlikely(tail > m_maxLine || length > m_maxLine - tail)) {
	close();
	return false;
      }
      auto frame = ZuMv(m_tail);
      if (!frame) frame = new StdioBuf{};
      if (!frame->append(ZuBSpan{input}.offset(begin).trunc(length))) {
	close();
	return false;
      }
      if (frame->length && frame->end()[-1] == '\r') --frame->length;
      l(ZuMv(frame));
      begin = i + 1;
    }
    if (begin == n) return true;
    unsigned length = n - begin;
    unsigned tail = m_tail ? m_tail->length : 0;
    if (ZuUnlikely(tail > m_maxLine || length > m_maxLine - tail)) {
      close();
      return false;
    }
    if (!m_tail) m_tail = new StdioBuf{};
    if (!m_tail->append(ZuBSpan{input}.offset(begin).trunc(length))) {
      close();
      return false;
    }
    return true;
  }

  bool eof() {
    if (m_tail && m_tail->length) {
      close();
      return false;
    }
    close();
    return true;
  }

private:
  ZmRef<ZiIOBuf>	m_tail;
  unsigned	m_maxLine;
  int		m_state = Open;
};

inline ZmRef<ZiIOBuf> stdioFrame(ZuCSpan message)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  *frame << message << '\n';
  if (frame->failed()) return nullptr;
  return frame;
}

inline ZmRef<ZiIOBuf> stdioFrame(ZuCSpan message, unsigned maxLine)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  BufOutput out{*frame, uint64_t(maxLine) + 1};
  out << message << '\n';
  if (!out) return nullptr;
  return frame;
}

template <typename M>
inline ZmRef<ZiIOBuf> stdioFrame(const M &message, unsigned maxLine)
{
  ZmRef<ZiIOBuf> frame = new StdioBuf{};
  BufOutput out{*frame, uint64_t(maxLine) + 1};
  message.write(out);
  out << '\n';
  if (!out) return nullptr;
  return frame;
}

struct StdioRecord {
  // Shared pooled I/O buffers cannot carry this channel's private queue link.
  ZmRef<ZiIOBuf> buf;
};

template <ZuString ID>
using StdioQueue_ = ZmList<StdioRecord,
  ZmListNode<StdioRecord, ZmListHeapID<ID>>>;

ZuDerive(StdioRxQueue, (StdioQueue_<"Zjrpc.Stdio.RxQueue">));
ZuDerive(StdioTxQueue, (StdioQueue_<"Zjrpc.Stdio.TxQueue">));

namespace StdioState {
  enum { Initial, Open, Closing, Failed, Closed };
}

namespace StdioOutcome {
  enum { None, EOF_, Failed, Stopped };
}

template <typename Impl>
class IOLink {
public:
  IOLink(
      Impl *impl_, ZiMultiplex *mx_,
      unsigned ownerThread_, StdioConfig config) :
    m_impl{impl_}, m_mx{mx_}, m_limits{config.limits()},
    m_framer{m_limits.maxLineBytes}, m_ownerThread{ownerThread_},
    m_rxThread{mx_ ? mx_->sid(config.rxThread()) : 0},
    m_txThread{mx_ ? mx_->sid(config.txThread()) : 0}
  {
    m_input.init(config.input(), ZiFile::ReadOnly | ZiFile::GC);
    m_output.init(config.output(), ZiFile::WriteOnly | ZiFile::GC);
  }

  ~IOLink() {
    ZiAssert(m_state == StdioState::Initial || m_state == StdioState::Closed,
	"Zjrpc", (),
	"destroying active stdio transport", ());
  }

  int state() const { return m_state; }
  unsigned queued() const {
    ZmGuard<ZmPLock> guard(m_txLock);
    return m_txQueued;
  }
  uint64_t queuedBytes() const {
    ZmGuard<ZmPLock> guard(m_txLock);
    return m_txQueuedBytes;
  }

  bool start_() {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio start outside owner shard", return false);
    if (m_state != StdioState::Initial || !m_impl || !m_mx ||
	!m_ownerThread ||
	!m_rxThread || !m_txThread || m_rxThread == m_txThread ||
	m_ownerThread == m_rxThread || m_ownerThread == m_txThread ||
	m_ownerThread > m_mx->params().nThreads() ||
	m_rxThread > m_mx->params().nThreads() ||
	m_txThread > m_mx->params().nThreads() ||
	m_rxThread == m_mx->rxThread() || m_rxThread == m_mx->txThread() ||
	m_txThread == m_mx->rxThread() || m_txThread == m_mx->txThread() ||
	!m_mx->params().thread(m_rxThread).isolated() ||
	!m_mx->params().thread(m_txThread).isolated() ||
	m_input.handle() == m_output.handle() ||
	!m_input || !m_output || !m_limits.maxLineBytes ||
	!m_limits.maxQueue || !m_limits.maxQueueBytes ||
	!m_limits.workBatch)
      return false;
    m_state = StdioState::Open;
    m_mx->wakeFn(m_rxThread,
      ZmScheduler::WakeFn{this, [](IOLink *io) { io->wakeRx_(); }});
    m_mx->run([this]() { rxWork_(); }, m_rxThread);
    m_mx->run([this]() { txWork_(); }, m_txThread);
    return true;
  }

  template <typename P>
  bool send_(const P &message) {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio send outside owner shard", return false);
    try {
      return send_(stdioFrame(message, m_limits.maxLineBytes));
    } catch (...) {
      fail_();
      return false;
    }
  }

  bool send_(ZmRef<ZiIOBuf> frame) {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio send outside owner shard", return false);
    if (m_state != StdioState::Open || !frame || frame->failed() || !frame->length ||
	frame->end()[-1] != '\n' ||
	frame->length - 1 > m_limits.maxLineBytes)
      return false;
    {
      ZmGuard<ZmPLock> guard(m_txLock);
      if (m_txStopping || m_txQueued >= m_limits.maxQueue ||
	  m_txQueuedBytes > m_limits.maxQueueBytes ||
	  frame->length > m_limits.maxQueueBytes - m_txQueuedBytes)
	return false;
      unsigned length = frame->length;
      try {
	m_txQueue.push(StdioRecord{ZuMv(frame)});
      } catch (...) {
	guard.unlock();
	fail_();
	return false;
      }
      m_txQueuedBytes += length;
      ++m_txQueued;
    }
    m_txSem.post();
    return true;
  }

  void stop_() {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio stop outside owner shard", return);
    if (m_state == StdioState::Closed ||
	m_state == StdioState::Closing) return;
    if (m_state != StdioState::Failed) m_state = StdioState::Closing;
    stopRx_();
  }

private:
  bool invoked_() const {
    return m_mx && m_mx->invoked(m_ownerThread);
  }

  void wakeRx_() {
    if (!m_rxStopping) return;
    m_input.close();
    ZmGuard<ZmPLock> guard(m_rxLock);
    Private::interruptStdio(m_rxNative);
  }

  void rxWork_() {
    ZiFile input{m_input};
    uintptr_t native = Private::stdioThread();
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      m_rxNative = native;
    }
    try {
      for (;;) {
	if (!native) {
	  rxOutcome_(StdioOutcome::Failed);
	  break;
	}
	ZmRef<ZiIOBuf> buf = new StdioBuf{};
	int n = input.read(buf->data(), buf->size, false);
	if (m_rxStopping) break;
	if (n <= 0) {
	  rxOutcome_(n == Zi::EndOfFile ?
	    StdioOutcome::EOF_ : StdioOutcome::Failed);
	  break;
	}
	buf->length = unsigned(n);
	if (!rxEnqueue_(ZuMv(buf))) {
	  rxOutcome_(StdioOutcome::Failed);
	  break;
	}
      }
    } catch (...) {
      rxOutcome_(StdioOutcome::Failed);
    }
    if (m_rxStopping) rxOutcome_(StdioOutcome::Stopped);
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      m_rxNative = 0;
      Private::closeStdioThread(native);
    }
    m_mx->push([this]() { rxExited_(); }, m_rxThread);
  }

  void txWork_() {
    ZiFile output{m_output};
    for (;;) {
      m_txSem.wait();
      StdioRecord record;
      {
	ZmGuard<ZmPLock> guard(m_txLock);
	if (m_txStopping) break;
	auto node = m_txQueue.shift();
	if (!node) {
	  if (m_txClosing) break;
	  continue;
	}
	record = ZuMv(node->data());
      }
      int result = output.write(record.buf->data(), record.buf->length);
      bool drained;
      {
	ZmGuard<ZmPLock> guard(m_txLock);
	--m_txQueued;
	m_txQueuedBytes -= record.buf->length;
	drained = !m_txQueued;
      }
      if (result != Zi::OK) {
	m_txStopping = 1;
	m_mx->run([this]() { txFailed_(); }, m_ownerThread);
	break;
      }
      if (m_txClosing && drained) break;
    }
    m_mx->push([this]() { txExited_(); }, m_txThread);
  }

  bool rxEnqueue_(ZmRef<ZiIOBuf> buf) {
    bool post = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (m_rxStopping || m_rxOutcome ||
	  m_rxQueued >= m_limits.maxQueue ||
	  m_rxQueuedBytes > m_limits.maxQueueBytes ||
	  buf->length > m_limits.maxQueueBytes - m_rxQueuedBytes)
	return false;
      unsigned length = buf->length;
      m_rxQueue.push(StdioRecord{ZuMv(buf)});
      m_rxQueuedBytes += length;
      ++m_rxQueued;
      if (!m_rxDequeuing) post = m_rxDequeuing = true;
    }
    if (post) m_mx->run([this]() { drainRx_(); }, m_ownerThread);
    return true;
  }

  void rxOutcome_(int outcome) {
    bool post = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (!m_rxOutcome) m_rxOutcome = outcome;
      if (!m_rxDequeuing) post = m_rxDequeuing = true;
    }
    if (post) m_mx->run([this]() { drainRx_(); }, m_ownerThread);
  }

  void drainRx_() {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio drain outside owner shard", return);
    if (m_state == StdioState::Failed) {
      drainFailedRx_();
      return;
    }
    unsigned visited = 0;
    while (visited < m_limits.workBatch) {
      StdioRecord record;
      {
	ZmGuard<ZmPLock> guard(m_rxLock);
	auto node = m_rxQueue.shift();
	if (!node) break;
	record = ZuMv(node->data());
	--m_rxQueued;
	m_rxQueuedBytes -= record.buf->length;
      }
      ++visited;
      bool accepted = true;
      bool framed = false;
      try {
	framed = m_framer.feed(ZuCSpan{record.buf->cspan()},
	  [this, &accepted](ZmRef<ZiIOBuf> frame) {
	    if (!accepted) return;
	    try {
	      accepted = m_impl->stdioFrame(ZuMv(frame));
	    } catch (...) {
	      accepted = false;
	    }
	  });
      } catch (...) {
	accepted = false;
      }
      if (!framed || !accepted) {
	fail_();
	return;
      }
    }

    int outcome = StdioOutcome::None;
    bool repost = false;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      if (m_rxQueued) {
	repost = true;
      } else if (m_rxOutcome) {
	outcome = m_rxOutcome;
	m_rxDequeuing = false;
      } else {
	m_rxDequeuing = false;
      }
    }
    if (repost) {
      m_mx->run([this]() { drainRx_(); }, m_ownerThread);
      return;
    }
    if (!outcome) return;
    if (outcome == StdioOutcome::Failed ||
	(outcome == StdioOutcome::EOF_ && !m_framer.eof()))
      fail_();
    else
      rxDrained_(outcome == StdioOutcome::EOF_);
  }

  void fail_() {
    if (m_state == StdioState::Closed) return;
    m_state = StdioState::Failed;
    m_framer.close();
    stopRx_();
    drainFailedRx_();
  }

  void drainFailedRx_() {
    if (m_rxDrained || m_rxFailDraining) return;
    m_rxFailDraining = true;
    unsigned visited = 0;
    bool remaining;
    {
      ZmGuard<ZmPLock> guard(m_rxLock);
      while (visited < m_limits.workBatch) {
	auto node = m_rxQueue.shift();
	if (!node) break;
	++visited;
	--m_rxQueued;
	m_rxQueuedBytes -= node->data().buf->length;
      }
	remaining = m_rxQueued;
	if (!remaining) m_rxDequeuing = false;
    }
    m_rxFailDraining = false;
    if (remaining) {
      m_mx->run([this]() { drainFailedRx_(); }, m_ownerThread);
      return;
    }
    rxDrained_();
  }

  void stopRx_() {
    if (m_rxStopping.cmpXch(1, 0) != 0) return;
    m_mx->run([]() { }, m_rxThread);
  }

  void rxDrained_(bool flush = false) {
    if (m_rxDrained) return;
    m_rxDrained = true;
    m_framer.close();
    if (m_state == StdioState::Open) m_state = StdioState::Closing;
    stopTx_(flush);
    closed_();
  }

  void stopTx_(bool flush = false) {
    if (flush) {
      if (m_txStopping || m_txClosing.cmpXch(1, 0) != 0) return;
      m_txSem.post();
      return;
    }
    if (m_txStopping.cmpXch(1, 0) != 0) return;
    m_output.close();
    m_txSem.post();
  }

  void txFailed_() {
    ZiAssert(invoked_(), "Zjrpc", (),
	"stdio failure outside owner shard", return);
    if (m_state != StdioState::Closed) m_state = StdioState::Failed;
    stopRx_();
    stopTx_();
  }

  void rxExited_() {
    m_mx->wakeFn(m_rxThread, {});
    m_mx->run([this]() {
      m_rxExited = true;
      closed_();
    }, m_ownerThread);
  }

  void txExited_() {
    m_mx->run([this]() {
      m_txExited = true;
      drainTx_();
    }, m_ownerThread);
  }

  void drainTx_() {
    unsigned visited = 0;
    bool remaining;
    {
      ZmGuard<ZmPLock> guard(m_txLock);
      while (visited < m_limits.workBatch) {
	auto node = m_txQueue.shift();
	if (!node) break;
	++visited;
	--m_txQueued;
	m_txQueuedBytes -= node->data().buf->length;
      }
      remaining = m_txQueued;
    }
    if (remaining) {
      m_mx->run([this]() { drainTx_(); }, m_ownerThread);
      return;
    }
    m_txDrained = true;
    closed_();
  }

  void closed_() {
    if (m_state == StdioState::Closed || !m_rxDrained || !m_rxExited ||
	!m_txExited || !m_txDrained)
      return;
    bool failed = m_state == StdioState::Failed;
    m_input.close();
    m_output.close();
    m_state = StdioState::Closed;
    if (failed) {
      try { m_impl->stdioFailed(); } catch (...) { }
    } else {
      try { m_impl->stdioClosed(); } catch (...) { }
    }
  }

  Impl			*m_impl;
  ZiMultiplex		*m_mx;
  Limits		m_limits;
  StdioFramer		m_framer;
  ZiFile		m_input;
  ZiFile		m_output;
  unsigned		m_ownerThread;
  unsigned		m_rxThread;
  unsigned		m_txThread;

  alignas(Zm::CacheLineSize)
  mutable ZmPLock	m_rxLock;
  StdioRxQueue		m_rxQueue;
  uintptr_t		m_rxNative = 0;
  uint64_t		m_rxQueuedBytes = 0;
  unsigned		m_rxQueued = 0;
  int			m_rxOutcome = StdioOutcome::None;
  bool			m_rxDequeuing = false;
  ZmAtomic<unsigned>	m_rxStopping = 0;

  alignas(Zm::CacheLineSize)
  mutable ZmPLock	m_txLock;
  StdioTxQueue		m_txQueue;
  ZmSemaphore		m_txSem;
  uint64_t		m_txQueuedBytes = 0;
  unsigned		m_txQueued = 0;
  ZmAtomic<unsigned>	m_txStopping = 0;
  ZmAtomic<unsigned>	m_txClosing = 0;

  alignas(Zm::CacheLineSize)
  int			m_state = StdioState::Initial;
  bool			m_rxDrained = false;
  bool			m_rxFailDraining = false;
  bool			m_rxExited = false;
  bool			m_txExited = false;
  bool			m_txDrained = false;
};

// Shared IO endpoint lifecycle. Protocol state drains after the workers exit;
// ioDone() then releases a synchronous stop waiter on the owner shard.
template <typename Derived>
class IOEndpoint {
  using IO = IOLink<Derived>;
public:
  bool start() {
    if (!m_mx || !m_up.load_() || m_started || m_done.load_()) return false;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	m_started = m_io.template p<IO>().start_();
	bool ok = m_started;
	if (ok) {
	  try { derived()->ioReady(); } catch (...) { fail_(); ok = false; }
	} else {
	  m_failure = true;
	  m_up = false;
	  derived()->ioClosed(true);
	}
	wake(ok);
      }, m_owner);
    });
  }

  bool stop() {
    if (!m_mx) return false;
    if (m_done.load_()) return !m_failure;
    return ZmBlock<bool>{}([this](auto wake) mutable {
      m_mx->run([this, wake = ZuMv(wake)]() mutable {
	if (m_done.load_()) { wake(!m_failure); return; }
	m_stop = ZuMv(wake);
	m_up = false;
	if (m_started) m_io.template p<IO>().stop_();
	else derived()->ioClosed(false);
      }, m_owner);
    });
  }

  void final() {
    if (!m_mx) return;
    (void)stop();
    m_io.null();
    m_mx = nullptr;
    m_owner = 0;
  }

  template <typename L>
  bool ownerRun(L &&l) {
    if (!m_mx || !m_up.load_()) return false;
    if (invoked()) l();
    else m_mx->run(ZuFwd<L>(l), m_owner);
    return true;
  }

  CompletionRoute completionRoute() const { return {m_mx, m_owner}; }
  bool invoked() const { return m_mx && m_mx->invoked(m_owner); }
  bool up() const { return m_up.load_(); }
  void stdioClosed() { m_up = false; derived()->ioClosed(false); }
  void stdioFailed() { m_up = false; m_failure = true; derived()->ioClosed(true); }

protected:
  bool init(ZiMultiplex *mx, StdioConfig config) {
    if (m_mx || !mx || !mx->txThread() || !valid(config.limits())) return false;
    m_mx = mx;
    m_owner = mx->txThread();
    new (m_io.template new_<IO>()) IO{derived(), mx, m_owner, ZuMv(config)};
    m_up = true;
    m_done = false;
    m_started = m_failure = false;
    return true;
  }

  template <typename M>
  bool send(const M &message) {
    return up() && m_io.template p<IO>().send_(message);
  }

  void fail_() {
    if (m_done.load_() || m_failure) return;
    m_failure = true;
    m_up = false;
    m_io.template p<IO>().stop_();
  }

  template <typename L>
  void continue_(L &&l) { m_mx->run(ZuFwd<L>(l), m_owner); }

  void ioDone() {
    m_done = true;
    auto stop = ZuMv(m_stop);
    m_stop = {};
    if (stop) stop(!m_failure);
  }

private:
  auto derived() { return static_cast<Derived *>(this); }

  ZuUnion<void, IO> m_io;
  ZiMultiplex *m_mx = nullptr;
  ZmFn<void(bool)> m_stop;
  unsigned m_owner = 0;
  ZmAtomic<unsigned> m_up = 0;
  ZmAtomic<unsigned> m_done = 0;
  bool m_started = false;
  bool m_failure = false;
};

} // Zjrpc

#endif /* ZjrpcStdio_HH */
