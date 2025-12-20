#include <stdlib.h>

#include <iostream>

#include <zlib/ZuPtr.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

struct A {
  int *x;
  A(int *x_) : x{x_} { ++*x; }
  ~A() { ++*x; }
};

void foo(A *a) {
  CHECK(*(a->x) == 1);
}

int main()
{
  int i = 0;
  {
    ZuPtr<A> a = new A{&i};
    foo(ZuMv(a));
  }
  CHECK(i == 2);
}
