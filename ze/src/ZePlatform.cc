//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// platform-specific

#include <zlib/ZuDerive.hh>
#include <zlib/ZuUTF.hh>

#include <zlib/ZmSingleton.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmGuard.hh>

#include <zlib/ZtPlatform.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZtRegex.hh>

#include <zlib/ZePlatform.hh>

ZuCSpan Ze::severity(unsigned i)
{
  static const char * const name[] = {
    "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"
  };
  static constexpr unsigned namelen[] = { 5, 4, 7, 5, 5 };

  return i > 4 ? ZuCSpan("UNKNOWN", 7) : ZuCSpan(name[i], namelen[i]);
}

ZuCSpan Ze::file(ZuCSpan s)
{
  ZtRegexCaptures(c, 1);
#ifndef _WIN32
  const auto &r = ZtREGEX("([^/]*)$");
#else
  const auto &r = ZtREGEX("([^:/\\]*)$");
#endif
  if (r.m(s, c) < 1) return s;
  return c[2];
}

ZuCSpan Ze::function(ZuCSpan s)
{
  ZtRegexCaptures(c, 1);
  if (ZtREGEX("([a-zA-Z_][a-zA-Z_0-9:]*)\(").m(s, c) < 2) return s;
  return c[2];
}

#ifdef _WIN32

static constexpr struct {
  Ze::ErrNo	code;
  const char	*msg;
} ZePlatform_WSAErrors_[] = {
{ WSAEINTR,		  "Interrupted system call" },
{ WSAEBADF,		  "Bad file number" },
{ WSAEACCES,		  "Permission denied" },
{ WSAEFAULT,		  "Bad address" },
{ WSAEINVAL,		  "Invalid argument" },
{ WSAEMFILE,		  "Too many open sockets" },
{ WSAEWOULDBLOCK,	  "Operation would block" },
{ WSAEINPROGRESS,	  "Operation now in progress" },
{ WSAEALREADY,		  "Operation already in progress" },
{ WSAENOTSOCK,		  "Socket operation on non-socket" },
{ WSAEDESTADDRREQ,	  "Destination address required" },
{ WSAEMSGSIZE,		  "Message too long" },
{ WSAEPROTOTYPE,	  "Protocol wrong type for socket" },
{ WSAENOPROTOOPT,	  "Bad protocol option" },
{ WSAEPROTONOSUPPORT,	  "Protocol not supported" },
{ WSAESOCKTNOSUPPORT,	  "Socket type not supported" },
{ WSAEOPNOTSUPP,	  "Operation not supported on socket" },
{ WSAEPFNOSUPPORT,	  "Protocol family not supported" },
{ WSAEAFNOSUPPORT,	  "Address family not supported" },
{ WSAEADDRINUSE,	  "Address already in use" },
{ WSAEADDRNOTAVAIL,	  "Can't assign requested address" },
{ WSAENETDOWN,		  "Network is down" },
{ WSAENETUNREACH,	  "Network is unreachable" },
{ WSAENETRESET,		  "Net connection reset" },
{ WSAECONNABORTED,	  "Software caused connection abort" },
{ WSAECONNRESET,	  "Connection reset by peer" },
{ WSAENOBUFS,		  "No buffer space available" },
{ WSAEISCONN,		  "Socket is already connected" },
{ WSAENOTCONN,		  "Socket is not connected" },
{ WSAESHUTDOWN,		  "Can't send after socket shutdown" },
{ WSAETOOMANYREFS,	  "Too many references, can't splice" },
{ WSAETIMEDOUT,		  "Connection timed out" },
{ WSAECONNREFUSED,	  "Connection refused" },
{ WSAELOOP,		  "Too many levels of symbolic links" },
{ WSAENAMETOOLONG,	  "File name too long" },
{ WSAEHOSTDOWN,		  "Host is down" },
{ WSAEHOSTUNREACH,	  "No route to host" },
{ WSAENOTEMPTY,		  "Directory not empty" },
{ WSAEPROCLIM,		  "Too many processes" },
{ WSAEUSERS,		  "Too many users" },
{ WSAEDQUOT,		  "Disc quota exceeded" },
{ WSAESTALE,		  "Stale NFS file handle" },
{ WSAEREMOTE,		  "Too many levels of remote in path" },
{ WSASYSNOTREADY,	  "Network system is unavailable" },
{ WSAVERNOTSUPPORTED,	  "Winsock version out of range" },
{ WSANOTINITIALISED,	  "WSAStartup not yet called" },
{ WSAEDISCON,		  "Graceful shutdown in progress" },
{ WSAHOST_NOT_FOUND,	  "Host not found" },
{ WSANO_DATA,		  "No host data of that type was found" },
{ WSAENOMORE,		  "No more results" },
{ WSAECANCELLED,	  "Call cancelled" },
{ WSAEINVALIDPROCTABLE,	  "Invalid procedure call table" },
{ WSAEINVALIDPROVIDER,	  "Invalid requested service provider" },
{ WSAEPROVIDERFAILEDINIT, "Could not load or initialize service provider" },
{ WSASYSCALLFAILURE,	  "Critical system call failure" },
{ WSASERVICE_NOT_FOUND,	  "No such service known" },
{ WSATYPE_NOT_FOUND,	  "Class not found" },
{ WSA_E_NO_MORE,	  "No more results" },
{ WSA_E_CANCELLED,	  "Call cancelled" },
{ WSAEREFUSED,		  "Database query refused" },
{ WSATRY_AGAIN,		  "Transient error - retry" },
{ WSANO_RECOVERY,	  "Unrecoverable database query error" },
{ 0,			  NULL }
};
class ZePlatform_WSAErrors {
public:
  ZuDerive(Hash,
    (ZmLHashKV<DWORD, const char *,
      ZmLHashStatic<6,
	ZmLHashLock<ZmNoLock>>>));

  ZePlatform_WSAErrors() {
    m_hash = new Hash();
    const char *msg;
    for (unsigned i = 0; msg = ZePlatform_WSAErrors_[i].msg; i++)
      m_hash->add(ZePlatform_WSAErrors_[i].code, msg);
  }
  ZuInline const char *lookup(DWORD i) {
    return m_hash->findVal(i);
  }

private:
  ZmRef<Hash>	m_hash;
};

struct ZePlatform_FMBuf : public ZmObject {
  ZuWArray<ZiLog_BUFSIZ / 2>	w;
  ZuCArray<ZiLog_BUFSIZ>	s;
};
static ZePlatform_FMBuf *fmBuf()
{
  ZePlatform_FMBuf *buf =
    ZmSpecific<ZePlatform_FMBuf,
      ZmSpecificCleanup<ZmCleanup::Platform>>::instance();
  buf->w.null();
  buf->s.null();
  return buf;
}
ZuInline static constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}
const char *Ze::strerror(ErrNo e)
{
  const char *msg;

  if (msg = ZmSingleton<ZePlatform_WSAErrors>::instance()->lookup(e))
    return msg;

  if (e == ERROR_DUP_NAME)
    return "Duplicate network name or too many network end-points";

  ZePlatform_FMBuf *buf = fmBuf();

  DWORD n = FormatMessage(
	FORMAT_MESSAGE_IGNORE_INSERTS |
	FORMAT_MESSAGE_FROM_SYSTEM,
	0, e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	buf->w.data(), buf->w.size(), 0);

  if (!n || !buf->w[0]) return "";

  buf->w.length(n);

  buf->s.length(ZuUTF<char, wchar_t>::cvt(
	ZuSpan<char>(buf->s.data(), buf->s.size() - 1), buf->w));

  // FormatMessage() often returns verbose junk; clean it up
  {
    char *ptr = buf->s.data();
    char *end = ptr + buf->s.length();
    int c;
    while (ptr < end) {
      if (ZuUnlikely(isspace__(c = *ptr++))) {
	char *ws = --ptr;
	while (ZuLikely(ws < end) && ZuUnlikely(isspace__(c = *++ws)));
	*ptr++ = ' ';
	if (ZuUnlikely(ws > ptr)) {
	  memmove(ptr, ws, end - ws);
	  end -= (ws - ptr);
	}
      }
    }
    while (ZuUnlikely(isspace__(c = *--end) || c == '.'));
    buf->s.length(++end - buf->s.data());
  }

  return buf->s.data();
}

#endif /* _WIN32 */
