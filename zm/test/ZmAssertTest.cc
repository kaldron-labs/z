#include <zlib/ZuTestUtil.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmAssert.hh>

using namespace ZuTestUtil;

ZmAtomic<int> baz = 42;

int foo() { return baz; }
int bar() { return ++baz; }

int main(int argc, char **argv)
{
  parse(argc, argv);
  ZuTestMain();
  ZuCheck(foo() == bar() - 1);
  ZmAssert(foo() == bar() - 1);
}
