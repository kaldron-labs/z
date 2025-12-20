//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface
//
// readline compatible blocking interface to Zrl::CLI
//
// synopsis:
//
// using namespace Zrl;
// char *readline(const char *prompt);

#ifndef Zrl_HH
#define Zrl_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <zlib/ZrlCLI.hh>

extern "C" {
  ZrlExtern char *Zrl_readline(const char *prompt);

  // optional
  ZrlExtern void Zrl_start(const char *prompt);
  ZrlExtern void Zrl_stop();
  ZrlExtern bool Zrl_running();
}

namespace Zrl {
  inline char *readline(const char *prompt) { return Zrl_readline(prompt); }

  // optional
  inline void start(const char *prompt) { return Zrl_start(prompt); }
  inline void stop() { return Zrl_stop(); }
  inline bool running() { return Zrl_running(); }
}

#endif /* Zrl_HH */
