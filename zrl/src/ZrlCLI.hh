//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface
//
// high-level idempotent wrapper for Zrl::Editor
//
// synopsis:
//
// using namespace Zrl;
// CLI cli;
// cli.init(App{
//   .prompt = [](Prompt &s) { if (!s) s = "prompt> "; },
//   .enter = [](ZuCSpan s) { std::cout << s << '\n'; }
// });
// if (cli.open()) {
//   cli.start();
//   cli.join(); // wait until complete
//   cli.stop();
//   cli.close();
// }

#ifndef ZrlCLI_HH
#define ZrlCLI_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <zlib/ZuPtr.hh>
#include <zlib/ZuInt.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZmFn.hh>
#include <zlib/ZmScheduler.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZrlEditor.hh>

namespace Zrl {

class ZrlAPI CLI : public Editor {
  using Lock = ZmLock;
  using Guard = ZmGuard<Lock>;

  enum { Created = 0, Initialized, Opened, Running }; // state

public:
  ~CLI() { final(); }

  void init(App app);		// set up callbacks
  void final();			// optional teardown

  bool open();			// open terminal - returns true if ok
  void close();			// void close() - close terminal

  bool start();			// start running - returns true if ok
  void stop();			// stop running
  void join();			// block until EOF, signal or other end event
  using Editor::running;	// bool running() - check if running

  void invoke(ZmFn<> fn);	// invoke fn in terminal thread

private:
  void final_();

  bool open_();
  void close_();

  void stop_();

  ZuPtr<ZmScheduler>	m_sched; // internal thread
    ZmLock		  m_lock;
    int			  m_state = Created;
};

} // Zrl

#endif /* ZrlCLI_HH */
