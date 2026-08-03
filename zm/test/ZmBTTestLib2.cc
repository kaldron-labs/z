#include <zlib/ZmBackTrace.hh>

typedef ZmBackTrace (*Fn)();

ZmBackTrace baz2(Fn fn) { return fn(); }

ZmBackTrace bar2(Fn fn) { return baz2(fn); }

extern
#ifdef _WIN32
ZuExport_API
#endif
ZmBackTrace xfoo2(Fn fn) { return bar2(fn); }
