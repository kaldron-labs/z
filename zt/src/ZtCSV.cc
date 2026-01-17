//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// CSV parser/generator

#include <stdio.h>
#include <ctype.h>

#include <zlib/ZtCSV.hh>

namespace ZtCSV {

// Microsoft Excel compatible quoting: a, " ,"",",b -> a| ,",|b

// lots of intentional goto usage here - these are hard-coded FSM parsers
// where the state is implied by the code block

// split a line - header version
int split(ZuSpan<char> span, Header &header)
{
  unsigned n = span.length();
  unsigned b = 0, i = 0 /* , o = o; */;
  char c;

cell:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      goto quoted;
    }
unquoted:
    if (ZuUnlikely(c == ',' || c == '\r' || c == '\n')) {
      // if (i > o) memset(&span[o], 0, i - o);
      header.push(ZuSpan<char>(&span[b], i - b /* o - b */));
      if (c == '\r' || c == '\n') {
	++i;
	if (c == '\r' && i < n && (c = span[i]) == '\n') ++i;
	return i;
      }
      // c == ','
      b = ++i;
      goto cell;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;

quoted:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      if (i >= n) break;
      c = span[i];
      if (c != '"') goto unquoted;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;
}

// split a line - row version
int split(ZuSpan<char> span, Row &row)
{
  unsigned n = span.length();
  unsigned b = 0, i = 0 /* , o = 0 */;
  char c;

  // the implicit intersection operator "@" was introduced in Excel 365/2021
  // as part of the Dynamic Arrays update; this update fundamentally
  // changed how Excel handles arrays in formulae, enabling cell spilling;
  // array values in CSV files are interpreted as non-spilling and have the
  // form "={1;2;3;...}"; newer Excel versions use "=@{1;2;3;...}"

  // lots of intentional goto usage here - this is a hard-coded FSM parser
  // where the state is implied by the code block

cell:
  // if the cell begins with ={...} or =@{...}, parse it as an array value
  if (i >= n) return -1;
  c = span[i];
  if (c == '=' && (i + 2) < n) {
    c = span[++i];
    if (c == '{' || ((i + 3) < n && c == '@' && (c = span[++i]) == '{')) {
      b = ++i;
      goto array;
    }
  }

  // within a cell, oscillate between unquoted (the initial state) and quoted 
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      goto quoted;
    }
unquoted:
    if (ZuUnlikely(c == ',' || c == '\r' || c == '\n')) {
      // if (i > o) memset(&span[o], 0, i - o);
      row.push(Cell{ZuSpan<char>(&span[b], i - b /* o - b */)});
      if (c == '\r' || c == '\n') {
	if (c == '\r' && i < n) c = span[++i];
	if (c != '\n') return -1;
	return i + 1;
      }
      // c == ','
      b = /* o = */ ++i;
      goto cell;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;

quoted:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      if (i >= n) break;
      c = span[i];
      if (c != '"') goto unquoted;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;

array:
  ArrayCell array;

array_element:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      goto array_quoted;
    }
array_unquoted:
    if (ZuUnlikely(c == ';' || c == '}')) {
      // if (i > o) memset(&span[o], 0, i - o);
      array.push(ZuSpan<char>(&span[b], i - b /* o - b */));
      if (c == '}') {
	row.push(Cell{ZuMv(array)});
	c = span[++i];
	if (c == ',') { b = ++i; goto cell; }
	if (c == '\r' && i < n) c = span[++i];
	if (c != '\n') return -1;
	return i;
      }
      // c == ';'
      b = /* o = */ ++i;
      goto array_element;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;

array_quoted:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      if (i >= n) break;
      c = span[i];
      if (c != '"') goto array_unquoted;
    }
    // if (o < i) span[o] = c;
    // ++o;
    ++i;
  }
  return -1;
}

// idempotently unquote a span
// - null-terminates span
void unquote(ZuSpan<char> &span)
{
  unsigned n = span.length();
  if (ZuLikely(!span[n])) return; // already unquoted
  unsigned i = 0, o = 0;
  char c;

unquoted:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      goto quoted;
    }
    if (o < i) span[o] = c;
    ++o, ++i;
  }
  goto ret;

quoted:
  while (i < n) {
    c = span[i];
    if (ZuUnlikely(c == '"')) {
      ++i;
      if (i >= n) break;
      c = span[i];
      if (c != '"') goto unquoted;
    }
    if (o < i) span[o] = c;
    ++o, ++i;
  }

ret:
  // zero-fill overwrite delimiter following span
  memset(&span[o], 0, (i - o) + 1);
  span.trunc(o);
}

} // ZtCSV
