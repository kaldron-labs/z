//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// file I/O

#include <zlib/ZuDerive.hh>

#include <zlib/ZiFile.hh>

#include <zlib/ZtArray.hh>

#include <zlib/ZtRegex.hh>

#define ZiFile_CopyBufSize	(128<<10)	// 128k

#ifndef _WIN32
#include <sys/uio.h>
#endif

#ifdef _WIN32

#include <stdlib.h>

#include <zlib/ZmRBTree.hh>
#include <zlib/ZmSingleton.hh>
#include <zlib/ZmNoLock.hh>

static bool islower__(wchar_t c) { return c >= 'a' && c <= 'z'; }

extern "C" {
  typedef struct {
    WORD Length;
    WORD MaximumLength;
    wchar_t *Buffer;
  } UNICODE_STRING;

  typedef LONG (WINAPI *PNtQueryObject)(HANDLE, int, void *, ULONG, PULONG);
}

class ZiFile_WindowsDrives {
public:
  static int blkSize(ZtWString<> path) {
    return instance()->blkSize_path(ZuMv(path));
  }
  static int blkSize(HANDLE handle) {
    return instance()->blkSize_handle(handle);
  }

#if 0
  static void dump() {
    return instance()->dump_();
  }
#endif

  ZiFile_WindowsDrives();
  ~ZiFile_WindowsDrives();

private:
  int blkSize_path(ZtWString<> path);
  int blkSize_handle(HANDLE handle);

#if 0
  void dump_();
#endif

  void refresh();

  static ZiFile_WindowsDrives *instance();

  ZuDerive(DriveLetters,
    (ZmRBTreeKV<ZtWString<>, char,
      ZmRBTreeLock<ZmNoLock>>));
  ZuDerive(DriveBlkSizes,
    (ZmRBTreeKV<char, unsigned,
      ZmRBTreeUnique<true,
	ZmRBTreeLock<ZmNoLock>>>));

  HMODULE		m_ntdll;
  PNtQueryObject	m_ntQueryObject;
  ZmLock		m_lock;
  ZuTime		m_lastRefresh;
  DriveLetters		m_driveLetters;
  DriveBlkSizes		m_driveBlkSizes;
};

ZiFile_WindowsDrives *ZiFile_WindowsDrives::instance()
{
  return
    ZmSingleton<ZiFile_WindowsDrives,
      ZmSingletonCleanup<ZmCleanup::Platform>>>::instance();
}

ZiFile_WindowsDrives::ZiFile_WindowsDrives()
{
  if (m_ntdll = LoadLibrary(L"ntdll.dll"))
    m_ntQueryObject = (PNtQueryObject)GetProcAddress(m_ntdll, "NtQueryObject");
  else
    m_ntQueryObject = 0;

  refresh();
}

ZiFile_WindowsDrives::~ZiFile_WindowsDrives()
{
  if (m_ntdll) FreeLibrary(m_ntdll);
}

void ZiFile_WindowsDrives::refresh()
{
  ZuTime now = Zm::now();

  if ((now - m_lastRefresh).sec() < 1) return;

  m_driveLetters.clean();
  m_driveBlkSizes.clean();

  DWORD len = 0;
  ZtArray<wchar_t> buf;
  do {
    buf.length(len, false);
  } while ((len = GetLogicalDriveStrings(len, buf.data())) >
	     static_cast<DWORD>(buf.length()));
  unsigned i = 0;
  ZtWString<> drive;
  drive += L" :\\";
  ZtWString<> pathBuf(Zi::PathMax + 1);
  ZtWString<> path;
  char dl;
  do {
    dl = buf[i];
    if (islower__(dl)) dl += 'A' - 'a';
    drive[0] = dl;
    drive[2] = 0;
    if (QueryDosDevice(drive, pathBuf, Zi::PathMax + 1) > 0) {
      pathBuf.calcLength();
      if (!pathBuf.cmp(L"\\\\?\\", 4))
	path.init(&pathBuf[4], pathBuf.length() - 4);
      else
	path = pathBuf;
      m_driveLetters.add(path, dl);
      if (!path.icmp(L"\\Device\\Harddisk", 16)) {
	drive[2] = '\\';
	{
	  DWORD sectorsPerCluster, bytesPerSector, d2, d3;

	  GetDiskFreeSpace(
	      drive, &sectorsPerCluster, &bytesPerSector, &d2, &d3);
	  m_driveBlkSizes.add(dl, static_cast<unsigned>(sectorsPerCluster * bytesPerSector));
	}
      } else
	m_driveBlkSizes.add(dl, 512U);
    }
    while (buf[i++] && i < len);
  } while (buf[i] && i < len);

  m_lastRefresh = now;
}

#if 0
#include <stdio.h>

void ZiFile_WindowsDrives::dump_()
{
  {
    DriveLetters::CIter i(m_driveLetters);
    DriveLetters::NodeRef dln;

    while (dln = i()) {
      printf("%p %S -> %c\n",
	     (void *)dln->key().data(), dln->key().data(), (int)dln->val());
    }
  }
  {
    DriveBlkSizes::CIter i(m_driveBlkSizes);
    DriveBlkSizes::NodeRef dbn;

    while (dbn = i()) {
      printf("%c -> %d\n", (int)dbn->key(), (int)dbn->val());
    }
  }
}
#endif

int ZiFile_WindowsDrives::blkSize_path(ZtWString<> path)
{
  if (!path.cmp(L"\\\\?\\", 4)) path.splice(4);

  int dl = 0;
  if (path[1] == ':')
    dl = path[0];
  else {
    if (path[0] == '\\') {
      if (path.icmp(L"\\Device\\Harddisk", 16)) return 0;

      ZmGuard<ZmLock> guard(m_lock);
      bool retried = false;

retry:
      auto i = m_driveLetters.citer<ZmRBTreeLessEqual>(path);
      DriveLetters::NodeRef dln = i();
      if (dln) {
	const ZtWString<> &drive = dln->key();
	if (!drive.cmp(path, drive.length())) dl = dln->val();
      }
      if (!dl) {
	if (!retried) { retried = true; refresh(); goto retry; }
	return 0;
      }
    } else {
      ZtWString<> dir_(Zi::PathMax + 1);
      dir_.length(GetCurrentDirectory(Zi::PathMax + 1, dir_));
      if (dir_.length()) {
	ZtWString<> dir(Zi::PathMax + 1);
	dir.length(GetFullPathName(dir_, Zi::PathMax + 1, dir, 0));
	if (dir.length()) dl = dir[0];
      }
      if (!dl) return 0;
    }
  }

  if (islower__(dl)) dl += 'A' - 'a';

  {
    ZmGuard<ZmLock> guard(m_lock);
    int n = m_driveBlkSizes.findVal(dl);
    return ZuCmp<int>::null(n) ? 512 : n;
  }
}

int ZiFile_WindowsDrives::blkSize_handle(HANDLE handle)
{
  if (!m_ntQueryObject) return 0;
  LONG l;
  ULONG len = BUFSIZ;
  ZtArray<char> buf;
  do {
    buf.length(len, false);
    l = m_ntQueryObject(handle, 1, buf.data(), buf.length(), &len);
    if (l && len <= (ULONG)buf.length()) return 0;
  } while (len > (ULONG)buf.length());
  return blkSize_path(((UNICODE_STRING *)buf.data())->Buffer);
}

#endif /* _WIN32 */

int ZiFile::open(
    const Path &name, unsigned flags, unsigned mode, Offset length)
{
  return open_(name, flags, mode, length);
}

int ZiFile::open_(
    const Path &name, unsigned flags, unsigned mode, Offset length)
{
  if (!Zi::nullHandle(m_handle)) {
#ifndef _WIN32
    m_error = EINVAL;
#else
    m_error = ERROR_INVALID_PARAMETER;
#endif
    return Zi::IOError;
  }

  Handle h;
  unsigned blkSize;

#ifndef _WIN32
  int openFlags;
  openFlags = (flags & ReadOnly) ? O_RDONLY :
	      (flags & WriteOnly) ? O_WRONLY : O_RDWR;
  if (flags & Create)	 openFlags |= O_CREAT;	// create if not existing
  if (flags & Exclusive) openFlags |= O_EXCL;	// do not open existing file
  if (flags & Direct)	 openFlags |= O_DIRECT;	// direct I/O - bypass OS cache
  if (flags & Sync)	 openFlags |= O_DSYNC;	// synchronize all writes
  if (flags & Shm) {
    if (length <= 0) goto einval;
    Zi::Path name_(name.length() + 2);
    name_ << '/' << name;
    h = shm_open(name_, openFlags, mode);
    if (h < 0) goto error;
    blkSize = ::sysconf(_SC_PAGESIZE);
    length = ((length + blkSize - 1) / blkSize) * blkSize;
  } else {
    h = ::open(name, openFlags, mode);
    if (h < 0) goto error;
    {
      struct stat s;

      if (fstat(h, &s) < 0) { ::close(h); goto error; }
      blkSize = s.st_blksize;
    }
  }
  if (length >= 0 && (size() < length || (flags & Truncate))) {
    if (ftruncate(h, length) < 0) { ::close(h); goto error; }
  }
#else
  if (flags & Shm) {
    if (length <= 0) goto einval;
    Path name_(name.length() + 8);
    name_ << "Local\\" << name; // was Global
    DWORD protectFlags = (flags & ReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
    if (flags & ShmMirror)
      blkSize = 64<<10; // Windows - 64k, not the system page size
    else
      { SYSTEM_INFO si; GetSystemInfo(&si); blkSize = si.dwPageSize; }
    length = ((length + blkSize - 1) / blkSize) * blkSize;
    h = CreateFileMapping(
	INVALID_HANDLE_VALUE, 0, protectFlags, 0,
	static_cast<DWORD>(length), name_);
  } else {
    blkSize = ZiFile_WindowsDrives::blkSize(name);
    DWORD accessFlags = (flags & ReadOnly) ? GENERIC_READ :
      (flags & WriteOnly) ? GENERIC_WRITE : GENERIC_READ | GENERIC_WRITE;
    DWORD shareFlags = (flags & ReadOnly) ? FILE_SHARE_READ :
      FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD createFlags = !(flags & Create) ? OPEN_EXISTING :
      (flags & Exclusive) ? CREATE_NEW : OPEN_ALWAYS;
    DWORD fileFlags = FILE_FLAG_OVERLAPPED;
    if (flags & Direct) fileFlags |= FILE_FLAG_NO_BUFFERING;
    if (flags & Sync) fileFlags |= FILE_FLAG_WRITE_THROUGH;
    h = CreateFile(
	name, accessFlags, shareFlags, nullptr, createFlags, fileFlags, NULL);
    if (h == INVALID_HANDLE_VALUE) goto error;
    if ((length > 0 && size() < length) || (flags & Truncate)) {
      LONG high = length>>32;
      if ((SetFilePointer(h, length & 0xffffffffU, &high, FILE_BEGIN) ==
	    INVALID_SET_FILE_POINTER &&
	  GetLastError() != NO_ERROR) || !SetEndOfFile(h)) {
	CloseHandle(h);
	goto error;
      }
    }
  }
#endif

  init_(h, flags | GC, blkSize);
  return Zi::OK;

error:
  m_error = ZeLastError;
  return Zi::IOError;

einval:
#ifndef _WIN32
  m_error = EINVAL;
#else
  m_error = ERROR_INVALID_PARAMETER;
#endif
  return Zi::IOError;
}

int ZiMMapFile::mmap(
    const Path &name, unsigned flags, Offset length, bool shared,
    int mmapFlags, unsigned mode)
{
  if (length <= 0) {
#ifndef _WIN32
    m_error = EINVAL;
#else
    m_error = ERROR_INVALID_PARAMETER;
#endif
    return Zi::IOError;
  }

  int r;

  if ((r = open_(name, flags | MMap, mode, length)) != Zi::OK) return r;

  m_mmapLength = length;

#ifndef _WIN32
  int prot = ((flags & ReadOnly) ? PROT_READ :
	      (flags & WriteOnly) ? PROT_WRITE : PROT_READ | PROT_WRITE);
  mmapFlags |= shared ? MAP_SHARED : MAP_PRIVATE;
#ifdef MAP_POPULATE
  if (flags & MMPopulate) mmapFlags |= MAP_POPULATE;
#endif
  if (flags & ShmMirror) {
    m_addr = ::mmap(
	0, m_mmapLength<<1, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (!m_addr) goto error;
    if (m_addr == MAP_FAILED) { m_addr = nullptr; goto error; }
    void *addr = ::mmap(
	m_addr, m_mmapLength, prot, mmapFlags | MAP_FIXED, m_handle, 0);
    if (addr != m_addr) {
      ::munmap(m_addr, m_mmapLength<<1);
      m_addr = nullptr;
      goto error;
    }
    addr = ::mmap(
	static_cast<uint8_t *>(m_addr) + m_mmapLength, m_mmapLength,
	prot, mmapFlags | MAP_FIXED, m_handle, 0);
    if (addr != static_cast<void *>(
	  static_cast<uint8_t *>(m_addr) + m_mmapLength)) {
      ::munmap(m_addr, m_mmapLength<<1);
      m_addr = nullptr;
      goto error;
    }
  } else {
    m_addr = ::mmap(0, m_mmapLength, prot, mmapFlags, m_handle, 0);
    if (!m_addr) goto error;
    if (m_addr == MAP_FAILED) { m_addr = nullptr; goto error; }
  }
  *(static_cast<uint8_t *>(m_addr) + (m_mmapLength - 1)) = 0;
#else
  if (flags & Shm)
    m_mmapHandle = m_handle;
  else {
    DWORD protectFlags = (flags & ReadOnly) ? PAGE_READONLY : PAGE_READWRITE;
    m_mmapHandle = CreateFileMapping(m_handle, 0, protectFlags, 0, 0, nullptr);
    if (Zi::nullHandle(m_mmapHandle)) {
      m_mmapHandle = Zi::nullHandle();
      goto error;
    }
  }
  {
    DWORD accessFlags = (flags & ReadOnly) ? FILE_MAP_READ : FILE_MAP_WRITE;
    if (flags & ShmMirror) {
retry:
      m_addr = VirtualAlloc(
	  0, static_cast<DWORD>(m_mmapLength<<1), MEM_RESERVE, PAGE_NOACCESS);
      if (!m_addr) {
	CloseHandle(m_mmapHandle);
	m_mmapHandle = Zi::nullHandle();
	goto error;
      }
      if (!VirtualFree(m_addr, 0, MEM_RELEASE)) {
	CloseHandle(m_mmapHandle);
	m_mmapHandle = Zi::nullHandle();
	m_addr = nullptr;
	goto error;
      }
      void *addr = MapViewOfFileEx(
	  m_mmapHandle, static_cast<DWORD>(accessFlags), 0, 0,
	  static_cast<DWORD>(m_mmapLength), m_addr);
      if (!addr) goto retry;
      if (addr != m_addr) { UnmapViewOfFile(addr); goto retry; }
      addr = MapViewOfFileEx(
	  m_mmapHandle, static_cast<DWORD>(accessFlags), 0, 0,
	  static_cast<DWORD>(m_mmapLength),
	  static_cast<uint8_t *>(m_addr) + m_mmapLength);
      if (!addr) goto retry;
      if (addr != static_cast<void *>(
	    static_cast<uint8_t *>(m_addr) + m_mmapLength)) {
	UnmapViewOfFile(m_addr);
	UnmapViewOfFile(addr);
	goto retry;
      }
    } else {
      m_addr = MapViewOfFile(m_mmapHandle, accessFlags, 0, 0, 0);
      if (!m_addr) {
	CloseHandle(m_mmapHandle);
	m_mmapHandle = Zi::nullHandle();
	goto error;
      }
    }
  }
#endif

  return Zi::OK;

error:
  close();
  m_error = ZeLastError;
  return Zi::IOError;
}

void ZiFile::close()
{
  if (m_handle == Zi::nullHandle()) return;

  if (m_flags & Shadow) goto closed;

#ifndef _WIN32
  ::close(m_handle);
#else
  CloseHandle(m_handle);
#endif

closed:
  m_handle = Zi::nullHandle();
  m_flags = 0;
  m_offset = 0;
}

void ZiMMapFile::close()
{
  if (m_addr) {
#ifndef _WIN32
    ::munmap(m_addr, m_mmapLength);
    if (m_flags & ShmMirror)
      ::munmap(static_cast<uint8_t *>(m_addr) + m_mmapLength, m_mmapLength);
#else
    if (!Zi::nullHandle(m_mmapHandle) && m_mmapHandle != m_handle)
      CloseHandle(m_mmapHandle);
    m_mmapHandle = Zi::nullHandle();
    UnmapViewOfFile(m_addr);
    if (m_flags & ShmMirror)
      UnmapViewOfFile(static_cast<uint8_t *>(m_addr) + m_mmapLength);
#endif

    m_addr = nullptr;
    m_mmapLength = 0;
#ifdef _WIN32
    m_mmapHandle = Zi::nullHandle();
#endif
  }

  ZiFile::close();
}

int ZiFile::init(Handle handle, unsigned flags)
{
  int blkSize;

#ifndef _WIN32
  struct stat s;

  if (fstat(handle, &s) < 0) goto error;
  blkSize = s.st_blksize;
#else
  blkSize = ZiFile_WindowsDrives::blkSize(handle);
#endif

  if (m_flags & GC) close();
  init_(handle, flags, blkSize);
  return Zi::OK;

#ifndef _WIN32
error:
  m_error = ZeLastError;
  return Zi::IOError;
#endif
}

void ZiFile::init_(Handle handle, unsigned flags, int blkSize)
{
  m_handle = handle;
  m_flags = flags;
  m_blkSize = blkSize;
  m_offset = (flags & Append) ? size() : 0;
}

ZiFile ZiFile::stdIn() { ZiFile file; file.openStdIn(); return file; }
ZiFile ZiFile::stdOut() { ZiFile file; file.openStdOut(); return file; }
ZiFile ZiFile::stdErr() { ZiFile file; file.openStdErr(); return file; }

void ZiFile::openStdIn()
{
#ifndef _WIN32
  init_(0, ReadOnly | StdIn, 0);
#else
  init_(GetStdHandle(STD_INPUT_HANDLE), ReadOnly | StdIn, 0);
#endif
}

void ZiFile::openStdOut()
{
#ifndef _WIN32
  init_(1, WriteOnly | StdOut, 0);
#else
  init_(GetStdHandle(STD_OUTPUT_HANDLE), WriteOnly | StdOut, 0);
#endif
}

void ZiFile::openStdErr()
{
#ifndef _WIN32
  init_(2, WriteOnly | StdErr, 0);
#else
  init_(GetStdHandle(STD_ERROR_HANDLE), WriteOnly | StdErr, 0);
#endif
}

ZiFile::Offset ZiFile::size()
{
#ifndef _WIN32
  off_t o;
  if ((o = lseek(m_handle, 0, SEEK_END)) == static_cast<off_t>(-1))
    return 0;
  return o;
#else
  DWORD l, h;

  l = GetFileSize(m_handle, &h);
  return (Offset(h)<<32) | l;
#endif
}

// ZiFile maintains its own offset, no need to keep the OS offset synchronized
#if 0
ZiFile::Offset ZiFile::offset()
{
#ifndef _WIN32
  return lseek(m_handle, 0, SEEK_CUR);
#else
  return SetFilePointer(m_handle, 0, 0, FILE_CURRENT);
#endif
}

int ZiFile::seek(Offset offset)
{
#ifndef _WIN32
  if (lseek(m_handle, offset, SEEK_BEG) == (off_t)-1) goto error;
#else
  if (SetFilePointer(m_handle, offset, 0, FILE_BEGIN) ==
      INVALID_SET_FILE_POINTER)
    goto error;
#endif

  m_offset = offset;

  return Zi::OK;

error:
  m_error = ZeLastError;
  return Zi::IOError;
}
#endif

int ZiFile::read(void *ptr, unsigned len)
{
  int r = pread(m_offset, ptr, len);
  if (r > 0) m_offset += r;
  return r;
}

int ZiFile::readv(const ZiVec *vecs, unsigned nVecs)
{
  int r = preadv(m_offset, vecs, nVecs);
  if (r > 0) m_offset += r;
  return r;
}

int ZiFile::preadv(Offset offset, const ZiVec *vecs, unsigned nVecs)
{
#ifndef _WIN32
  unsigned len = 0;
  unsigned i;

  for (i = 0; i < nVecs; i++) len += ZiVec_len(vecs[i]);

  Ze::ErrNo errNo;
  int r;

retry:
  r = ::preadv(m_handle, vecs, nVecs, offset);
  if (r < 0) {
    errNo = errno;
    switch (errNo) {
      case EINTR:
      case EAGAIN:
	goto retry;
      default:
	goto error;
    }
  }

  if (!r) return Zi::EndOfFile;

  if (r < static_cast<int>(len)) {
    // adjust r downwards to the nearest Vec boundary
    {
      int r_ = r;
      int n;
      r = 0;
      for (i = 0; i < nVecs; i++) {
	n = ZiVec_len(vecs[i]);
	if (r_ < n) break;
	r_ -= n;
	r += n;
      }
    }
    vecs += i;
    nVecs -= i;
    offset += r;
    len -= r;
    goto retry;
  }

  return Zi::OK;

error:
  m_error = ZeError(errNo);
  return Zi::IOError;
#else
  // Windows
  // - ReadFileScatter() cannot be used since it requires
  //   page-sized and page-aligned buffers

  int total = 0, r = 0;

  for (unsigned i = 0; i < nVecs; i++) {
    void *ptr = ZiVec_ptr(vecs[i]);
    unsigned len = ZiVec_len(vecs[i]);
    r = pread(offset, ptr, len);
    if (r < 0) return total ? total : r;
    total += r, offset += r;
    if (r < static_cast<int>(len)) break;
  }
  return total;
#endif
}

int ZiFile::pread(Offset offset, void *ptr, unsigned len)
{
  if (!len) return 0;

  Ze::ErrNo errNo;
  unsigned total = 0;
#ifndef _WIN32
  int r;
#else
  DWORD r;
#endif

retry:

#ifndef _WIN32
  r = ::pread(m_handle, ptr, len, offset);
  if (r < 0) {
    errNo = errno;
    switch (errNo) {
      case EINTR:
      case EAGAIN:
	goto retry;
      default:
	goto error;
    }
  }
#else
  OVERLAPPED o{0};
  o.Offset = static_cast<DWORD>(offset);
  o.OffsetHigh = static_cast<DWORD>(offset>>32);
  if (!ReadFile(m_handle, ptr, len, &r, &o)) {
    errNo = GetLastError();
    if (errNo == ERROR_HANDLE_EOF) return total ? total : Zi::EndOfFile;
    if (errNo != ERROR_IO_PENDING) goto error;
    if (!GetOverlappedResult(m_handle, &o, &r, TRUE)) {
      errNo = GetLastError();
      if (errNo == ERROR_HANDLE_EOF) return total ? total : Zi::EndOfFile;
      goto error;
    }
  }
#endif

  if (!r) return total ? total : Zi::EndOfFile;

  total += r, offset += r;

  if ((unsigned)r < len) {
    ptr = static_cast<void *>(static_cast<uint8_t *>(ptr) + r);
    len -= r;
    goto retry;
  }

  return total;

error:
  m_error = ZeError(errNo);
  return total ? total : Zi::IOError;
}

int ZiFile::write(const void *ptr, unsigned len)
{
  int r = pwrite(m_offset, ptr, len);
  if (r == Zi::OK) m_offset += len;
  return r;
}

int ZiFile::writev(const ZiVec *vecs, unsigned nVecs)
{
  int r = pwritev(m_offset, vecs, nVecs);
  if (r == Zi::OK)
    for (unsigned i = 0; i < nVecs; i++) m_offset += ZiVec_len(vecs[i]);
  return r;
}

int ZiFile::pwritev(Offset offset, const ZiVec *vecs, unsigned nVecs)
{
#ifndef _WIN32
  unsigned len = 0;
  unsigned i;

  for (i = 0; i < nVecs; i++) len += ZiVec_len(vecs[i]);

  Ze::ErrNo errNo;
  int r;

retry:
  r = ::pwritev(m_handle, vecs, nVecs, offset);
  if (r < 0) {
    errNo = errno;
    switch (errNo) {
      case EINTR:
      case EAGAIN:
	goto retry;
      default:
	goto error;
    }
  }

  if (r < static_cast<int>(len)) {
    // adjust r downwards to the nearest Vec boundary
    {
      int r_ = r;
      int n;
      r = 0;
      for (i = 0; i < nVecs; i++) {
	n = ZiVec_len(vecs[i]);
	if (r_ < n) break;
	r_ -= n;
	r += n;
      }
    }
    vecs += i;
    nVecs -= i;
    offset += r;
    len -= r;
    goto retry;
  }

  return Zi::OK;

error:
  m_error = ZeError(errNo);
  return Zi::IOError;
#else
  // Windows
  // - WriteFileGather() cannot be used since it requires
  //   page-sized and page-aligned buffers

  int r = 0;

  for (unsigned i = 0; i < nVecs; i++) {
    const void *ptr = ZiVec_ptr(vecs[i]);
    unsigned len = ZiVec_len(vecs[i]);
    r = pwrite(offset, ptr, len);
    if (r != Zi::OK) return r;
    offset += len;
  }
  return r;
#endif
}

int ZiFile::pwrite(Offset offset, const void *ptr, unsigned len)
{
  if (!len) return Zi::OK;

  Ze::ErrNo errNo;
#ifndef _WIN32
  int r;
#else
  DWORD r;
#endif

retry:

#ifndef _WIN32
  r = ::pwrite(m_handle, ptr, len, offset);
  if (r <= 0) {
    errNo = errno;
    switch (errNo) {
      case EINTR:
      case EAGAIN:
	goto retry;
      default:
	goto error;
    }
  }
#else
  OVERLAPPED o;

  o.Internal = 0;
  o.InternalHigh = 0;
  o.Offset = static_cast<DWORD>(offset);
  o.OffsetHigh = static_cast<DWORD>(offset>>32);
  o.hEvent = 0;

  if (!WriteFile(m_handle, ptr, len, &r, &o)) {
    errNo = GetLastError();
    if (errNo != ERROR_IO_PENDING) goto error;
    if (!GetOverlappedResult(m_handle, &o, &r, TRUE)) r = 0;
  }
  if (!r) {
    errNo = GetLastError();
    goto error;
  }
#endif

  offset += r;

  if ((unsigned)r < len) {
    ptr = static_cast<const void *>(static_cast<const uint8_t *>(ptr) + r);
    len -= r;
    goto retry;
  }

  return Zi::OK;

error:
  m_error = ZeError(errNo);
  return Zi::IOError;
}

int ZiFile::truncate(Offset offset)
{
  Ze::ErrNo errNo;

#ifndef _WIN32
retry:
  int r = ftruncate(m_handle, offset);
  if (r < 0) {
    errNo = errno;
    switch (errNo) {
      case EINTR:
      case EAGAIN:
	goto retry;
      default:
	goto error;
    }
  }
#else
  errNo = NO_ERROR;
  LONG high = offset>>32;
  if (SetFilePointer(m_handle, offset & 0xffffffffU, &high, FILE_BEGIN) ==
	INVALID_SET_FILE_POINTER)
     errNo = GetLastError();
  if (errNo != NO_ERROR) goto error;
  if (!SetEndOfFile(m_handle)) {
    errNo = GetLastError();
    goto error;
  }
#endif

  return Zi::OK;

error:
  m_error = ZeError(errNo);
  return Zi::IOError;
}

int ZiFile::sync()
{
#ifndef _WIN32
  if (fsync(m_handle) < 0) goto error;
#else
  if (!FlushFileBuffers(m_handle)) goto error;
#endif

  return Zi::OK;

error:
  m_error = ZeLastError;
  return Zi::IOError;
}

int ZiMMapFile::msync(void *addr, Offset length)
{
  if (!m_addr) {
#ifndef _WIN32
    m_error = EBADF;
#else
    m_error = ERROR_INVALID_HANDLE;
#endif
    return Zi::IOError;
  }
#ifndef _WIN32
  if (::msync(addr ? addr : m_addr, length ? length : m_mmapLength,
	      MS_SYNC | MS_INVALIDATE) < 0) goto error;
#else
  if (!FlushViewOfFile(addr ? addr : m_addr,
		       (SIZE_T)(length ? length : m_mmapLength))) goto error;
#endif

  return Zi::OK;

error:
  m_error = ZeLastError;
  return Zi::IOError;
}

ZuTime ZiFile::mtime(const Path &name, ZeError *e)
{
#ifndef _WIN32
  struct stat s;
  if (::stat(name, &s) < 0) goto error;
  return ZuTime{s.st_mtime};
#else
  Handle h;
  FILETIME mtime;
  h = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
  if (h == INVALID_HANDLE_VALUE) goto error;
  if (!GetFileTime(h, 0, 0, &mtime)) { CloseHandle(h); goto error; }
  CloseHandle(h);
  return ZuTime{mtime};
#endif

error:
  if (e) *e = ZeLastError;
  return ZuTime{};
}

bool ZiFile::exists(const Path &name, ZeError *e)
{
#ifndef _WIN32
  struct stat s;
  if (::stat(name, &s) < 0) goto error;
  return true;
#else
  DWORD a = GetFileAttributes(name);
  if (a == INVALID_FILE_ATTRIBUTES) goto error;
  return true;
#endif

error:
  if (e) *e = ZeLastError;
  return false;
}

bool ZiFile::isdir(const Path &name, ZeError *e)
{
#ifndef _WIN32
  struct stat s;
  if (::stat(name, &s) < 0) goto error;
  return S_ISDIR(s.st_mode);
#else
  DWORD a = GetFileAttributes(name);
  if (a == INVALID_FILE_ATTRIBUTES) goto error;
  return a & FILE_ATTRIBUTE_DIRECTORY;
#endif

error:
  if (e) *e = ZeLastError;
  return false;
}

int ZiFile::remove(const Path &name, ZeError *e)
{
#ifndef _WIN32
  if (::remove(name) < 0) goto error;
#else
  if (!DeleteFile(name)) goto error;
#endif

  return Zi::OK;

error:
  if (e) *e = ZeLastError;
  return Zi::IOError;
}

int ZiFile::rename(const Path &oldName, const Path &newName, ZeError *e)
{
#ifndef _WIN32
  if (::rename(oldName, newName) < 0) goto error;
#else
  if (!MoveFileEx(oldName, newName, MOVEFILE_REPLACE_EXISTING)) goto error;
#endif

  return Zi::OK;

error:
  if (e) *e = ZeLastError;
  return Zi::IOError;
}

int ZiFile::copy(const Path &oldName, const Path &newName, ZeError *e_)
{
  ZeError e;

#ifndef _WIN32
  {
    ZiFile oldHandle, newHandle;
    if (oldHandle.open(oldName, ReadOnly, 0777) != Zi::OK) goto error;
    if (newHandle.open(newName, Write, 0777) != Zi::OK) goto error;
    unsigned oldBlkSize = oldHandle.blkSize();
    unsigned newBlkSize = newHandle.blkSize();
    unsigned maxBlkSize = oldBlkSize > newBlkSize ? oldBlkSize : newBlkSize;
    unsigned bufSize = ZiFile_CopyBufSize;
    bufSize += maxBlkSize - 1;
    bufSize -= (bufSize % maxBlkSize);
    Offset size = oldHandle.size();
    if ((Offset)bufSize > size) bufSize = size;
    ZtArray<char> buf(bufSize);
    for (Offset o = 0; o < size; o += bufSize) {
      Offset n = size - o;
      if (n > (Offset)bufSize) n = bufSize;
      if (oldHandle.pread(o, buf.data(), unsigned(n)) != Zi::OK) goto error;
      if (newHandle.pwrite(o, buf.data(), unsigned(n)) != Zi::OK) goto error;
    }
  }
#else
  if (!CopyFileEx(oldName, newName, 0, 0, 0, 0)) {
    e = ZeLastError;
    goto error;
  }
#endif

  return Zi::OK;

error:
  if (e_) *e_ = e;
  return Zi::IOError;
}

int ZiFile::mkdir(const Path &name, ZeError *e)
{
#ifndef _WIN32
  if (::mkdir(name, 0777) < 0) goto error;
#else
  if (!CreateDirectory(name, 0)) goto error;
#endif
  return Zi::OK;

error:
  if (e) *e = ZeLastError;
  return Zi::IOError;
}

int ZiFile::rmdir(const Path &name, ZeError *e)
{
#ifndef _WIN32
  if (::rmdir(name) < 0) goto error;
#else
  if (!RemoveDirectory(name)) goto error;
#endif
  return Zi::OK;

error:
  if (e) *e = ZeLastError;
  return Zi::IOError;
}

void ZiFile::age(const Name &name, unsigned max)
{
  unsigned size = name.size() + ZuBoxed(max).length() + 4;

  Name prevName_(size), nextName_(size), sideName_(size);
  Name *prevName = &prevName_;
  Name *nextName = &nextName_;
  Name *sideName = &sideName_;

  *prevName << name;
  bool last = false;
  unsigned i;
  for (i = 0; i < max && !last; i++) {
    nextName->length(0);
    *nextName << name << '.' << ZuBoxed(i + 1);
    sideName->length(0);
    *sideName << *nextName << '_';
    last = (rename(*nextName, *sideName) == Zi::IOError);
    rename(*prevName, *nextName);
    Name *oldName = prevName;
    prevName = sideName;
    sideName = oldName;
  }
  if (i == max) remove(*prevName);
}

ZiFile::Path ZiFile::cwd()
{
  Path ret(Zi::PathMax + 1);

#ifndef _WIN32
  if (!getcwd(ret.data(), Zi::PathMax + 1))
    ret.null();
  else {
    ret.calcLength();
    ret.truncate();
  }
#else
  ZtWString<> dir(Zi::PathMax + 1);
  dir.length(GetCurrentDirectory(Zi::PathMax + 1, dir));
  if (!dir.length())
    ret.null();
  else {
    ret.length(GetFullPathName(dir, Zi::PathMax + 1, ret.data(), 0));
    ret.truncate();
  }
#endif
  return ret;
}

bool ZiFile::absolute(const Path &name)
{
#ifndef _WIN32
  return name[0] == '/';
#else
  return name[0] == L'\\' || name[0] == L'/' ||
	 (((name[0] >= L'a' && name[0] <= L'z') ||
	   (name[0] >= L'A' && name[0] <= L'Z')) && name[1] == L':');
#endif
}

ZiFile::Path ZiFile::leafname(const Path &name)
{
  int o, n = name.length();

  for (o = n; --o >= 0; )
#ifndef _WIN32
    if (name[o] == '/') break;
#else
    if (name[o] == L'\\' || name[o] == L'/') break;
#endif
  if (o < 0) return name;
  return Path(name.cspan().offset(o + 1));
}

ZiFile::Path ZiFile::dirname(const Path &name)
{
  int o, n = name.length();

  for (o = n; --o >= 0; )
#ifndef _WIN32
    if (name[o] == '/') break;
  if (o < 0) return ".";
  if (!o) return "/";
#else
    if (name[o] == L'\\' || name[o] == L'/') break;
  if (o < 0) return L".";
  if (!o) return L"/";
#endif
  return Path(name.cspan().trunc(o));
}

ZiFile::Path ZiFile::append(const Path &dir, const Path &name)
{
  Path ret(dir.length() + 1 + name.length() + 1);

  ret << dir;
#ifndef _WIN32
  ret << '/';
#else
  ret << L"\\"; // not single wchar_t due to type ambiguity
#endif
  ret << name;
  return ret;
}
