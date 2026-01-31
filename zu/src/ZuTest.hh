//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TAP-emitting static test framework
// - https://testanything.org/tap-version-14-specification.html
// - statically scans tests before main() runs to emit accurate test counts
//   so that interactive harnesses can accurately track progress to 100%
// - intentionally targeted at functional testing workloads that are
//   static at compile-time
// - supports arbitrarily nested sub-tests
// - tests can reside in dynamic shared libraries

// usage:
// - start TAP output and establish the top-level scope:
//   ZuTestMain();
// - evaluate expression expr (ok if true):
//   ZuCheck(expr);
// - an inline block sub-test named "sub":
//   { ZuTest(sub); ... }
// - a sub-test named "loop" with N fixed variations/iterations
//   { ZuTestRepeat(loop, N); for (...) ... }
//   - N must be a compile-time constant
// - sub-test in a function or lambda named "fn"
//   void fn() { ZuTestScope(fn); ... }
// - call a function-wrapped sub-test
//   ZuTestCall(fn);

#ifndef ZuTest_HH
#define ZuTest_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>

struct ZuTestScope {
  const char	*name;
  unsigned	count;
};

struct alignas(32) ZuTestStep {
  ZuTestScope	*scope;
  const char	*file;
  unsigned	line;
  const char	*name;
  unsigned	id;
  unsigned	count;
};

namespace ZuTest_ { inline void addImage_(); }

// intentionally single-threaded
class ZuAPI ZuTestMgr {
  ZuTestMgr();
  ~ZuTestMgr();

  ZuTestMgr(const ZuTestMgr &) = delete;
  ZuTestMgr &operator =(const ZuTestMgr &) = delete;

public:
  static void start() { instance().start_(); }
  static void indent() { instance().indent_(); }

  static void begin(ZuTestScope *scope) {
    instance().begin_(scope);
  }
  static void end(ZuTestScope *scope) {
    instance().end_(scope);
  }
  static void check(ZuTestStep *step, bool ok) {
    instance().check_(step, ok);
  }
  static void call(ZuTestStep *step) {
    instance().call_(step);
  }

private:
  static ZuTestMgr &instance();

friend void ZuTest_::addImage_();

  void addSection(ZuTestStep *begin, ZuTestStep *end);

  struct Section;

  struct RunContext {
    RunContext	*parent = nullptr;
    ZuTestScope	*scope = nullptr;
    ZuTestStep	*step = nullptr;
    unsigned	iteration = 0;
    unsigned	failed = 0;
  };

  void init();

  void start_();

  void indent_();

  void begin_(ZuTestScope *scope);
  void end_(ZuTestScope *scope);
  void check_(ZuTestStep *step, bool ok);
  void call_(ZuTestStep *step);

  Section	*m_head = nullptr;
  Section	*m_tail = nullptr;
  RunContext	m_root;
  RunContext	*m_context = nullptr;
  unsigned	m_indent = 0;
  bool		m_finalized = false;
};

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef _WIN32

#if __has_attribute(retain) || \
    (defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11)
#define ZuTest_Retain __attribute__((retain))
#else
#define ZuTest_Retain
#endif

#define ZuTest_Section __attribute__((section("ZuTest"), used)) \
  ZuTest_Retain

extern "C" {
  extern ZuTestStep __start_ZuTest[] __attribute__((weak));
  extern ZuTestStep __stop_ZuTest[] __attribute__((weak));
}

#else /* _WIN32 */

#define ZuTest_Section __attribute__((section(".ztest$M"), used))

extern "C" {
  __attribute__((section(".ztest$A"), used))
  inline ZuTestStep ZuTest_begin = { 0 };

  __attribute__((section(".ztest$Z"), used))
  inline ZuTestStep ZuTest_end = { 0 };
}

#endif /* _WIN32 */

struct ZuTestContext {
  ZuTestScope	*scope;

  ZuTestContext(ZuTestScope *scope_) : scope{scope_} {
    ZuTestMgr::begin(scope);
  }
  ~ZuTestContext() {
    ZuTestMgr::end(scope);
  }
};

#define ZuTestMain() \
  static ZuTestScope ZuTest_scope = { nullptr }; \
  ZuTestContext ZuTest_context(&ZuTest_scope); \
  ZuTestMgr::start()

#define ZuTestScope(name) \
  static ZuTestScope ZuTest_scope = { #name }; \
  ZuTestContext ZuTest_context(&ZuTest_scope)

#define ZuCheck(x) \
  do { \
    static ZuTestStep ZuTest_check ZuTest_Section = { \
      &ZuTest_scope, __FILE__, __LINE__, #x, 0, 0 \
    }; \
    ZuTestMgr::check(&ZuTest_check, (x)); \
  } while (0)

#define ZuTestRepeat_(name, count) { \
    static ZuTestStep ZuTest_call ZuTest_Section = { \
      &ZuTest_scope, __FILE__, __LINE__, #name, 0, count \
    }; \
    ZuTestMgr::call(&ZuTest_call); \
  }

#define ZuTestCall_(name) ZuTestRepeat_(name, 1)

#define ZuTest(name) \
  ZuTestCall_(name); \
  ZuTestScope(name)

#define ZuTestRepeat(name, count) \
  ZuTestRepeat_(name, count); \
  ZuTestScope(name)

#define ZuTestCall(name, ...) \
  ZuTestCall_(name); \
  name(__VA_ARGS__)

namespace ZuTest_ {
  inline void addImage_() {
#ifndef _WIN32
    ZuTestStep *begin = __start_ZuTest;
    ZuTestStep *end = __stop_ZuTest;
#else
    ZuTestStep *begin = &ZuTest_begin + 1;
    ZuTestStep *end = &ZuTest_end;
#endif
    if (!begin || !end || begin == end) return;
    ZuTestMgr::instance().addSection(begin, end);
  }

  struct Image {
    Image() { addImage_(); }
  };

  __attribute__((used))
  inline Image register_;
}

#endif /* ZuTest_HH */
