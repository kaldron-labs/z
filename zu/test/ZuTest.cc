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

int main()
{
  ZuTestMain();
  // bool harnessed = ::getenv("HARNESS_ACTIVE");
  ZuCheck(true);
  ZuCheck(true);
  ZuTestCall(foo);
  { ZuTest(bar__); }
  {
    ZuTestRepeat(baz, 5);
    for (unsigned i = 0; i < 5; i++) {
      // if (harnessed) ::sleep(1);
      ZuCheck(true);
    }
  }
}
