//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZuDerive - reduce boilerplate and symbol length of heavily templated types
// - ZuDerive(Derived, Base) is a macro used as an alternative to a type
//   aliases "using X = Y<Z...>"; however unlike a type alias or typedef,
//   it introduces an explicit new type ID for the derived struct; the new type
//   wraps the base and inherits constructors and assignment operators
// - Base is typically a template with numerous parameters such as
//   compile-time string literals, or a policy-based type like ZmHash
//   that uses a named template parameter (NTP) convention - aliases to
//   such templates have very long mangled symbols that impede debugging
// - ZuDerive_(Derived, Base) can be used when the outer declaration
//   needs template parameters, customization and/or additional members
// - background context: type aliases "using X = Y<Z...>" do not introduce
//   a new type ID in C++, which causes typename bloat due to the combinatorial
//   explosion of aliases with complex templates; this in turn causes
//   linker problems with extremely long mangled symbols and also obstructs
//   debugging (use of ZuDerive may also speed up build times)
// - ZuDerive creates a new type ID with an empty derived class
// - It also provides boilerplate reduction for other derived classes
//   that need to inherit constructors and assignment operators
// - intended for use with regular composite types intended to be used
//   at run-time or as template parameters
// - with C++23, derived types automatically inherit base class operators
//   and can be compared, shifted etc. as if they were the base
// - Z resolves traits via ZuTraitsType(T *), fields via ZuFields_(T *) etc. 
//   so fields, traits, JSON handlers, etc. all resolve consistently

// Note: do not use ZuDerive for consteval types - use plain old inheritance

#ifndef ZuDerive_HH
#define ZuDerive_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuPP.hh>

// make sure to SFINAE-constrain the constructor args
#define ZuDerive_(Derived, Base_) \
  using Base = ZuPP_Strip(Base_); \
  using Base::Base; \
  using Base::operator =; \
  template <typename ...Args, \
    decltype(Base(ZuDeclVal<Args &&>()...), int()) = 0> \
  Derived(Args &&...args) : Base(ZuFwd<Args>(args)...) { }

#define ZuDerive(Derived, Base) \
  struct Derived : public ZuPP_Strip(Base) { ZuDerive_(Derived, Base) }

#endif /* ZuDerive_HH */
