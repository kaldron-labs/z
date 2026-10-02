//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// TOML configuration file I/O

#ifndef ZvTOML_HH
#define ZvTOML_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZuString.hh>

#include <zlib/ZfTOML.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiFileTxStream.hh>

namespace ZvTOMLError {

constexpr auto Component = "ZvTOML"_z;

template <ZuString Op>
inline auto fileError(const Zi::Path &path, ZeError error) {
  return [path = Zi::Path{path}, error](auto &s) {
    s << Op.cspan() << "(\"" << path << "\"): " << error;
  };
}

inline auto file2Big(const Zi::Path &path) {
  return [path = Zi::Path{path}](auto &s) {
    s << '"' << path << "\": file too big";
  };
}

} // ZvTOMLError

#define ZvTOML_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZvTOMLError::Component, __VA_ARGS__)

namespace ZvTOML {

using namespace ZfTOML;
using ZfTOML::AnyNode;

ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> load(
  const Zi::Path &, ZfTOML::Limits = {});

template <
  typename Facet = ZuFacet::TOML,
  template <typename> class Filter = ZfFieldFilter::Save,
  typename O>
inline void save(const Zi::Path &path, const O &v) {
  ZiFile file;
  if (file.open(path, ZiFile::Write | ZiFile::GC) != Zi::OK)
    throw ZvTOML_EXCEPT(
      ZvTOMLError::fileError<"open">(path, file.error()));
  ZiFileTxStream<> stream{file};
  ZfTOML::save<Facet, Filter>(stream, v);
  if (!stream.flush()) {
    if (file.error())
      throw ZvTOML_EXCEPT(
	ZvTOMLError::fileError<"write">(path, file.error()));
    throw ZvTOML_EXCEPT(
      ([path = Zi::Path{path}](auto &s) { s << '"' << path << "\": output failed"; }));
  }
}

template <typename Facet = ZuFacet::TOML, typename O>
inline void saveUpd(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Upd>(path, v);
}

template <typename Facet = ZuFacet::TOML, typename O>
inline void saveDel(const Zi::Path &path, const O &v) {
  save<Facet, ZfFieldFilter::Del>(path, v);
}

} // ZvTOML

#endif /* ZvTOML_HH */
