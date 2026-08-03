//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// generic tuple with different implementation priorities than std::tuple
// - consteval eligible
// - structured binding via std::tuple_element, tuple_size
// - interoperates with std::tuple, pair, array
// - no effort is made to interoperate with std::tuple_cat or std::tie
// - flat non-recursive type hierarchy using multiple-inheritance
// - storage of elements ordered from largest to smallest
// - reference elements
// - partial initialization - e.g. ZuTuple<int, int> i = { 42 };
// - recursive decay - e.g. from ZuTuple<int &&> && to ZuTuple<int>
// - p<I>() - access by position - lvalue, rvalue and rvalue ref
// - p<I>(v) - set by position
// - p<T>() - access by type
// - p<T>(v) - set by type
// - Types - type list (ZuTypeList)
// - Indices - compile-time index sequence (ZuSeq)
// - dispatch(unsigned, lambda) - dispatcher
// - cdispatch(unsigned, lambda) - const dispatcher
// - operator <=>() - aggregate comparison using positional priority
// - uses defaulted move operators throughout to leverage the compiler's
//   automatic determination of noexcept

// ZuDeclTuple(Type, (Type0, Fn0), (Type1, Fn1), ...) creates
// a ZuTuple<Type0, ...> with named member functions Fn0, Fn1, etc.
// that alias p<0>, p<1>, etc.
//
// ZuDeclTuple(Person, (ZtString<>, name), (unsigned, age), (bool, gender));
// Person p = Person().name("Fred").age(1).gender(1);
// p.age() = 42;
// std::cout << p.name() << '\n';

#ifndef ZuTuple_HH
#define ZuTuple_HH

#ifndef ZuLib_HH
#include <zlib/ZuLib.hh>
#endif

#include <type_traits>

#include <zlib/ZuSpan.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuLargest.hh>
#include <zlib/ZuPrint.hh>
#include <zlib/ZuSwitch.hh>
#include <zlib/ZuUnroll.hh>

namespace std {
  template <typename ...> class tuple;
  template <typename, typename> struct pair;
  template <typename, size_t> class array;
}

namespace Zu_ {

template <typename ...Ts> class Tuple;

// tuple implementation - element container
template <typename, typename> class Tuple_;

// resolve tuple type
void TupleType(...); // default for non-tuple types
template <typename ...Ts> std::tuple<Ts...> TupleType(std::tuple<Ts...> *);
template <typename L, typename R> std::pair<L, R> TupleType(std::pair<L, R> *);
template <typename E, size_t N> std::array<E, N> TupleType(std::array<E, N> *);
// Note: ZuTuple itself is ADL friend-injected

template <typename T>
using TupleT = ZuDecay<decltype(TupleType(ZuDeclVal<ZuDecay<T> *>()))>;

// SFINAE
template <typename>
struct IsTuple_ : public ZuFalse { };
template <typename ...Ts>
struct IsTuple_<Tuple<Ts...>> : public ZuTrue { };
template <typename ...Ts>
struct IsTuple_<std::tuple<Ts...>> : public ZuTrue { };
template <typename L, typename R>
struct IsTuple_<std::pair<L, R>> : public ZuTrue { };
template <typename E, size_t N>
struct IsTuple_<std::array<E, N>> : public ZuTrue { };
template <typename T>
struct IsTuple : public IsTuple_<TupleT<T>> { };
template <typename T, typename R = void>
using MatchTuple = ZuIfT<IsTuple<T>{}, R>;

// recursive decay
struct Tuple_RDecayer {
  template <typename> struct Decay_;
  template <typename ...Ts>
  struct Decay_<Tuple<Ts...>> {
    using T = ZuTypeApply<Tuple, ZuTypeMap<ZuRDecay, Ts...>>;
  };
  template <typename T> using Decay = Decay_<TupleT<T>>;
};

// individual tuple element
template <unsigned I_, typename T_> struct Tuple_Elem {
  enum { I = I_ };
  using T = T_;
  T v;
};

// tuple - uses MI to derive from all elements in a flat hierarchy
template <typename ...Elems_, typename ...StoredElems_>
class Tuple_<ZuTypeList<Elems_...>, ZuTypeList<StoredElems_...>> :
  public StoredElems_... {
template <typename, typename> friend class Tuple_;

public:
  using Elems = ZuTypeList<Elems_...>;
  using StoredElems = ZuTypeList<StoredElems_...>;

  // index->type dispatcher using overload resolution
  template <unsigned J, typename U>
  static Tuple_Elem<J, U> *elem_(const Tuple_Elem<J, U> &);

  template <unsigned J>
  struct Elem_ {
    using T = ZuDeref<decltype(*elem_<J>(ZuDeclVal<const Tuple_ &>()))>;
  };
  template <unsigned J> using Elem = typename Elem_<J>::T;

  template <unsigned J> using P = Elem<J>::T;
  template <unsigned J> using U = ZuDeref<P<J>>;

  using Indices = ZuSeq<Elems_::I...>;
  using Types = ZuTypeList<typename Elems_::T...>;
  template <unsigned J> using Type = Elem<J>::T;
  enum { N = sizeof...(Elems_) };
  template <typename T>
  using Index = ZuTypeIndex<T, Types>;

  // binding of ZuTuple
  template <
    unsigned J, typename V,
    typename R = typename V::template Elem<J>::T,
    bool = ZuTraits<R>::IsReference>
  struct Bind {
    using E = typename V::template Elem<J>;
    static constexpr const auto &p(const V &v) {
      return static_cast<const E &>(v).v;
    }
    static constexpr auto &p(V &v) {
      return static_cast<E &>(v).v;
    }
    static constexpr decltype(auto) p(V &&v) {
      return ZuMv(static_cast<E &&>(v).v);
    }
  };
  template <unsigned J, typename V, typename R> struct Bind<J, V, R, true> {
    using E = typename V::template Elem<J>;
    static constexpr const auto &p(const V &v) {
      return static_cast<const E &>(v).v;
    }
    static constexpr auto &p(V &v) {
      return static_cast<E &>(v).v;
    }
  };
  // binding of STL tuple/pair/array
  template <
    unsigned J, typename V,
    typename R =
      ZuDecay<decltype(std::get<J>(ZuDeclVal<const ZuDecay<V> &>()))>,
    bool = ZuTraits<R>::IsReference>
  struct StdBind {
    static decltype(auto) p(const V &v) { return std::get<J>(v); }
    static decltype(auto) p(V &v) { return std::get<J>(v); }
    static decltype(auto) p(V &&v) { return std::get<J>(ZuMv(v)); }
  };
  template <unsigned J, typename V, typename R> struct StdBind<J, V, R, true> {
    static decltype(auto) p(const V &v) { return std::get<J>(v); }
    static decltype(auto) p(V &v) { return std::get<J>(v); }
  };

  // constructible ZuTuple
  template <typename V> struct IsConTuple_ : public ZuFalse { };
  template <typename ...Ts>
  struct IsConTuple_<Tuple<Ts...>> :
    public ZuTLConstructs<ZuTypeList<Ts...>, Types> { };
  template <typename V>
  using IsConTuple = IsConTuple_<TupleT<V>>;
  template <typename V, typename R = void>
  using ConTuple = ZuIfT<IsConTuple<V>{}, R>;

  // convertible ZuTuple
  template <typename V> struct IsCvtTuple_ : public ZuFalse { };
  template <typename ...Ts>
  struct IsCvtTuple_<Tuple<Ts...>> :
    public ZuTLConverts<ZuTypeList<Ts...>, Types> { };
  template <typename V>
  using IsCvtTuple = IsCvtTuple_<TupleT<V>>;
  template <typename V, typename R = void>
  using CvtTuple = ZuIfT<IsCvtTuple<V>{}, R>;

  // constructible std::tuple, pair, array
  template <typename V> struct IsConStdTuple_ : public ZuFalse { };
  template <typename ...Ts>
  struct IsConStdTuple_<std::tuple<Ts...>> :
    public ZuTLConstructs<ZuTypeList<Ts...>, Types> { };
  template <typename L, typename R>
  struct IsConStdTuple_<std::pair<L, R>> :
    public ZuTLConstructs<ZuTypeList<L, R>, Types> { };
  template <typename E, size_t N>
  struct IsConStdTuple_<std::array<E, N>> :
    public ZuTLConstructs<ZuTypeRepeat<N, E>, Types> { };
  template <typename V>
  using IsConStdTuple = IsConStdTuple_<TupleT<V>>;
  template <typename V, typename R = void>
  using ConStdTuple = ZuIfT<IsConStdTuple<V>{}, R>;

  // convertible std::tuple, pair, array
  template <typename V> struct IsCvtStdTuple_ : public ZuFalse { };
  template <typename ...Ts>
  struct IsCvtStdTuple_<std::tuple<Ts...>> :
    public ZuTLConverts<ZuTypeList<Ts...>, Types> { };
  template <typename L, typename R>
  struct IsCvtStdTuple_<std::pair<L, R>> :
    public ZuTLConverts<ZuTypeList<L, R>, Types> { };
  template <typename E, size_t N>
  struct IsCvtStdTuple_<std::array<E, N>> :
    public ZuTLConverts<ZuTypeRepeat<N, E>, Types> { };
  template <typename V>
  using IsCvtStdTuple = IsCvtStdTuple_<TupleT<V>>;
  template <typename V, typename R = void>
  using CvtStdTuple = ZuIfT<IsCvtStdTuple<V>{}, R>;

  // constructible tuple
  template <typename V>
  using IsConAnyTuple = ZuBool<IsConTuple<V>{} || IsConStdTuple<V>{}>;
  template <typename V, typename R = void>
  using ConAnyTuple = ZuIfT<IsConAnyTuple<V>{}, R>;

  // convertible tuple
  template <typename V>
  using IsCvtAnyTuple = ZuBool<IsCvtTuple<V>{} || IsCvtStdTuple<V>{}>;
  template <typename V, typename R = void>
  using CvtAnyTuple = ZuIfT<IsCvtAnyTuple<V>{}, R>;

  // convertible first element
  template <typename V, unsigned N_ = N>
  struct IsCvtFirstElem_ {
    using T = ZuBool<
      !IsCvtAnyTuple<V>{} &&
      ZuIsConvertible<V, ZuType<0, Types>>{}>;
  };
  template <typename V>
  struct IsCvtFirstElem_<V, 0> { using T = ZuFalse; };
  template <typename V>
  using IsCvtFirstElem = typename IsCvtFirstElem_<V>::T;
  template <typename V, typename R = void>
  using CvtFirstElem = ZuIfT<IsCvtFirstElem<V>{}, R>;

  // convertible only element (in a single-element tuple)
  template <typename V, unsigned N_ = N>
  struct IsCvtElem_ {
    using T = ZuBool<
      !IsCvtAnyTuple<V>{} &&
      ZuIsConvertible<V, ZuType<0, Types>>{} && N_ == 1>;
  };
  template <typename V>
  struct IsCvtElem_<V, 0> { using T = ZuFalse; };
  template <typename V>
  using IsCvtElem = typename IsCvtElem_<V>::T;

  template <typename V, typename R = void>
  using CvtElem = ZuIfT<IsCvtElem<V>{}, R>;

  template <
    typename V, ConTuple<V, int> = 0,
    bool NoExcept = (...&& noexcept(
      StoredElems_(Bind<StoredElems_::I, ZuDecay<V>>::p(ZuDeclVal<V &&>()))))>
  constexpr Tuple_(V &&v) noexcept(NoExcept) :
    StoredElems_(Bind<StoredElems_::I, ZuDecay<V>>::p(ZuFwd<V>(v)))... { }

  template <
    typename V, ConStdTuple<V, int> = 0,
    bool NoExcept = (...&& noexcept(
      StoredElems_(
	StdBind<StoredElems_::I, ZuDecay<V>>::p(ZuDeclVal<V &&>()))))>
  constexpr Tuple_(V &&v) noexcept(NoExcept) :
    StoredElems_(StdBind<StoredElems_::I, ZuDecay<V>>::p(ZuFwd<V>(v)))... { }

  template <
    typename V, CvtTuple<V, int> = 0,
    bool NoExcept = (...&& noexcept(
      ZuDeclVal<Elems_ &>().v =
	Bind<Elems_::I, ZuDecay<V>>::p(ZuDeclVal<V &&>())))>
  constexpr Tuple_ &operator =(V &&v) noexcept(NoExcept) {
    ZuUnroll::all<Indices>([this, &v](auto J) {
      this->p<J>(Bind<J, ZuDecay<V>>::p(ZuFwd<V>(v)));
    });
    return *this;
  }

  template <
    typename V, CvtStdTuple<V, int> = 0,
    bool NoExcept = (...&& noexcept(
      ZuDeclVal<Elems_ &>().v =
	StdBind<Elems_::I, ZuDecay<V>>::p(ZuDeclVal<V &&>())))>
  constexpr Tuple_ &operator =(V &&v) noexcept(NoExcept) {
    ZuUnroll::all<Indices>([this, &v](auto J) {
      this->p<J>(StdBind<J, ZuDecay<V>>::p(ZuFwd<V>(v)));
    });
    return *this;
  }

  template <typename V>
  constexpr CvtFirstElem<V, Tuple_ &> operator =(V &&v) {
    this->p<0>(ZuFwd<V>(v));
    return *this;
  }

#ifdef __GNUC__
#pragma GCC diagnostic push
#ifdef __llvm__
#pragma GCC diagnostic ignored "-Wreorder-ctor"
#else
#pragma GCC diagnostic ignored "-Wreorder"
#endif
#endif
  template <
    typename ...HeadElems,
    typename ...TailElems,
    typename ...Vs,
    ZuIfT<ZuTLConstructs<
      ZuTypeList<Vs...>,
      ZuTypeList<typename HeadElems::T...>>{}, int> = 0,
    bool NoExcept = (...&& noexcept(HeadElems(ZuDeclVal<Vs &&>())))>
  constexpr Tuple_(
      ZuTypeList<HeadElems...>,
      ZuTypeList<TailElems...>,
      Vs &&... v) noexcept(NoExcept) :
    HeadElems(ZuFwd<Vs>(v))..., TailElems{}... { }
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

  constexpr Tuple_() = default;
  constexpr Tuple_(const Tuple_ &) = default;
  constexpr Tuple_(Tuple_ &&) = default;
  constexpr Tuple_ &operator =(const Tuple_ &) = default;
  constexpr Tuple_ &operator =(Tuple_ &&) = default;
  constexpr ~Tuple_() = default;

  // access by position
  template <unsigned J>
  constexpr auto &&p(this auto &&self) {
    ZuAssert(J < N);
    return ZuFwdLike<decltype(self)>(
      ZuFwdLike<decltype(self), Elem<J>>(self).v);
  }
  template <unsigned J, typename V>
  constexpr void p(V &&v) {
    ZuAssert(J < N);
    static_cast<Elem<J> &>(*this).v = ZuFwd<V>(v);
  }

  // access by type
  template <typename T>
  constexpr auto &&p(this auto &&self) {
    return ZuFwdLike<decltype(self)>(
      ZuFwdLike<decltype(self), Elem<ZuTypeIndex<T, Types>{}>>(self).v);
  }
  template <typename T, typename V>
  constexpr void p(V &&v) && {
    static_cast<Elem<ZuTypeIndex<T, Types>{}> &>(*this).v = ZuFwd<V>(v);
  }

  // iteration
  template <typename L>
  decltype(auto) all(L &&l) const & {
    return ZuUnroll::all<Indices>(
      [this, &l](auto J) mutable -> decltype(auto) {
	return ZuFwd<L>(l)(static_cast<const Tuple_ &>(*this).template p<J>());
      });
  }
  template <typename L>
  decltype(auto) all(L &&l) & {
    return ZuUnroll::all<Indices>(
      [this, &l](auto J) mutable -> decltype(auto) {
	return ZuFwd<L>(l)(static_cast<Tuple_ &>(*this).template p<J>());
      });
  }
  template <typename L>
  decltype(auto) all(L &&l) && {
    return ZuUnroll::all<Indices>(
      [this, &l](auto J) mutable -> decltype(auto) {
	return ZuFwd<L>(l)(ZuMv(*this).template p<J>());
      });
  }

public:
  // comparisons
  template <typename V>
  constexpr CvtTuple<V, bool> equals(const V &v) const {
    return ZuUnroll::all<Indices, bool>(true, [this, &v](auto J, bool b) {
      return b && this->p<J>() == v.template p<J>();
    });
  }
  template <typename V>
  constexpr CvtTuple<V, int> cmp(const V &v) const {
    return ZuUnroll::all<Indices, int>(0, [this, &v](auto J, int i) {
      if (i) return i;
      return ZuCmp<Type<J>>::cmp(this->p<J>(), v.template p<J>());
    });
  }

  // permit direct comparison of single-element tuples with the contained type
  template <typename V>
  constexpr CvtElem<V, bool> equals(const V &v) const {
    return this->p<0>() == v;
  }
  template <typename V>
  constexpr CvtElem<V, int> cmp(const V &v) const {
    return ZuCmp<Type<0>>::cmp(this->p<0>(), v);
  }

  constexpr bool operator !() const {
    return ZuUnroll::all<Indices, bool>(true, [this](auto J, bool b) {
      return b && !this->p<J>();
    });
  }

  uint32_t hash() const {
    return ZuUnroll::all<Indices, uint32_t>(0, [this](auto J, uint32_t v) {
      return v ^ ZuHash<Type<J>>::hash(this->p<J>());
    });
  }

  // printing
  struct Print {
    const Tuple_	&tuple;
    ZuCSpan		delim;

    template <typename S>
    void print(S &s) const {
      s << '{';
      ZuUnroll::all<Indices>([this, &s](auto J) {
	if (J) s << delim;
	if constexpr (Zu_::IsTuple<Type<J>>{})
	  s << tuple.p<J>().fmt(delim);
	else
	  s << tuple.p<J>();
      });
      s << '}';
    }
    
    friend ZuPrintFn ZuPrintType(Print *);
  };
  Print fmt(ZuCSpan delim) const { return Print{*this, delim}; }
  template <typename S> void print(S &s) const { s << fmt(","); }
  friend ZuPrintFn ZuPrintType(Tuple_ *);

  // dispatching
  template <typename L>
  constexpr auto dispatch(unsigned i, L &&l) {
    return ZuSwitch::dispatch<N>(i, [this, &l](auto I) mutable {
      return ZuFwd<L>(l)(I, this->p<I>());
    });
  }
  template <typename L>
  constexpr auto cdispatch(unsigned i, L &&l) const {
    return ZuSwitch::dispatch<N>(i, [this, &l](auto I) mutable {
      return ZuFwd<L>(l)(I, this->p<I>());
    });
  }

  // traits
  struct Traits : public ZuBaseTraits<Tuple_> {
    template <typename T> using NotPOD = ZuBool<!ZuTraits<T>::IsPOD>;
    enum { IsPOD = !ZuTypeGrep<NotPOD, Types>::N };
  };
  friend Traits ZuTraitsType(Tuple_ *) { return {}; }

  // recursive decay
  friend Tuple_RDecayer ZuRDecayer(Tuple_ *);
};

// sort elements largest to smallest
template <typename Elem>
using TupleSort = ZuInt<-int(ZuSize<typename Elem::T>{})>;

// resolve base tuple type
template <typename, typename> struct TupleBaseT_;
template <unsigned ...I, typename ...Ts>
struct TupleBaseT_<ZuSeq<I...>, ZuTypeList<Ts...>> {
  using Elems = ZuTypeList<Tuple_Elem<I, Ts>...>; // tandem expansion
  using StoredElems = ZuTypeSort<TupleSort, Elems>;
  using T = Tuple_<Elems, StoredElems>;
};
template <typename I, typename Ts>
using TupleBaseT = typename TupleBaseT_<I, Ts>::T;

// main tuple class
template <typename ...Ts>
class Tuple : public TupleBaseT<ZuMkSeq<sizeof...(Ts)>, ZuTypeList<Ts...>> {
public:
  using Base = TupleBaseT<ZuMkSeq<sizeof...(Ts)>, ZuTypeList<Ts...>>;
  using Elems = typename Base::Elems;

  template <typename V>
  using IsConAnyTuple_ = typename Base::template IsConAnyTuple<V>;
  template <typename V>
  struct IsConAnyTuple : public ZuBool<
    !ZuIsSame<ZuDecay<V>, Tuple>{} && bool(IsConAnyTuple_<V>{})> { };
  template <typename V, typename R = void>
  using ConAnyTuple = ZuIfT<IsConAnyTuple<V>{}, R>;

  template <typename V>
  struct IsCvtAnyTuple : public ZuBool<
    bool(typename Base::template IsCvtAnyTuple<V>{}) &&
    !ZuIsSame<ZuDecay<V>, Tuple>{}> { };
  template <typename V, typename R = void>
  using CvtAnyTuple = ZuIfT<IsCvtAnyTuple<V>{}, R>;

  template <typename V, typename R = void>
  using CvtFirstElem = Base::template CvtFirstElem<V, R>;

  using Types = typename Base::Types;
  enum { N = Base::N };

  // need to explicitly default these for overload resolution
  constexpr Tuple() = default;
  constexpr Tuple(const Tuple &) = default;
  constexpr Tuple &operator =(const Tuple &) = default;
  constexpr Tuple(Tuple &&) = default;
  constexpr Tuple &operator =(Tuple &&) = default;
  constexpr ~Tuple() = default;

  template <
    typename V, ConAnyTuple<V, int> = 0,
    bool NoExcept = noexcept(Base{ZuDeclVal<V &&>()})>
  constexpr Tuple(V &&v) noexcept(NoExcept) : Base{ZuFwd<V>(v)} { }

  template <
    typename V0,
    typename ...Vs,
    ZuIfT<
      (sizeof...(Vs) + 1 <= N) &&
      (sizeof...(Vs) > 0 || (
	!ZuIsSame<ZuDecay<V0>, Tuple>{} && !IsConAnyTuple_<V0>{})), int> = 0,
    typename HeadElems = ZuTypeHead<sizeof...(Vs) + 1, Elems>,
    typename TailElems = ZuTypeTail<sizeof...(Vs) + 1, Elems>,
    bool NoExcept = noexcept(
      Base{HeadElems{}, TailElems{},
	ZuDeclVal<V0 &&>(), ZuDeclVal<Vs &&>()...})>
  constexpr Tuple(V0 &&v0, Vs &&... v) noexcept(NoExcept) :
    Base{HeadElems{}, TailElems{}, ZuFwd<V0>(v0), ZuFwd<Vs>(v)...} { }

  template <
    typename V, CvtAnyTuple<V, int> = 0,
    bool NoExcept = noexcept(ZuDeclVal<Base &>() = ZuDeclVal<V &&>())>
  constexpr Tuple &operator =(V &&v) noexcept(NoExcept) {
    Base::operator =(ZuFwd<V>(v));
    return *this;
  }

  template <
    typename V, CvtFirstElem<V, int> = 0,
    bool NoExcept = noexcept(ZuDeclVal<Base &>() = ZuDeclVal<V &&>())>
  constexpr Tuple &operator =(V &&v) noexcept(NoExcept) {
    Base::operator =(ZuFwd<V>(v));
    return *this;
  }

  friend Tuple TupleType(Tuple *);
};
template <typename ...Args>
Tuple(Args &&...args) -> Tuple<ZuDecay<Args>...>;

// global comparison operators, carefully navigating C++20 ambiguity
// - see https://www.open-std.org/jtc1/sc22/wg21/docs/cwg_active.html#2804

template <typename L, typename R> struct TupleCanCmp_ : public ZuFalse { };
template <typename ...L, typename ...R>
struct TupleCanCmp_<Tuple<L...>, Tuple<R...>> :
  public ZuBool<
    typename Tuple<L...>::template IsCvtTuple<Tuple<R...>>{} ||
    typename Tuple<R...>::template IsCvtTuple<Tuple<L...>>{}> { };
template <typename L, typename R>
struct TupleCanCmp :
  public TupleCanCmp_<
    decltype(TupleType(ZuDeclVal<ZuDecay<L> *>())),
    decltype(TupleType(ZuDeclVal<ZuDecay<R> *>()))> { };

template <typename L, typename R>
constexpr ZuIfT<TupleCanCmp<L, R>{}, bool>
operator ==(const L &l, const R &r) {
  return l.equals(r);
}
template <typename L, typename R>
constexpr ZuIfT<TupleCanCmp<L, R>{}, int>
operator <=>(const L &l, const R &r) {
  return l.cmp(r);
}

template <typename L, typename R> struct TupleCanCmpElem_ : public ZuFalse { };
template <typename ...L, typename R>
struct TupleCanCmpElem_<Tuple<L...>, R> :
  public Tuple<L...>::template IsCvtElem<R> { };
template <typename L, typename R>
struct TupleCanCmpElem :
  public TupleCanCmpElem_<
    decltype(TupleType(ZuDeclVal<ZuDecay<L> *>())), ZuDecay<R>> { };

template <typename L, typename R>
constexpr ZuIfT<TupleCanCmpElem<L, R>{}, bool>
operator ==(const L &l, const R &r) { return l.equals(r); }
template <typename L, typename R>
constexpr ZuIfT<TupleCanCmpElem<L, R>{}, int>
operator <=>(const L &l, const R &r) { return l.cmp(r); }

} // namespace Zu_

// clang up to v19 is still missing P1814R0, CTAD for alias templates
#if defined(__clang__) && (__clang_major__ <= 19)
#define ZuTuple Zu_::Tuple
#else
template <typename ...Ts> using ZuTuple = Zu_::Tuple<Ts...>;
#endif

template <typename T> using ZuIsTuple = Zu_::IsTuple<T>;
template <typename T, typename R = void>
using ZuMatchTuple = Zu_::MatchTuple<T, R>;

template <typename ...Ts>
inline auto ZuFwdTuple(Ts &&... args) {
  return ZuTuple<Ts &&...>{ZuFwd<Ts>(args)...};
}

template <typename ...Ts>
inline auto ZuMvTuple(Ts... args) {
  return ZuTuple<Ts...>{ZuMv(args)...};
}

// generic accessor
template <
  typename P,
  unsigned I,
  bool = ZuTraits<typename P::template Type<I>>::IsReference>
struct ZuTupleAxor_Bind {
  static decltype(auto) get(const P &v) { return v.template p<I>(); }
  static decltype(auto) get(P &v) { return v.template p<I>(); }
  static decltype(auto) get(P &&v) { return ZuMv(v).template p<I>(); }
};
template <typename P, unsigned I>
struct ZuTupleAxor_Bind<P, I, true> {
  static decltype(auto) get(const P &v) { return v.template p<I>(); }
  static decltype(auto) get(P &v) { return v.template p<I>(); }
};
template <unsigned I = 0>
constexpr auto ZuTupleAxor() {
  return []<unsigned I_ = I, typename P>(P &&v) -> decltype(auto) {
    return ZuTupleAxor_Bind<ZuDecay<P>, I_>::get(ZuFwd<P>(v));
  };
}

// generic call
// Example:
// ZuTupleCall(ZuMvTuple("the answer is", 42, "not", 43),
//   []<typename Arg, typename ...Args>(Arg arg, Args... args) {
//     std::cout << arg;
//     (std::cout << ' ' << ... << args) << '\n';
//   });
template <typename P, typename L>
decltype(auto) ZuTupleCall(P &&v, L &&l) {
  return ZuSeqCall<ZuDecay<P>::N, ZuTupleAxor()>(ZuFwd<P>(v), ZuFwd<L>(l));
}

// STL structured binding cruft

namespace std {

template <class> struct tuple_size;
template <typename ...Ts>
struct tuple_size<ZuTuple<Ts...>> :
public integral_constant<size_t, sizeof...(Ts)> { };

template <size_t, typename> struct tuple_element;
template <size_t I, typename ...Ts>
struct tuple_element<I, ZuTuple<Ts...>> {
  using type = typename ZuTuple<Ts...>::template Type<I>;
};

} // std

namespace Zu_ {

// Note: ADL resolves get() from within Zu_ namespace

using size_t = std::size_t;
namespace {
  template <size_t I, typename T>
  using tuple_element_t = typename std::tuple_element<I, T>::type;
}
template <size_t I, typename ...Ts>
ZuInline constexpr tuple_element_t<I, Tuple<Ts...>> &
get(Tuple<Ts...> &p) noexcept { return p.template p<I>(); }
template <size_t I, typename ...Ts>
ZuInline constexpr const tuple_element_t<I, Tuple<Ts...>> &
get(const Tuple<Ts...> &p) noexcept { return p.template p<I>(); }
template <size_t I, typename ...Ts>
ZuInline constexpr tuple_element_t<I, Tuple<Ts...>> &&
get(Tuple<Ts...> &&p) noexcept {
  return static_cast<tuple_element_t<I, Tuple<Ts...>> &&>(
      p.template p<I>());
}
template <size_t I, typename ...Ts>
ZuInline constexpr const tuple_element_t<I, Tuple<Ts...>> &&
get(const Tuple<Ts...> &&p) noexcept {
  return static_cast<const tuple_element_t<I, Tuple<Ts...>> &&>(
      p.template p<I>());
}

template <typename T, typename ...Ts>
ZuInline constexpr T &get(Tuple<Ts...> &p) noexcept {
  return p.template p<T>();
}
template <typename T, typename ...Ts>
ZuInline constexpr const T &get(const Tuple<Ts...> &p) noexcept {
  return p.template p<T>();
}
template <typename T, typename ...Ts>
ZuInline constexpr T &&get(Tuple<Ts...> &&p) noexcept {
  return static_cast<T &&>(p.template p<T>());
}
template <typename T, typename ...Ts>
ZuInline constexpr const T &&get(const Tuple<Ts...> &&p) noexcept {
  return static_cast<const T &&>(p.template p<T>());
}

} // namespace Zu_

#define ZuTuple_FieldType(args) \
  ZuPP_Defer(ZuTuple_FieldType_)()(ZuPP_Strip(args))
#define ZuTuple_FieldType_() ZuTuple_FieldType__
#define ZuTuple_FieldType__(type, fn) ZuPP_Strip(type)

#define ZuTuple_FieldFn(N, args) \
  ZuPP_Defer(ZuTuple_FieldFn_)()(N, ZuPP_Strip(args))
#define ZuTuple_FieldFn_() ZuTuple_FieldFn__
// decltype(auto) here causes problems, need explicit deduction of return type
#define ZuTuple_FieldFn__(N, type, fn) \
  decltype(ZuDeclVal<const Tuple &>().Tuple::template p<N>()) fn() const & { \
    return this->Tuple::template p<N>(); \
  } \
  decltype(ZuDeclVal<Tuple &>().Tuple::template p<N>()) fn() & { \
    return this->Tuple::template p<N>(); \
  } \
  decltype(ZuDeclVal<Tuple &&>().Tuple::template p<N>()) fn() && { \
    return ZuMv(ZuMv(*this).Tuple::template p<N>()); \
  } \
  template <typename P> \
  auto &fn(P &&v) { this->template p<N>(ZuFwd<P>(v)); return *this; }

#define ZuDeclTuple(Type, ...) \
using Type##_ = \
  ZuTuple<ZuPP_Eval(ZuPP_MapComma(ZuTuple_FieldType, __VA_ARGS__))>; \
class Type : public Type##_ { \
  using Tuple = Type##_; \
public: \
  Type() = default; \
  Type(const Type &) = default; \
  Type &operator =(const Type &) = default; \
  Type(Type &&) = default; \
  Type &operator =(Type &&) = default; \
  ~Type() = default; \
  template <typename ...Ts> \
  Type(Ts &&... args) : Tuple{ZuFwd<Ts>(args)...} { } \
  template <typename T> \
  Type &operator =(T &&v) { \
    Tuple::operator =(ZuFwd<T>(v)); \
    return *this; \
  } \
  ZuPP_Eval(ZuPP_MapIndex(ZuTuple_FieldFn, 0, __VA_ARGS__)) \
}

#endif /* ZuTuple_HH */
