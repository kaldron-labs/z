//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// cleanup for ZmSingleton/ZmSpecific

#ifndef ZmGlobal_HH
#define ZmGlobal_HH

#ifndef ZmLib_HH
#include <zlib/ZmLib.hh>
#endif

#include <stdlib.h>

#include <typeinfo>
#include <typeindex>

#include <zlib/ZmPlatform.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmCleanup.hh>

extern "C" {
  ZmExtern void ZmGlobal_atexit();
}

#ifdef ZDEBUG
class ZuVStream;
#endif

class ZmAPI ZmGlobal {
friend ZmAPI void ZmGlobal_atexit();

public:
  virtual ~ZmGlobal() { }

protected:
  ZmGlobal() :
    m_type(typeid(void)),
#ifdef ZDEBUG
    m_name(nullptr),
#endif
    m_next(nullptr) { }

  ZmGlobal(const ZmGlobal &) = delete;
  ZmGlobal &operator =(const ZmGlobal &) = delete;

private:
  std::type_index	m_type;		// type index
#ifdef ZDEBUG
  const char		*m_name;	// type name
#endif
  ZmGlobal		*m_next;	// level linked list

  static ZmGlobal *add(
      std::type_index type, unsigned level, ZmGlobal *(*ctor)());
#ifdef ZDEBUG
  static void dump(ZuVStream &);
#endif

  template <typename T> struct Ctor {
    static ZmGlobal *_() {
      ZmGlobal *global = static_cast<ZmGlobal *>(new T());
      const std::type_info &info = typeid(T);
      global->m_type = std::type_index(info);
#ifdef ZDEBUG
      global->m_name = info.name();
#endif
      return global;
    }
  };

protected:
  template <typename T, unsigned Level> static T *global() {
    static uintptr_t addr_ = 0;
    auto addr = ZuLaunder(reinterpret_cast<ZmAtomic<uintptr_t> *>(&addr_));
    uintptr_t ptr;
    while (ZuUnlikely(!((ptr = addr->load_()) & ~1))) {
      if ((ptr == 1) || addr->cmpXch(1, 0)) {
	Zm::yield();
	continue;
      }
      *addr = ptr = reinterpret_cast<uintptr_t>(
	ZmGlobal::add(typeid(T), Level, &Ctor<T>::_));
    }
    return static_cast<T *>(reinterpret_cast<ZmGlobal *>(ptr));
  }
};

#endif /* ZmGlobal_HH */
