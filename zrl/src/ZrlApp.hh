//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface - application callbacks

#ifndef ZrlApp_HH
#define ZrlApp_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

#include <signal.h>
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZePlatform.hh>

namespace Zrl {

using ErrorFn = ZmFn<void(ZuCSpan)>;		// (message)

using OpenFn = ZmFn<void(bool)>;		// (ok)
using CloseFn = ZmFn<>;

ZuDerive(Prompt, (ZtArray<uint8_t, ZtArrayHeapID<"Zrl.Prompt">>));
using PromptFn = ZmFn<void(Prompt &)>;

using EnterFn = ZmFn<bool(ZuCSpan)>;
using EndFn = ZmFn<>;
using SigFn = ZmFn<bool(int)>;

using CompSpliceFn = ZmFn<void(	// splice completion
  unsigned,			// off     - byte offset
  ZuUTFSpan,			// span    - UTF8 span to be replaced
  ZuBSpan,			// replace - replacement data
  ZuUTFSpan)>;			// rspan   - UTF8 span of replacement

using CompIterFn = ZmFn<void(	// iterate completion
  ZuBSpan,			// data    - completion data
  ZuUTFSpan)>;			// span    - UTF8 span of completion

using CompInitFn = ZmFn<void(	// initialize completion
  ZuBSpan,			// data    - line data (entire line)
  unsigned,			// cursor  - byte offset of cursor
  CompSpliceFn)>;		// splice  - line splice function

using CompStartFn = ZmFn<>;	// re-start iteration

using CompSubstFn = ZmFn<bool(	// substitute next/prev completion
  CompSpliceFn,			// splice  - line splice function
  bool)>;			// next    - true for next, false for previous

using CompNextFn = ZmFn<bool(CompIterFn)>; // iterate next completion

using CompFinalFn = ZmFn<>;		// finalize completion

using HistFn = ZmFn<void(ZuBSpan)>;

using HistSaveFn = ZmFn<void(unsigned, ZuBSpan)>;
using HistLoadFn = ZmFn<bool(unsigned, HistFn)>;

struct App {
  ErrorFn	error;		// I/O error

  OpenFn	open;		// terminal opened
  CloseFn	close;		// terminal closed

  PromptFn	prompt;

  EnterFn	enter;		// line entered
  EndFn		end;		// end of input (EOF)
  SigFn		sig;		// signal (^C ^\ ^Z)

  CompInitFn	compInit;	// initialize completions
  CompFinalFn	compFinal;	// finalize completions
  CompStartFn	compStart;	// (re-)start enumeration of completions
  CompSubstFn	compSubst;	// substitute next completion in sequence
  CompNextFn	compNext;	// iterate next completion in sequence

  HistSaveFn	histSave;	// save line in history with index
  HistLoadFn	histLoad;	// load line from history given index
};

} // Zrl

#endif /* ZrlApp_HH */
