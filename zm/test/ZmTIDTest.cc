#include <zlib/ZmPlatform.hh>
#include <zlib/ZuTime.hh>

#include <iostream>

inline void out(const char *s) { std::cout << s << '\n'; }

#define CHECK(x) ((x) ? out("OK  " #x) : out("NOK " #x))

int main()
{
  CHECK(Zm::getTID() == ::getpid());
}
