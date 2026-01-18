//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// error handling - platform primitives

#ifndef ZePlatform_HH
#define ZePlatform_HH

#ifndef ZeLib_HH
#include <zlib/ZeLib.hh>
#endif

#ifndef _WIN32
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>
#include <syslog.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuFnName.hh>
#include <zlib/ZuArray.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuDateTime.hh>
#include <zlib/ZuMatcher.hh>

#include <zlib/ZmObject.hh>
#include <zlib/ZmRef.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmLocal.hh>

#include <zlib/ZtString.hh>
#include <zlib/ZtEnum.hh>

// Ze::ErrNo is a regular native OS error code
//
// In addition to the native OS error code, need to handle the EAI_ error
// codes returned by getaddrinfo() (Windows - GetAddrInfo())
//
//   Windows	- EAI_ codes are #defined identically to equiv. system codes
//   Unix	- EAI_ codes are negative, all errno codes are positive
//
// Both types of platform also use a signed integer type for system errors,
// so both sets of codes can be stored in the same type; However,
// strerror() on Unix will not work with EAI_ codes - on Unix we
// need to check for < 0 explicitly and call gai_strerror()

#define ZiLog_BUFSIZ (32<<10)	// caps individual log message size to 32k

// normalized severity levels
namespace Ze {
  ZtEnum(Ze, int8_t, Debug, Info, Warning, Error, Fatal);
}

// normalized OS error number
namespace Ze {

#ifndef _WIN32
using ErrNo = int;
constexpr ErrNo OK() { return 0; }

inline ErrNo errNo() { return errno; }
inline ErrNo sockErrNo() { return errno; }
inline const char *strerror(ErrNo e) {
  return e >= 0 ? ::strerror(e) : gai_strerror(e);
}
#else
using ErrNo = DWORD;				// <= sizeof(int)
constexpr ErrNo OK() { return ERROR_SUCCESS; }	// == 0

inline ErrNo errNo() { return GetLastError(); }
inline ErrNo sockErrNo() { return WSAGetLastError(); }
ZeExtern const char *strerror(ErrNo e);
#endif

}

// general purpose on-heap string for error logging
ZuDerive(ZeString,
  (ZtString<
    ZtStringBuiltin<32,
      ZtStringHeapID<"ZeString">>>));

// OS error number wrapper
class ZeError {
public:
  using ErrNo = Ze::ErrNo;

  ZeError() = default;
  ZeError(const ZeError &) = default;
  ZeError &operator =(const ZeError &) = default;
  ZeError(ZeError &&) = default;
  ZeError &operator =(ZeError &&) = default;

  ZeError(ErrNo e) : m_errNo(e) { }
  ZeError &operator =(ErrNo e) {
    m_errNo = e;
    return *this;
  }

  ErrNo errNo() const { return m_errNo; }
  const char *message() const { return Ze::strerror(m_errNo); }

  bool operator !() const { return m_errNo == Ze::OK(); }
  ZuOpBool

  template <typename S> void print(S &s) const { s << message(); }
  friend ZuPrintFn ZuPrintType(ZeError *);

  struct Traits : public ZuBaseTraits<ZeError> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZeError *);

private:
  ErrNo		m_errNo = Ze::OK();
};

inline ZeError Ze_OK() { return ZeError{}; }
#define ZeOK Ze_OK()

inline ZeError Ze_LastError() { return ZeError{Ze::errNo()}; }
#define ZeLastError Ze_LastError()

inline ZeError Ze_LastSockError() { return ZeError{Ze::sockErrNo()}; }
#define ZeLastSockError Ze_LastSockError()

// event time, thread ID, severity, file name, line number, function
struct ZeEventInfo {
  using ThreadID = Zm::ThreadID;

  ZuTime	time;
  ThreadID	tid = 0;
  ZuCSpan	file;
  int32_t	line = -1;
  ZuCSpan	function;
  ZuCSpan	component;
  int8_t	severity = -1;	// Ze:: Debug, Info, Warning, Error, Fatal

  ZeEventInfo() = default;

  ZeEventInfo(
      int8_t severity_,
      ZuCSpan file_, unsigned line_,
      ZuCSpan function_, ZuCSpan component_) :
    time{Zm::now()}, tid{Zm::getTID()},
    file{file_}, line{int32_t(line_)},
    function{function_}, component{component_},
    severity{severity_} { }

  ZeEventInfo(
      ZuTime time_, ThreadID tid_,
      int8_t severity_,
      ZuCSpan file_, unsigned line_,
      ZuCSpan function_, ZuCSpan component_) :
    time{time_}, tid{tid_},
    file{file_}, line{int32_t(line_)},
    function{function_}, component{component_},
    severity{severity_} { }

  ZeEventInfo(const ZeEventInfo &) = default;
  ZeEventInfo &operator =(const ZeEventInfo &) = default;

  ZeEventInfo(ZeEventInfo &&) = default;
  ZeEventInfo &operator =(ZeEventInfo &&) = default;

  ~ZeEventInfo() = default;

  bool operator !() const { return !*time; }
  ZuOpBool
};

// log buffer
// - many output streams (cout, cerr, etc.) interleave stream operator calls
//   during concurrent fan-in
// - the log buffer serves as both a consistent interface type and a
//   mechanism to properly sequence interleaved output
// - ZiLogBuf intentionally limits any single log entry to ZiLog_BUFSIZ
using ZiLogBuf = ZuCArray<ZiLog_BUFSIZ>;

// message as function delegate
using ZeMsgFn = ZmFn<void(ZiLogBuf &, const ZeEventInfo &)>;

// event base class
struct ZeAnyEvent : public ZeEventInfo {
  ZeAnyEvent(
      int8_t severity,
      ZuCSpan file, unsigned line,
      ZuCSpan function, ZuCSpan component) :
    ZeEventInfo(severity, file, line, function, component) { }

  virtual ZeMsgFn fn() const = 0;

  ZeAnyEvent() = default;

  ZeAnyEvent(const ZeAnyEvent &) = default;
  ZeAnyEvent &operator =(const ZeAnyEvent &) = default;

  ZeAnyEvent(ZeAnyEvent &&) = default;
  ZeAnyEvent &operator =(ZeAnyEvent &&) = default;

protected:
  ~ZeAnyEvent() = default;

public:
  template <typename S> void print(S &s) const {
    auto buf = ZmLocal(ZiLogBuf);
    fn()(*buf, *this);
    s << *buf;
  }
  friend ZuPrintFn ZuPrintType(ZeAnyEvent *);
};

// event enriched with lambda message - [...](auto &s) { s << ... }
template <typename L>
struct ZeEvent : public ZeAnyEvent {
  mutable L	l;

  template <typename L_>
  ZeEvent(
      int8_t severity,
      ZuCSpan file, unsigned line,
      ZuCSpan function, ZuCSpan component,
      L_ &&l_) :
    ZeAnyEvent(severity, file, line, function, component),
    l{ZuFwd<L_>(l_)} { }

  template <typename S, typename L_ = L>
  friend decltype(
      ZuDeclVal<L_ &>()(ZuDeclVal<S &>()),
      ZuDeclVal<S &>())
  operator <<(S &s, const ZeEvent &e) { e.l(s); return s; }
  template <typename S, typename L_ = L>
  friend decltype(
      ZuDeclVal<L_ &>()(ZuDeclVal<S &>(), ZuDeclVal<const ZeEventInfo &>()),
      ZuDeclVal<S &>())
  operator <<(S &s, const ZeEvent &e) { e.l(s, e); return s; }

  template <typename L_ = L>
  decltype(ZuDeclVal<L_ &>()(
	ZuDeclVal<ZiLogBuf &>()),
      ZeMsgFn())
  fn_() const {
    return {[l_ = ZuMv(l)](auto &s, const auto &) mutable { l_(s); }};
  }
  template <typename L_ = L>
  decltype(ZuDeclVal<L_ &>()(
	ZuDeclVal<ZiLogBuf &>(),
	ZuDeclVal<const ZeEventInfo &>()),
      ZeMsgFn())
  fn_() const {
    return {ZuMv(l)};
  }
  ZeMsgFn fn() const { return fn_(); }

  ZeEvent() = delete;

  ZeEvent(const ZeEvent &) = delete;
  ZeEvent &operator =(const ZeEvent &) = delete;

  ZeEvent(ZeEvent &&) = default;
  ZeEvent &operator =(ZeEvent &&) = default;

  ~ZeEvent() = default;
};

// monomorphic (type-erased) event
struct ZeException_HeapID : public ZuStringT<"ZeException"> { };
template <>
struct ZeEvent<ZeMsgFn> : public ZeAnyEvent {
  using L = ZeMsgFn;
  using Mk = ZeMsgFn::Lambda<ZeException_HeapID>;

  mutable L	l;

  template <
    typename L_,
    decltype(ZuDeclVal<L_ &>()(ZuDeclVal<ZiLogBuf &>()), int()) = 0>
  ZeEvent(
      int8_t severity,
      ZuCSpan file, unsigned line,
      ZuCSpan function, ZuCSpan component, L_ l_) :
    ZeAnyEvent(severity, file, line, function, component),
    l{Mk::fn([l_ = ZuMv(l_)](auto &s, const auto &) mutable { l_(s); })} { }
  template <
    typename L_,
    decltype(ZuDeclVal<L_ &>()(
	ZuDeclVal<ZiLogBuf &>(),
	ZuDeclVal<const ZeEventInfo &>()), int()) = 0>
  ZeEvent(
      int8_t severity,
      ZuCSpan file, unsigned line,
      ZuCSpan function, ZuCSpan component, L_ l_) :
    ZeAnyEvent(severity, file, line, function, component),
    l{Mk::fn(ZuMv(l_))} { }

  ZeEvent() = default;

  ZeEvent(const ZeEvent &) = default;
  ZeEvent &operator =(const ZeEvent &) = default;

  ZeEvent(ZeEvent &&) = default;
  ZeEvent &operator =(ZeEvent &&) = default;

  ~ZeEvent() = default;

  bool operator !() const { return !l; }

  ZeMsgFn fn() const { return {ZuMv(l)}; }
};
using ZeException = ZeEvent<ZeMsgFn>;

template <typename L>
ZeEvent(int8_t, ZuCSpan, unsigned, ZuCSpan, ZuCSpan, L) -> ZeEvent<ZuDecay<L>>;

// convert string/printable to lambda
namespace ZeMsg_ {
template <typename U> struct IsLiteral_ : public ZuBool<
  bool(ZuIsSame<U, char [sizeof(U)]>{}) ||
  bool(ZuIsSame<U, char (&)[sizeof(U)]>{}) ||
  bool(ZuIsSame<U, const char [sizeof(U)]>{}) ||
  bool(ZuIsSame<U, const char (&)[sizeof(U)]>{})> { };
template <typename U> struct IsLiteral : public IsLiteral_<U> { };
template <typename U, typename R = void>
using MatchLiteral = ZuIfT<IsLiteral<U>{}, R>;

template <typename U> struct IsPrint_ : public ZuBool<
    !IsLiteral_<U>{} && (ZuTraits<U>::IsString || ZuPrint<U>::OK)> { };
template <typename U> struct IsPrint : public IsPrint_<U> { };
template <typename U, typename R = void>
using MatchPrint = ZuIfT<IsPrint<U>{}, R>;

template <typename U> struct IsOther_ :
  public ZuBool<!IsLiteral_<U>{} && !IsPrint_<U>{}> { };
template <typename U> struct IsOther : public IsOther_<ZuDecay<U>> { };
template <typename U, typename R = void>
using MatchOther = ZuIfT<IsOther<U>{}, R>;

template <typename Msg, decltype(MatchOther<Msg>(), int()) = 0>
inline decltype(auto) fn(Msg &&msg) {
  return ZuFwd<Msg>(msg);
}
template <typename Msg, decltype(MatchLiteral<Msg>(), int()) = 0>
inline auto fn(Msg &&msg) {
  return [msg = static_cast<const char *>(msg)](auto &s) mutable { s << msg; };
}
template <typename Msg, decltype(MatchPrint<Msg>(), int()) = 0>
inline auto fn(Msg &&msg) {
  return [msg = ZuFwd<Msg>(msg)](auto &s) mutable { s << ZuMv(msg); };
}
} // ZeMsg_

// make a polymorphic (typed) event
template <typename Msg>
auto ZeMkEvent(
    int8_t severity,
    ZuCSpan file, unsigned line,
    ZuCSpan function, ZuCSpan component,
    Msg &&msg) {
  return ZeEvent(
    severity, file, line, function, component,
    ZeMsg_::fn(ZuFwd<Msg>(msg)));
}
#define ZeEVENT_(sev, component, msg) \
  ZeMkEvent(sev, __FILE__, __LINE__, ZuFnName, component, msg)
#define ZeEVENT(sev, component, msg) ZeEVENT_(Ze:: sev, component, msg)

// make a monomorphic (type-erased) event
// - unlike the regular typed event, ZeException can be copied and thrown
template <typename Msg>
ZeException ZeMkException(
    int8_t severity,
    ZuCSpan file, unsigned line,
    ZuCSpan function, ZuCSpan component,
    Msg &&msg) {
  return ZeException(
    severity, file, line, function, component,
    ZeMsg_::fn(ZuFwd<Msg>(msg)));
}
#define ZeEXCEPT_(sev, component, msg) \
  ZeMkException(sev, __FILE__, __LINE__, ZuFnName, component, msg)
#define ZeEXCEPT(sev, component, msg) ZeEXCEPT_(Ze:: sev, component, msg)

namespace Ze {

ZeExtern ZuCSpan severity(unsigned i);
ZeExtern ZuCSpan file(ZuCSpan s);
ZeExtern ZuCSpan function(ZuCSpan s);

}

#endif /* ZePlatform_HH */
