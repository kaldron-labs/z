//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TAP-emitting test framework
// - https://testanything.org/tap-version-14-specification.html
// - static test count established before main()
// - interactive harnesses can use early counts to track progress to 100%
// - intentionally targeted at functional testing workloads that are
//   predominantly static, i.e. established at compile-time
// - supports arbitrarily nested sub-tests
// - tests can reside in dynamic shared libraries

// usage:
// - start TAP output and establish the top-level scope:
//   ZuTestMain();
// - evaluate expression expr (ok if true):
//   ZuCheck(expr);
// - an inline block sub-test named "sub":
//   { ZuTest(sub); ... }
// - a sub-test named "loop" with N fixed variations/iterations:
//   { ZuTestRepeat(loop, N); for (...) ... }
//   - N must be a compile-time constant
// - call a sub-test written in a callable named fn:
//   ZuTestCall(fn);
// - sub-test in a callable named fn:
//   void fn() { ZuTestScope(fn); ... }
// - dynamic sub-tests that vary at runtime should use RT equivalents:
//   ZuTestCallRT / ZuTestScopeRT / ZuCheckRT / ZuTestRT

#ifndef ZuTest_HH
#define ZuTest_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuSpan.hh>
#include <zlib/ZuString.hh>

struct ZuTest_Scope {
  const char	*name;
  unsigned	count;
  bool		dynamic;
};

struct alignas(32) ZuTest_Step {
  ZuTest_Scope	*scope;
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

  static void begin(ZuTest_Scope *scope) {
    instance().begin_(scope);
  }
  static void end(ZuTest_Scope *scope) {
    instance().end_(scope);
  }
  static bool check(ZuTest_Step *step, bool ok, const char *name) {
    return instance().check_(step, ok, name);
  }
  static void call(ZuTest_Step *step) {
    instance().call_(step);
  }

private:
  static ZuTestMgr &instance();

friend void ZuTest_::addImage_();

  void addSection(ZuTest_Step *begin, ZuTest_Step *end);

  struct Section;

  struct RunContext {
    RunContext		*parent = nullptr;
    ZuTest_Scope	*scope = nullptr;
    ZuTest_Step		*step = nullptr;
    unsigned		iteration = 0;
    unsigned		failed = 0;
  };

  void init();

  void start_();

  void indent_();

  void begin_(ZuTest_Scope *scope);
  void end_(ZuTest_Scope *scope);
  bool check_(ZuTest_Step *step, bool ok, const char *name);
  void call_(ZuTest_Step *step);

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
  extern ZuTest_Step __start_ZuTest[] __attribute__((weak));
  extern ZuTest_Step __stop_ZuTest[] __attribute__((weak));
}

#else /* _WIN32 */

#define ZuTest_Section __attribute__((section(".ztest$M"), used))

extern "C" {
  __attribute__((section(".ztest$A"), used))
  inline ZuTest_Step ZuTest_begin = { 0 };

  __attribute__((section(".ztest$Z"), used))
  inline ZuTest_Step ZuTest_end = { 0 };
}

#endif /* _WIN32 */

namespace ZuTest_ {
// this is carefully written to ensure static initialization
// in the image without COMDAT or ODR conflicts
// - gcc: cannot place vague-linkage template statics in the shared ZuTest
//   section without a COMDAT conflict; internal linkage avoids that conflict.
// - clang: retains external linkage to coalesce inline-header registrations.
#if defined(__GNUC__) && !defined(__clang__)
#define ZuTest_Linkage static
#else
#define ZuTest_Linkage
#endif
  template <
    ZuTest_Scope *Scope, ZuString File, unsigned Line,
    ZuString Expr, unsigned Count>
  ZuTest_Linkage ZuInline ZuTest_Step &step_() {
    static constinit ZuTest_Step step ZuTest_Section = {
      Scope, File.data_, Line, Expr.data_, 0, Count
    };
    return step;
  }
#undef ZuTest_Linkage
}

struct ZuTest_Context {
  ZuTest_Scope	*scope;

  ZuTest_Context(ZuTest_Scope *scope_) : scope{scope_} {
    ZuTestMgr::begin(scope);
  }
  ~ZuTest_Context() {
    ZuTestMgr::end(scope);
  }
};

#define ZuTestMain() \
  static ZuTest_Scope ZuTest_scope = { nullptr, 0, false }; \
  ZuTest_Context ZuTest_context(&ZuTest_scope); \
  ZuTestMgr::start()

#define ZuTestScope_(name) \
  static ZuTest_Scope ZuTest_scope = { name, 0, false }; \
  ZuTest_Context ZuTest_context(&ZuTest_scope)

#define ZuTestScope(name) \
  ZuTestScope_(ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(name))))

#define ZuCheck(x, ...) \
  do { \
    auto &ZuTest_check = ZuTest_::step_< \
      &ZuTest_scope, __FILE__, __LINE__, \
      ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(x))), 0>(); \
    __VA_OPT__(if) (!ZuTestMgr::check(&ZuTest_check, (x), nullptr)) \
    __VA_OPT__(ZuPP_Eval(ZuPP_MapComma(ZuPP_Strip, __VA_ARGS__))); \
  } while (0)

#define ZuTestScopeRT_(name) \
  static ZuTest_Scope ZuTest_scope = { name, 0, true }; \
  ZuTest_Context ZuTest_context(&ZuTest_scope)

#define ZuTestScopeRT(name) \
  ZuTestScopeRT_(ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(name))))

#define ZuCheckRT(x, ...) \
  do { \
    __VA_OPT__(if) (!ZuTestMgr::check(nullptr, (x), \
      ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(x))))) \
    __VA_OPT__(ZuPP_Eval(ZuPP_MapComma(ZuPP_Strip, __VA_ARGS__))); \
  } while (0)

#define ZuTestRepeat__(name, count) \
  do { \
    auto &ZuTest_call = ZuTest_::step_< \
      &ZuTest_scope, __FILE__, __LINE__, \
      name, count>(); \
    ZuTestMgr::call(&ZuTest_call); \
  } while (0)

#define ZuTestCall__(name) ZuTestRepeat__(name, 1)

#define ZuTest_(name) \
  ZuTestCall__(name); \
  ZuTestScope_(name)

#define ZuTest(name) \
  ZuTest_(ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(name))))

#define ZuTestRepeat_(name, count) \
  ZuTestRepeat__(name, count); \
  ZuTestScope_(name)

#define ZuTestRepeat(name, count) \
  ZuTestRepeat_(ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(name))), count)

#define ZuTestRT_(name) \
  ZuTestCall__(name); \
  ZuTestScopeRT_(name)

#define ZuTestRT(name) \
  ZuTestRT_(ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(name))))

#define ZuTestCall_(name, fn, ...) \
  do { \
    ZuTestCall__(name); \
    ZuPP_Strip(fn)(__VA_ARGS__); \
  } while (0)

#define ZuTestCall(fn, ...) \
  ZuTestCall_( \
    ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(fn))), \
    fn __VA_OPT__(, __VA_ARGS__))

#define ZuTestCallRT_(name, fn, ...) \
  do { \
    static ZuTest_Step ZuTest_call = { \
      &ZuTest_scope, __FILE__, __LINE__, \
      name, 0, 1 \
    }; \
    ZuTestMgr::call(&ZuTest_call); \
    ZuPP_Strip(fn)(__VA_ARGS__); \
  } while (0)

#define ZuTestCallRT(fn, ...) \
  ZuTestCallRT_( \
    ZuPP_Eval(ZuPP_Defer(ZuPP_Q)(ZuPP_Strip(fn))), \
    fn __VA_OPT__(, __VA_ARGS__))

namespace ZuTest_ {
  inline void addImage_() {
#ifndef _WIN32
    ZuTest_Step *begin = __start_ZuTest;
    ZuTest_Step *end = __stop_ZuTest;
#else
    ZuTest_Step *begin = &ZuTest_begin + 1;
    ZuTest_Step *end = &ZuTest_end;
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
