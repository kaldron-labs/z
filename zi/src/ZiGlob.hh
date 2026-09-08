//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// file system pathname auto-completion and directory scanning
//
// not a full glob-expression processor, this performs path
// completion for CLIs (typically via TAB or ^D)

#ifndef ZiGlob_HH
#define ZiGlob_HH

#ifndef ZiLib_HH
#include <zlib/ZiLib.hh>
#endif

#include <zlib/ZuDerive.hh>
#include <zlib/ZuPtr.hh>
#include <zlib/ZuSpan.hh>

#include <zlib/ZtString.hh>

#include <zlib/ZmNoLock.hh>
#include <zlib/ZmRBTree.hh>

#include <zlib/ZiFile.hh>
#include <zlib/ZiDir.hh>

class ZiAPI ZiGlob {
  ZiGlob(ZiGlob &) = delete;
  ZiGlob &operator =(ZiGlob &) = delete;
  ZiGlob(ZiGlob &&) = delete;
  ZiGlob &operator =(ZiGlob &&) = delete;

  struct Entry {
    Zi::Path	name;
    bool	isdir = false;

    static const Zi::Path &NameAxor(const Entry &entry) { return entry.name; }
  };
  ZmRBTreeDerive(Entries, Entry, ZmRBTreeKey<Entry::NameAxor>);
  using Iter = decltype(ZuDeclVal<const Entries &>().citer());

public:
  ZiGlob() = default;

  bool init(Zi::Path prefix, ZeError *e = nullptr);
  void final();

  const Zi::Path &dirName() const { return m_dirName; }
  const Zi::Path &leafName() const { return m_leafName; }

  const Entry *iterate(bool next, bool wrap) const;

  void reset() const;

private:
  using NodePtr = Entries::Node *;

  bool match(NodePtr);

  Zi::Path			m_dirName;
  Zi::Path			m_leafName;
  ZuPtr<ZiDir>			m_dir;
  ZuPtr<Entries>		m_entries;
  NodePtr			m_first, m_last;
  mutable NodePtr		m_node;
};

#endif /* ZiGlob_HH */
