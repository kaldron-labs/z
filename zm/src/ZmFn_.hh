//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// function delegates for multithreading and deferred execution (callbacks)

#ifndef ZmFn__HH
#define ZmFn__HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <zlib/ZuTL.hh>
#include <zlib/ZuCmp.hh>
#include <zlib/ZuHash.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTraits.hh>
#include <zlib/ZuLambdaTraits.hh>

#include <zlib/ZmContext.hh>

template <typename Fn> class ZmFn;

// ZmFn base class
class ZmAnyFn : public ZmContext {
  struct Pass {
    Pass &operator =(const Pass &) = delete;
  };

public:
  ZmAnyFn() = default;
  ~ZmAnyFn() = default;

  ZmAnyFn(const ZmAnyFn &fn) : ZmContext(fn), m_invoker(fn.m_invoker) { }

  ZmAnyFn(ZmAnyFn &&fn) :
    ZmContext(static_cast<ZmContext &&>(fn)), m_invoker(fn.m_invoker)
  {
    fn.m_invoker = 0;
  }

  ZmAnyFn &operator =(const ZmAnyFn &fn) {
    if (ZuLikely(this != &fn)) {
      ZmContext::copy(fn);
      m_invoker = fn.m_invoker;
    }
    return *this;
  }

  ZmAnyFn &operator =(ZmAnyFn &&fn) {
    ZmContext::move(static_cast<ZmContext &&>(fn));
    m_invoker = fn.m_invoker;
    fn.m_invoker = 0;
    return *this;
  }

  // accessed from ZmFn<...>
protected:
  template <typename Invoker, typename O>
  ZmAnyFn(const Invoker &invoker, O &&o) :
    ZmContext(ZuFwd<O>(o)),
    m_invoker{reinterpret_cast<uintptr_t>(invoker)} { }

public:
  // downcast to ZmFn<...>
  template <typename Fn> const Fn &as() const {
    return *static_cast<const Fn *>(this);
  }
  template <typename Fn> Fn &as() {
    return *static_cast<Fn *>(this);
  }

  // access invoker
  uintptr_t invoker() const { return m_invoker; }

  bool equals(const ZmAnyFn &fn) const {
    return m_invoker == fn.m_invoker && ZmContext::equals(fn);
  }
  int cmp(const ZmAnyFn &fn) const {
    if (m_invoker < fn.m_invoker) return -1;
    if (m_invoker > fn.m_invoker) return 1;
    return ZmContext::cmp(fn);
  }
  friend inline bool operator ==(const ZmAnyFn &l, const ZmAnyFn &r) {
    return l.equals(r);
  }
  friend inline int operator <=>(const ZmAnyFn &l, const ZmAnyFn &r) {
    return l.cmp(r);
  }

  bool operator !() const { return !m_invoker; }
  ZuOpBool

  uint32_t hash() const {
    return ZuHash<uintptr_t>::hash(m_invoker) ^ ZmContext::hash();
  }

  struct Traits : public ZuBaseTraits<ZmAnyFn> { enum { IsPOD = 1 }; };
  friend Traits ZuTraitsType(ZmAnyFn *);

protected:
  uintptr_t		m_invoker = 0;
};

// consteval wrapper for function pointers
template <auto> struct ZmFnPtr;
template <
  typename R_,
  typename ...Args_,
  R_ (*Fn)(Args_...)>
struct ZmFnPtr<Fn> : public ZuConstant<decltype(Fn), Fn> {
  using R = R_;
  using O = void;
  using Args = ZuTypeList<Args_...>;
};
template <
  typename O_,
  typename R_,
  typename ...Args_,
  R_ (O_::*Fn)(Args_...)>
struct ZmFnPtr<Fn> : public ZuConstant<decltype(Fn), Fn> {
  using R = R_;
  using O = O_;
  using Args = ZuTypeList<Args_...>;
};
template <
  typename O_,
  typename R_,
  typename ...Args_,
  R_ (O_::*Fn)(Args_...) const>
struct ZmFnPtr<Fn> : public ZuConstant<decltype(Fn), Fn> {
  using R = R_;
  using O = const O_;
  using Args = ZuTypeList<Args_...>;
};

template <typename T> struct ZmIsFnPtr : public ZuFalse { };
template <auto Fn> struct ZmIsFnPtr<ZmFnPtr<Fn>> : public ZuTrue { };

struct ZmLambda_HeapID : public ZuStringT<"ZmLambda"> { };

template <typename Fn = void()> class ZmFn;
template <typename R_, typename ...Args_>
class ZmFn<R_(Args_...)> : public ZmAnyFn {
  class Pass {
    friend ZmFn;
    Pass &operator =(const Pass &) = delete;
  };

public:
  using R = R_;
  using Args = ZuTypeList<Args_...>;

private:
  typedef R (*Invoker)(uintptr_t &, Args_...);
  template <auto> struct FnInvoker;
  template <typename, auto> struct BoundInvoker;
  template <typename, auto> struct MemberInvoker;

public:
  template <typename L, typename Args__, typename = void>
  struct IsCallable_ : public ZuFalse { };
  template <typename L, typename ...Args__>
  struct IsCallable_<L, ZuTypeList<Args__...>,
    decltype(ZuDeclVal<L &>()(ZuDeclVal<Args__>()...), void())> : public ZuBool<
      !ZuIs_<L, ZmAnyFn>{} && !ZmIsFnPtr<L>{} &&
      ZuIsConvertible<decltype(ZuDeclVal<L &>()(ZuDeclVal<Args__>()...)), R>{}
    > { };
  template <typename L>
  struct IsCallable : public IsCallable_<L, ZuTypeList<Args_...>> { };
  template <typename L, typename R__ = void>
  using MatchCallable = ZuIfT<IsCallable<L>{}, R__>;
  template <typename O, typename L>
  struct IsBoundCallable : public IsCallable_<L, ZuTypeList<O, Args_...>> { };
  template <typename L>
  struct IsBoundCallable<Pass, L> : public ZuFalse { };
  template <typename O, typename L, typename R__ = void>
  using MatchBoundCallable = ZuIfT<IsBoundCallable<O, L>{}, R__>;

  template <typename>
  struct IsUnboundFn : public ZuFalse { };
  template <auto Fn>
  struct IsUnboundFn<ZmFnPtr<Fn>> :
    public ZuBool<
      bool(ZuIsSame<typename ZmFnPtr<Fn>::O, void>{}) &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<Args, typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename Fn, typename R__ = void>
  using MatchUnboundFn = ZuIfT<IsUnboundFn<Fn>{}, R__>;

  template <typename, typename>
  struct IsBoundFn : public ZuFalse { };
  template <typename O, auto Fn>
  struct IsBoundFn<O *, ZmFnPtr<Fn>> :
    public ZuBool<
      bool(ZuIsSame<typename ZmFnPtr<Fn>::O, void>{}) &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<
	typename Args::template Unshift<O *>,
	typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsBoundFn<const O *, ZmFnPtr<Fn>> :
    public ZuBool<
      bool(ZuIsSame<typename ZmFnPtr<Fn>::O, void>{}) &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<
	typename Args::template Unshift<const O *>,
	typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsBoundFn<ZmRef<O>, ZmFnPtr<Fn>> :
    public ZuBool<
      bool(ZuIsSame<typename ZmFnPtr<Fn>::O, void>{}) &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<
	typename Args::template Unshift<ZmRef<O>>,
	typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsBoundFn<ZmRef<const O>, ZmFnPtr<Fn>> :
    public ZuBool<
      bool(ZuIsSame<typename ZmFnPtr<Fn>::O, void>{}) &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<
	typename Args::template Unshift<ZmRef<const O>>,
	typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, typename Fn, typename R__ = void>
  using MatchBoundFn = ZuIfT<IsBoundFn<ZuDecay<O>, Fn>{}, R__>;

  template <typename, typename>
  struct IsMemberFn : public ZuFalse { };
  template <typename O, auto Fn>
  struct IsMemberFn<O *, ZmFnPtr<Fn>> :
    public ZuBool<
      ZuIs_<O, typename ZmFnPtr<Fn>::O>{} &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<Args, typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsMemberFn<const O *, ZmFnPtr<Fn>> :
    public ZuBool<
      ZuIs_<O, typename ZmFnPtr<Fn>::O>{} &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<Args, typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsMemberFn<ZmRef<O>, ZmFnPtr<Fn>> :
    public ZuBool<
      ZuIs_<O, typename ZmFnPtr<Fn>::O>{} &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<Args, typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, auto Fn>
  struct IsMemberFn<ZmRef<const O>, ZmFnPtr<Fn>> :
    public ZuBool<
      ZuIs_<O, typename ZmFnPtr<Fn>::O>{} &&
      ZuIsConvertible<typename ZmFnPtr<Fn>::R, R>{} &&
      bool(ZuTLConverts<Args, typename ZmFnPtr<Fn>::Args>{})> { };
  template <typename O, typename Fn, typename R__ = void>
  using MatchMemberFn = ZuIfT<IsMemberFn<ZuDecay<O>, Fn>{}, R__>;

public:
  ZmFn() : ZmAnyFn{} { }
  ZmFn(const ZmFn &fn) : ZmAnyFn{fn} { }
  ZmFn(ZmFn &&fn) : ZmAnyFn{static_cast<ZmAnyFn &&>(fn)} { }

private:
  template <typename ...Args__>
  ZmFn(Pass, Args__ &&...args) : ZmAnyFn(ZuFwd<Args__>(args)...) { }

public:
  // callable objects
  template <typename L, decltype(MatchCallable<L>(), int()) = 0>
  ZmFn(L &&l) : ZmAnyFn{fn(ZuFwd<L>(l))} { }
  template <
    typename O, typename L,
    decltype(MatchBoundCallable<ZuDeref<O>, L>(), int()) = 0>
  ZmFn(O &&o, L &&l) :
      ZmAnyFn{fn(ZuFwd<O>(o), ZuFwd<L>(l))} { }

  // member function pointers via ZmFnPtr<>
  template <
    typename O, typename Fn,
    decltype(MatchMemberFn<O, Fn>(), int()) = 0>
  ZmFn(O &&o, Fn fn) :
    ZmAnyFn{
      &MemberInvoker<ZuDeref<O>, typename Fn::T(fn)>::invoke,
      ZuFwd<O>(o)} { }

  // bound function pointers via ZmFnPtr<>
  template <
    typename O, typename Fn,
    decltype(MatchBoundFn<O, Fn>(), int()) = 0>
  ZmFn(O &&o, Fn fn) :
    ZmAnyFn{
      &BoundInvoker<ZuDeref<O>, typename Fn::T(fn)>::invoke,
      ZuFwd<O>(o)} { }

  // plain function pointers via ZmFnPtr<>
  template <
    typename Fn,
    decltype(MatchUnboundFn<Fn>(), int()) = 0>
  ZmFn(Fn fn) :
    ZmAnyFn{
      &FnInvoker<typename Fn::T(fn)>::invoke,
      static_cast<void *>(nullptr)} { }

  ZmFn &operator =(const ZmFn &fn) {
    ZmAnyFn::operator =(fn);
    return *this;
  }
  ZmFn &operator =(ZmFn &&fn) {
    ZmAnyFn::operator =(static_cast<ZmAnyFn &&>(fn));
    return *this;
  }

private:
  ZmFn &operator =(const ZmAnyFn &fn) {
    ZmAnyFn::operator =(fn);
    return *this;
  }
  ZmFn &operator =(ZmAnyFn &&fn) {
    ZmAnyFn::operator =(static_cast<ZmAnyFn &&>(fn));
    return *this;
  }

public:
  template <typename ...Args__, typename R__ = R>
  ZuSame<void, R__, R> operator ()(Args__ &&... args) const {
    if (ZmAnyFn::operator !()) return;
    (*reinterpret_cast<Invoker>(m_invoker))(
      object_(), ZuFwd<Args__>(args)...);
  }
  template <typename ...Args__, typename R__ = R>
  ZuNotSame<void, R__, R> operator ()(Args__ &&... args) const {
    if (ZmAnyFn::operator !()) return {};
    return (*reinterpret_cast<Invoker>(m_invoker))(
      object_(), ZuFwd<Args__>(args)...);
  }

  // lambda matching
  template <typename HeapID, bool Sharded> struct Lambda;
  template <typename L>
  static MatchCallable<L, ZmFn> fn(L &&l) {
    return Lambda<ZmLambda_HeapID>::fn(ZuFwd<L>(l));
  }
  template <typename O, typename L>
  static MatchBoundCallable<ZuDeref<O>, L, ZmFn> fn(O &&o, L &&l) {
    return Lambda<ZmLambda_HeapID>::fn(ZuFwd<O>(o), ZuFwd<L>(l));
  }
  template <typename L>
  static MatchCallable<L, ZmFn> mvFn(L &&l) {
    return Lambda<ZmLambda_HeapID>::mvFn(ZuFwd<L>(l));
  }
  template <typename O, typename L>
  static MatchBoundCallable<ZuDeref<O>, L, ZmFn> mvFn(O &&o, L &&l) {
    return Lambda<ZmLambda_HeapID>::mvFn(ZuFwd<O>(o), ZuFwd<L>(l));
  }

private:
  // deduce mutability of lambda
  template <typename L, typename ArgList>
  using IsMutable_ = ZuLambdaTraits::IsMutable<L, ArgList>;
  template <typename L>
  using IsMutable = IsMutable_<L, ZuTypeList<Args_...>>;
  template <typename O, typename L>
  using IsBoundMutable = IsMutable_<L, ZuTypeList<O, Args_...>>;

  // deduce statefulness of lambda
  template <typename L>
  using IsStateless = ZuIsStatelessLambda<L, ZuTypeList<Args_...>>;
  template <typename O, typename L>
  using IsBoundStateless = ZuIsStatelessLambda<L, ZuTypeList<O, Args_...>>;

  // pre-declare lambda invokers
  template <typename HeapID, bool Sharded, typename L,
    bool Stateless = IsStateless<L>{},
    bool Mutable = IsMutable<L>{}>
  struct LambdaInvoker;
  template <typename HeapID, bool Sharded, typename O, typename L,
    bool Stateless = IsBoundStateless<O *, L>{},
    bool Mutable = IsBoundMutable<O *, L>{}>
  struct LambdaPtrInvoker;
  template <typename HeapID, bool Sharded, typename O, typename L,
    bool Stateless = IsBoundStateless<ZmRef<O>, L>{},
    bool Mutable = IsBoundMutable<ZmRef<O>, L>{}>
  struct LambdaRefInvoker;
  template <typename HeapID, bool Sharded, typename O, typename L,
    bool Stateless = IsBoundStateless<ZmRef<O>, L>{},
    bool Mutable = IsBoundMutable<ZmRef<O>, L>{}>
  struct LambdaMvRefInvoker;

public:
  // lambdas (specifying heap ID)
  template <typename HeapID, bool Sharded = false>
  struct Lambda {
    template <typename L>
    static MatchCallable<L, ZmFn> fn(L &&l) {
      return LambdaInvoker<HeapID, Sharded, L>::fn(ZuFwd<L>(l));
    }
    template <typename O, typename L>
    static MatchBoundCallable<O *, L, ZmFn> fn(O *o, L &&l) {
      return LambdaPtrInvoker<HeapID, Sharded, O, L>::fn(
	  o, ZuFwd<L>(l));
    }
    template <typename O, typename L>
    static MatchBoundCallable<ZmRef<O>, L, ZmFn> fn(ZmRef<O> o, L &&l) {
      return LambdaRefInvoker<HeapID, Sharded, O, L>::fn(
	  ZuMv(o), ZuFwd<L>(l));
    }
    template <typename O, typename L>
    static MatchBoundCallable<ZmRef<O>, L, ZmFn> mvFn(ZmRef<O> o, L &&l) {
      return LambdaMvRefInvoker<HeapID, Sharded, O, L>::fn(
	  ZuMv(o), ZuFwd<L>(l));
    }
  };

private:
  // unbound functions
  template <typename R__, R__ (*Fn)(Args_...)> struct FnInvoker<Fn> {
    static R invoke(uintptr_t &, Args_... args) {
      return (*Fn)(ZuFwd<Args_>(args)...);
    }
  };

  // bound functions
  template <typename O, typename C, typename R__, R__ (*Fn)(C *, Args_...)>
  struct BoundInvoker<O *, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (*Fn)(static_cast<C *>(ptr<O>(o)), ZuFwd<Args_>(args)...);
    }
  };
  template <typename O, typename R__, R__ (*Fn)(ZmRef<O>, Args_...)>
  struct BoundInvoker<ZmRef<O>, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (*Fn)(ptr<O>(o), ZuFwd<Args_>(args)...);
    }
    static R mvInvoke(uintptr_t &o, Args_... args) {
      o = disown(o);
      return (*Fn)(ZmRef<O>::acquire(ptr<O>(o)), ZuFwd<Args_>(args)...);
    }
  };

  // member functions
  template <typename O, typename C, typename R__, R__ (C::*Fn)(Args_...)>
  struct MemberInvoker<O *, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (static_cast<C *>(ptr<O>(o))->*Fn)(ZuFwd<Args_>(args)...);
    }
  };
  template <typename O, typename C, typename R__, R__ (C::*Fn)(Args_...)>
  struct MemberInvoker<ZmRef<O>, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (static_cast<C *>(ptr<O>(o))->*Fn)(ZuFwd<Args_>(args)...);
    }
  };
  template <typename O, typename C, typename R__, R__ (C::*Fn)(Args_...) const>
  struct MemberInvoker<O *, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (static_cast<const C *>(ptr<O>(o))->*Fn)(ZuFwd<Args_>(args)...);
    }
  };
  template <typename O, typename C, typename R__, R__ (C::*Fn)(Args_...) const>
  struct MemberInvoker<ZmRef<O>, Fn> {
    static R invoke(uintptr_t &o, Args_... args) {
      return (static_cast<const C *>(ptr<O>(o))->*Fn)(ZuFwd<Args_>(args)...);
    }
  };

  // stateless lambda
  template <typename HeapID, bool Sharded, typename L>
  struct LambdaInvoker<HeapID, Sharded, L, true, false> {
    static R invoke(uintptr_t &, Args_... args) {
      return ZuInvokeLambda<L, ZuTypeList<Args_...>>(ZuFwd<Args_>(args)...);
    }
    static ZmFn fn(L &&) {
      return {
	ZmFn::Pass{}, &LambdaInvoker::invoke,
	static_cast<void *>(nullptr)};
    }
  };
  // stateful immutable lambda
  template <typename HeapID, bool Sharded, typename L>
  struct LambdaInvoker<HeapID, Sharded, L, false, false> {
    template <typename L_> static ZmFn fn(L_ &&l);
  };
  // stateful mutable lambda
  template <typename HeapID, bool Sharded, typename L>
  struct LambdaInvoker<HeapID, Sharded, L, false, true> {
    template <typename L_> static ZmFn fn(L_ &&l);
  };
  // stateful immutable lambda bound to pointer
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaPtrInvoker<HeapID, Sharded, O, L, false, false> {
    static ZmFn fn(O *o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o, l = ZuMv(l)](Args_... args) {
	    l(o, ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateful mutable lambda bound to pointer
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaPtrInvoker<HeapID, Sharded, O, L, false, true> {
    static ZmFn fn(O *o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o, l = ZuMv(l)](Args_... args) mutable {
	    l(o, ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateful immutable lambda bound to ZmRef
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaRefInvoker<HeapID, Sharded, O, L, false, false> {
    static ZmFn fn(ZmRef<O> o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o = ZuMv(o), l = ZuMv(l)](Args_... args) {
	    l(o, ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateful mutable lambda bound to ZmRef
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaRefInvoker<HeapID, Sharded, O, L, false, true> {
    static ZmFn fn(ZmRef<O> o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o = ZuMv(o), l = ZuMv(l)](Args_... args) mutable {
	    l(o, ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateful "one-shot" immutable lambda bound to moved ZmRef
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaMvRefInvoker<HeapID, Sharded, O, L, false, false> {
    static ZmFn fn(ZmRef<O> o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o = ZuMv(o), l = ZuMv(l)](Args_... args) mutable {
	    l(ZuMv(o), ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateful "one-shot" mutable lambda bound to moved ZmRef
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaMvRefInvoker<HeapID, Sharded, O, L, false, true> {
    static ZmFn fn(ZmRef<O> o, L l) {
      return Lambda<HeapID, Sharded>::fn(
	  [o = ZuMv(o), l = ZuMv(l)](Args_... args) mutable {
	    l(ZuMv(o), ZuFwd<Args_>(args)...);
	  });
    }
  };
  // stateless lambda bound to pointer
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaPtrInvoker<HeapID, Sharded, O, L, true, false> {
    static R invoke(uintptr_t &o, Args_... args) {
      return reinterpret_cast<const L *>(0)->operator ()(
	    ptr<O>(o), ZuFwd<Args_>(args)...);
    }
    static ZmFn fn(O *o, L) {
      return ZmFn{ZmFn::Pass{}, &LambdaPtrInvoker::invoke, o};
    }
  };
  // stateless lambda bound to ref
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaRefInvoker<HeapID, Sharded, O, L, true, false> {
    static R invoke(uintptr_t &o, Args_... args) {
      return reinterpret_cast<const L *>(0)->operator ()(
	    ptr<O>(o), ZuFwd<Args_>(args)...);
    }
    static ZmFn fn(ZmRef<O> o, L) {
      return ZmFn{ZmFn::Pass{}, &LambdaRefInvoker::invoke, ZuMv(o)};
    }
  };
  // stateless "one-shot" lambda bound to moved ZmRef
  template <typename HeapID, bool Sharded, typename O, typename L>
  struct LambdaMvRefInvoker<HeapID, Sharded, O, L, true, false> {
    static R invoke(uintptr_t &o, Args_... args) {
      o = disown(o);
      return reinterpret_cast<const L *>(0)->operator ()(
	    ZmRef<O>::acquire(ptr<O>(o)), ZuFwd<Args_>(args)...);
    }
    static ZmFn fn(ZmRef<O> o, L) {
      return ZmFn{ZmFn::Pass{}, &LambdaMvRefInvoker::invoke, ZuMv(o)};
    }
  };

  friend ZuTraits<ZmAnyFn> ZuTraitsType(ZmFn *);
};

#endif /* ZmFn__HH */
