
//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// ZfStruct

#include <zlib/ZfStruct.hh>

namespace ZfStruct_::Scan {

unsigned string(ZuSpan<char> dst, ZuCSpan &src)
{
  bool dblQuoted = false;
  bool sglQuoted = false;
  unsigned i = 0, j = 0;
  unsigned n = src.length();
  unsigned m = dst.length();

  while (i < n && j < m) {
    char c = src[i++];
    if (sglQuoted) {
      sglQuoted = false;
    } else {
      if (c == '\\') { sglQuoted = true; continue; }
      if (dblQuoted) {
	if (c == '"') { dblQuoted = false; continue; }
      } else {
	if (c == '"') { dblQuoted = true; continue; }
	if (isspace__(c)) { --i; break; }
      }
    }
    dst[j++] = c;
  }
  src.offset(i);
  return j;
}

unsigned strElem(
  ZuSpan<char> dst, ZuCSpan &src,
  ZuCSpan delim, ZuCSpan suffix)
{
  bool dblQuoted = false;
  bool sglQuoted = false;
  unsigned delimMatch = 0;
  unsigned suffixMatch = 0;
  unsigned i = 0, j = 0;
  unsigned n = src.length();
  unsigned m = dst.length();

  while (i < n && j < m) {
    char c = src[i++];
    if (sglQuoted) {
      sglQuoted = false;
    } else {
      if (delimMatch) {
	if (c == delim[delimMatch]) {
	  if (++delimMatch >= delim.length()) { --i; break; }
	  continue;
	}
	for (unsigned i = 0; i < delimMatch; i++) dst[j++] = delim[i];
	delimMatch = 0;
      } else if (suffixMatch) {
	if (c == suffix[suffixMatch]) {
	  if (++suffixMatch >= suffix.length()) { --i; break; }
	  continue;
	}
	for (unsigned i = 0; i < suffixMatch; i++) dst[j++] = suffix[i];
	suffixMatch = 0;
      }
      if (c == '\\') { sglQuoted = true; continue; }
      if (dblQuoted) {
	if (c == '"') { dblQuoted = false; continue; }
      } else {
	if (c == '"') { dblQuoted = true; continue; }
	if (isspace__(c)) { --i; break; }
	if (delim.length() && c == delim[0]) {
	  delimMatch = 1;
	  if (delim.length() == 1) { --i; break; }
	  continue;
	}
	if (suffix.length() && c == suffix[0]) {
	  suffixMatch = 1;
	  if (suffix.length() == 1) { --i; break; }
	  continue;
	}
      }
    }
    dst[j++] = c;
  }
  src.offset(i);
  return j;
}

} // ZfStruct_::Scan
