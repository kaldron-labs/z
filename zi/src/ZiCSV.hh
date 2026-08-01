//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// CSV file I/O
// - layered on ZfCSV

#ifndef ZiCSV_HH
#define ZiCSV_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZfCSV.hh>

#include <zlib/ZePlatform.hh>

#include <zlib/ZiFile.hh>

namespace ZiCSV {

using namespace ZfCSV;

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
  bool writeHeader() {
    auto data = ZmScratch(char, MaxRowLen);
    ZuStream buf{data.span()};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    buf.finish(data);
    return write(data.data(), data.length());
  }
  bool matchHeader() {
    auto data = ZmScratch(char, MaxRowLen);
    do {
      unsigned length = data.length();
      auto r = file.read(data.data() + length, MaxRowLen - length);
      if (r == Zi::IOError) { error = ioError(path, file); return false; }
      if (r <= 0) { error = headerError(path); return false; }
      data.length(length + r);
      ZuSpan<char> header_[Base::AllFields::N];
      Header header(&header_[0], 0, Base::AllFields::N, false);
      auto n = split(ZuSpan<char>(data.data(), data.length()), header);
      if (n >= 0) {
	if (this->matchHdr(header)) return true;
	error = headerError(path);
	return false;
      }
    } while (data.length() < MaxRowLen);
    error = overflow();
    return false;
  }
  void open(WriteMode mode) {
    switch (mode) {
      case Replace:
	if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return;
	}
	writeHeader();
	return;
      case Create:
	if (file.open(
	    path, ZiFile::Create | ZiFile::WriteOnly | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return;
	}
	if (file.size() > 0) { error = existsError(path); return; }
	writeHeader();
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
	    if (file.seek(0) != Zi::OK) { error = ioError(path, file); return; }
	    if (!matchHeader()) return;
	    if (file.seek(size) != Zi::OK) { error = ioError(path, file); return; }
	  } else
	    writeHeader();
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
    auto data = ZmScratch(char, MaxRowLen);
    ZuStream buf{data.span()};
    this->save(buf, o);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    buf.finish(data);
    return write(data.data(), data.length());
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
  bool writeHeader(const Path &path, ZiFile &file) {
    auto data = ZmScratch(char, MaxRowLen);
    ZuStream buf{data.span()};
    this->saveHdr(buf);
    if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
    buf.finish(data);
    return write_(path, file, data.data(), data.length());
  }
  bool matchHeader(const Path &path, ZiFile &file) {
    auto data = ZmScratch(char, MaxRowLen);
    do {
      unsigned length = data.length();
      auto r = file.read(data.data() + length, MaxRowLen - length);
      if (r == Zi::IOError) { error = ioError(path, file); return false; }
      if (r <= 0) { error = headerError(path); return false; }
      data.length(length + r);
      ZuSpan<char> header_[Base::AllFields::N];
      Header header(&header_[0], 0, Base::AllFields::N, false);
      auto n = split(ZuSpan<char>(data.data(), data.length()), header);
      if (n >= 0) {
	if (this->matchHdr(header)) return true;
	error = headerError(path);
	return false;
      }
    } while (data.length() < MaxRowLen);
    error = overflow();
    return false;
  }
  bool open(ZiFile &file, const Path &path, WriteMode mode) {
    switch (mode) {
      case Replace:
	if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	return writeHeader(path, file);
      case Create:
	if (file.open(
	    path, ZiFile::Create | ZiFile::WriteOnly | ZiFile::GC) != Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	if (file.size() > 0) { error = existsError(path); return false; }
	return writeHeader(path, file);
      case Append:
	if (file.open(path, ZiFile::Create | ZiFile::Append_ | ZiFile::GC) !=
	    Zi::OK) {
	  error = ioError(path, file);
	  return false;
	}
	{
	  auto size = file.size();
	  if (size > 0) {
	    if (file.seek(0) != Zi::OK) { error = ioError(path, file); return false; }
	    if (!matchHeader(path, file)) return false;
	    if (file.seek(size) != Zi::OK) { error = ioError(path, file); return false; }
	  } else if (!writeHeader(path, file))
	    return false;
	}
	return true;
    }
    ZuUnreachable();
  }

  template <typename L>
  bool write(const Path &path, WriteMode mode, L l) {
    ZiFile file;
    if (!open(file, path, mode)) return false;
    auto data = ZmScratch(char, MaxRowLen);
    for (;;) {
      ZuStream buf{data.span()};
      if (!l([this, &buf](const O &o) { this->save(buf, o); })) return true;
      if (ZuUnlikely(buf.overflow())) { error = overflow(); return false; }
      buf.finish(data);
      if (!write_(path, file, data.data(), data.length())) return false;
      data.null();
    }
    ZuUnreachable(); // unreachable
  }

public:
  template <typename L>
  PullFile(const Path &path, WriteMode mode, L l) {
    write(path, mode, ZuMv(l));
  }
  template <typename L>
  PullFile(Columns columns, const Path &path, WriteMode mode, L l) :
      Base{columns} {
    write(path, mode, ZuMv(l));
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
  PullFile<O, Facet, MaxRowLen> pull(ZuMv(path), mode, ZuMv(l));
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
  PullFile<O, Facet, MaxRowLen> pull(
    columns, ZuMv(path), mode, ZuMv(l));
  if (pull) return {};
  return ZuMv(pull.error);
}

template <typename O_, typename Facet = ZuFacet::Core>
struct Reader : public ZfCSV::Reader<O_, Facet> {
  using O = O_;
  using Base = ZfCSV::Reader<O, Facet>;
  using Base::Base;
  template <typename ...Args>
  Reader(Args &&...args) : Base(ZuFwd<Args>(args)...) { }

  template <unsigned MaxRowLen = 4096, typename Path, typename L>
  ZuUnion<void, ZeException> readFile(const Path &path, L l) {
    ZiFile file;
    if (file.open(path, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK) goto error;
    {
      auto buf = ZmScratch(char, MaxRowLen);
      do {
	unsigned length = buf.length();
	auto r = file.read(buf.data() + length, MaxRowLen - length);
	if (r == Zi::IOError) goto error;
	if (r <= 0) return {};
	buf.length(length + r);
	auto n = this->process(ZuSpan<char>(buf.data(), buf.length()), l);
	unsigned consumed = n > 0 ? n : 0;
	if (consumed && consumed <= buf.length()) {
	  unsigned length = buf.length() - consumed;
	  if (length) memmove(buf.data(), buf.data() + consumed, length);
	  buf.length(length);
	}
      } while (buf.length() < MaxRowLen);

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
