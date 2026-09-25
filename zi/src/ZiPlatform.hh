//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// platform-specific I/O primitives

#ifndef ZiPlatform_HH
#define ZiPlatform_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#ifndef _WIN32

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <netdb.h>

#else /* !_WIN32 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#endif /* !_WIN32 */

#include <zlib/ZuInt.hh>
#include <zlib/ZuTraits.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZtString.hh>

namespace Zi {

// Zi::Name is always a char string, while Zi::Path is wchar_t[] on Windows
ZuDerive(Name, (ZtString<ZtStringHeapID<"Zi.Name">>));

#ifndef _WIN32
using Handle = int;
ZuInline constexpr Handle nullHandle() { return -1; }
ZuInline constexpr bool nullHandle(Handle i) { return i < 0; }
using Socket = int;
ZuInline constexpr Socket nullSocket() { return -1; }
ZuInline constexpr bool nullSocket(Socket i) { return i < 0; }
inline void closeSocket(Socket s) { ::close(s); }
ZuDerive(Path, (ZtString<ZtStringHeapID<"Zi.Path">>));
using Offset = off_t;
ZuDerive(Hostname, (ZtString<ZtStringBuiltin<48, ZtStringHeapID<"Zi.Hostname">>>));
ZuDerive(Username, (ZtString<ZtStringBuiltin<48, ZtStringHeapID<"Zi.Username">>>));
enum {
  PathMax = PATH_MAX,
  NameMax = NAME_MAX,
  NVecMax = IOV_MAX
};
#else
using Handle = HANDLE;
ZuInline const Handle nullHandle() { return INVALID_HANDLE_VALUE; }
ZuInline bool nullHandle(Handle i) { return !i || i == INVALID_HANDLE_VALUE; }
using Socket = SOCKET;
ZuInline constexpr Socket nullSocket() { return INVALID_SOCKET; }
ZuInline constexpr bool nullSocket(Socket i) { return i == INVALID_SOCKET; }
inline void closeSocket(Socket s) { ::closesocket(s); }
ZuDerive(Path, (ZtWString<ZtStringHeapID<"Zi.Path">>));
using Offset = int64_t;	// 2x DWORD
using Hostname = ZtWString<ZtStringBuiltin<48, ZtStringHeapID<"Zi.Hostname">>>;
using Username = ZtWString<ZtStringBuiltin<48, ZtStringHeapID<"Zi.Username">>>;
enum {
  PathMax = 32767,	// NTFS limit (MAX_PATH is 260 for FAT)
  NameMax = 255,	// NTFS & FAT limit
  NVecMax = 2048	// Undocumented system-wide limit
};
#endif

enum {
  HostnameMax = NI_MAXHOST,
  ServicenameMax = NI_MAXSERV
};

ZiExtern Username username(ZeError *e = 0); /* effective username */
ZiExtern Hostname hostname(ZeError *e = 0);

} // namespace Zi

#ifndef _WIN32
using ZiVec = struct iovec;
using ZiVecPtr = void *;
using ZiVecLen = size_t;
#define ZiVec_ptr(x) (x).iov_base
#define ZiVec_len(x) (x).iov_len
#else
using ZiVec = WSABUF;
using ZiVecPtr = char *;
using ZiVecLen = u_long;
#define ZiVec_ptr(x) (x).buf
#define ZiVec_len(x) (x).len
#endif
inline void ZiVec_init(ZiVec &vec, void *ptr, size_t len) {
  ZiVec_ptr(vec) = static_cast<ZiVecPtr>(ptr);
  ZiVec_len(vec) = len;
}

#ifndef _WIN32
#include <dlfcn.h>	// dlerror()
#include <zlib/ZePlatform.hh>
#endif

#endif /* ZiPlatform_HH */
