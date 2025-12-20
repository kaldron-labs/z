//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

// daemon-ization

#ifndef ZvDaemon_HH
#define ZvDaemon_HH

#ifndef ZvLib_HH
#include <zlib/ZvLib.hh>
#endif

class ZvAPI ZvDaemon {
  ZvDaemon() = delete;
  ZvDaemon(const ZvDaemon &) = delete;
  ZvDaemon &operator =(const ZvDaemon &) = delete;

public:
  enum { OK = 0, Error = -1, Running = -2 };

  // * Unix
  //   - if daemonize is true the calling program is forked
  //   - if username is set the calling program must be running as root
  //   - password is ignored
  //
  // * Windows
  //   - the calling program is terminated and re-invoked regardless
  //   - if username is set, the calling user must have the
  //	 "Replace a process level token" right; this is granted by default
  //	 to local and network services but not to regular programs run by
  //	 Administrators; go to Control Panel / Administrative Tools /
  //	 Local Security Policy and add the users/groups needed
  //   - umask is ignored and has no effect
  //
  // * If forked (Unix) or re-invoked (Windows):
  //   - All running threads will be terminated
  //   - All open files and sockets will be closed
  //
  // * If daemonize is true
  //   - Standard input, output and error are closed and become unavailable

  static int init(
      const char *username,
      const char *password,
      int umask,
      bool daemonize,
      const char *pidFile);
};

#endif /* ZvDaemon_HH */
