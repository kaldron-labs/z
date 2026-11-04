//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Exercise actual role handlers and retirement without a DB election/socket.
#include "../src/ztchub_daemon.cc"

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmBlock.hh>
#include <zlib/ZmSemaphore.hh>

using namespace ZuTestUtil;

namespace ZtcHubRoleTest_ {

static Ztc::HubdCf config()
{
  Ztc::HubdCf cf;
  cf.listeners.push(Ztc::ListenerCf{
    "127.0.0.1", "/ztc", "server.crt", "server.key", "/session", {}});
  cf.issuerURL = "https://zum.example/oauth2/7";
  cf.audience = "https://ztchub.example";
  cf.managementIssuer = "https://zum.example";
  cf.managementClientID = "ztchub-manager";
  cf.actions.push("Request");
  cf.actions.push("Telemetry");
  cf.roles.push("Client");
  cf.roles.push("Agent");
  return cf;
}

static void retirement()
{
  ZuTestScopeRT(retirement);
  ZiMultiplex mx{ZiMxParams{}.scheduler([](auto &s) {
    s.nThreads(2)
      .thread(1, [](auto &t) { t.name("rx"); t.isolated(true); })
      .thread(2, [](auto &t) { t.name("tx"); t.isolated(true); });
  }).rxThread(1).txThread(2)};
  ZuCheckRT(mx.start());
  Ztc::Hubd owner;
  ZuCheckRT(owner.init(config()) && owner.start());

  // Isolate the role controller from DB replication and service publication.
  // Its real owner engine supplies only the runtime-Running predicate.
  ZmRef<Ztc::Hubd_::State> state = new Ztc::Hubd_::State;
  state->owner = &owner;
  state->multiplex = &mx;
  state->cf.schedulerTurnWork = 2;
  Ztc::Hubd_::HubDB db{state.ptr()};
  Ztc::HubCtrlFn held;
  unsigned stops = 0, starts = 0, partial = 0;
  bool clean = false;
  ZmSemaphore activated, probed;
  state->activateFn = [
    state = state.ptr(), &held, &stops, &starts, &clean, &activated
  ](bool active, Ztc::HubCtrlFn complete) mutable {
    if (!active) { ++stops; held = ZuMv(complete); return; }
    ++starts;
    clean = !state->sessions.count_() && !state->roleDraining;
    complete(true);
    activated.post();
  };
  ZmBlock<>{}([state, &db](auto wake) {
    state->post([state, &db, wake = ZuMv(wake)]() mutable {
      for (unsigned i = 1; i <= 5; ++i)
        state->sessions.add(i, Ztc::Hubd_::Session{});
      Ztc::dbDown(&db, false);
      Ztc::dbUp(&db, nullptr);
      Ztc::dbDown(&db, false);
      Ztc::dbUp(&db, nullptr);
      wake();
    });
  });
  auto retained = ZmBlock<bool>{}([state, &held, &stops, &starts](auto wake) {
    state->post([state, &held, &stops, &starts, wake = ZuMv(wake)]() mutable {
      wake(bool(held) && stops == 1 && !starts && state->roleUp &&
        state->roleDraining && state->state == Ztc::HubdState::Down &&
        state->sessions.count_() == 5);
    });
  });
  ZuCheckRT(retained);

  // Queue the probe behind the first retirement turn, ahead of its remainder.
  mx.rxRun([state, &mx, &held, &partial, &probed]() {
    auto complete = ZuMv(held);
    complete(false); // diagnostic failure still retires this listener generation
    mx.rxRun([state, &partial, &probed]() {
      partial = state->sessions.count_();
      probed.post();
    });
  });
  probed.wait();
  activated.wait();
  ZmBlock<>{}([&mx](auto wake) { mx.rxRun(ZuMv(wake)); });
  ZuCheckRT(partial > 0 && partial < 5);
  ZuCheckRT(starts == 1 && stops == 1 && clean && !held);
  ZuCheckRT(state->state == Ztc::HubdState::Up && !state->posts.load_());
  state->activateFn = {};
  state = nullptr;
  ZuCheckRT(owner.stop());
  ZuCheckRT(mx.stop());
  owner.final();
}

} // ZtcHubRoleTest_

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuTestCall(ZtcHubRoleTest_::retirement);
  return 0;
}
