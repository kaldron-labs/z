//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// GTK startup queue and wake-source ordering

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZGtkApp.hh>

using namespace ZuTestUtil;

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  {
    ZuTestRT(startupQueue);
    ZmSemaphore worker, release, bootstrap, completed, detached;
    ZmScheduler scheduler{ZmSchedParams{}.nThreads(1).thread(1,
      [](auto &p) { p.isolated(1); })};
    scheduler.threadInit([&worker, &release] {
      worker.post();
      release.wait();
    });
    bool started = scheduler.start();
    ZuCheckRT(started);
    if (!started) return 1;
    worker.wait();
    ZGtk::App app;
    app.attach(&scheduler, 1);
    // Both jobs precede attach_()'s loop installation. Bootstrap publishes
    // another job before the old installer has installed the GLib wake source.
    app.run([&app, &bootstrap, &completed] {
      app.run([&completed] { completed.post(); });
      bootstrap.post();
    });
    release.post();
    bool initialized = !bootstrap.timedwait(Zm::now(15));
    ZuCheckRT(initialized);
    bool progressed = initialized && !completed.timedwait(Zm::now(2));
    ZuCheckRT(progressed);
    // A fresh, post-install wake allows the original implementation to clean
    // up too, preserving a complete failing result instead of killing a hang.
    app.run([&app, &detached] {
      app.detach([&detached] { detached.post(); });
    });
    bool closed = !detached.timedwait(Zm::now(10));
    ZuCheckRT(closed);
    if (!closed) Zm::exit(2);
    ZuCheckRT(scheduler.stop());
    return progressed ? 0 : 1;

  }
}
