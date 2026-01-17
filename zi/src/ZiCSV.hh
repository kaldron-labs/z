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

inline ZeException overflow() {
  return ZeEXCEPT(Error, "ZiCSV", "maximum row length exceeded");
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
  void writeHeader() {
    if (file.open(path, ZiFile::Append | ZiFile::GC) != Zi::OK) {
      error = ioError(path, file);
      return;
    }
    auto buf_ = ZmAlloc(char, MaxRowLen);
    ZuStream buf{&buf_[0], MaxRowLen};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return; }
    write(&buf_[0], &buf[0] - &buf_[0]);
  }

public:
  PushFile(Path path_) : path{ZuMv(path_)} { writeHeader(); }
  PushFile(Columns columns, Path path_) : Base{columns}, path{ZuMv(path_)} {
    writeHeader();
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
inline auto writeFile(Path path) {
  return PushFile<O, Facet, MaxRowLen>{ZuMv(path)};
}

template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096>
inline auto writeFile(Columns columns, Path path) {
  return PushFile<O, Facet, MaxRowLen>{columns, ZuMv(path)};
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

  template <typename L>
  bool write(const Path &path, char *buf_, L l) {
    ZiFile file;
    if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
      error = ioError(path, file);
      return false;
    }
    {
      ZuStream buf{buf_, MaxRowLen};
      this->saveHdr(buf);
      if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
      if (!write_(path, file, buf_, &buf[0] - buf_)) return false;
    }
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
  PullFile(const Path &path, char *buf_, L l) { write(path, buf_, ZuMv(l)); }
  template <typename L>
  PullFile(Columns columns, const Path &path, char *buf_, L l) : Base{columns} {
    write(path, buf_, ZuMv(l));
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
ZuUnion<void, ZeException> writeFile(Path path, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  PullFile<O, Facet, MaxRowLen> pull(ZuMv(path), &buf_[0], ZuMv(l));
  if (pull) return {};
  return ZuMv(pull.error);
}

template <
  typename O,
  typename Facet = ZuFacet::Core,
  unsigned MaxRowLen = 4096,
  typename L>
ZuUnion<void, ZeException> writeFile(Columns columns, Path path, L l) {
  auto buf_ = ZmAlloc(char, MaxRowLen);
  PullFile<O, Facet, MaxRowLen> pull(columns, ZuMv(path), &buf_[0], ZuMv(l));
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
