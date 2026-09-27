//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// file I/O
// - all ZiFile calls are unbuffered and direct to the operating system
// - file operations are intrinsically less frequent and high-latency
// - not a policy based template

#ifndef ZiFile_HH
#define ZiFile_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuSpan.hh>

#include <zlib/ZmScratch.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiPlatform.hh>

#ifndef _WIN32
#include <sys/mman.h>
#endif

#ifndef _WIN32
#include <alloca.h>
#endif

class ZiAPI ZiStat {
  friend class ZiFile;

public:
  using Path = Zi::Path;
  using Offset = Zi::Offset;

  ZiStat() = default;
  ZiStat(const ZiStat &) = default;
  ZiStat &operator =(const ZiStat &) = default;
  ZiStat(ZiStat &&) = default;
  ZiStat &operator =(ZiStat &&) = default;

  ZiStat(Path path) : m_path{ZuMv(path)} { }

  Offset size() const;
  ZuTime mtime() const;
  bool exists() const;
  bool isdir() const;
  bool islink() const;

  ZeError error() const { return m_error; }

  bool operator !() const { return m_error; }
  ZuOpBool

private:
#ifndef _WIN32
  bool init_() const;
#else
  bool attrs_() const;
  bool size_() const;
  bool mtime_() const;
#endif

  Path			m_path;
#ifndef _WIN32
  mutable struct stat	m_stat;
  mutable int		m_result = Zi::NotReady;
#else
  mutable ZuTime	m_mtime;
  mutable Offset	m_size;
  mutable DWORD		m_attrs;
  mutable int		m_mtimeResult = Zi::NotReady;
  mutable int		m_sizeResult = Zi::NotReady;
  mutable int		m_attrsResult = Zi::NotReady;
#endif
  mutable ZeError	m_error;
};

class ZiAPI ZiFile {
public:
  using Handle = Zi::Handle;
  using Name = Zi::Name;
  using Path = Zi::Path;
  using Offset = Zi::Offset;

  enum Flags {
    ReadOnly	= 0x00001,
    WriteOnly	= 0x00002,
    Create	= 0x00004,
    Exclusive	= 0x00008,
    Truncate	= 0x00010,
    Append_	= 0x00020,// append without create is little used in practice
    Direct	= 0x00040,// O_DIRECT (Unix) / FILE_FLAG_NO_BUFFERING  (Win)
    Sync	= 0x00080,// O_DSYNC  (Unix) / FILE_FLAG_WRITE_THROUGH (Win)
    GC		= 0x00100,// close() handle in destructor
    MMap	= 0x00200,// memory-mapped file (set internally by mmap())
    Shm		= 0x00400,// global named shared memory, not a real file
    ShmMirror	= 0x00800,// map two adjacent copies of the same memory
    MMPopulate	= 0x01000,// MAP_POPULATE
    Shadow	= 0x02000,// shadow already opened file
    StdIn	= 0x04000,// standard input
    StdOut	= 0x08000,// standard output
    StdErr	= 0x10000,// standard error
    NoFollow	= 0x20000,// do not follow final symlink/reparse point
    Directory	= 0x40000,// open directory
    Unpublished	= 0x80000,// deny concurrent opens until mode() publishes

    // frequently used combinations
    Write	= Create | WriteOnly | Truncate,
    Append	= Create | WriteOnly | Append_
  };

  // Note: Direct requires caller align all reads/writes to blkSize()

  ZiFile() = default;

  ZiFile(const ZiFile &file) :
    m_handle{file.m_handle},
    m_flags{file.m_flags | Shadow},
    m_blkSize{file.m_blkSize},
    m_error{file.m_error} { }
  ZiFile &operator =(const ZiFile &file) {
    if (this != &file) {
      this->~ZiFile();
      new (this) ZiFile{file};
    }
    return *this;
  }
  ZiFile(ZiFile &&file) :
    m_handle{file.m_handle},
    m_flags{file.m_flags},
    m_blkSize{file.m_blkSize},
    m_error{file.m_error}
  {
    file.m_handle = Zi::nullHandle();
  }
  ZiFile &operator =(ZiFile &&file) {
    this->~ZiFile();
    new (this) ZiFile{ZuMv(file)};
    return *this;
  }

  ~ZiFile() { final(); }

  ZuInline Handle handle() const { return m_handle; }

  ZuInline unsigned flags() const { return m_flags; }
  void setFlags(int f) { m_flags |= f; }
  void clrFlags(int f) { m_flags &= ~f; }

  ZuInline ZeError error() const { return m_error; }

  int init(Handle handle, unsigned flags);

  static ZiFile stdIn();
  static ZiFile stdOut();
  static ZiFile stdErr();

protected:
  void final_() {
    m_handle = Zi::nullHandle();
  }
private:
  void final() {
    if (m_flags &GC)
      close();
    else
      final_();
  }

public:
  bool operator !() const { return Zi::nullHandle(m_handle); }
  ZuOpBool

  ZiFile(
    const Path &name, unsigned flags, unsigned mode = 0666, Offset length = 0)
  {
    open(name, flags, mode, length);
  }

  int open(
    const Path &name, unsigned flags, unsigned mode = 0666, Offset length = 0);
  int openAt(
    const ZiFile &dir, const Path &name,
    unsigned flags, unsigned mode = 0666, Offset length = 0);
  int dup(const ZiFile &file, unsigned flags = GC);
  void openStdIn();
  void openStdOut();
  void openStdErr();

  void close();

  Offset size();
  ZiStat fstat() const;
  int blkSize() { return m_blkSize; }

  Offset offset();
  int seek(Offset offset);

  // change permissions and publish/unpublish an Unpublished file
  int mode(unsigned mode);
  int sync();

  // all=false returns after the first successful underlying read
  int read(void *ptr, unsigned len, bool all = true);
  int readv(const ZiVec *vecs, unsigned nVecs);

  int write(const void *ptr, unsigned len);
  int writev(const ZiVec *vecs, unsigned nVecs);

  int pread(Offset offset, void *ptr, unsigned len);
  int preadv(Offset offset, const ZiVec *vecs, unsigned nVecs);

  int pwrite(Offset offset, const void *ptr, unsigned len);
  int pwritev(Offset offset, const ZiVec *vecs, unsigned nVecs);

  int truncate(Offset offset);

  // Note: unbuffered!
  template <typename V> ZiFile &operator <<(V &&v) {
    append_(ZuFwd<V>(v));
    return *this;
  }

  static int remove(const Path &name, ZeError *e = nullptr);
  static int rename(
      const Path &oldName, const Path &newName, ZeError *e = nullptr);
  static int copy(
      const Path &oldName, const Path &newName, ZeError *e = nullptr);
  static int mkdir(const Path &name, ZeError *e = nullptr) {
    return mkdir(name, 0777, e);
  }
  static int mkdir(
      const Path &name, unsigned mode, ZeError *e = nullptr);
  static int rmdir(const Path &name, ZeError *e = nullptr);
  static int removeTree(const Path &name, ZeError *e = nullptr);

  static Path cwd();
  static Path tmpDir();
  static Path canonical(const Path &name);

  static bool absolute(const Path &name);

  static Path leafname(const Path &name);
  static Path dirname(const Path &name);
  static Path append(const Path &dir, const Path &name);

  static void age(const Path &name, unsigned max);
  static void ageTree(const Path &name, unsigned max);

protected:
  int open_(const Path &name, unsigned flags, unsigned mode, Offset length);

private:
  void init_(Handle handle, unsigned flags, int blkSize);

  template <typename U, typename R = void>
  using MatchPDelegate =
    ZuIfT<ZuPrint<U>::Delegate && !ZuTraits<U>::IsString, R>;
  template <typename U, typename R = void>
  using MatchPBuffer =
    ZuIfT<ZuPrint<U>::Buffer && !ZuTraits<U>::IsString, R>;

  template <typename S,
    typename = ZuIfT<(ZuTraits<S>::IsString) || (ZuPrint<S>::Delegate &&
      !ZuTraits<S>::IsString)>> void append_(S &&s_) {
    if constexpr (ZuTraits<S>::IsString) {
      ZuCSpan s(s_);
      if (ZuUnlikely(!s)) return;
      if (ZuUnlikely(write(s.data(), s.length()) != Zi::OK))
	throw m_error;
    } else {
      ZuPrint<S>::print(*this, ZuFwd<S>(s_));
    }
  }
  template <typename P, typename = MatchPBuffer<P>> void append_(const P &p) {
    unsigned len = ZuPrint<P>::length(p);
    auto buf = ZmScratch(char, len);
    if (!buf) throw ZeError{ZiENOMEM};
    buf.length(ZuPrint<P>::print(buf.data(), len, p));
    if (ZuUnlikely(write(buf.data(), buf.length()) != Zi::OK))
      throw m_error;
  }

protected:
  Handle	m_handle = Zi::nullHandle();
  unsigned	m_flags = 0;
  int		m_blkSize = 0;
  ZeError	m_error;
};

class ZiAPI ZiMMapFile : public ZiFile {
public:
  using Path = Zi::Path;
  using Offset = Zi::Offset;

  ZiMMapFile() = default;

  ZiMMapFile(const ZiMMapFile &file) :
    ZiFile{file},
    m_addr{file.m_addr},
    m_mmapLength{file.m_mmapLength}
#ifdef _WIN32
    , m_mmapHandle{file.m_mmapHandle}
#endif
    { }
  ZiMMapFile &operator =(const ZiMMapFile &file) {
    if (this != &file) {
      this->~ZiMMapFile();
      new (this) ZiMMapFile{file};
    }
    return *this;
  }
  ZiMMapFile(ZiMMapFile &&file) :
    ZiFile{static_cast<ZiFile &&>(file)},
    m_addr{file.m_addr},
    m_mmapLength{file.m_mmapLength}
#ifdef _WIN32
    , m_mmapHandle{ZuMv(file.m_mmapHandle)}
#endif
  {
#ifdef _WIN32
    file.m_mmapHandle = Zi::nullHandle();
#endif
  }
  ZiMMapFile &operator =(ZiMMapFile &&file) {
    this->~ZiMMapFile();
    new (this) ZiMMapFile{ZuMv(file)};
    return *this;
  }

  ZuInline void *addr() const { return m_addr; }
  ZuInline Offset mmapLength() const { return m_mmapLength; }
  ZuInline ZuSpan<uint8_t> span() {
    return {
      static_cast<uint8_t *>(m_addr),
      uint64_t(m_mmapLength)};
  }
  ZuInline ZuBSpan cspan() const {
    return {
      static_cast<const uint8_t *>(m_addr),
      uint64_t(m_mmapLength)};
  }

private:
  void final() {
    if (m_flags &GC)
      close();
    else {
      ZiFile::final_();
#ifdef _WIN32
      m_mmapHandle = Zi::nullHandle();
#endif
    }
  }

public:
  using ZiFile::operator !;
  ZuOpBool

  ZiMMapFile(
    const Path &name, unsigned flags, Offset length,
    bool shared = true, int mmapFlags = 0, unsigned mode = 0666)
  {
    mmap(name, flags, length, shared, mmapFlags, mode);
  }

  int mmap(
    const Path &name, unsigned flags, Offset length,
    bool shared = true, int mmapFlags = 0, unsigned mode = 0666);

  int msync(void *addr = nullptr, Offset length = 0);

  void close();

private:
  void	 	*m_addr = nullptr;
  Offset	m_mmapLength = 0;
#ifdef _WIN32
  Handle	m_mmapHandle = Zi::nullHandle();
#endif
};

#endif /* ZiFile_HH */
