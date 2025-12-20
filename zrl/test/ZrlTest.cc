#include <zlib/Zrl.hh>

int main()
{
  while (auto s = Zrl::readline("ZrlTest] ")) {
    puts(s);
    ::free(s);
  }
  return 0;
}
