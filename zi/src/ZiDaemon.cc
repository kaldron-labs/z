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
    GetModuleFileName(0, path.data(), path.size());
    path.calcLength();
    path.truncate();

    {
      wchar_t *s = _wgetenv(L"_ZiDaemon");

      if (path == s) daemon = true;
    }

    if (!daemon) {
      _wputenv(ZtWString<>{L"_ZiDaemon="} << path);

      // get command line
      ZtWString<> commandLine(GetCommandLine());

      STARTUPINFO si;
      memset(&si, 0, sizeof(si));
      si.cb = sizeof(si);
      if (daemonize) {
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = INVALID_HANDLE_VALUE;
	si.hStdOutput = INVALID_HANDLE_VALUE;
	si.hStdError = INVALID_HANDLE_VALUE;
      }

      int flags = CREATE_UNICODE_ENVIRONMENT;
      if (daemonize) flags |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;

      PROCESS_INFORMATION pi;

      if (!username) {
	// re-invoke same program
	if (!CreateProcess(
	      path, commandLine, 0, 0, TRUE, flags, 0, 0, &si, &pi)) {
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "CreateProcess failed: " << e;
	  }));
	  return Error;
	}
      } else {
	// re-invoke same program as impersonated user
	HANDLE user;
	if (!LogonUser(
	      ZtWString<>(username), 0, ZtWString<>(password),
	      LOGON32_LOGON_NETWORK, LOGON32_PROVIDER_DEFAULT, &user
	    )) {
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "LogonUser failed: " << e;
	  }));
	  return Error;
	}

	HANDLE token;
	if (!DuplicateTokenEx(
	      user, TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY, 0,
	      SecurityImpersonation, TokenPrimary, &token
	    )) {
	  CloseHandle(user);
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "DuplicateTokenEx failed: " << e;
	  }));
	  return Error;
	}

	CloseHandle(user);

	if (!ImpersonateLoggedOnUser(token)) {
	  CloseHandle(token);
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "ImpersonateLoggedOnUser failed: " << e;
	  }));
	  return Error;
	}

	SECURITY_DESCRIPTOR sd;
	memset(&sd, 0, sizeof(sd));
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, -1, 0, 0);

	SECURITY_ATTRIBUTES sa;
	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = &sd;
	sa.bInheritHandle = FALSE;

	static wchar_t desktop_[] = L"Winsta0\\Default";
	si.lpDesktop = &desktop_[0];

	if (!CreateProcessAsUser(
	    token, path, commandLine,
	    &sa, 0, TRUE, flags, 0, 0, &si, &pi)) {
	  RevertToSelf();
	  CloseHandle(token);
	  ZiLOG(Fatal, "ZiDaemon", ([e = ZeLastError](auto &s) {
	    s << "CreateProcessAsUser failed: " << e;
	  }));
	  return Error;
	}

	RevertToSelf();
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
	HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);

	if (h) {
	  CloseHandle(h);
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
