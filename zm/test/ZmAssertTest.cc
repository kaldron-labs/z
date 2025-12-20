#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAssert.hh>

#include <iostream>

ZmAtomic<int> baz = 42;

int foo() { return baz; }
int bar() { return ++baz; }

int main()
{
  ZmAssert(foo() == bar() - 1);
  ZmAssert(foo() != bar() - 1);
}
