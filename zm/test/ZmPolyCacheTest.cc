#include <zlib/ZuDerive.hh>
#include <zlib/ZuDemangle.hh>
#include <zlib/ZuPrint.hh>

#include <zlib/ZmPolyCache.hh>

#include <iostream>

struct Foo_ {
  int i, j, k, l;

  template <typename S> void print(S &s) const {
    s << '{' << i << ',' << j << ',' << k << ',' << l << '}';
  }
  friend ZuPrintFn ZuPrintType(Foo_ *);
};

ZuStruct(Foo_,
    ((i), (Keys<0>)),
    ((j), ((Keys<1, 2>))),
    ((k), ((Keys<0, 1>))),
    ((l), (Keys<3>)));

ZuDerive(Cache, (ZmPolyCache<Foo_>));
using Foo = Cache::Node;

int main()
{
  Cache cache("test");
  cache.add(new Foo{1,2,3,4});
  cache.add(new Foo{2,3,4,5});
  cache.add(new Foo{3,4,5,6});
  cache.add(new Foo{5,5,5,5});
  {
    auto x = cache.del<0>(ZuFwdTuple(1,3));
    std::cout << ZuDemangle<decltype(x)>{} << '\n';
    std::cout << ZuBoxPtr(static_cast<Foo *>(x)).hex() << '\n';
    std::cout << *x << '\n';
  }
  {
    std::cout << "iteration:\n";
    cache.allSync([](auto node, auto wake) {
      std::cout << *node << '\n';
      wake();
    });
  }
  {
    auto x = cache.find<0>(ZuFwdTuple(2,4));
    std::cout << "find<0>({2,4}): " << *x << '\n';
    x = cache.find<1>(ZuFwdTuple(3,4));
    std::cout << "find<1>({3,4}): " << *x << '\n';
    x = cache.find<2>(ZuFwdTuple(3));
    std::cout << "find<2>({3}): " << *x << '\n';
    x = cache.find<3>(ZuFwdTuple(5));
    std::cout << "find<3>({5}): " << *x << '\n';
  }
}
