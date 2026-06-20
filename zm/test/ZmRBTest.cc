//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// red/black tree test program

#include <stdlib.h>
#include <time.h>

#include <zlib/ZuTestUtil.hh>
#include <zlib/ZuDerive.hh>
#include <zlib/ZuCmp.hh>

#include <zlib/ZmRef.hh>
#include <zlib/ZmObject.hh>
#include <zlib/ZmRBTree.hh>
#include <zlib/ZmNoLock.hh>
#include <zlib/ZmAssert.hh>

using namespace ZuTestUtil;

struct Z : public ZmObject {
  Z(int z) : m_z(z) { }

  struct Traits : public ZuBaseTraits<Z> {
    enum { IsReal = 0, IsPrimitive = 0, IsPOD = 1 };
  };
  friend Traits ZuTraitsType(Z *);

  int m_z;
};

template <typename>
struct ZCmp {
  static int cmp(Z *z1, Z *z2) {
    return z1->m_z - z2->m_z;
  }
  static ZmRef<Z> null() { 
    static ZmRef<Z> tmp = new Z(0);
    return tmp; 
  }
};

ZuDerive(Tree, (ZmRBTree<ZmRef<Z>, ZmRBTreeCmp<ZCmp> >));

static void delptr(Tree *tree, Z *z) {
  tree->del(z);
#if 0
  auto iter = tree->iter<ZmRBTreeEqual>(z);
  Tree::NodeRef node;

  while (node = iter()) if (z == node->key()) { iter.del(); return; }
#endif
}

static bool verify(Tree &tree)
{
  auto iter = tree.iter();
  Tree::NodeRef node;
  unsigned count = 0;
  int prev = 0;
  bool first = true;

  while (node = iter()) {
    int v = node->key()->m_z;
    if (!first && prev > v) return false;
    prev = v;
    first = false;
    ++count;
  }

  if (count != tree.count_()) return false;
  if (!count) return true;

  auto minNode = tree.minimum();
  auto maxNode = tree.maximum();

  return minNode && maxNode && minNode->key()->m_z <= maxNode->key()->m_z;
}

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  Tree tree;
  log("sizeof(Tree::Node)=", sizeof(Tree::Node));
  ZmRef<Z> z;
  int i;

  for (i = 0; i < 20; i++) tree.add(ZmRef<Z>(new Z(i)));
  ZuCheck(tree.count_() == 20);
  ZuCheck(verify(tree));

  if (verbose) std::cerr << "0 to 19: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  for (i = 0; i < 20; i += 2) { ZmRef<Z> z = new Z(i); tree.del(z); }
  ZuCheck(tree.count_() == 10);
  ZuCheck(verify(tree));

  if (verbose) std::cerr << "17 to 1, odd: ";
  {
    auto iter = tree.iter<ZmRBTreeLess>(tree.maximumKey());
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  i = 6;

  if (verbose) std::cerr << "7 to 19, odd: ";
  {
    ZmRef<Z> iz = new Z(i);
    auto iter = tree.iter<ZmRBTreeGreater>(iz);
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  i = 7;

  if (verbose) std::cerr << "1 to 7, odd: ";
  {
    ZmRef<Z> iz = new Z(i);
    auto iter = tree.iter<ZmRBTreeLessEqual>(iz);
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i++) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #1: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; --i >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #1: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i++) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #2: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; --i >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #2: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i += 2) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 1; i < 20; i += 2) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #3: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; (i -= 2) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 41; (i -= 2) >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #3: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i += 2) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 1; i < 20; i += 2) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #4: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; (i -= 2) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 41; (i -= 2) >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #4: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 1; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 2; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #5: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 40; i++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 41; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 42; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #5: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 0; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 1; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 2; i < 20; i += 3) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "20 to 39 #6: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 40; --i >= 0;) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 40; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 41; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 42; (i -= 3) >= 20;) tree.del(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19 #6: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  tree.clean();
  ZuCheck(tree.count_() == 0);

  {
    ZmRef<Z> zarray[40];
    int j;

    for (i = 0; i < 40; i++) { j = i>>2; tree.add(zarray[i] = new Z(j)); }

    if (verbose) std::cerr << "0 to 9 with 4 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 0; i < 10; i++) delptr(&tree, zarray[i<<2]);

    if (verbose) std::cerr << "0 to 9 with 3 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 0; i < 10; i++) delptr(&tree, zarray[(i<<2) + 1]);

    if (verbose) std::cerr << "0 to 9 with 2 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 0; i < 10; i++) delptr(&tree, zarray[(i<<2) + 2]);

    if (verbose) std::cerr << "0 to 9 with 1 duplicate: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 0; i < 10; i++) delptr(&tree, zarray[(i<<2) + 3]);

    if (verbose) std::cerr << "empty: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';
  }

  {
    ZmRef<Z> zarray[40];
    int j;

    for (i = 0; i < 40; i++) { j = i>>2; tree.add(zarray[i] = new Z(j)); }

    if (verbose) std::cerr << "0 to 9 with 4 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 0; i < 10; i += 2)
      for (j = 0; j < 4; j++)
	tree.del(ZmRef<Z>(new Z(i)));
    for (i = 1; i < 10; i += 2) delptr(&tree, zarray[(i<<2)]);

    if (verbose) std::cerr << "0 to 9, odd, with 3 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    i = 4;

    if (verbose) std::cerr << "5 to 9, odd, with 3 duplicates: ";
    {
      ZmRef<Z> iz = new Z(i);
      auto iter = tree.iter<ZmRBTreeGreater>(iz);
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 1; i < 10; i += 2) delptr(&tree, zarray[(i<<2) + 1]);

    if (verbose) std::cerr << "0 to 9, odd, with 2 duplicates: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 1; i < 10; i += 2) delptr(&tree, zarray[(i<<2) + 2]);

    if (verbose) std::cerr << "0 to 9, odd, with 1 duplicate: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';

    for (i = 1; i < 10; i += 2) delptr(&tree, zarray[(i<<2) + 3]);

    if (verbose) std::cerr << "empty: ";
    {
      auto iter = tree.iter();
      Tree::NodeRef node;

      while (node = iter())
	if (verbose) std::cerr << node->key()->m_z << ' ';
    }
    if (verbose) std::cerr << '\n';
  }

  int j;

  for (i = 0, j = 1; i < 100; i += j, j++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 2, j = 1; i < 100; i += j, j += 2) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 4, j = 1; i < 100; i += j, j += 3) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 6, j = 1; i < 100; i += j, j += 4) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 10, j = 1; i < 100; i += j, j += 5) tree.add(ZmRef<Z>(new Z(i)));

  i = 1;

  {
    ZmRef<Z> iz = new Z(i);
    Tree::NodeRef node = tree.find(iz);

    if (node) z = node->key(); else z = 0;
  }
  ZuCheck(z && z->m_z == 1);

  for (i = 0, j = 1; i < 100; i += j, j++) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 2, j = 1; i < 100; i += j, j += 2) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 4, j = 1; i < 100; i += j, j += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 6, j = 1; i < 100; i += j, j += 4) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 10, j = 1; i < 100; i += j, j += 5) tree.del(ZmRef<Z>(new Z(i)));

  ZuCheck(tree.count_() == 0);

  for (i = 0, j = 1; i < 100; i += j, j++) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 2, j = 1; i < 100; i += j, j += 2) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 4, j = 1; i < 100; i += j, j += 3) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 6, j = 1; i < 100; i += j, j += 4) tree.add(ZmRef<Z>(new Z(i)));
  for (i = 10, j = 1; i < 100; i += j, j += 5) tree.add(ZmRef<Z>(new Z(i)));

  for (i = 10, j = 1; i < 100; i += j, j += 5) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 6, j = 1; i < 100; i += j, j += 4) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 4, j = 1; i < 100; i += j, j += 3) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 2, j = 1; i < 100; i += j, j += 2) tree.del(ZmRef<Z>(new Z(i)));
  for (i = 0, j = 1; i < 100; i += j, j++) tree.del(ZmRef<Z>(new Z(i)));

  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 20; i++) tree.add(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19, deleting all elements: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter()) {
      if (verbose) std::cerr << node->key()->m_z << ' ';
      iter.del(node);
    }
  }
  if (verbose) std::cerr << '\n';

  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 20; i++) tree.add(ZmRef<Z>(new Z(i)));

  if (verbose) std::cerr << "0 to 19, deleting odd elements: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter()) {
      z = node->key();
      if (verbose) std::cerr << z->m_z << ' ';
      if (z->m_z & 1) iter.del(node);
    }
  }
  if (verbose) std::cerr << '\n';

  if (verbose) std::cerr << "0 to 18, even: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 60; i++) { int j = i / 3; tree.add(ZmRef<Z>(new Z(j))); }

  if (verbose) std::cerr << "0 to 19 with 3 duplicates, deleting every fourth element: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;
    int j = 0;

    while (node = iter()) {
      if (verbose) std::cerr << node->key()->m_z << ' ';
      if (!(j & 3)) iter.del(node);
      j++;
    }
  }
  if (verbose) std::cerr << '\n';

  i = 20;

  if (verbose) std::cerr << "0 to 19 reverse order, remaining duplicates: ";
  {
    ZmRef<Z> iz = new Z(i);
    auto iter = tree.iter<ZmRBTreeLess>(iz);
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  tree.clean();
  ZuCheck(tree.count_() == 0);

  for (i = 0; i < 60; i++) { int j = i / 3; tree.add(ZmRef<Z>(new Z(j))); }

  i = 20;

  if (verbose) std::cerr << "0 to 19 with 3 duplicates reverse order, deleting every fourth element: ";
  {
    ZmRef<Z> iz = new Z(i);
    auto iter = tree.iter<ZmRBTreeLess>(iz);
    Tree::NodeRef node;
    int j = 0;

    while (node = iter()) {
      if (verbose) std::cerr << node->key()->m_z << ' ';
      if (!(j & 3)) iter.del(node);
      j++;
    }
  }
  if (verbose) std::cerr << '\n';

  if (verbose) std::cerr << "0 to 19, remaining duplicates: ";
  {
    auto iter = tree.iter();
    Tree::NodeRef node;

    while (node = iter())
      if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  log("min: ", tree.minimum()->key()->m_z, ", max: ", tree.maximum()->key()->m_z);

  tree.clean();
  ZuCheck(tree.count_() == 0);

  if (verbose) std::cerr << "empty tree: ";
  {
    auto i = tree.iter();
    while (auto node = i()) if (verbose) std::cerr << node->key()->m_z << ' ';
  }
  if (verbose) std::cerr << '\n';

  {
    ZmRBTree<uint64_t> tree2;
    uint64_t add[] = {
      0x7fd2c4296790, 0x7fd2c4296800, 0x7fd2c4296870, 0x7fd2c42a2f80,
      0x7fd2c42975d0, 0x7fd2c4297640, 0x7fd2c429a870, 0x7fd2c42a2490,
      0x7fd2c42a2500, 0x7fd2c42a2570, 0x7fd2c4295a00, 0x7fd230005610,
      0x7fd2300056b0, 0x7fd230005c70, 0x7fd230005d70
    };
    uint64_t del[] = {
      0x7fd2c4296870, 0x7fd2c4296800, 0x7fd2c4296790, 0x7fd2c4297640,
      0x7fd2c42975d0, 0x7fd2c42a2f80, 0x7fd2c42a2500, 0x7fd2c42a2490,
      0x7fd2c429a870, 0x7fd2c4295a00, 0x7fd2c42a2570, 0x7fd230005610
    };
    for (unsigned i = 0; i < 15; i++) tree2.add(add[i]);
    bool deleted = true;
    for (unsigned i = 0; i < 12; i++) deleted &= bool(tree2.del(del[i]));
    ZuCheck(deleted);
    ZuCheck(tree2.count_() == 3);
  }
}
