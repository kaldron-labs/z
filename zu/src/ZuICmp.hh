//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// case-insensitive matching

#ifndef ZuICmp_HH
#define ZuICmp_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuSpan.hh>
#include <zlib/ZuStringFn.hh>

template <typename T>
struct ZuICmp : public ZuCmp<T> {
public:
  static int cmp(ZuCSpan s1, ZuCSpan s2) {
    int l1 = s1.length(), l2 = s2.length();
    if (!l1) return l2 ? -1 : 0;
    if (!l2) return 1;
    int i = Zu::stricmp_(s1.data(), s2.data(), l1 > l2 ? l2 : l1);
    if (i) return i;
    return l1 - l2;
  }
  static bool less(ZuCSpan s1, ZuCSpan s2) {
    int l1 = s1.length(), l2 = s2.length();
    if (!l1) return l2;
    if (!l2) return false;
    int i = Zu::stricmp_(s1.data(), s2.data(), l1 > l2 ? l2 : l1);
    if (i) return i < 0;
    return l1 < l2;
  }
  static int equals(ZuCSpan s1, ZuCSpan s2) {
    int l1 = s1.length(), l2 = s2.length();
    if (l1 != l2) return false;
    return !Zu::stricmp_(s1.data(), s2.data(), l1);
  }
};

#endif /* ZuICmp_HH */
