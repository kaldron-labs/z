#include <stdlib.h>

#include <zlib/ZuTest.hh>

// static nested sub-test in a function
void foo() {
  ZuTestScope(foo);
  ZuCheck(true);
  ZuCheck(true);
  {
    ZuTest(foo_to_the_power_of_N);
    ZuCheck(true);
  }
}

// run-time variable sub-test in a function
void baz() {
  ZuTestScopeRT(baz);
  ZuCheckRT(true);
}

// run-time variable sub-test calling sub-test
void bar(unsigned n) {
  ZuTestScopeRT(bar);
  for (unsigned i = 0; i < n; i++) ZuTestCallRT(baz);
}

int main()
{
  ZuTestMain();

  // bool harnessed = ::getenv("HARNESS_ACTIVE");

  ZuCheck(true);
  ZuCheck(true);

  { ZuTest(empty); }

  ZuTestCall(foo);

  ZuTestCall(bar, 3);

  {
    ZuTestRepeat(bah, 5);
    for (unsigned i = 0; i < 5; i++) {
      // if (harnessed) ::sleep(1);
      ZuCheck(true);
    }
  }
}
