//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Gtk wrapper

#ifndef ZGtkCallback_HH
#define ZGtkCallback_HH

#ifndef ZGtkLib_HH
#include <zlib/ZGtkLib.hh>
#endif

#include <glib.h>

#include <gtk/gtk.h>

#include <zlib/ZuLambdaTraits.hh>

namespace ZGtk {

template <typename L>
constexpr auto callback(const L &l) {
  return G_CALLBACK(ZuInvokeFn(l));
}

} // ZGtk

#endif /* ZGtkCallback_HH */
