//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#ifndef ZDashModule_HH
#define ZDashModule_HH

#include <stdint.h>
#include <gtk/gtk.h>
#include <zlib/ZuSpan.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZiIOBuf.hh>

namespace ZDash {

// GTK-owned view of a publisher; identity is valid until its source retires.
struct SourceView {
  const void *identity = nullptr;
  uint64_t generation = 0;
  unsigned count = 0;
  int row = -1;
  int rag = 0;
};

// Calls ending in _ require the indicated owner. Posted work must not outlive
// the session; closing is delivered on GTK after Rx and refresh work drain.
struct ModuleHost {
  ZmFn<void(ZmFn<void()>)> rxRun;
  ZmFn<void(ZmFn<void()>)> gtkRun;
  ZmFn<ZmRef<ZiIOBuf>()> request_;		// Rx
  ZmFn<bool(ZuBSpan, bool)> receive_;	// Rx; bool selects subscription filter
  ZmFn<SourceView(ZuCSpan, ZuCSpan)> source_; // GTK
  ZmFn<unsigned()> pending_;		// GTK
  ZmFn<void()> stop;			// any thread
  GtkTreeModel *model = nullptr;		// GTK
  GtkWindow *window = nullptr;		// GTK
  int publisherCol = 0;
  int deviceCol = 0;
};

struct ModuleSession {
  ZuCSpan token;
  unsigned timeout = 0;
  bool hidden = false;
  bool offline = false;
  ZmFn<void(const ModuleHost &)> ready;	// GTK
  ZmFn<void(bool)> consumed;		// GTK; record spans mirror boundary
  ZmFn<void()> drained;			// GTK
  ZmFn<void()> closing;			// GTK
  ZmFn<void()> closed;			// GTK; real window destruction
};

struct Module {
  ZmFn<bool(ModuleSession &)> session;	// main; one shot, returns after drain
  bool online = false;
};

using ModuleFn = int (*)(const Module &);
#define ZDashModuleFnSym "ZdashModule"

} // ZDash

#endif
