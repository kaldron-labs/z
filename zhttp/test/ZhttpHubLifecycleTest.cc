//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTestUtil.hh>

#include <zlib/Zhttp.hh>

using namespace ZuTestUtil;

namespace ZhttpHubLifecycleTest_ {

struct Event {
  enum { Init, Start, StopAccepting, Stop, Final };
};

using Events =
  ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Test.HubLife">>;

inline unsigned event(unsigned id, unsigned type)
{
  return id * 10 + type;
}

template <unsigned ID>
struct Fake {
  Events		*events = nullptr;
  Zhttp::Hubs	*hubs = nullptr;
  bool			startOK = true;
  bool			stopOK = true;
  bool			stopOnStart = false;
  bool			deferStart = false;
  bool			deferStop = false;
  unsigned		inits = 0;
  unsigned		starts = 0;
  unsigned		stops = 0;
  unsigned		finals = 0;
  unsigned		stopOnStartDone = 0;

  bool init(bool ok = true) {
    ++inits;
    events->push(event(ID, Event::Init));
    return ok;
  }
  template <typename Done>
  void start(Done done) {
    ++starts;
    events->push(event(ID, Event::Start));
    if (stopOnStart)
      hubs->stop([this](bool ok) {
	stopOnStartDone += ok ? 1 : 2;
      });
    if (deferStart) {
      startDone = Zhttp::Hubs::DoneFn{ZuMv(done)};
      return;
    }
    done(startOK);
  }
  void stopAccepting() {
    events->push(event(ID, Event::StopAccepting));
  }
  template <typename Done>
  void stop(Done done) {
    ++stops;
    events->push(event(ID, Event::Stop));
    if (deferStop) {
      stopDone = Zhttp::Hubs::DoneFn{ZuMv(done)};
      return;
    }
    done(stopOK);
  }
  void final() {
    ++finals;
    events->push(event(ID, Event::Final));
  }

  Zhttp::Hubs::DoneFn	startDone;
  Zhttp::Hubs::DoneFn	stopDone;
};

int index(const Events &events, unsigned value)
{
  for (unsigned i = 0; i < events.length(); ++i)
    if (events[i] == value) return int(i);
  return -1;
}

void testEmpty()
{
  ZuTestScope(testEmpty);

  Zhttp::Hubs hubs;
  unsigned completed = 0;
  bool result = true;
  hubs.start([&completed, &result](bool ok) {
    ++completed;
    result = ok;
  });
  ZuCHECK(!result && completed == 1);
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Failed);
  ZuCHECK(!hubs.stop());
  hubs.final();
}

void testOneAndAll()
{
  ZuTestScope(testOneAndAll);

  {
    Events events;
    Fake<1> one{.events = &events};
    Zhttp::Hubs hubs;
    ZuCHECK(hubs.init(one));
    ZuCHECK(hubs.start() && hubs.start());
    ZuCHECK(hubs.stop());
    hubs.final();
    ZuCHECK(one.inits == 1 && one.starts == 1 &&
	one.stops == 1 && one.finals == 1);
  }
  {
    Events events;
    Fake<1> tcp{.events = &events};
    Fake<2> tls{.events = &events};
    Fake<3> h3{.events = &events};
    Zhttp::Hubs hubs;
    ZuCHECK(hubs.init(tcp) && hubs.init(tls) && hubs.init(h3));
    ZuCHECK(hubs.start() && hubs.count() == 3);
    unsigned completed = 0;
    hubs.stop([&completed](bool ok) { if (ok) ++completed; });
    hubs.stop([&completed](bool ok) { if (ok) ++completed; });
    hubs.final();
    ZuCHECK(completed == 2);
    ZuCHECK(index(events, event(3, Event::Stop)) <
	index(events, event(2, Event::Stop)) &&
	index(events, event(2, Event::Stop)) <
	index(events, event(1, Event::Stop)));
    ZuCHECK(index(events, event(3, Event::Final)) <
	index(events, event(2, Event::Final)) &&
	index(events, event(2, Event::Final)) <
	index(events, event(1, Event::Final)));
  }
}

void testInitFailure(unsigned failure)
{
  ZuTestScope(testInitFailure);

  Events events;
  Fake<1> a{.events = &events};
  Fake<2> b{.events = &events};
  Fake<3> c{.events = &events};
  Zhttp::Hubs hubs;
  bool ok = hubs.init(a, failure != 0);
  if (ok) ok = hubs.init(b, failure != 1);
  if (ok) ok = hubs.init(c, failure != 2);
  ZuCHECK(!ok);
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Failed);
  ZuCHECK(a.finals == (failure > 0) &&
      b.finals == (failure > 1) && c.finals == 0);
  hubs.final();
}

void testStartFailure(unsigned failure)
{
  ZuTestScope(testStartFailure);

  Events events;
  Fake<1> a{.events = &events, .startOK = failure != 0};
  Fake<2> b{.events = &events, .startOK = failure != 1};
  Fake<3> c{.events = &events, .startOK = failure != 2};
  Zhttp::Hubs hubs;
  ZuCHECK(hubs.init(a) && hubs.init(b) && hubs.init(c));
  ZuCHECK(!hubs.start());
  ZuCHECK(a.stops == (failure > 0) &&
      b.stops == (failure > 1) && c.stops == 0);
  ZuCHECK(a.finals == 1 && b.finals == 1 && c.finals == 1);
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Failed);
}

void testStopDuringStart()
{
  ZuTestScope(testStopDuringStart);

  Events events;
  Zhttp::Hubs hubs;
  Fake<1> a{
    .events = &events, .hubs = &hubs, .stopOnStart = true};
  Fake<2> b{.events = &events};
  ZuCHECK(hubs.init(a) && hubs.init(b));
  ZuCHECK(!hubs.start());
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Stopped);
  ZuCHECK(a.starts == 1 && a.stops == 1 && a.stopOnStartDone == 1 &&
      b.starts == 0);
  hubs.final();
  ZuCHECK(a.finals == 1 && b.finals == 1);
}

void testAsyncStopDuringStart()
{
  ZuTestScope(testAsyncStopDuringStart);

  Events events;
  Zhttp::Hubs hubs;
  Fake<1> a{
    .events = &events, .hubs = &hubs,
    .deferStart = true, .deferStop = true};
  unsigned starts = 0, stops = 0;
  ZuCHECK(hubs.init(a));
  hubs.start([&starts](bool ok) { starts += ok ? 1 : 2; });
  hubs.start([&starts](bool ok) { starts += ok ? 10 : 20; });
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Starting && !starts);
  hubs.stop([&stops](bool ok) { stops += ok ? 1 : 2; });
  ZuCHECK(!starts && !stops);
  auto startDone = ZuMv(a.startDone);
  startDone(true);
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Stopping);
  ZuCHECK(!starts && !stops && a.stops == 1);
  auto stopDone = ZuMv(a.stopDone);
  stopDone(true);
  ZuCHECK(starts == 22 && stops == 1);
  ZuCHECK(hubs.state() == Zhttp::Hubs::State::Stopped);
  hubs.final();
}

} // namespace ZhttpHubLifecycleTest_

int main(int argc, char **argv)
{
  using namespace ZhttpHubLifecycleTest_;

  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(testEmpty);
  ZuTestCall(testOneAndAll);
  ZuTestCall(testInitFailure, 0U);
  ZuTestCall(testInitFailure, 1U);
  ZuTestCall(testInitFailure, 2U);
  ZuTestCall(testStartFailure, 0U);
  ZuTestCall(testStartFailure, 1U);
  ZuTestCall(testStartFailure, 2U);
  ZuTestCall(testStopDuringStart);
  ZuTestCall(testAsyncStopDuringStart);
  return 0;
}
