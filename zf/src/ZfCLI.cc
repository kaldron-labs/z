//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZfCLI.hh>

namespace ZfCLI {

// find end of string, un-quoting in-place

ZuTuple<int, int, char> eos(ZuSpan<char> span, unsigned position)
{
  unsigned n = span.length();

  if (ZuUnlikely(n < 1)) return {-1};

  // fast path - no unquoting/mutation
  unsigned i;
  for (i = 0; i < n; i++) {
    char c = span[i];
    if (ZuUnlikely(c == '"' || c == '\'' || c == '\\')) goto slow;
    if (ZuUnlikely(isspace__(c))) {
      auto o = i;
      while (++i < n && isspace__(span[i]));
      memset(&span[o], 0, i - o); // terminate and pad with zeros
      return {o, i, c};
    }
    if (ZuUnlikely(c == '<' || c == '>' || c == '#' || c == ';')) {
      if (!i) {
	if (c == '#' || c == ';') return {1, 1, c};
	continue;
      }
      if (i == 1 && c == '>' && span[0] == '>') continue;
      return {i, i, c};
    }
  }
  return {i, i, 0};

slow:
  // slow path - span[i] =~ ["'\\] is a precondition
  unsigned o = i;
  while (i < n) {
    char c = span[i];
    if (c == '"') {
      bool closed = false;
      while (++i < n) {
	c = span[i];
	if (c == '\\') {
	  if (++i < n) span[o++] = span[i];
	  continue;
	}
	if (c == '"') {
	  ++i;
	  closed = true;
	  break;
	}
	span[o++] = span[i];
      }
      if (ZuUnlikely(!closed))
	throw ZfCLI_EXCEPT(ZfCLIError::unterminated(position, '"'));
      continue;
    }
    if (c == '\'') {
      bool closed = false;
      while (++i < n) {
	c = span[i];
	if (c == '\\') {
	  if (++i < n) span[o++] = span[i];
	  continue;
	}
	if (c == '\'') {
	  ++i;
	  closed = true;
	  break;
	}
	span[o++] = span[i];
      }
      if (ZuUnlikely(!closed))
	throw ZfCLI_EXCEPT(ZfCLIError::unterminated(position, '\''));
      continue;
    }
    if (ZuUnlikely(isspace__(c))) {
      while (++i < n && isspace__(span[i]));
      memset(&span[o], 0, i - o); // terminate and pad with zeros
      return {o, i, c};
    }
    if (ZuUnlikely(c == '<' || c == '>' || c == '#' || c == ';')) {
      memset(&span[o], 0, i - o); // terminate and pad with zeros
      return {o, i, c};
    }
    if (c == '\\') {
      if (++i >= n)
	throw ZfCLI_EXCEPT(ZfCLIError::unterminated(position, 0));
      c = span[i];
    }
    ++i;
    span[o++] = c; // o < i is guaranteed
  }
  memset(&span[o], 0, i - o); // terminate and pad with zeros
  return {o, i, 0};
}

// find end of key '='
// - uses memchr to leverage any available performance advantage

int eok(ZuCSpan data) {
  auto p = static_cast<const char *>(memchr(&data[0], '=', data.length()));
  if (!p) return -1;
  return p - &data[0];
}

ZuTuple<int, ZuCSpan> scanKey(ZuCSpan key)
{
  unsigned e = key.length();
  if (!e) return {-1};
  auto c = key[0];

  if (c == '[') {
    unsigned i = 0;
    while (++i < e) {
      c = key[i];
      if (c == ']') return {i + 1, ZuCSpan(&key[1], i - 1)};
    }
    return {-1};
  }

  if (c == '.') return {-1};

  unsigned i = 0;
  while (++i < e) {
    c = key[i];
    if (c == '[') break;
    if (c == '.') return {i + 1, ZuCSpan(&key[0], i)};
  }
  return {i, ZuCSpan(&key[0], i)};
}

} // ZfCLI
