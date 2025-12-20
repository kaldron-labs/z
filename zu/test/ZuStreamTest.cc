#include <iostream>
#include <sstream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuVStream.hh>
#include <zlib/ZuArray.hh>

struct A {
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const A &) {
    return s << "A";
  }
};

template <typename S>
void foo(S &s_) {
  ZuVStream s{s_};
  s << 42 << ' ' << A{} << " " << 42.0 << ' ' << ZuBoxed(42.0F) << "\n";
}

int main()
{
  ZuCArray<100> s1;
  std::stringstream s2;
  foo(s1);
  std::cout << s1;
  foo(s2);
  std::cout << s2.str();
  foo(std::cout);
  foo(std::cerr);
}
