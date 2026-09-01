//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// daemon-ization

#include <zlib/ZiDaemon.hh>

#include <zlib/ZmSingleton.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiPIDFile.hh>
#include <zlib/ZiProgram.hh>

#ifndef _WIN32
#include <sys/types.h>
#include <pwd.h>
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4996)
#endif

int ZiDaemon::init(
  const char *username, const char *password,
  int umask, bool daemonize, const Zi::Path &pidName)
{
#ifndef _WIN32

  // set uid/gid

  if (username) {
    struct passwd *p = getpwnam(username);

    if (!p) {
      ZiLOG(Error, "ZiDaemon", ([username](auto &s) {
	s << "getpwnam(\"" << username << "\") failed";
      }));
    } else {
      setregid(p->pw_gid, p->pw_gid);
      setreuid(p->pw_uid, p->pw_uid);
    }
  }

  // set umask

  if (umask >= 0) ::umask(umask);

  // daemon-ize

  if (daemonize) {
    close(0);

    switch (fork()) {
    case -1:
      ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	s << "fork() failed: " << e.message() << "";
      }));
      return Error;
      break;
    case 0:
      ZiLog::forked();
      setsid();
      break;
    default:
      _exit(0);
      break;
    }
  }

#else

  if (username || daemonize) {
    // on Windows, re-invoke the same program unless ZiDaemon is set
    bool daemon = false;

    auto cmdLine = ZiProgram::cmdLine();
    auto program = ZtWString<>{} << ZiProgram::name(cmdLine);
    if (!program) return Error;

    {
      wchar_t *s = _wgetenv(L"_ZiDaemon");

      if (program == s) daemon = true;
    }

    if (!daemon) {
      _wputenv(ZtWString<>{L"_ZiDaemon="} << program);

      STARTUPINFOW si;
      memset(&si, 0, sizeof(si));
      si.cb = sizeof(si);
      if (daemonize) {
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;
      }

      DWORD flags = CREATE_UNICODE_ENVIRONMENT;
      if (daemonize) flags |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

      PROCESS_INFORMATION pi;

      if (!username) {
	// re-invoke same program
	if (!CreateProcessW(
	      program, cmdLine, 0, 0, FALSE, flags, 0, 0, &si, &pi)) {
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "CreateProcess failed: " << e;
	  }));
	  return Error;
	}
      } else {
	// re-invoke same program as impersonated user
	HANDLE user;
	if (!LogonUserW(
	      ZtWString<>(username), 0, ZtWString<>(password),
	      LOGON32_LOGON_BATCH, LOGON32_PROVIDER_DEFAULT, &user
	    )) {
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "LogonUser failed: " << e;
	  }));
	  return Error;
	}

	HANDLE token;
	if (!DuplicateTokenEx(
	      user, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
	      TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID, 0,
	      SecurityImpersonation, TokenPrimary, &token
	    )) {
	  CloseHandle(user);
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "DuplicateTokenEx failed: " << e;
	  }));
	  return Error;
	}

	CloseHandle(user);

	if (!CreateProcessAsUserW(
	    token, program, cmdLine,
	    0, 0, FALSE, flags, 0, 0, &si, &pi)) {
	  CloseHandle(token);
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "CreateProcessAsUser failed: " << e;
	  }));
	  return Error;
	}

	CloseHandle(token);
      }

      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);

      ExitProcess(0);
    }
  }

#endif /* !_WIN32 */

  // create / check PID file
  if (pidName) {
    auto file = ZmSingleton<ZiPIDFile>::instance();
    int r = file->init(pidName);
    if (r == ZiPIDFile::Running) {
      ZiLOG(Error, "ZiDaemon", ([pid = file->pid()](auto &s) {
	s << "PID " << pid << " still running";
      }));
      return Running;
    }
    if (r != ZiPIDFile::OK) {
      ZiLOG(Error, "ZiDaemon", ([
	f = ZeString{file->path()},
	e = file->error()](auto &s) {
	s << "PID file " << f << ": " << e;
      }));
      return Error;
    }
  }

  return OK;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
