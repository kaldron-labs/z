#include <iostream>
#include <sstream>

#include <zlib/ZuBox.hh>
#include <zlib/ZuStream.hh>
#include <zlib/ZuVStream.hh>
#include <zlib/ZuArray.hh>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

struct A {
  template <typename S>
  friend inline decltype(auto) operator <<(S &s, const A &) {
    return s << "A";
  }
};

template <typename S>
void test(S &s) {
  s << 42 << ' ' << A{} << L" " << 42.42 << ' ' << ZuBoxed(42.0F) << "\n";
}

template <typename S>
void vtest(S &s_) {
  ZuVStream s{s_};
  test(s);
}

template <typename Char>
void stest() {
  Char buf[80];
  ZuSpan<Char> s{&buf[0], sizeof(buf)};
  ZuStream_<Char> s_(s);
  s_ << L"hello " << "world" << L'!' << ' ' << 42.42;
  s.trunc(s_.data() - s.data());
  if constexpr (sizeof(Char) == 1) {
    CHECK(s == "hello world! 42.42");
  } else {
    CHECK(s == L"hello world! 42.42");
  }
}

int main()
{
  {
    ZuCArray<80> s1;
    std::stringstream s2;
    vtest(s1);
    vtest(s2);
    CHECK(s1 == s2.str());
    ZuArray<wchar_t, 80> w1;
    test(w1);
    s1 = w1;
    CHECK(s1 == s2.str());
  }
  vtest(std::cout);
  {
    stest<char>();
    stest<wchar_t>();
  }
}
