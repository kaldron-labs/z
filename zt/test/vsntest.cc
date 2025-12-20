//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuLib.hh>

#include <zlib/ZmPlatform.hh>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

class S {
  char		*m_buf;
  int		m_length;
  int		m_increment;
  int		m_maxLen;

public:
  S(int increment, int maxLen) :
    m_buf(0), m_length(0), m_increment(increment), m_maxLen(maxLen) { }
  ~S() { free(m_buf); }

  char &operator [](int i) { return m_buf[i]; }
  char operator [](int i) const { return m_buf[i]; }
  operator char *() { return m_buf; }
  operator const char *() const { return m_buf; }

private:
  int length() const { return m_length; }
  void length(int n) {
    if (m_length >= n) return;
    if (!m_buf)
      m_buf = (char *)malloc(n);
    else
      m_buf = (char *)realloc(m_buf, n);
    m_length = n;
  }
  bool grow() {
    if (m_length >= m_maxLen) return false;
    length(m_length + m_increment);
    return true;
  }

public:
  void vsnprintf(const char *format, va_list args) {
retry:
    int n;

#ifdef va_copy
    {
      va_list args2;
      va_copy(args2, args);
      n = ::vsnprintf(m_buf, m_length, format, args2);
      va_end(args2);
    }
#else
    n = ::vsnprintf(m_buf, m_length, format, args);
#endif

    printf("m_length=%d return=%d\n", m_length, n);

    if (n < 0) {
      if (!grow()) return;
      goto retry;
    }

    if (n == m_length || n == m_length - 1) {
      if (!grow()) return;
      goto retry;
    }

    if (n > m_length) {
      if (!grow()) return;
      goto retry;
    }

    length(n);
  }

  void sprintf(const char *format, ...) {
    va_list args;

    va_start(args, format);
    vsnprintf(format, args);
    va_end(args);
  }
};

void usage()
{
  puts("Usage: vsntest increment max");
  Zm::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 3) usage();

  int increment, maxLen;

  if ((increment = atoi(argv[1])) <= 0) usage();
  if ((maxLen = atoi(argv[2])) <= 0) usage();

  S s(increment, maxLen);

  s.sprintf("%s %s %s %s %s %s %s %s %s %s",
    "Hello", "World",
    "Hello", "World",
    "Hello", "World",
    "Hello", "World",
    "Hello", "World");

  puts(s);
}
