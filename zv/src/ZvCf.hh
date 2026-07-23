//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// mmap-backed configuration file loading

#ifndef ZvCf_HH
#define ZvCf_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

#include <zlib/ZfCf.hh>

#include <zlib/ZiFile.hh>

namespace ZvCfError {

constexpr auto Component = "ZvCf"_Zu;

template <ZuString Op>
inline auto fileError(
    const Zi::Path &path, ZeError error, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, error, chain = ZeString{chain}
  ](auto &s) {
    s << Op.cspan() << "(\"" << path << "\"): " << error;
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto file2Big(const Zi::Path &path, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, chain = ZeString{chain}
  ](auto &s) {
    s << '"' << path << "\": file too big";
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto includeArgs(
    const Zi::Path &path, ZuCSpan chain = {}) {
  return [
    path = Zi::Path{path}, chain = ZeString{chain}
  ](auto &s) {
    s << '"' << path << "\": %include requires one non-empty argument";
    if (chain) s << " [include chain: " << chain << ']';
  };
}

inline auto includeCycle(ZuCSpan chain) {
  return [chain = ZeString{chain}](auto &s) {
    s << "recursive %include [include chain: " << chain << ']';
  };
}

} // ZvCfError

#define ZvCf_EXCEPT(...) \
  ZeMkException(Ze::Error, __FILE__, __LINE__, ZuFnName, \
    ZvCfError::Component, __VA_ARGS__)

namespace ZvCf {

using namespace ZfCf;
using ZfCf::AnyNode;

ZvExtern ZuTuple<int, ZuPtr<const AnyNode>> read(
  const Zi::Path &, PctFn = {}, ZmRef<Defines> = new Defines());

} // ZvCf

#endif /* ZvCf_HH */
