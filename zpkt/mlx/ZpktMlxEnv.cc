//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>
#include <infiniband/mlx5dv.h>

#include <zlib/ZiLog.hh>

#include "ZpktMlxEnv.hh"

namespace Zpkt::Mlx {

static ZeException contextError(const char *operation, int code)
{
  return ZeEXCEPT(Error, "Zpkt.Mlx", ([operation, code](auto &s) {
    s << operation << ": " << ZeError{code};
  }));
}

Env::~Env()
{
  auto result = final();
  if (result.is<ZeException>()) ZiLogEvent(ZuMv(result).p<ZeException>());
}

ZuUnion<ibv_context *, ZeException> Env::context(unsigned deviceIndex)
{
  int count = 0;
  auto devices = ibv_get_device_list(&count);
  if (!devices) return contextError("enumerate RDMA devices", errno);

  int code = ENODEV;
  ibv_context *verbs = nullptr;
  for (int i = 0; i < count; ++i) {
    auto device = devices[i];
    int index = ibv_get_device_index(device);
    if (index < 0 || unsigned(index) != deviceIndex) continue;
    if (!mlx5dv_is_supported(device)) {
      code = ENOTSUP;
      break;
    }
    uint64_t guid = ibv_get_device_guid(device);
    for (const auto &entry : m_contexts)
      if (entry.index == index && entry.guid == guid) {
	verbs = entry.verbs;
	break;
      }
    if (!verbs) {
      verbs = ibv_open_device(device);
      if (verbs) {
	m_contexts.push(Context{verbs, index, guid});
      } else {
	code = errno;
      }
    }
    break;
  }
  ibv_free_device_list(devices);
  if (!verbs) return contextError("open mlx5 RDMA device", code);
  return verbs;
}

ZuUnion<void, ZeException> Env::final()
{
  while (m_contexts.length()) {
    auto &entry = m_contexts[m_contexts.length() - 1];
    if (int rc = ibv_close_device(entry.verbs))
      return contextError("close mlx5 RDMA device", rc);
    m_contexts.pop();
  }
  return {};
}

} // namespace Zpkt::Mlx
