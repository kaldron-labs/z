//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// CSV file I/O
// - layered on ZtCSV

#ifndef ZiCSV_HH
#define ZiCSV_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZtCSV.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>

namespace ZiCSV {

using namespace ZtCSV;

using Path = Zi::Path;

enum WriteMode {
  Replace,
  Create,
  Append
};

inline ZeException overflow() {
  return ZeEXCEPT(Error, "ZiCSV", "maximum row length exceeded");
}

inline ZeException existsError(const Path &path) {
  return ZeEXCEPT(Error, "ZiCSV", ([path](auto &s) {
    s << '"' << path << "\" " << "file already contains data";
  }));
}

inline ZeException headerError(const Path &path) {
  return ZeEXCEPT(Error, "ZiCSV", ([path](auto &s) {
    s << '"' << path << "\" " << "CSV header mismatch";
  }));
}

inline ZeException ioError(const Path &path, const ZiFile &file) {
  return ZeEXCEPT(Error, "ZiCSV", ([path, e = file.error()](auto &s) {
    s << '"' << path << "\" " << e;
  }));
}

template <
  typename O,
  typename Facet,
  unsigned MaxRowLen>
struct PushFile : Writer<O, Facet> {
  using Base = Writer<O, Facet>;

  Path		path;
  ZiFile	file;
  ZeException	error;

private:
  bool write(const char *data, unsigned n) {
    if (ZuUnlikely(file.write(data, n) < 0)) {
      error = ioError(path, file);
      return false;
    }
    return true;
  }
  bool writeHeader(char *buf_) {
    ZuStream buf{buf_, MaxRowLen};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    return write(buf_, &buf[0] - buf_);
  }
  bool matchHeader(char *buf_) {
    unsigned offset = 0;
    do {
      auto r = file.read(&buf_[offset], MaxRowLen - offset);
      if (r == Zi::IOError) { error = ioError(path, file); return false; }
      if (r <= 0) { error = headerError(path); return false; }
      offset += r;
      ZuSpan<char> header_[Base::AllFields::N];
      Header header(&header_[0], 0, Base::AllFields::N, false);
      auto n = split(ZuSpan<char>(&buf_[0], offset), header);
      if (n >= 0) {
	if (this->matchHdr(header)) return true;
	error = headerError(path);
	return false;
      }
    } while (offset < MaxRowLen);
    error = overflow();
    return false;
  }
  void open(WriteMode mode) {
    auto buf_ = ZmAlloc(char, MaxRowLen);
    switch (mode) {
      case Replace:
	if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return;
	}
	writeHeader(&buf_[0]);
	return;
      case Create:
	if (file.open(
	    path, ZiFile::Create | ZiFile::WriteOnly | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return;
	}
	if (file.size() > 0) { error = existsError(path); return; }
	writeHeader(&buf_[0]);
	return;
      case Append:
	if (file.open(path, ZiFile::Create | ZiFile::Append_ | ZiFile::GC) !=
	    Zi::OK) {
	  error = ioError(path, file);
	  return;
	}
	{
	  auto size = file.size();
	  if (size > 0) {
	    file.seek(0);
	    if (!matchHeader(&buf_[0])) return;
	    file.seek(size);
	  } else
	    writeHeader(&buf_[0]);
	}
	return;
    }
  }

public:
  PushFile(Path path_, WriteMode mode) : path{ZuMv(path_)} { open(mode); }
  PushFile(Columns columns, Path path_, WriteMode mode) :
      Base{columns}, path{ZuMv(path_)} {
    open(mode);
  }
  PushFile(PushFile &&) = default;
  PushFile &operator =(PushFile &&) = default;

  ~PushFile() {
    file.close();
  }

  bool operator ()(const O &o) {
    if (ZuUnlikely(error)) return false;
    auto buf_ = ZmAlloc(char, MaxRowLen);
    ZuStream buf{&buf_[0], MaxRowLen};
    this->save(buf, o);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    return write(&buf_[0], &buf[0] - &buf_[0]);
  }

  bool operator !() { return error; }
  ZuOpBool
};

template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096>
inline auto writeFile(Path path, WriteMode mode) {
  return PushFile<O, Facet, MaxRowLen>{ZuMv(path), mode};
}

template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096>
inline auto writeFile(Columns columns, Path path, WriteMode mode) {
  return PushFile<O, Facet, MaxRowLen>{columns, ZuMv(path), mode};
}

template <
  typename O,
  typename Facet,
  unsigned MaxRowLen>
struct PullFile : public Writer<O, Facet> {
  using Base = Writer<O, Facet>;

  ZeException	error;

private:
  bool write_(const Path &path, ZiFile &file, const char *data, unsigned n) {
    if (ZuUnlikely(file.write(data, n) < 0)) {
      error = ioError(path, file);
      return false;
    }
    return true;
  }
  bool writeHeader(const Path &path, ZiFile &file, char *buf_) {
    ZuStream buf{buf_, MaxRowLen};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    return write_(path, file, buf_, &buf[0] - buf_);
  }
  bool matchHeader(const Path &path, ZiFile &file, char *buf_) {
    unsigned offset = 0;
    do {
      auto r = file.read(&buf_[offset], MaxRowLen - offset);
      if (r == Zi::IOError) { error = ioError(path, file); return false; }
      if (r <= 0) { error = headerError(path); return false; }
      offset += r;
      ZuSpan<char> header_[Base::AllFields::N];
      Header header(&header_[0], 0, Base::AllFields::N, false);
      auto n = split(ZuSpan<char>(&buf_[0], offset), header);
      if (n >= 0) {
	if (this->matchHdr(header)) return true;
	error = headerError(path);
	return false;
      }
    } while (offset < MaxRowLen);
    error = overflow();
    return false;
  }
  bool open(ZiFile &file, const Path &path, WriteMode mode, char *buf_) {
    switch (mode) {
      case Replace:
	if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	return writeHeader(path, file, buf_);
      case Create:
	if (file.open(
	    path, ZiFile::Create | ZiFile::WriteOnly | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	if (file.size() > 0) { error = existsError(path); return false; }
	return writeHeader(path, file, buf_);
      case Append:
	if (file.open(path, ZiFile::Create | ZiFile::Append_ | ZiFile::GC) !=
	    Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	{
	  auto size = file.size();
	  if (size > 0) {
	    file.seek(0);
	    if (!matchHeader(path, file, buf_)) return false;
	    file.seek(size);
	  } else if (!writeHeader(path, file, buf_))
	    return false;
	}
	return true;
    }
    ZuUnreachable();
  }

  template <typename L>
  bool write(const Path &path, WriteMode mode, char *buf_, L l) {
    ZiFile file;
    if (!open(file, path, mode, buf_)) return false;
    for (;;) {
      ZuStream buf{buf_, MaxRowLen};
      if (!l([this, &buf](const O &o) { this->save(buf, o); })) return true;
      if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
      if (!write_(path, file, buf_, &buf[0] - buf_)) return false;
    }
    ZuUnreachable(); // unreachable
  }

public:
  template <typename L>
  PullFile(const Path &path, WriteMode mode, char *buf_, L l) {
    write(path, mode, buf_, ZuMv(l));
  }
  template <typename L>
  PullFile(Columns columns, const Path &path, WriteMode mode, char *buf_, L l) :
      Base{columns} {
    write(path, mode, buf_, ZuMv(l));
  }

  bool operator !() const { return error; }
  ZuOpBool
};

// lambda l is of the form:
// [...](auto l) -> bool {
//   ...;			// next object
//   if (!EOF) l(o);		// write object
//   return EOF;		// return true if no more data
// }
template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096,
  typename L>
ZuUnion<void, ZeException> writeFile(Path path, WriteMode mode, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  PullFile<O, Facet, MaxRowLen> pull(ZuMv(path), mode, &buf_[0], ZuMv(l));
  if (pull) return {};
  return ZuMv(pull.error);
}

template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096,
  typename L>
ZuUnion<void, ZeException> writeFile(
    Columns columns, Path path, WriteMode mode, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  PullFile<O, Facet, MaxRowLen> pull(
    columns, ZuMv(path), mode, &buf_[0], ZuMv(l));
  if (pull) return {};
  return ZuMv(pull.error);
}

template <typename O_, typename Facet = ZuFacet::Core>
struct Reader : public ZtCSV::Reader<O_, Facet> {
  using O = O_;
  using Base = ZtCSV::Reader<O, Facet>;
  using Base::Base;
  template <typename ...Args>
  Reader(Args &&...args) : Base(ZuFwd<Args>(args)...) { }

  template <unsigned MaxRowLen = 4096, typename Path, typename L>
  ZuUnion<void, ZeException> readFile(const Path &path, L l) {
    ZiFile file;
    if (file.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) goto error;
    {
      auto buf = ZmAlloc(char, MaxRowLen);
      unsigned offset = 0;
      do {
	auto r = file.read(&buf[offset], MaxRowLen - offset);
	if (r == Zi::IOError) goto error;
	if (r <= 0) return {};
	offset += r;
	auto n = this->process(ZuSpan<char>(&buf[0], offset), l);
	if (n > 0 && n <= offset) {
	  if (offset -= n) memmove(&buf[0], &buf[n], offset);
	}
      } while (offset < MaxRowLen);

      return ZeEXCEPT(Error, "ZiCSV", ([path, e = file.error()](auto &s) {
	s << '"' << path << "\" " << "maximum row length exceeded";
      }));
    }

  error:
    return ZeEXCEPT(Error, "ZiCSV", ([path, e = file.error()](auto &s) {
      s << '"' << path << "\" " << e;
    }));
  }
};

template <typename O, typename Facet = ZuFacet::Core>
inline auto reader() {
  return Reader<O, Facet>{};
}

} // ZiCSV

#endif /* ZiCSV_HH */
