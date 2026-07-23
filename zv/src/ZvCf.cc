//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZtArray.hh>

#include <zlib/ZvCf.hh>

namespace ZvCf {

namespace {

enum { MaxFileSize = 1<<20 }; // 1Mb

ZuDerive(Paths, (ZtArray<Zi::Path, ZtArrayHeapID<"ZvCf.Paths">>));

void setDefine(Defines *defines, ZuCSpan key, ZuCSpan value)
{
  if (auto node = defines->findPtr(key)) {
    node->val() = value;
    return;
  }
  defines->add(DefKey{key}, DefVal{value});
}

class Reader {
public:
  Reader(PctFn appPctFn, ZmRef<Defines> defines) :
    m_appPctFn{ZuMv(appPctFn)}, m_defines{ZuMv(defines)} {
    if (!m_defines) m_defines = new Defines();
  }

  ZuTuple<int, ZuPtr<const AnyNode>> load(const Zi::Path &path) {
    auto root = ZiFile::canonical(path);
    if (!root)
      throw ZvCf_EXCEPT(
	ZvCfError::fileError<"open">(path, ZeLastError));
    m_topDir = ZiFile::dirname(root);
    setDefine(m_defines, "TOPDIR", m_topDir);
    m_paths.push(root);
    auto mapped = map(root);
    setDefine(m_defines, "CURDIR", m_topDir);

    ZfCf::Scan scan{
      mapped.span,
      PctFn{this, [](Reader *this_, Scan &scan, ZuCSpan directive,
	  ZuSpan<const ZuCSpan> args, PctExpandFn expand) {
	return this_->pct(scan, directive, args, ZuMv(expand));
      }},
      m_defines
    };
    try {
      return scan.scan();
    } catch (const ZeException &) {
      const auto &error = scan.error();
      if (!error.failed) throw;
      const auto &errorPath = m_errorPath ? m_errorPath : root;
      throw ZeMkException(
	Ze::Error, __FILE__, __LINE__, ZuFnName, ZfCfError::Component,
	ZfCfError::badSyntax(
	  error.line, error.column, error.offset, error.ch, errorPath));
    }
  }

private:
  struct Mapped {
    Mapped(const Mapped &) = delete;
    Mapped &operator =(const Mapped &) = delete;
    Mapped(Mapped &&) = delete;
    Mapped &operator =(Mapped &&) = delete;

    Mapped() {
      static const char empty = 0;
      span = {&empty, 0};
    }
    Mapped(const Zi::Path &path, Zi::Offset length, ZuCSpan chain) {
      if (file.mmap(
	  path, ZiFile::ReadOnly | ZiFile::GC, length, false) != Zi::OK)
	throw ZvCf_EXCEPT(
	  ZvCfError::fileError<"mmap">(path, file.error(), chain));
      span = file.cspan();
    }

    ZiMMapFile	file;
    ZuCSpan	span;
  };

  ZeString chain(const Zi::Path *tail = nullptr) const {
    ZeString s;
    for (unsigned i = 0; i < m_paths.length(); ++i) {
      if (i) s << " -> ";
      s << m_paths[i];
    }
    if (tail) {
      if (s) s << " -> ";
      s << *tail;
    }
    return s;
  }

  Mapped map(const Zi::Path &path) {
    ZiStat stat{path};
    auto length = stat.size();
    if (stat.error())
      throw ZvCf_EXCEPT(
	ZvCfError::fileError<"stat">(path, stat.error(), chain()));
    if (length >= MaxFileSize)
      throw ZvCf_EXCEPT(ZvCfError::file2Big(path, chain()));
    if (!length) return Mapped{};
    auto includeChain = chain();
    return Mapped{path, length, includeChain};
  }

  bool pct(
      Scan &scan, ZuCSpan directive, ZuSpan<const ZuCSpan> args,
      PctExpandFn expand) {
    if (directive != "include") {
      if (!m_appPctFn) {
	if (!m_errorPath) m_errorPath = m_paths[m_paths.length() - 1];
	return false;
      }
      bool ok = m_appPctFn(scan, directive, args, ZuMv(expand));
      if (!ok && !m_errorPath)
	m_errorPath = m_paths[m_paths.length() - 1];
      return ok;
    }
    if (args.length() != 1 || !args[0])
      throw ZvCf_EXCEPT(
	ZvCfError::includeArgs(m_paths[m_paths.length() - 1], chain()));

    Zi::Path path{args[0]};
    if (!ZiFile::absolute(path))
      path = ZiFile::append(
	ZiFile::dirname(m_paths[m_paths.length() - 1]), path);
    auto resolved = ZiFile::canonical(path);
    if (!resolved)
      throw ZvCf_EXCEPT(
	ZvCfError::fileError<"open">(path, ZeLastError, chain(&path)));

    for (unsigned i = 0; i < m_paths.length(); ++i)
      if (m_paths[i] == resolved)
	throw ZvCf_EXCEPT(
	  ZvCfError::includeCycle(chain(&resolved)));

    auto mapped = map(resolved);
    auto parentDir = ZiFile::dirname(m_paths[m_paths.length() - 1]);
    auto dir = ZiFile::dirname(resolved);
    m_paths.push(resolved);
    setDefine(m_defines, "CURDIR", dir);
    bool ok;
    try {
      ok = expand(mapped.span);
    } catch (...) {
      m_paths.pop();
      setDefine(m_defines, "CURDIR", parentDir);
      throw;
    }
    if (!ok && !m_errorPath) m_errorPath = resolved;
    m_paths.pop();
    setDefine(m_defines, "CURDIR", parentDir);
    return ok;
  }

  PctFn		m_appPctFn;
  ZmRef<Defines> m_defines;
  Paths		m_paths;
  Zi::Path	m_topDir;
  Zi::Path	m_errorPath;
};

} // namespace

ZuTuple<int, ZuPtr<const AnyNode>> load(
    const Zi::Path &path, PctFn pctFn, ZmRef<Defines> defines)
{
  return Reader{ZuMv(pctFn), ZuMv(defines)}.load(path);
}

} // ZvCf
