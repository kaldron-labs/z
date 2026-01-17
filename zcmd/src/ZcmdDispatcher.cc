//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <zlib/ZcmdDispatcher.hh>

namespace Zcmd {

void Dispatcher::init()
{
}
void Dispatcher::final()
{
  m_fnMap.clean();
}

void Dispatcher::deflt(DefltFn fn)
{
  m_defltFn = fn;
}

void Dispatcher::map(ZuID id, Fn fn)
{
  Guard guard(m_lock);
  if (auto data = m_fnMap.find(id))
    FnMap::ValAxor(*const_cast<FnMap::T *>(data)) = ZuMv(fn);
  else
    m_fnMap.add(id, ZuMv(fn));
}

int Dispatcher::dispatch(
  ZuID id, void *link, ZmRef<ZiIOBuf> buf, ZuBSpan out)
{
  if (auto node = m_fnMap.find(id))
    return (node->template p<1>())(link, ZuMv(buf), out);
  if (m_defltFn) return m_defltFn(link, id, ZuMv(buf), out);
  return -1;
}

} // Zcmd
