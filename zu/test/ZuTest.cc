#include <stdlib.h>

#include <zlib/ZuTest.hh>

void foo() {
  ZuTestScope(foo);
  ZuCheck(true);
  ZuCheck(true);
  {
    ZuTest(foo_to_the_power_of_N);
    ZuCheck(true);
  }
}

void bar() {
  ZuTestScopeRT(bar);
  for (unsigned i = 0; i < 3; i++) ZuCheckRT(true);
}

int main()
{
  ZuTestMain();
  // bool harnessed = ::getenv("HARNESS_ACTIVE");
  ZuCheck(true);
  ZuCheck(true);
  { ZuTest(empty); }
  ZuTestCall(foo);
  ZuTestCall(bar);
  {
    ZuTestRepeat(baz, 5);
    for (unsigned i = 0; i < 5; i++) {
      // if (harnessed) ::sleep(1);
      ZuCheck(true);
    }
  }
}
