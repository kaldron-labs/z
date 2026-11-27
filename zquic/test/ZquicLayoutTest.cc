//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTest.hh>
#include <zlib/ZuTestUtil.hh>
#include <zlib/Zquic.hh>

#include "ZquicFiniteProvider.hh"

using namespace ZuTestUtil;

#define Zquic_CheckRT_1(x, ...) ZuCheckRT(x, log_(__VA_ARGS__))

struct LayoutApp : Zquic::Hub<LayoutApp> { };
struct LayoutLink;
struct LayoutStream :
  Zquic::Stream<LayoutLink, LayoutStream> {
  using Base = Zquic::Stream<LayoutLink, LayoutStream>;
  using Base::Base;
  using Base::ownerLayout;
  uint64_t applicationTail = 0;

  int process(Zquic::RxStream &) { return 0; }
};
struct LayoutLink :
  ZmNHeap<LayoutLink>,
  Zquic::Link<LayoutApp, LayoutLink, LayoutStream> {
friend inline ZuStringT<"Zquic.Layout.Link"> ZmNHeapID(LayoutLink *);
  using ZmNHeap<LayoutLink>::operator new;
  using ZmNHeap<LayoutLink>::operator delete;
  using Base =
    Zquic::Link<LayoutApp, LayoutLink, LayoutStream>;
  using Base::Base;
  using Base::ownerLayout;
  uint64_t applicationTail = 0;
};

template <typename T> bool isolated(const T &value)
{
  enum { line = Zm::CacheLineSize };
  bool ok = true;
  value.ownerLayout([&ok](const void *address, size_t size) {
    ok &= !(uintptr_t(address) % line) && !(size % line);
  });
  return ok;
}

void testOwnerStorage()
{
  ZuTestScopeRT(testOwnerStorage);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) { s.nThreads(4); })
    .rxThread(1).txThread(2)};
  LayoutApp app;
  Zquic_CheckRT_1(app.init(Zquic::HubParams(&mx, "3", "4")), "layout app initialization failed");
  {
    ZmRef<LayoutLink> links[16];
    for (auto &link : links) {
      link = new LayoutLink{&app};
      Zquic_CheckRT_1(!(uintptr_t(link.ptr()) % Zm::CacheLineSize) && isolated(*link),
	"allocated link owner storage is not isolated");
      Zquic_CheckRT_1(uintptr_t(&link->applicationTail) >=
	  uintptr_t(static_cast<LayoutLink::Base *>(link.ptr())) + sizeof(LayoutLink::Base),
	"application tail reused link owner storage");
      auto membership = static_cast<Zquic_::ServerLinks::Node::Ext *>(link.ptr());
      Zquic_CheckRT_1(uintptr_t(membership) >=
	  uintptr_t(static_cast<Zquic_::EndpointLink *>(link.ptr())) +
	    sizeof(Zquic_::EndpointLink),
	"endpoint membership shares the link reference count's cache line");
      ZmRef<LayoutStream> stream = new LayoutLink::Streams::Node{link.ptr(), 0};
      Zquic_CheckRT_1(!(uintptr_t(stream.ptr()) % Zm::CacheLineSize) && isolated(*stream),
	"allocated stream owner storage is not isolated");
      Zquic_CheckRT_1(uintptr_t(&stream->applicationTail) >=
	  uintptr_t(static_cast<LayoutStream::Base *>(stream.ptr())) + sizeof(LayoutStream::Base),
	"application tail reused stream owner storage");
      auto txHash = static_cast<Zquic::TxStreams::Node::Ext *>(stream.ptr());
      auto txQueue = static_cast<Zquic::StreamQueue::Node::Ext *>(stream.ptr());
      auto rx = static_cast<LayoutStream::Rx *>(stream.ptr());
      auto node = static_cast<LayoutLink::Streams::Node *>(stream.ptr());
      auto rxHash = static_cast<LayoutLink::Streams::Node::Ext *>(node);
      Zquic_CheckRT_1(uintptr_t(txHash) >=
	  uintptr_t(static_cast<Zquic::StreamData *>(stream.ptr())) +
	    sizeof(Zquic::StreamData),
	"Tx stream registry shares the stream reference count's cache line");
      Zquic_CheckRT_1(uintptr_t(rx)/Zm::CacheLineSize >
	  (uintptr_t(txQueue) + sizeof(*txQueue) - 1)/Zm::CacheLineSize,
	"Rx stream base shares a cache line with Tx intrusive links");
      Zquic_CheckRT_1(uintptr_t(rxHash)/Zm::CacheLineSize >
	  (uintptr_t(&stream->applicationTail) + sizeof(stream->applicationTail) - 1)/
	    Zm::CacheLineSize,
	"Rx stream registry shares the application tail's cache line");
    }
  }
  app.final();
}

void testProviderStorage()
{
  ZuTestScope(testProviderStorage);
  ZiMultiplex mx;
  ZmRef<ZquicFiniteProvider> provider =
    new ZquicFiniteProvider{&mx, ZquicFiniteProvider::MaxSlots};
  ZuCheck(!(uintptr_t(provider.ptr()) % Zm::CacheLineSize) && isolated(*provider));
  uint8_t *slots[ZquicFiniteProvider::MaxSlots];
  for (auto &slot : slots) {
    slot = provider->alloc();
    ZuCheck(slot && !(uintptr_t(slot) % Zm::CacheLineSize));
  }
  ZuCheck(!provider->alloc());
  for (auto slot : slots) if (slot) provider->free(slot);
}

void testLoggerStorage()
{
  ZuTestScope(testLoggerStorage);
#ifdef Zquic_DEBUG
  auto logger = ZquicLogger::instance();
  ZuCheck(!(uintptr_t(logger) % Zm::CacheLineSize) && isolated(*logger));
#else
  ZuCheck(!ZquicLogger::enabled());
#endif
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testOwnerStorage);
  ZuTestCall(testProviderStorage);
  ZuTestCall(testLoggerStorage);
}
