//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// Perl compatible regular expressions (pcre)

#define ZtRegex_CC

#include <zlib/ZtRegex.hh>

ZtRegex::ZtRegex(const char *pattern, int options) : m_extra(0)
{
  ZtRegexError error;

  m_regex = pcre_compile2(
    pattern, options, &error.code, &error.message, &error.offset, 0);

  if (!m_regex) throw error;

  if (!pcre_fullinfo(m_regex, 0, PCRE_INFO_CAPTURECOUNT, &m_captureCount))
    m_captureCount++;
  else
    m_captureCount = 1;
}

ZtRegex::~ZtRegex()
{
  if (m_extra) (pcre_free_study)(m_extra);
  if (m_regex) (pcre_free)(m_regex);
}

void ZtRegex::study()
{
  ZtRegexError error;

  error.offset = -1;
  error.code = -1;

  m_extra = pcre_study(m_regex, PCRE_STUDY_JIT_COMPILE, &error.message);

  if (error.message) {
    if (m_extra) (pcre_free)(m_extra);
    m_extra = 0;
    throw error;
  }
}

int ZtRegex::index(const char *name) const
{
  int i = pcre_get_stringnumber(m_regex, name);
  if (i < 0) return -1;
  return i + 1;
}

static const char *exec_errors[] = {
  "NOMATCH",
  "NULL",
  "BADOPTION",
  "BADMAGIC",
  "UNKNOWN_OPCODE",
  "NOMEMORY",
  "NOSUBSTRING",
  "MATCHLIMIT",
  "CALLOUT",
  "BADUTF",
  "BADUTF_OFFSET",
  "PARTIAL",
  "BADPARTIAL",
  "INTERNAL",
  "BADCOUNT",
  "DFA_UITEM",
  "DFA_UCOND",
  "DFA_UMLIMIT",
  "DFA_WSSIZE",
  "DFA_RECURSE",
  "RECURSIONLIMIT",
  "NULLWSLIMIT",
  "BADNEWLINE",
  "BADOFFSET",
  "SHORTUTF",
  "RECURSELOOP",
  "JIT_STACKLIMIT",
  "BADMODE",
  "BADENDIANNESS",
  "DFA_BADRESTART",
  "JIT_BADOPTION",
  "BADLENGTH",
  "UNSET"
};

const char *ZtRegexError::strerror(int i)
{
  enum { N = sizeof(exec_errors) / sizeof(exec_errors[0]) };
  i = -i - 1;
  if (i < 0 || i >= N) return "UNKNOWN";
  return exec_errors[i];
}
