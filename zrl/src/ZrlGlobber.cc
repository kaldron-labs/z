//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// command line interface

#include <zlib/ZrlGlobber.hh>

namespace Zrl {

typedef bool (*QuoteFn)(uint32_t);

QuoteFn Globber::quoteFn()
{
  switch (m_qmode & QMode::Mask) {
    case QMode::BackQuote:
      return [](uint32_t c) {
	return
	  c == '\\' || isspace__(c) ||
	  c == '*' || c == '?' || c == '{' || c == '}' ||
	  c == '\'' || c == '"';
      };
    case QMode::SglQuote:
      return [](uint32_t c) {
	return c == '\\' || c == '\'';
      };
    case QMode::DblQuote:
      return [](uint32_t c) {
	return c == '\\' || c == '"';
      };
  }
  return nullptr;
}

void Globber::init(
    ZuBSpan data,	// line data
    unsigned cursor,			// cursor offset (in bytes)
    CompSpliceFn splice)		// line splice callback function
{
  unsigned begin = 0;			// beginning of path in line
  unsigned end = data.length();		// end of line data

  // if cursor is at line end or cursor is not on white space,
  // then we need to append a space to every completion, unless
  // the completion is a directory in which case a '/' is appended
  m_appendSpace = cursor == end || !isspace__(data[cursor]);

// --- 1st pass - find the beginning of the quoted path ending at cursor

  // regrettably a 2-pass scan is more efficient than a 1-pass due
  // to the need to persist the unquoted path; any re-quoted span
  // is subsequently spliced back into the line
  int qstate = QState::WhiteSpace;
  unsigned off = 0;
  while (off < cursor) {
    uint32_t c;
    unsigned n = ZuUTF8::in(&data[off], end - off, c);
    if (!n) break;
    if (qstate & QState::BackQuote) {
      qstate &= ~QState::BackQuote;
    } else {
      if (c == '\\') {
	if ((qstate & QState::Mask) == QState::WhiteSpace) {
	  qstate = QState::Unquoted;
	  begin = off;
	}
	qstate |= QState::BackQuote;
      } else {
	switch (qstate & QState::Mask) {
	  case QState::WhiteSpace:
	    switch (c) {
	      case '"':  qstate = QState::DblQuoted; begin = off; break;
	      case '\'': qstate = QState::SglQuoted; begin = off; break;
	      default:
		if (!isspace__(c)) {
		  qstate = QState::Unquoted;
		  begin = off;
		}
		break;
	    }
	    break;
	  case QState::Unquoted:
	    switch (c) {
	      case '"':  qstate = QState::DblQuoted; break;
	      case '\'': qstate = QState::SglQuoted; break;
	      default:
		if (isspace__(c)) qstate = QState::WhiteSpace;
		break;
	    }
	    break;
	  case QState::SglQuoted:
	    if (c == '\'') qstate = QState::Unquoted;
	    break;
	  case QState::DblQuoted:
	    if (c == '"') qstate = QState::Unquoted;
	    break;
	}
      }
    }
    off += n;
  }

// --- 2nd pass - re-scan the line, building the unquoted path

  // {} not used - avoid std::initializer_list<T> ctor overload
  ZtArray<uint8_t> path(cursor - begin);// unquoted path (estimated size)

  // capture any span within the path that needs re-quoting

  unsigned qoff = UINT_MAX;		// quote span offset in line
  ZuUTFSpan qspan;			// quote span in line
  unsigned pqoff = UINT_MAX;		// quote offset in path

  m_qmode = QMode::Unset;		// extant quoting mode at cursor

  // capture leafname span within the quoted path

  m_loff = begin;			// leafname offset in line
  m_lspan = {};				// leafname span in line
  int xqstate = QState::Unquoted;	// extant quoting state at leafname

  qstate = QState::Unquoted;
  off = begin;
  while (off < cursor) {
    uint32_t c;
    unsigned n = ZuUTF8::in(&data[off], end - off, c);
    if (!n) break;
    if (qstate == QState::Unquoted && !(qstate & QState::BackQuote) &&
	qoff == UINT_MAX && (c == '*' || c == '?' || c == '{' || c == '}')) {
      qoff = off;
      pqoff = path.length();
    }
    {
      ZuUTFSpan span(n, 1, ZuUTF32::width(c));
      if (qoff < UINT_MAX)
	qspan += span;
      else if (c != '/')
	m_lspan += span;	// don't extend leaf span into re-quote span
    }
    if (qstate & QState::BackQuote) {
      qstate &= ~QState::BackQuote;
    } else {
      if (c == '\\') { qstate |= QState::BackQuote; off += n; continue; }
      switch (qstate & QState::Mask) {
	case QState::Unquoted:
	  switch (c) {
	    case '"': qstate = QState::DblQuoted; off += n; continue;
	    case '\'': qstate = QState::SglQuoted; off += n; continue;
	    default:
	      if (isspace__(c)) qstate = QState::WhiteSpace;
	      break;
	  }
	  break;
	case QState::SglQuoted:
	  if (c == '\'') { qstate = QState::Unquoted; off += n; continue; }
	  break;
	case QState::DblQuoted:
	  if (c == '"') { qstate = QState::Unquoted; off += n; continue; }
	  break;
      }
      if (qstate == QState::WhiteSpace) break; // white space under cursor
      if (c == '/') {
	m_loff = off + 1;
	m_lspan = {};
	xqstate = qstate;
      }
    }
    path << ZuBSpan(&data[off], n);
    off += n;
  }
  {
    bool extant = xqstate != QState::Unquoted;
    if (!extant) xqstate = qstate;	// use last qstate within leafname
    switch (xqstate & QState::Mask) {
      case QState::Unquoted:
	m_qmode = QMode::BackQuote;
	break;
      case QState::SglQuoted:
	m_qmode = QMode::SglQuote;
	break;
      case QState::DblQuoted:
	m_qmode = QMode::DblQuote;
	break;
    }
    if (extant) m_qmode |= QMode::Extant;
  }

  // initialize filesystem globber
  m_glob.init(ZuCSpan(path));

  // if qoff is past cursor, elide re-quoting
  if (qoff >= cursor) return;

// --- path needs re-quoting - re-quote it and splice it back into the line

  // re-quote path from qoff to cursor, building replace and rspan, and
  // updating m_loff and m_lspan as needed
  ZtArray<uint8_t> replace;
  ZuUTFSpan rspan;		// replacement span in line
  {
    unsigned rlen = path.length() - pqoff;
    rlen += (rlen>>3);
    replace.size(rlen);
  }
  auto quoteFn = this->quoteFn();
  off = pqoff;
  while (off < path.length()) {
    uint32_t c;
    unsigned n = ZuUTF8::in(&path[off], path.length() - off, c);
    if (!n) break;
    if (quoteFn(c)) { replace << '\\'; ++rspan; ++m_lspan; }
    replace << ZuBSpan(&path[off], n);
    {
      ZuUTFSpan span(n, 1, ZuUTF32::width(c));
      rspan += span;
      if (c == '/') {
	m_loff = qoff + replace.length();
	m_lspan = {};
      } else
	m_lspan += span;
    }
    off += n;
  }
  switch (m_qmode & QMode::Mask) {
    case QMode::SglQuote: replace << '\''; ++rspan; ++m_lspan; break;
    case QMode::DblQuote: replace << '"';  ++rspan; ++m_lspan; break;
  }

  // splice replacement quoted path back into line
  splice(qoff, qspan, replace, rspan);
}

void Globber::final()
{
  m_appendSpace = false;
  m_qmode = QMode::Unset;
  m_loff = 0;
  m_lspan = {};
  m_glob.final();
}

void Globber::start()
{
  m_glob.reset();
}

bool Globber::subst(CompSpliceFn splice, bool next)
{
skip:
  auto entry = m_glob.iterate(next, true);
  if (!entry) return false;
#ifndef _WIN32
  ZuBSpan leaf = entry->name;
#else
  Zi::Name leaf_ = entry->name;	// convert from Windows UCS2 to UTF8
  ZuBSpan leaf = leaf_;
#endif
  if (!m_glob.leafName() && leaf[0] == '.') goto skip;

  // quote leaf into replace, building rspan
  ZtArray<uint8_t> replace;
  ZuUTFSpan rspan;
  {
    unsigned rlen = leaf.length();
    rlen += (rlen>>3);
    replace.size(rlen);
  }
  if (!(m_qmode & QMode::Extant))
    switch (m_qmode & QMode::Mask) {
      case QMode::SglQuote: replace << '\''; ++rspan; break;
      case QMode::DblQuote: replace << '"';  ++rspan; break;
    }
  unsigned off = 0;
  auto quoteFn = this->quoteFn();
  while (off < leaf.length()) {
    uint32_t c;
    unsigned n = ZuUTF8::in(&leaf[off], leaf.length() - off, c);
    if (!n) break;
    if (quoteFn(c)) { replace << '\\'; ++rspan; }
    replace << ZuBSpan(&leaf[off], n);
    rspan += ZuUTFSpan(n, 1, ZuUTF32::width(c));
    off += n;
  }
  switch (m_qmode & QMode::Mask) {
    case QMode::SglQuote: replace << '\''; ++rspan; break;
    case QMode::DblQuote: replace << '"';  ++rspan; break;
  }
  if (entry->isdir) {
    replace << '/';
    ++rspan;
  } else if (m_appendSpace) {
    replace << ' ';
    ++rspan;
  }

  // splice replacement leafname into line
  splice(m_loff, m_lspan, replace, rspan);

  // remember revised leafname span
  m_lspan = rspan;

  return true;
}

bool Globber::next(CompIterFn iter)
{
skip:
  auto entry = m_glob.iterate(true, false);
  if (!entry) return false;
#ifndef _WIN32
  ZuBSpan leaf = entry->name;
#else
  Zi::Name leaf_ = entry->name;	// convert from Windows UCS2 to UTF8
  ZuBSpan leaf = leaf_;
#endif
  if (!m_glob.leafName() && leaf[0] == '.') goto skip;
  iter(leaf, ZuUTF<uint32_t, uint8_t>::span(leaf));
  return true;
}

} // Zrl
