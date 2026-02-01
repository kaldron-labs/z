//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <iostream>

#include <assert.h>

#include <zlib/ZuTest.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuSort.hh>

struct ZuTestMgr::Section {
  ZuTestStep	*begin;
  ZuTestStep	*end;
  Section	*next;
};

ZuTestMgr::ZuTestMgr() { }

ZuTestMgr::~ZuTestMgr()
{
  Section *next;
  for (Section *section = m_head; section; section = next) {
    next = section->next;
    delete section;
  }
}

ZuTestMgr &ZuTestMgr::instance()
{
  static ZuTestMgr _;
  return _;
}

void ZuTestMgr::addSection(ZuTestStep *begin, ZuTestStep *end)
{
  if (!begin || !end || begin == end) return;
  for (Section *section = m_head; section; section = section->next)
    if (section->begin == begin && section->end == end) return;

  auto *section = new Section{begin, end, nullptr};
  if (m_tail)
    m_tail->next = section;
  else
    m_head = section;
  m_tail = section;
}

void ZuTestMgr::init()
{
  if (m_finalized) return;
  // sort steps and establish IDs
  for (Section *section = m_head; section; section = section->next) {
    auto n = static_cast<unsigned>(section->end - section->begin);
    ZuSort(section->begin, n, [](const ZuTestStep &l, const ZuTestStep &r) {
      // pointer comparison is fine here
      if (int cmp = ZuCompare(l.file, r.file)) return cmp;
      return ZuCompare(l.line, r.line);
    });
    for (ZuTestStep *step = section->begin; step < section->end; step++) {
      auto scope = step->scope;
      step->id = ++scope->count;
    }
  }
  m_finalized = true;
}

void ZuTestMgr::start_()
{
  if (m_context) return;
  init();
  m_root = {};
  m_context = &m_root;
  m_indent = 0;
  std::cout << "TAP version 14\n" << std::flush;
}

void ZuTestMgr::indent_()
{
  for (unsigned i = 0; i < m_indent; i++) std::cout << "    ";
}

void ZuTestMgr::begin_(ZuTestScope *scope)
{
  start_();
  unsigned n;
  if (!scope->name) { // root
    m_context->scope = scope;
    if (!scope->dynamic) n = scope->count;
  } else {
    unsigned loops = !m_context->step ? 1 : m_context->step->count;
    m_context = new RunContext{m_context, scope};
    if (!scope->dynamic) n = scope->count * loops;
    indent_(); std::cout << "# Subtest: " << scope->name << '\n';
    ++m_indent;
  }

  if (!scope->dynamic) {
    indent_(); std::cout << "1.." << n << '\n' << std::flush;
  }
}

void ZuTestMgr::end_(ZuTestScope *scope)
{
  start_();
  assert(scope);
  assert(scope == m_context->scope);
  if (RunContext *parent = static_cast<RunContext *>(m_context->parent)) {
    if (scope->dynamic) {
      indent_(); std::cout << "1.." << scope->count << '\n' << std::flush;
      scope->count = 0; // dyanmic scopes may be repeatedly entered
    }
    bool ok = !m_context->failed;
    delete m_context;
    m_context = parent;
    if (m_indent > 0) --m_indent;
    assert(m_context->step);
    check_(m_context->step, ok, m_context->step->name);
  }
}

void ZuTestMgr::check_(ZuTestStep *step, bool ok, const char *name)
{
  start_();
  auto scope = m_context->scope;
  assert(scope);
  if (step) {
    name = step->name;
    m_context->step = step;
  }
  indent_();
  if (!ok) {
    ++m_context->failed;
    std::cout << "not ";
  }
  unsigned id;
  if (!scope->dynamic) {
    assert(step);
    id = (m_context->iteration * scope->count) + step->id;
  } else
    id = ++scope->count;
  std::cout << "ok " << id;
  if (name) std::cout << " - " << name;
  std::cout << '\n' << std::flush;
  if (!scope->dynamic && step->id == scope->count) ++m_context->iteration;
}

void ZuTestMgr::call_(ZuTestStep *step)
{
  start_();
  assert(m_context->scope);
  m_context->step = step;
}
