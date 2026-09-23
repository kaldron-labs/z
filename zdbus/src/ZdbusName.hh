//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// D-Bus protocol names; object paths and signatures belong to ZfDBUS.

#ifndef ZdbusName_HH
#define ZdbusName_HH

#ifndef ZdbusLib_HH
#include <zlib/ZdbusLib.hh>
#endif

#include <zlib/ZuSpan.hh>

namespace Zdbus_::Name {

// The D-Bus name limit includes the optional leading ':' of a unique name.
enum { MaxLength = 255 };

inline bool letter(uint8_t c)
{
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

inline bool digit(uint8_t c) { return c >= '0' && c <= '9'; }

template <bool Bus>
bool dotted(ZuCSpan name)
{
  unsigned n = name.length();
  if (!n || n > MaxLength) return false;
  bool unique = Bus && name[0] == ':';
  unsigned begin = unsigned(unique);
  bool separator = false;
  bool first = true;
  for (unsigned i = begin; i < n; ++i) {
    uint8_t c = name[i];
    if (c == '.') {
      if (first) return false;
      separator = true;
      first = true;
      continue;
    }
    if (!(letter(c) || digit(c) || c == '_' || (Bus && c == '-')) ||
        (first && digit(c) && !unique)) return false;
    first = false;
  }
  return separator && !first;
}

inline bool interface(ZuCSpan name) { return dotted<false>(name); }
inline bool error(ZuCSpan name) { return interface(name); }
inline bool bus(ZuCSpan name) { return dotted<true>(name); }

inline bool member(ZuCSpan name)
{
  unsigned n = name.length();
  if (!n || n > MaxLength || digit(name[0])) return false;
  for (unsigned i = 0; i < n; ++i) {
    uint8_t c = name[i];
    if (!(letter(c) || digit(c) || c == '_')) return false;
  }
  return true;
}

} // Zdbus_::Name

#endif /* ZdbusName_HH */
