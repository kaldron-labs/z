//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// daemon-ization

#include <zlib/ZiDaemon.hh>

#include <zlib/ZiLog.hh>
#include <zlib/ZiFile.hh>

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
  int umask, bool daemonize, const char *pidFile)
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

    // get path to current program
    ZtWString<> path{Zi::PathMax};
    DWORD pathLen = GetModuleFileNameW(
	0, path.data(), static_cast<DWORD>(path.size()));
    if (!pathLen || pathLen >= path.size()) {
      ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	s << "GetModuleFileName failed: " << e;
      }));
      return Error;
    }
    path.length(pathLen);
    path.truncate();

    {
      wchar_t *s = _wgetenv(L"_ZiDaemon");

      if (path == s) daemon = true;
    }

    if (!daemon) {
      _wputenv(ZtWString<>{L"_ZiDaemon="} << path);

      // get command line
      ZtWString<> commandLine(GetCommandLineW());

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
	      path, commandLine, 0, 0, FALSE, flags, 0, 0, &si, &pi)) {
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
	    token, path, commandLine,
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

  if (pidFile) {
    ZiFile file;
    ZuCArray<16> buf;

    if (file.open(pidFile,
	ZiFile::Create | ZiFile::Exclusive | ZiFile::GC, 0644) != Zi::OK) {
      if (file.open(pidFile, ZiFile::GC, 0) != Zi::OK) {
	ZiLOG(Error, "ZiDaemon", ([f = ZeString{pidFile}, e = file.error()](auto &s) {
	  s << "open(" << f << "): " << e;
	}));
	return Error;
      }

      int n;

      if ((n = file.read(buf.data(), 15)) < 0) {
	ZiLOG(Error, "ZiDaemon", ([f = ZeString{pidFile}, e = file.error()](auto &s) {
	  s << "read(" << f << "): " << e;
	}));
	return Error;
      }

      buf.length(n);

      ZuBox<int> pid(buf);

      if (pid > 0) {
#ifndef _WIN32
	int i = kill(pid, 0);

	if (i >= 0 || (i < 0 && errno == EPERM)) {
	  ZiLOG(Error, "ZiDaemon", ([pid](auto &s) {
	    s << "PID " << pid << " still running";
	  }));
	  return Running;
	}
#else
	HANDLE h = OpenProcess(
	    PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);

	if (h) {
	  DWORD exitCode = 0;
	  bool running =
	    WaitForSingleObject(h, 0) == WAIT_TIMEOUT &&
	    GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE;
	  CloseHandle(h);
	  if (running) {
	    ZiLOG(Error, "ZiDaemon", ([pid](auto &s) {
	      s << "PID " << pid << " still running";
	    }));
	    return Running;
	  }
	} else if (GetLastError() == ERROR_ACCESS_DENIED) {
	  ZiLOG(Error, "ZiDaemon", ([pid](auto &s) {
	    s << "PID " << pid << " still running";
	  }));
	  return Running;
	}
#endif
      }

      file.seek(0);
    }

    buf.null();
    buf << ZuBox<int>(Zm::getPID());

    if (file.write(buf.data(), buf.length()) != Zi::OK) {
      ZiLOG(Error, "ZiDaemon", ([f = ZeString{pidFile}, e = file.error()](auto &s) {
	s << "write(" << f << "): " << e;
      }));
      return Error;
    }
  }

  return OK;
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
