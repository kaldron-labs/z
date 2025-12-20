//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface - application configuration

#ifndef ZrlConfig_HH
#define ZrlConfig_HH

#ifndef ZrlLib_HH
#include <zlib/ZrlLib.hh>
#endif

namespace Zrl {

// line editor configuration
struct Config {
  unsigned	vkeyInterval = 100;	// vkey seq. interval (milliseconds)
  unsigned	maxLineLen = 32768;	// maximum line length
  unsigned	maxCompPages = 5;	// max # pages of possible completions
  unsigned	histOffset = 0;		// initial history offset
  unsigned	maxStackDepth = 10;	// maximum mode stack depth
  unsigned	maxFileSize = 1<<20;	// maximum keymap file size
  unsigned	maxSynVKey = 10;	// maximum # synthetic keystrokes
  unsigned	maxUndo = 100;		// maximum undo/redo
};

} // Zrl

#endif /* ZrlConfig_HH */
