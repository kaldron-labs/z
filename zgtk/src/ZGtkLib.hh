//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// Z Gtk integration

#ifndef ZGtkLib_HH
#define ZGtkLib_HH

#include <glib.h>

#include <gtk/gtk.h>

#include <zlib/ZuLib.hh>

#ifdef _WIN32

#ifdef ZGTK_EXPORTS
#define ZGtkAPI ZuExport_API
#define ZGtkExplicit ZuExport_Explicit
#else
#define ZGtkAPI ZuImport_API
#define ZGtkExplicit ZuImport_Explicit
#endif
#define ZGtkExtern extern ZGtkAPI

#else

#define ZGtkAPI
#define ZGtkExplicit
#define ZGtkExtern extern

#endif

#endif /* ZGtkLib_HH */
