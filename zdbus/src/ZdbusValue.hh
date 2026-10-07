//  -*- mode:c++; indent-tabs-mode:t; tab-width=8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Internal single-field schemas for D-Bus header variant values.

#ifndef ZdbusValue_HH
#define ZdbusValue_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZfDBUS.hh>

namespace Zdbus_ {

struct PathValue { ZuCSpan value; };
struct TextValue { ZuCSpan value; };
struct SigValue { ZuCSpan value; };
struct UIntValue { uint32_t value; };

ZfStruct(, PathValue, (value, (Mutable), String));
ZfStructRender(, PathValue, DBUS,
  (value, (DBUS::Type<ZfDBUS::Type::ObjectPath>)));
ZfStruct(, TextValue, (value, (Mutable), String));
ZfStructRender(, TextValue, DBUS, value);
ZfStruct(, SigValue, (value, (Mutable), String));
ZfStructRender(, SigValue, DBUS,
  (value, (DBUS::Type<ZfDBUS::Type::Signature>)));
ZfStruct(, UIntValue, (value, (Mutable), UInt32));
ZfStructRender(, UIntValue, DBUS, value);

} // Zdbus_

#endif /* ZdbusValue_HH */
