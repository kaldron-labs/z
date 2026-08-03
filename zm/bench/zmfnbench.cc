//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZmRef.hh>
#include <zlib/ZmFn.hh>
#include <zlib/ZmTime.hh>
#include <zlib/ZmAtomic.hh>

struct Base {
  Base(ZmAtomic<uint64_t> &i_) : i(i_) { }
  void foo_() { i.xch(i.load_() + 1); }
  void foo() { foo_(); }
  virtual void bar() { }
  ZmAtomic<uint64_t> &i;
};

struct Derived : public Base {
  Derived(ZmAtomic<uint64_t> &i) : Base(i) { }
  void bar() { Base::foo_(); }
};

void usage()
{
  std::cerr <<
    "Usage: zmfnbench [N]\n\n"
    "Options:\n"
    "  N\tnumber of iterations\n";
  ::exit(1);
}

int main(int argc, char **argv)
{
  if (argc < 1 || argc > 2) usage();
  ZuBox<unsigned> n = 1000000;
  if (argc == 2) {
    n = ZuBox<unsigned>{argv[1]};
    if (!*n) usage();
  }

  ZmAtomic<uint64_t> i;
  {
    Derived d(i);
    ZuTime begin = Zm::now();
    for (unsigned i = 0; i < n; i++) d.foo();
    ZuTime end = Zm::now(); end -= begin;
    puts((ZuCArray<80>{} << "direct call:\t" << end.interval() <<
	"\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
  }
  {
    Derived d(i);
    ZmFn<> bar = ZmFn<>{&d, ZmFnPtr<&Base::foo>{}};
    ZuTime begin = Zm::now();
    for (unsigned i = 0; i < n; i++) bar();
    ZuTime end = Zm::now(); end -= begin;
    puts((ZuCArray<80>{} << "castFn:\t\t" << end.interval() <<
	"\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
  }
  {
    Derived d(i);
    ZmFn<> baz = ZmFn<>(&d, [](Derived *d_) { d_->foo(); });
    ZuTime begin = Zm::now();
    for (unsigned i = 0; i < n; i++) baz();
    ZuTime end = Zm::now(); end -= begin;
    puts((ZuCArray<80>{} << "fast lambdaFn:\t" << end.interval() <<
      "\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
  }
  {
    Derived d(i);
    ZmFn<> baz = ZmFn<>([&d]() { d.foo(); });
    ZuTime begin = Zm::now();
    for (unsigned i = 0; i < n; i++) baz();
    ZuTime end = Zm::now(); end -= begin;
    puts((ZuCArray<80>{} << "slow lambdaFn:\t" << end.interval() <<
      "\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
  }
  {
    Derived d(i);
    Base *b = &d;
    ZuTime begin = Zm::now();
    for (unsigned i = 0; i < n; i++) b->bar();
    ZuTime end = Zm::now(); end -= begin;
    puts((ZuCArray<80>{} << "virtual fn:\t" << end.interval() <<
      "\t(" << ZuBox<uint64_t>(d.i) << ")").terminate());
  }
}
