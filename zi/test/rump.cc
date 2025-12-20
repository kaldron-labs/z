//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2024 Psi Labs
// This code is licensed by the MIT license (see LICENSE for details)

#include <errno.h>

#include "rump.hh"

int rump_errno(int e)
{
  switch (e) {
    case RUMP_EPERM: return EPERM;
    case RUMP_ENOENT: return ENOENT;
    case RUMP_ESRCH: return ESRCH;
    case RUMP_EINTR: return EINTR;
    case RUMP_EIO: return EIO;
    case RUMP_ENXIO: return ENXIO;
    case RUMP_E2BIG: return E2BIG;
    case RUMP_ENOEXEC: return ENOEXEC;
    case RUMP_EBADF: return EBADF;
    case RUMP_ECHILD: return ECHILD;
    case RUMP_EDEADLK: return EDEADLK;
    case RUMP_ENOMEM: return ENOMEM;
    case RUMP_EACCES: return EACCES;
    case RUMP_EFAULT: return EFAULT;
    case RUMP_ENOTBLK: return ENOTBLK;
    case RUMP_EBUSY: return EBUSY;
    case RUMP_EEXIST: return EEXIST;
    case RUMP_EXDEV: return EXDEV;
    case RUMP_ENODEV: return ENODEV;
    case RUMP_ENOTDIR: return ENOTDIR;
    case RUMP_EISDIR: return EISDIR;
    case RUMP_EINVAL: return EINVAL;
    case RUMP_ENFILE: return ENFILE;
    case RUMP_EMFILE: return EMFILE;
    case RUMP_ENOTTY: return ENOTTY;
    case RUMP_ETXTBSY: return ETXTBSY;
    case RUMP_EFBIG: return EFBIG;
    case RUMP_ENOSPC: return ENOSPC;
    case RUMP_ESPIPE: return ESPIPE;
    case RUMP_EROFS: return EROFS;
    case RUMP_EMLINK: return EMLINK;
    case RUMP_EPIPE: return EPIPE;
    case RUMP_EDOM: return EDOM;
    case RUMP_ERANGE: return ERANGE;
    case RUMP_EAGAIN: return EAGAIN;
    // case RUMP_EWOULDBLOCK: return EWOULDBLOCK;
    case RUMP_EINPROGRESS: return EINPROGRESS;
    case RUMP_EALREADY: return EALREADY;
    case RUMP_ENOTSOCK: return ENOTSOCK;
    case RUMP_EDESTADDRREQ: return EDESTADDRREQ;
    case RUMP_EMSGSIZE: return EMSGSIZE;
    case RUMP_EPROTOTYPE: return EPROTOTYPE;
    case RUMP_ENOPROTOOPT: return ENOPROTOOPT;
    case RUMP_EPROTONOSUPPORT: return EPROTONOSUPPORT;
    case RUMP_ESOCKTNOSUPPORT: return ESOCKTNOSUPPORT;
    case RUMP_EOPNOTSUPP: return EOPNOTSUPP;
    case RUMP_EPFNOSUPPORT: return EPFNOSUPPORT;
    case RUMP_EAFNOSUPPORT: return EAFNOSUPPORT;
    case RUMP_EADDRINUSE: return EADDRINUSE;
    case RUMP_EADDRNOTAVAIL: return EADDRNOTAVAIL;
    case RUMP_ENETDOWN: return ENETDOWN;
    case RUMP_ENETUNREACH: return ENETUNREACH;
    case RUMP_ENETRESET: return ENETRESET;
    case RUMP_ECONNABORTED: return ECONNABORTED;
    case RUMP_ECONNRESET: return ECONNRESET;
    case RUMP_ENOBUFS: return ENOBUFS;
    case RUMP_EISCONN: return EISCONN;
    case RUMP_ENOTCONN: return ENOTCONN;
    case RUMP_ESHUTDOWN: return ESHUTDOWN;
    case RUMP_ETOOMANYREFS: return ETOOMANYREFS;
    case RUMP_ETIMEDOUT: return ETIMEDOUT;
    case RUMP_ECONNREFUSED: return ECONNREFUSED;
    case RUMP_ELOOP: return ELOOP;
    case RUMP_ENAMETOOLONG: return ENAMETOOLONG;
    case RUMP_EHOSTDOWN: return EHOSTDOWN;
    case RUMP_EHOSTUNREACH: return EHOSTUNREACH;
    case RUMP_ENOTEMPTY: return ENOTEMPTY;
    case RUMP_EPROCLIM: return EMFILE; //
    case RUMP_EUSERS: return EUSERS;
    case RUMP_EDQUOT: return EDQUOT;
    case RUMP_ESTALE: return ESTALE;
    case RUMP_EREMOTE: return EREMOTE;
    case RUMP_EBADRPC: return ENXIO; //
    case RUMP_ERPCMISMATCH: return ENXIO; //
    case RUMP_EPROGUNAVAIL: return ENXIO; //
    case RUMP_EPROGMISMATCH: return ENXIO; //
    case RUMP_EPROCUNAVAIL: return ENXIO; //
    case RUMP_ENOLCK: return ENOLCK;
    case RUMP_ENOSYS: return ENOSYS;
    case RUMP_EFTYPE: return ENOSYS; //
    case RUMP_EAUTH: return EPERM; //
    case RUMP_ENEEDAUTH: return EPERM; //
    case RUMP_EIDRM: return EIDRM;
    case RUMP_ENOMSG: return ENOMSG;
    case RUMP_EOVERFLOW: return EOVERFLOW;
    case RUMP_EILSEQ: return EILSEQ;
    case RUMP_ENOTSUP: return ENOTSUP;
    case RUMP_ECANCELED: return ECANCELED;
    case RUMP_EBADMSG: return EBADMSG;
    case RUMP_ENODATA: return ENODATA;
    case RUMP_ENOSR: return ENOSR;
    case RUMP_ENOSTR: return ENOSTR;
    case RUMP_ETIME: return ETIME;
    case RUMP_ENOATTR: return ENODATA; //
    case RUMP_EMULTIHOP: return EMULTIHOP;
    case RUMP_ENOLINK: return ENOLINK;
    case RUMP_EPROTO: return EPROTO;
    default: return e;
  }
}
