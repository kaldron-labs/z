//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// transport-neutral request admission tests

#include <zlib/ZuTestUtil.hh>

#include <zlib/ZhttpClientPool.hh>

using namespace ZuTestUtil;

namespace ZhttpAdmissionTest_ {

struct Request {
  unsigned id = 0;
};

struct App {
  void admit(Request &request) {
    admitted.push(request.id);
    ++running;
    if (running > highWater) highWater = running;
  }
  void cancel(Request &request) { cancelled.push(request.id); }

  ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Admission.Test">> admitted;
  ZtArray<unsigned, ZtArrayHeapID<"Zhttp.Admission.Cancel">> cancelled;
  unsigned running = 0;
  unsigned highWater = 0;
};

void admission()
{
  ZuTestScope(admission);
  App app;
  Zhttp::Admission<App, Request> scheduler;
  ZtArray<Request, ZtArrayHeapID<"Zhttp.Admission.Request">> requests;
  requests.length(5);
  for (unsigned i = 0; i < requests.length(); ++i) requests[i].id = i;
  ZuCHECK(scheduler.init(&app, 2, 3), "initialize");
  for (auto &request: requests)
    ZuCHECK(scheduler.submit(request), "submit");
  ZuCHECK(app.admitted.length() == 2 && scheduler.active() == 2 &&
      scheduler.pending() == 3 && app.highWater == 2,
    "global concurrency bound");

  --app.running;
  scheduler.release();
  ZuCHECK(app.admitted.length() == 3 && app.admitted[2] == 2 &&
      scheduler.active() == 2 && scheduler.pending() == 2,
    "capacity admits next request");

  scheduler.stop();
  ZuCHECK(app.cancelled.length() == 2 && app.cancelled[0] == 3 &&
      app.cancelled[1] == 4 && scheduler.pending() == 0 &&
      !scheduler.submit(requests[4]),
    "stop cancels pending in order");

  --app.running;
  scheduler.release();
  --app.running;
  scheduler.release();
  ZuCHECK(scheduler.idle(), "active work drains");
}

} // namespace ZhttpAdmissionTest_

int main(int argc, char **argv)
{
  using namespace ZhttpAdmissionTest_;

  (void)argc;
  (void)argv;
  ZuTestMain();
  ZuTestCall(admission);
  return 0;
}
