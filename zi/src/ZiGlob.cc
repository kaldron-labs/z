//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// file system pathname auto-completion and directory scanning

#include <zlib/ZiGlob.hh>

bool ZiGlob::init(Zi::Path prefix, ZeError *e)
{
  Zi::Path dirName = ZiFile::dirname(prefix);
  Zi::Path leafName = ZiFile::leafname(prefix);
  if (m_dirName != dirName) {
    m_leafName = {};
    if (m_dir) m_dir->close(); else m_dir = new ZiDir{};
    if (m_dir->open(dirName, e) != Zi::OK) {
      m_dir = nullptr;
      m_first = m_last = m_node = nullptr;
      return false;
    }
    m_dirName = ZuMv(dirName);
    if (m_entries) m_entries->clean(); else m_entries = new Entries{};
    Zi::Path name;
    while (m_dir->read(name) == Zi::OK) {
      bool isdir = ZiFile::isdir(ZiFile::append(dirName, name));
      m_entries->add(Entry{ZuMv(name), isdir});
    }
  }
  if (m_leafName != leafName) {
    m_leafName = ZuMv(leafName);
  }
  m_first = m_entries->findPtr<ZmRBTreeGreaterEqual>(m_leafName);
  if (!m_first || !match(m_first)) {
    m_first = m_last = m_node = nullptr;
    return true;
  }
  NodePtr prev = m_first;
  NodePtr next = m_entries->next(prev);
  while (next && match(next)) {
    prev = next;
    next = m_entries->next(prev);
  }
  m_last = prev;
  m_node = nullptr;
  return true;
}

void ZiGlob::final()
{
  m_first = m_last = m_node = nullptr;
  m_entries = nullptr;
  m_dir = nullptr;
  m_dirName = {};
  m_leafName = {};
}

const ZiGlob::Entry *ZiGlob::iterate(bool next, bool wrap) const
{
  if (next) {
    if (!m_node)
      m_node = m_first;
    else if (m_node != m_last)
      m_node = m_entries->next(m_node);
    else if (wrap)
      m_node = m_first;
    else
      m_node = nullptr;
  } else {
    if (!m_node)
      m_node = m_last;
    if (m_node != m_first)
      m_node = m_entries->prev(m_node);
    else if (wrap)
      m_node = m_last;
    else
      m_node = nullptr;
  }
  if (m_node) return &(m_node->val());
  return nullptr;
}

void ZiGlob::reset() const
{
  m_node = nullptr;
}

bool ZiGlob::match(NodePtr node)
{
  const Zi::Path &name = node->key();
  unsigned len = m_leafName.length();
  return name.length() >= len && !memcmp(name.data(), m_leafName.data(), len);
}
