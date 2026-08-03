#include <zlib/ZmBackTrace.hh>

extern
#ifdef _WIN32
ZuImport_API
#endif
ZmBackTrace xfoo2(ZmBackTrace (*fn)());

typedef ZmBackTrace (*Fn)();

ZmBackTrace baz(Fn fn) { return xfoo2(fn); }

ZmBackTrace bar(Fn fn) { return baz(fn); }

extern
#ifdef _WIN32
ZuExport_API
#endif
ZmBackTrace xfoo(Fn fn) { return bar(fn); }
