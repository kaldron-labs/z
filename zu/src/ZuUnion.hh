//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic discriminated union; different design trade-offs than std::variant
// - consteval eligible
// - void, primitive and pointer types in addition to composite types
// - trailing discriminator
// - never throws exceptions
// - N - number of types
// - p<I>() - positional accessor
// - p<I>(v) - positional set function
// - p<T>() - access by type
// - type() - return index of current type
// - Types - type list (ZuTypeList)
// - dispatch(lambda) - dispatcher
// - cdispatch(lambda) - const dispatcher
// - operator <=>() - comparison

// ZuUnion<void, T> can be used instead of std::optional<T>
// - ensures at most one trailing byte of overhead

// using U = ZuUnion<int, double>;
// U u, v;
// *(u.init<int>()) = 42;
// u.p<0>(42);
// u.p<0>() = 42;
// v = u;
// if (v.type() == 0) { printf("%d\n", v.p<0>()); }
// v.p<1>(42.0);
// if (v.is<double>()) { printf("%g\n", v.p<double>()); }
// u.~U();
// *reinterpret_cast<int *>(&u) = 43; // *(u.ptr_<Index<int>::I>()) = ...
// u.type_(U::Index<int>::I);
// printf("%d\n", u.p<int>());
//
// namespace {
//   void print(int i) const { printf("%d\n", i); }
//   void print(double d) const { printf("%g\n", d); }
// };
// u.cdispatch([]<typename I>(I &&i) { print(ZuFwd<I>(i)); });
// ZuSwitch::dispatch<U::N>(u.type(), [&u](auto I) { print(u.p<I>()); });

#ifndef ZuUnion_HH
#define ZuUnion_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <zlib/ZuTraits.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuLargest.hh>
#include <zlib/ZuMostAligned.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuSwitch.hh>

namespace Zu_ {

template <typename ...Ts> class Union;

namespace Union_ { // internal

  // consteval requires a C union rather than a reinterpreted buffer... bah
  template <typename ...Ts> struct Data;
  template <> struct Data<> { };
  template <typename T0> struct Data<T0> {
    ZuInline constexpr Data() noexcept { }
    ZuInline constexpr ~Data() noexcept { }
    union { T0 l; };
  };
  template <> struct Data<void> {
    ZuInline constexpr Data() noexcept { }
    ZuInline constexpr ~Data() noexcept { }
    union { ZuVoid l; };
  };
  template <typename T0, typename ...Ts>
  struct Data<T0, Ts...> {
    ZuInline constexpr Data() noexcept { }
    ZuInline constexpr ~Data() noexcept { }
    union { T0 l; Data<Ts...> r; };
  };
  template <typename ...Ts>
  struct Data<void, Ts...> {
    ZuInline constexpr Data() noexcept { }
    ZuInline constexpr ~Data() noexcept { }
    union { ZuVoid l; Data<Ts...> r; };
  };

  // the vast majority of use cases involve fewer than 8 types
  // - the compiler elides the recursive calls to p_(), but the debugging
  //   experience is improved by moderately unrolling the recursion
  template <unsigned N, typename Data>
  constexpr auto &&p_(Data &&data) noexcept {
    if constexpr (!N) return ZuFwd<Data>(data).l;
    else if constexpr (N == 1) return ZuFwd<Data>(data).r.l;
    else if constexpr (N == 2) return ZuFwd<Data>(data).r.r.l;
    else if constexpr (N == 3) return ZuFwd<Data>(data).r.r.r.l;
    else if constexpr (N == 4) return ZuFwd<Data>(data).r.r.r.r.l;
    else if constexpr (N == 5) return ZuFwd<Data>(data).r.r.r.r.r.l;
    else if constexpr (N == 6) return ZuFwd<Data>(data).r.r.r.r.r.r.l;
    else if constexpr (N == 7) return ZuFwd<Data>(data).r.r.r.r.r.r.r.l;
    else return p_<N - 8>(ZuFwd<Data>(data).r.r.r.r.r.r.r.r);
  }

  template <typename> struct OpBool;
  template <> struct OpBool<bool> { using T = void; };
  template <typename T, typename = void> struct OpStar {
    ZuInline static constexpr bool star(const T &p) {
      return !ZuNull(p);
    }
  };
  template <typename T>
  struct OpStar<T, typename OpBool<decltype(*(ZuDeclVal<const T &>()))>::T> {
    ZuInline static constexpr bool star(const T &p) { return *p; }
  };
  template <typename T, typename = void> struct OpBang {
    ZuInline static constexpr bool bang(const T &p) { return !p; }
  };
  template <typename T>
  struct OpBang<T, typename OpBool<decltype(!(ZuDeclVal<const T &>()))>::T> {
    ZuInline static constexpr bool bang(const T &p) {
      return ZuNull(p);
    }
  };

  template <typename T, bool IsPrimitive, bool IsPointer> class Ops_;
  template <typename T> class Ops_<T, 0, 0> :
    public OpStar<T>, public OpBang<T> {
  public:
    template <typename ...Args>
    ZuInline static constexpr void ctor(T *p, Args &&...args) {
      if (ZuConstEval())
	ZuNew<T>(p, ZuFwd<Args>(args)...);
      else
	new (p) T(ZuFwd<Args>(args)...);
    }
    ZuInline static constexpr void dtor(T *p) { p->T::~T(); }
  };
  template <typename T> class Ops_<T, 1, 0> {
  public:
    ZuInline static constexpr void ctor(T *p) { *p = ZuCmp<T>::null(); }
    template <typename V>
    ZuInline static constexpr void ctor(T *p, V &&v) { *p = ZuFwd<V>(v); }
    ZuInline static constexpr void dtor(T *p) { }
    ZuInline static constexpr bool star(const T &p) {
      return !ZuNull(p);
    }
    ZuInline static constexpr bool bang(const T &p) { return !p; }
  };
  template <typename T> class Ops_<T, 1, 1> {
  public:
    ZuInline static constexpr void ctor(T *p) { *p = nullptr; }
    template <typename V>
    ZuInline static constexpr void ctor(T *p, V &&v) { *p = ZuFwd<V>(v); }
    ZuInline static constexpr void dtor(T *p) { }
    ZuInline static constexpr bool star(const T &p) { return bool(p); }
    ZuInline static constexpr bool bang(const T &p) { return !p; }
  };
  template <typename T>
  struct Ops :
      public Ops_<T,
	ZuTraits<T>::IsPrimitive,
	ZuTraits<T>::IsPrimitive && ZuTraits<T>::IsPointer> {
    template <typename V>
    ZuInline static constexpr bool less(const T &p, const V &v) {
      return ZuCmp<T>::less(p, v);
    }
    template <typename V>
    ZuInline static constexpr bool equals(const T &p, const V &v) {
      return ZuEquals(p, v);
    }
    template <typename V>
    ZuInline static constexpr int cmp(const T &p, const V &v) {
      return ZuCompare(p, v);
    }
    ZuInline static uint32_t hash(const T &p) {
      return ZuHash<T>::hash(p);
    }
  };

  template <typename Base, typename ...Ts> struct Traits;
  template <typename Base, typename T0>
  struct Traits<Base, T0> : public Base {
    enum { IsPOD = ZuTraits<T0>::IsPOD };
  };
  template <typename Base, typename T0, typename ...Ts>
  struct Traits<Base, T0, Ts...> : public Base {
  private:
    using Left = ZuTraits<T0>;
    using Right = Traits<Base, Ts...>;
  public:
    enum { IsPOD = Left::IsPOD && Right::IsPOD };
  };

  template <typename T> struct IsVoid_ : public ZuFalse { };
  template <> struct IsVoid_<void> : public ZuTrue { };
  template <> struct IsVoid_<ZuVoid> : public ZuTrue { };
  template <typename T> using IsVoid = IsVoid_<ZuDecay<T>>;
  template <typename T> using NotVoid = ZuBool<!IsVoid_<ZuDecay<T>>{}>;

// recursive decay
  struct RDecayer {
    template <typename> struct Decay;
    template <typename ...Ts>
    struct Decay<Union<Ts...>> {
      using T = ZuTypeApply<Union, ZuTypeMap<ZuRDecay, Ts...>>;
    };
  };

// test for move/copy constructibility/assignability
  template <typename T, typename = void>
  struct CanMvConstruct_ : public ZuFalse { };
  template <typename T>
  struct CanMvConstruct_<T, decltype(T(ZuDeclVal<T &&>()), void())> :
    public ZuTrue { };
  template <typename T> struct CanMvConstruct : public CanMvConstruct_<T> { };
  template <> struct CanMvConstruct<void> : public ZuTrue { };

  template <typename T, typename = void>
  struct CanCpConstruct_ : public ZuFalse { };
  template <typename T>
  struct CanCpConstruct_<T, decltype(T(ZuDeclVal<const T &>()), void())> :
    public ZuTrue { };
  template <typename T> struct CanCpConstruct : public CanCpConstruct_<T> { };
  template <> struct CanCpConstruct<void> : public ZuTrue { };

  template <typename V> struct Construct {
    template <typename T>
    using Can = ZuIf<ZuIsLRef<V>{}, CanCpConstruct<T>, CanMvConstruct<T>>;
  };

  template <typename T, typename = void>
  struct CanMvAssign_ : public ZuFalse { };
  template <typename T>
  struct CanMvAssign_<
    T, decltype((ZuDeclVal<T &>() = ZuDeclVal<T &&>()), void())> :
      public ZuTrue { };
  template <typename T> struct CanMvAssign : public CanMvAssign_<T> { };
  template <> struct CanMvAssign<void> : public ZuTrue { };

  template <typename T, typename = void>
  struct CanCpAssign_ : public ZuFalse { };
  template <typename T>
  struct CanCpAssign_<
    T, decltype((ZuDeclVal<T &>() = ZuDeclVal<const T &>()), void())> :
      public ZuTrue { };
  template <typename T> struct CanCpAssign : public CanCpAssign_<T> { };
  template <> struct CanCpAssign<void> : public ZuTrue { };

  template <typename V> struct Assign {
    template <typename T>
    using Can = ZuIf<ZuIsLRef<V>{}, CanCpAssign<T>, CanMvAssign<T>>;
  };

// versions of ZuNXMove/ZuNxCopy that work with And
  template <typename T> using NXMove = ZuNXMove<T>;
  template <typename T> using NXCopy = ZuNXCopy<T>;

  template <template <typename> class Fn> struct And {
    template <typename TL> struct Reduce_;
    template <typename ...Ts> struct Reduce_<ZuTypeList<Ts...>> {
      using T = ZuBool<(...&& bool(Fn<Ts>{}))>;
    };
    template <typename TL> using Reduce = typename Reduce_<TL>::T;
  };

  // evaluate precise index and result type of u.p<V>()
  template <typename U, typename V>
  struct Result {
    using U_ = ZuDecay<U>;
    using I = typename U_::template Index<V>;
    using T = decltype(ZuDeclVal<U>().template p<I{}>());
  };

  // evaluate lambda return type
  template <typename U, typename L>
  struct Eval__ {
    using U_ = ZuDecay<U>;
    template <typename V> using Result_ = Result<U, V>;
    template <typename V> using Index = typename Result_<V>::I;
    template <typename V> using Type_ = typename Result_<V>::T;
    template <typename V>
    using Type = decltype(ZuDeclVal<L>()(Index<V>{}, ZuDeclVal<Type_<V>>()));
    using Types = ZuTypeMap<Type, ZuTypeGrep<NotVoid, typename U_::Types>>;
  };
  template <typename U, typename L, typename Types = typename Eval__<U, L>::Types>
  struct Eval_ { using T = ZuDecay<ZuType<0, Types>>; };
  template <typename U, typename L>
  struct Eval_<U, L, ZuTypeList<>> { using T = void; };
  template <typename U, typename L>
  using Eval = typename Eval_<U, L>::T;

} // Union_

template <typename ...Ts> class Union {
public:
  using Data = Union_::Data<Ts...>;
  using Types = ZuTypeList<Ts...>;
  using Largest = ZuLargest<Ts...>;
  enum { Size = ZuSize<Largest>{} };
  enum { N = sizeof...(Ts) };
  template <unsigned I> using Type = ZuType<I, Types>;
  template <unsigned I> using Type_ = ZuDecay<Type<I>>;
  template <typename T> using In = ZuTypeIn<T, Types>;
  template <typename T>
  using In_ = ZuTypeIn<ZuDecay<T>, ZuTypeMap<ZuDecay, Types>>;
  template <typename T> using Index = ZuTypeIndex<T, Types>;
  template <typename T>
  using Index_ = ZuTypeIndex<ZuDecay<T>, ZuTypeMap<ZuDecay, Types>>;

  constexpr Union() noexcept(bool(ZuNXConstruct<Type<0>>{})) {
    using namespace Union_;
    using T0 = Type<0>;
    m_type = 0;
    if constexpr (!IsVoid<T0>{}) Ops<T0>::ctor(ZuAddr(p_<0>(m_u)));
  }

  constexpr ~Union() noexcept((...&& bool(ZuNXDestroy<Ts>{}))) {
    ZuSwitch::dispatch<N>(m_type, [this](auto I) {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{}) Ops<T>::dtor(ZuAddr(p_<I>(m_u)));
    });
  }

  constexpr Union(const Union &u)
    noexcept(typename Union_::And<Union_::NXCopy>::template Reduce<Types>{}) :
    m_type{u.m_type}
  {
    using namespace Union_;
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	Ops<T>::ctor(ZuAddr(p_<I>(m_u)), p_<I>(u.m_u));
    });
  }
  constexpr Union &operator =(const Union &u)
    noexcept(typename Union_::And<Union_::NXCopy>::template Reduce<Types>{})
  {
    if (this == &u) return *this;
    if (m_type != u.m_type) {
      this->~Union();
      if (ZuConstEval())
	ZuNew<Union>(this, u);
      else
	new (this) Union(u);
      return *this;
    }
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{}) {
	if constexpr (CanCpAssign<T>{})
	  p_<I>(m_u) = p_<I>(u.m_u);
	else {
	  Ops<T>::dtor(ZuAddr(p_<I>(m_u)));
	  Ops<T>::ctor(ZuAddr(p_<I>(m_u)), p_<I>(u.m_u));
	}
      }
    });
    return *this;
  }

  constexpr Union(Union &&u)
    noexcept(typename Union_::And<Union_::NXMove>::template Reduce<Types>{}) :
    m_type{u.m_type} {
    using namespace Union_;
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	Ops<T>::ctor(ZuAddr(p_<I>(m_u)), ZuMv(p_<I>(ZuMv(u.m_u))));
    });
  }
  constexpr Union &operator =(Union &&u)
    noexcept(typename Union_::And<Union_::NXMove>::template Reduce<Types>{})
  {
    if (m_type != u.m_type) {
      this->~Union();
      if (ZuConstEval())
	ZuNew<Union>(this, ZuMv(u));
      else
	new (this) Union(ZuMv(u));
      return *this;
    }
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{}) {
	if constexpr (CanMvAssign<T>{})
	  p_<I>(m_u) = ZuMv(p_<I>(ZuMv(u.m_u)));
	else if (this != &u) {
	  Ops<T>::dtor(ZuAddr(p_<I>(m_u)));
	  Ops<T>::ctor(ZuAddr(p_<I>(m_u)), ZuMv(p_<I>(ZuMv(u.m_u))));
	}
      }
    });
    return *this;
  }

  // from a Union-derived type
  template <
    typename V,
    typename Vs = ZuTypeList<Ts...>,
    ZuIfT<bool(ZuIs_<Union, ZuDecay<V>>{}) && bool(typename Union_::And<
      Union_::Construct<V &&>::template Can>::template Reduce<Vs>{}), int> = 0,
    bool NoExcept = typename Union_::And<
      Union_::NXMove>::template Reduce<Vs>{}>
  constexpr Union(V &&u) noexcept(NoExcept) : m_type{u.m_type} {
    using namespace Union_;
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	Ops<T>::ctor(
	  ZuAddr(p_<I>(m_u)), ZuFwdLike<V &&>(p_<I>(ZuFwdLike<V &&>(u.m_u))));
    });
  }
  template <
    typename V,
    typename Vs = ZuTypeList<Ts...>,
    ZuIfT<bool(ZuIs_<Union, ZuDecay<V>>{}) && bool(typename Union_::And<
      Union_::Assign<V &&>::template Can>::template Reduce<Vs>{}), int> = 0,
    bool NoExcept = typename Union_::And<
      Union_::NXMove>::template Reduce<Vs>{}>
  constexpr Union &operator =(V &&u) noexcept(NoExcept) {
    if constexpr (ZuIsLRef<V &&>{}) if (this == &u) return *this;
    if (m_type != u.m_type) {
      this->~Union();
      if (ZuConstEval())
	ZuNew<Union>(this, ZuFwdLike<V &&>(u));
      else
	new (this) Union(ZuFwdLike<V &&>(u));
      return *this;
    }
    ZuSwitch::dispatch<N>(m_type, [this, &u](auto I) {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	p_<I>(m_u) = ZuFwdLike<V &&>(p_<I>(ZuFwdLike<V &&>(u.m_u)));
    });
    return *this;
  }

  // from a member type
  template <
    typename V,
    ZuIfT<In_<V>{} && !Union_::IsVoid<V>{} && !ZuIs_<V, Union>{}, int> = 0,
    bool NoExcept = noexcept(Type<Index_<V>{}>(ZuDeclVal<V &&>()))>
  constexpr Union(V &&v) noexcept(NoExcept) : m_type{Index_<V>{}} {
    using namespace Union_;
    enum { I = Index_<V>{} };
    using T = Type<I>;
    Ops<T>::ctor(ZuAddr(p_<Index_<V>{}>(m_u)), ZuFwd<V>(v));
  }
  template <
    typename V,
    ZuIfT<In_<V>{} && !Union_::IsVoid<V>{} && !ZuIs_<V, Union>{}, int> = 0,
    bool NoExcept = noexcept(
      ZuDeclVal<ZuDeref<Type<Index_<V>{}>> &>() = ZuDeclVal<V &&>())>
  constexpr Union &operator =(V &&v) noexcept(NoExcept) {
    enum { I = Index_<V>{} };
    if (m_type != I) {
      this->~Union();
      if (ZuConstEval())
	ZuNew<Union>(this, ZuFwd<V>(v));
      else
	new (this) Union(ZuFwd<V>(v));
      return *this;
    }
    p_<I>(m_u) = ZuFwd<V>(v);
    return *this;
  }

  // from void
  template <typename V, ZuIfT<Union_::IsVoid<V>{}, int> = 0>
  constexpr Union(V &&) noexcept : m_type{Index<void>{}} { }
  template <typename V, ZuIfT<Union_::IsVoid<V>{}, int> = 0>
  constexpr Union &operator =(V &&) noexcept {
    enum { I = Index_<void>{} };
    this->~Union();
    m_type = I;
    return *this;
  }

  // from caller-specified member constructor args
  template <unsigned I> struct New { };
  template <
    unsigned I, typename ...Args,
    typename V = Type<I>,
    decltype(Type<I>(ZuDeclVal<Args &&>()...), int()) = 0,
    bool NoExcept = noexcept(Type<I>(ZuDeclVal<Args &&>()...))>
  constexpr Union(New<I> _, Args &&...args)
    noexcept(NoExcept) : m_type{I}
  {
    using namespace Union_;
    using T = Type<I>;
    if constexpr (!IsVoid<T>{})
      Ops<T>::ctor(ZuAddr(p_<I>(m_u)), ZuFwd<Args>(args)...);
  }

  constexpr void null() {
    using namespace Union_;
    ZuSwitch::dispatch<N>(m_type, [this](auto I) {
      using T = Type<I>;
      if constexpr (!IsVoid<T>{}) Ops<T>::dtor(ZuAddr(p_<I>(m_u)));
    });
    m_type = 0;
  }

  template <typename P>
  constexpr ZuIs<P, Union, bool> equals(const P &p) const {
    if (this == &p) return true;
    if (m_type != p.m_type) return false;
    return ZuSwitch::dispatch<N>(m_type, [this, &p](auto I) -> bool {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::equals(p_<I>(m_u), p_<I>(p.m_u));
      else
	return true;
    });
  }
  template <typename P>
  constexpr ZuIsNot<P, Union, bool> equals(const P &p) const {
    return ZuSwitch::dispatch<N>(m_type, [this, &p](auto I) -> bool {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::equals(p_<I>(m_u), p);
      else
	return false;
    });
  }
  template <typename P>
  constexpr ZuIs<P, Union, int> cmp(const P &p) const {
    if (this == &p) return 0;
    if (int i = ZuCompare(m_type, p.m_type)) return i;
    return ZuSwitch::dispatch<N>(m_type, [this, &p](auto I) -> int {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::cmp(p_<I>(m_u), p_<I>(p.m_u));
      else
	return 0;
    });
  }
  template <typename P>
  constexpr ZuIsNot<P, Union, int> cmp(const P &p) const {
    return ZuSwitch::dispatch<N>(m_type, [this, &p](auto I) -> int {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::cmp(p_<I>(m_u), p);
      else
	return -1;
    });
  }
  template <typename L, typename R>
  friend constexpr ZuIfT<ZuIs_<L, Union>{}, bool>
  operator ==(const L &l, const R &r) { return l.equals(r); }
  template <typename L, typename R>
  friend constexpr ZuIfT<ZuIs_<L, Union>{}, int>
  operator <=>(const L &l, const R &r) { return l.cmp(r); }

  constexpr bool operator *() const {
    return ZuSwitch::dispatch<N>(m_type, [this](auto I) -> bool {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::star(p_<I>(m_u));
      else
	return false;
    });
  }

  constexpr bool operator !() const {
    return ZuSwitch::dispatch<N>(m_type, [this](auto I) -> bool {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return Ops<T>::bang(p_<I>(m_u));
      else
	return true;
    });
  }

  uint32_t hash() const {
    return ZuSwitch::dispatch<N>(m_type, [this](auto I) -> uint32_t {
      using namespace Union_;
      using T = Type<I>;
      if constexpr (!IsVoid<T>{})
	return ZuHash<uint8_t>::hash(I) ^ Ops<T>::hash(p_<I>(m_u));
      else
	return ZuHash<uint8_t>::hash(I);
    });
  }

  constexpr unsigned type() const { return m_type; }

  void type_(unsigned i) { m_type = i; }

  template <typename T>
  constexpr bool is() const { return m_type == Index<T>{}; }

  template <unsigned I, typename ...Args>
  constexpr auto &&p(this auto &&self, Args &&...args) {
    using namespace Union_;
    ZuAssert(I < N);
    if constexpr (!sizeof...(Args)) {
      assert(self.m_type == I);
      return ZuFwdLike<decltype(self)>(
	p_<I>(ZuFwdLike<decltype(self)>(self.m_u)));
    } else {
      if (self.m_type != I) {
	self.~Union();
	if (ZuConstEval())
	  ZuNew<Union>(ZuAddr(self), New<I>{}, ZuFwd<Args>(args)...);
	else
	  new (ZuAddr(self)) Union(New<I>{}, ZuFwd<Args>(args)...);
      } else {
	if constexpr (sizeof...(Args) == 1)
	  p_<I>(self.m_u) = (..., ZuFwd<Args>(args));
	else {
	  using T = Type<I>;
	  p_<I>(self.m_u) = T(ZuFwd<Args>(args)...);
	}
      }
      return ZuFwdLike<decltype(self)>(self);
    }
  }

  template <typename T, typename ...Args>
  ZuInline constexpr auto &&p(this auto &&self, Args &&...args) {
    return ZuFwdLike<decltype(self)>(self).template p<Index<T>{}>(
      ZuFwd<Args>(args)...);
  }

  template <unsigned I, bool New = false> // New elides destructor
  constexpr Type<I> *new_() {
    if constexpr (!New) this->~Union();
    m_type = I;
    return ZuAddr(p_<I>(m_u));
  }
  template <typename T, bool New = false>
  ZuInline constexpr Type<Index<T>{}> *new_() {
    return new_<Index<T>{}, New>();
  }

  template <unsigned I>
  constexpr Type<I> *ptr() {
    if (m_type != I) return nullptr;
    return ZuAddr(p_<I>(m_u));
  }
  template <typename T>
  ZuInline constexpr Type<Index<T>{}> *ptr() {
    return ptr<Index<T>{}>();
  }

  template <typename L>
  decltype(auto) dispatch(this auto &&self, L &&l);

  template <typename L>
  decltype(auto) cdispatch(L &&l) const & { return dispatch(ZuFwd<L>(l)); }

  // traits
  using Traits = Union_::Traits<ZuBaseTraits<Union>, Ts...>;
  friend Traits ZuTraitsType(Union *) { return {}; } // unused

  // recursive decay
  friend Union_::RDecayer ZuRDecayer(Union *);

private:
  Data		m_u;
  uint8_t	m_type = 0;
};

template <typename ...Ts>
template <typename L>
inline decltype(auto) Union<Ts...>::dispatch(this auto &&self, L &&l)
{
  using R = Union_::Eval<decltype(self), decltype(l)>;
  if constexpr (Union_::IsVoid<R>{}) {
    ZuSwitch::dispatch<N>(self.m_type, [&self, &l](auto I) mutable {
      if constexpr (!Union_::IsVoid<Type<I>>{})
	ZuFwd<L>(l)(I, ZuFwdLike<decltype(self)>(self).template p<I>());
    });
  } else {
    return ZuSwitch::dispatch<N>(self.m_type, [&self, &l](auto I) mutable -> R {
      if constexpr (!Union_::IsVoid<Type<I>>{})
	return ZuFwd<L>(l)(I, ZuFwdLike<decltype(self)>(self).template p<I>());
      else
	return R{};
    });
  }
}

} // namespace Zu_

template <typename ...Ts> using ZuUnion = Zu_::Union<Ts...>;

// STL variant interop cruft
#include <type_traits>
namespace std {

template <class> struct tuple_size;
template <typename ...Ts>
struct tuple_size<ZuUnion<Ts...>> :
public integral_constant<size_t, sizeof...(Ts)> { };

template <size_t, typename> struct tuple_element;
template <size_t I, typename ...Ts>
struct tuple_element<I, ZuUnion<Ts...>> {
  using type = typename ZuUnion<Ts...>::template Type<I>;
};

} // std

#include <variant>

namespace Zu_ {

using size_t = std::size_t;
using bad_variant_access = std::bad_variant_access;
namespace {
  template <size_t I, typename T>
  using tuple_element_t = typename std::tuple_element<I, T>::type;
}
template <size_t I, typename ...Ts>
constexpr tuple_element_t<I, Union<Ts...>> &
get(Union<Ts...> &p) {
  if (ZuUnlikely(p.type() != I)) throw bad_variant_access{};
  return p.template p<I>();
}
template <size_t I, typename ...Ts>
constexpr const tuple_element_t<I, Union<Ts...>> &
get(const Union<Ts...> &p) {
  if (ZuUnlikely(p.type() != I)) throw bad_variant_access{};
  return p.template p<I>();
}
template <size_t I, typename ...Ts>
constexpr tuple_element_t<I, Union<Ts...>> &&
get(Union<Ts...> &&p) {
  if (ZuUnlikely(p.type() != I)) throw bad_variant_access{};
  return static_cast<tuple_element_t<I, Union<Ts...>> &&>(
      p.template p<I>());
}
template <size_t I, typename ...Ts>
constexpr const tuple_element_t<I, Union<Ts...>> &&
get(const Union<Ts...> &&p) {
  if (ZuUnlikely(p.type() != I)) throw bad_variant_access{};
  return static_cast<const tuple_element_t<I, Union<Ts...>> &&>(
      p.template p<I>());
}

template <typename T, typename ...Ts>
constexpr T &get(Union<Ts...> &p) {
  if (ZuUnlikely(p.type() != typename Union<Ts...>::template Index<T>{}))
    throw bad_variant_access{};
  return p.template p<T>();
}
template <typename T, typename ...Ts>
constexpr const T &get(const Union<Ts...> &p) {
  if (ZuUnlikely(p.type() != typename Union<Ts...>::template Index<T>{}))
    throw bad_variant_access{};
  return p.template p<T>();
}
template <typename T, typename ...Ts>
constexpr T &&get(Union<Ts...> &&p) {
  if (ZuUnlikely(p.type() != typename Union<Ts...>::template Index<T>{}))
    throw bad_variant_access{};
  return static_cast<T &&>(p.template p<T>());
}
template <typename T, typename ...Ts>
constexpr const T &&get(const Union<Ts...> &&p) {
  if (ZuUnlikely(p.type() != typename Union<Ts...>::template Index<T>{}))
    throw bad_variant_access{};
  return static_cast<const T &&>(p.template p<T>());
}

} // namespace Zu_

#define ZuUnion_FieldType(args) \
  ZuPP_Defer(ZuUnion_FieldType_)()(ZuPP_Strip(args))
#define ZuUnion_FieldType_() ZuUnion_FieldType__
#define ZuUnion_FieldType__(type, fn) ZuPP_Strip(type)

#define ZuUnion_FieldFn(N, args) \
  ZuPP_Defer(ZuUnion_FieldFn_)()(N, ZuPP_Strip(args))
#define ZuUnion_FieldFn_() ZuUnion_FieldFn__
#define ZuUnion_FieldFn__(I, type_, fn) \
  auto is_##fn() const { return this->type() == I; } \
  auto &&fn(this auto &&self) { \
    return ZuFwdLike<decltype(self)>(self).template p<I>(); \
  } \
  template <typename P> \
  auto &fn(P &&v) { \
    this->template p<I>(ZuFwd<P>(v)); \
    return *this; \
  } \
  auto ptr_##fn() { return this->template ptr<I>(); } \
  auto new_##fn() { return this->template new_<I>(); }

#define ZuDeclUnion(Type, ...) \
using Type##_ = \
  ZuUnion<ZuPP_Eval(ZuPP_MapComma(ZuUnion_FieldType, __VA_ARGS__))>; \
struct Type : public Type##_ { \
  using Union = Type##_; \
  using Union::Union; \
  using Union::operator =; \
  Type(const Union &v) : Union(v) { }; \
  Type(Union &&v) : Union(ZuMv(v)) { }; \
  ZuPP_Eval(ZuPP_MapIndex(ZuUnion_FieldFn, 0, __VA_ARGS__)) \
  struct Traits : public ZuTraits<Type##_> { using T = Type; }; \
  friend Traits ZuTraitsType(Type *); \
}

#endif /* ZuUnion_HH */
