//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Staged actual-descriptor destruction regressions. No production hooks.
#include <string.h>
#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZiIOContext.hh>
#include <zlib/ZhttpNative.hh>

using namespace ZuTestUtil;
namespace HeaderLifeTest {

struct State {
  ZmAtomic<unsigned> sequence{0}, headers{0}, allocatorStep{0};
  ZmAtomic<unsigned> firstStep{0}, lastStep{0}, cleanSource{0};
  ZmSemaphore gone;
  Zi::IOBufAllocator *allocator = nullptr; // borrowed from the fixture owner
};

struct Header : Zhttp::Transport_::TCPHeader {
  State *state = nullptr;
  bool firstHeader = false;
  using TCPHeader::TCPHeader;
  ~Header() override {
    auto step = state->sequence.xchAdd(1) + 1;
    if (firstHeader) state->firstStep = step;
    state->lastStep = step;
    ++state->headers;
    state->gone.post();
  }
};

using Ordinary = Zi::IOBufBuiltinAllocator<Zi::IOBufType::App, Header,
  ZiIOBuf_DefltSize, ZiIOBuf_DefltMaxSize>;


template <typename Base>
struct Allocator : ZmNHeap<Allocator<Base>>, Base {
  friend inline ZuStringT<"Zhttp.Header.LifeAlloc"> ZmNHeapID(Allocator *) { return {}; }
  using ZmNHeap<Allocator>::operator new;
  using ZmNHeap<Allocator>::operator delete;
  State *state;
  template <typename ...Args>
  Allocator(State *state_, Args &&...args) : Base{ZuFwd<Args>(args)...}, state{state_} { }
  ~Allocator() {
    state->allocator = nullptr;
    state->allocatorStep = state->sequence.xchAdd(1) + 1;
  }
};

template <typename A>
ZmRef<Header> allocate(A &allocator, State &state, bool first)
{
  auto raw = allocator.allocBuf();
  auto head = ZmRef<Header>::acquire(static_cast<Header *>(ZuMv(raw).release()));
  head->state = &state;
  head->firstHeader = first;
  *static_cast<ZiIOBuf *>(head.ptr()) << "header";
  return head;
}

void ordinaryLifetime()
{
  ZuTestScope(ordinaryLifetime);
  State state;
  auto allocator = new Allocator<Ordinary>{&state};
  auto head = allocate(*allocator, state, true);
  delete allocator; // ordinary embedded backing needs no allocator pin
  ZuCheck(state.allocatorStep && !state.headers && head->cspan() == "header");
  head = nullptr;
  ZuCheck(state.headers == 1 && state.allocatorStep < state.firstStep);
}

void registeredLifetime(bool jumbo)
{
  ZuTestScopeRT(registeredLifetime);
  State state;
  {
    ZiMultiplex mx{ZiMxParams{}};
    auto allocator = mx.allocator<Zi::IOBufType::TxTCP, Header>();
    state.allocator = allocator;
    auto head = allocate(*allocator, state, true);
    ZuCheckRT(head->allocator == allocator && allocator->mx() == &mx &&
      allocator->type() == Zi::IOBufType::TxTCP);
    if (jumbo) {
      ZuCheckRT(head->ensure(head->size + 1));
      ZuCheckRT(!head->builtin() && head->cspan() == "header");
    }
    ZuCheckRT(!state.allocatorStep && !state.headers && head->cspan() == "header");
    head = nullptr;
    ZuCheckRT(state.headers == 1 && !state.allocatorStep);
    allocator = nullptr;
    ZuCheckRT(!state.allocatorStep); // multiplexer owns the registered allocator
  }
  ZuCheckRT(state.headers == 1);
}

struct Cxn : ZmNHeap<Cxn>, ZiConnection {
  friend inline ZuStringT<"Zhttp.Header.LifeCxn"> ZmNHeapID(Cxn *) { return {}; }
  using ZmNHeap<Cxn>::operator new;
  using ZmNHeap<Cxn>::operator delete;
  Cxn(ZiMultiplex *mx) : ZiConnection{mx, ZiCxnInfo{.socket = Zi::nullSocket()}} { }
  void connected(ZiRxContext &) override { }
  void disconnected(bool) override { }
};

using HeadRef = ZmRef<Zhttp::Transport_::TCPHeader>;
void configureCallbacks(Header *head, ZiMultiplex &mx)
{
  head->mx = &mx;
  head->appThread = 3;
  head->ownerAck = [](HeadRef head) {
    unsigned pending = --head->posts;
    if (head->appTerminal && !pending) {
      auto state = static_cast<Header *>(head.ptr())->state;
      ++state->cleanSource;
      head->connection = nullptr;
    }
  };
  head->terminalPost = [](HeadRef head) {
    auto mx = head->mx;
    auto thread = head->appThread;
    mx->run([head = ZuMv(head)]() mutable {
      head->appTerminal = true;
      auto ack = head->ownerAck;
      ack(ZuMv(head));
    }, thread);
  };
}

void postedLifetime(bool early)
{
  ZuTestScopeRT(postedLifetime);
  State state;
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(3); })
    .rxThread(1).txThread(2)};
  auto allocator = mx.allocator<Zi::IOBufType::TxTCP, Header>();
  bool started = mx.start();
  ZuCheckRT(started);
  if (!started) return;
  ZmRef<Cxn> cxn = new Cxn{&mx};
  state.allocator = allocator;
  auto head = allocate(*allocator, state, true);
  configureCallbacks(head, mx);
  head->connection = cxn;
  if (early) {
    ++head->posts;
    mx.txRun([held = HeadRef{head}]() mutable {
      Zhttp::Transport_::TCPTxOps::postAck_(ZuMv(held));
    });
  }
  mx.txRun([head = ZuMv(head)]() mutable {
    head->endIO_();
    head->postEnd_();
  });
  bool destroyed = state.gone.timedwait(Zm::now(20)) == 0;
  cxn = nullptr;
  mx.stop();
  ZuCheckRT(destroyed && state.headers == 1 && state.cleanSource == 1);
  ZuCheckRT(!state.allocatorStep);
  allocator = nullptr;
  ZuCheckRT(state.firstStep);
}

} // namespace HeaderLifeTest
int main(int argc, char **argv)
{
  using namespace HeaderLifeTest;
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ordinaryLifetime);
  ZuTestCall(registeredLifetime, false);
  ZuTestCall(registeredLifetime, true);
  ZuTestCall(postedLifetime, false);
  ZuTestCall(postedLifetime, true);
  return 0;
}
