//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZuTest.hh>

#include <iostream>

struct ZuTestMgr::Range_ {
  const ZuTest	*begin;
  const ZuTest	*end;
  unsigned	base;
  Range_	*next;
};

ZuTestMgr::ZuTestMgr() { }

ZuTestMgr &ZuTestMgr::instance()
{
  static ZuTestMgr _;
  return _;
}

void ZuTestMgr::registerRange(const ZuTest *begin, const ZuTest *end)
{
  if (!begin || !end || begin == end) return;
  for (Range_ *r = m_head; r; r = r->next)
    if (r->begin == begin && r->end == end) return;

  auto *r = new Range_{begin, end, 0, nullptr};
  if (m_tail) m_tail->next = r;
  else m_head = r;
  m_tail = r;

  unsigned count = static_cast<unsigned>(end - begin);
  if (m_finalized) r->base = m_total + 1;
  m_total += count;
}

void ZuTestMgr::finalize_()
{
  if (m_finalized) return;
  unsigned base = 1;
  for (Range_ *r = m_head; r; r = r->next) {
    r->base = base;
    base += static_cast<unsigned>(r->end - r->begin);
  }
  m_total = base - 1;
  m_finalized = true;
}

unsigned ZuTestMgr::count() const
{
  const_cast<ZuTestMgr *>(this)->finalize_();
  return m_total;
}

unsigned ZuTestMgr::idFor_(const ZuTest *d) const
{
  for (Range_ *r = m_head; r; r = r->next)
    if (d >= r->begin && d < r->end)
      return r->base + static_cast<unsigned>(d - r->begin);
  return 0;
}

unsigned ZuTestMgr::idFor(const ZuTest *d) const
{
  const_cast<ZuTestMgr *>(this)->finalize_();
  return idFor_(d);
}

void ZuTestMgr::begin()
{
  if (m_begun) return;
  finalize_();
  std::cout << "1.." << m_total << '\n';
  m_begun = true;
}

void ZuTestMgr::run(const ZuTest *d, bool ok, ZuCSpan desc)
{
  finalize_();
  unsigned id = idFor_(d);
  if (!ok) std::cout << "not ";
  std::cout << "ok " << id;
  if (desc) std::cout << " - " << desc;
  std::cout << '\n';
}
