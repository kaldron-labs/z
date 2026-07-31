//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static file server support for zhttpd

#ifndef Zhttpd_HH
#define Zhttpd_HH

#include <errno.h>
#include <string.h>
#include <time.h>

#include <zlib/ZuBase64.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZuString.hh>
#include <zlib/ZuTL.hh>
#include <zlib/ZuUnroll.hh>

#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmHeap.hh>
#include <zlib/ZmLock.hh>

#include <zlib/ZtArray.hh>
#include <zlib/ZtScratch.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZfCLI.hh>
#include <zlib/ZfURI.hh>

#include <zlib/ZiDir.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>

#include <zlib/Zhttp.hh>
#include <zlib/ZhttpService.hh>

ZfCLIConfig(CLI,
  (ZfCLI_ArrayFmt<ZfCLI::Delimited, ZfCLI_Delimiter<';'>>));

namespace Zhttpd {

ZtEnumNS(Http2Mode, int8_t, force, prefer, disable);

constexpr unsigned FileChunk = 16<<10;
constexpr unsigned MimeFileMax = 16<<20;
constexpr unsigned DateBufSize = 32;
constexpr unsigned DirEntriesBuiltin = 64;
constexpr unsigned DirNameBuiltin = 256;

ZuDerive(HdrString, ZtString<ZtStringHeapID<"zhttpd.HdrString">>);
ZuDerive(PathOffsets,
  (ZtArray<unsigned, ZtArrayHeapID<"zhttpd.PathOffsets">>));

template <typename L>
void fileChunks(uint64_t len, L l) {
  while (len) {
    unsigned n = len > FileChunk ? FileChunk : unsigned(len);
    l(n);
    len -= n;
  }
}

template <typename Body>
void sendSpanChunks(Body &body, const char *p, uint64_t len) {
  fileChunks(len, [&body, &p](unsigned n) {
    body << ZuCSpan{p, n};
    p += n;
  });
}

ZuInline constexpr bool isspace__(char c) {
  return ((c >= '\t' && c <= '\r') || c == ' ');
}

ZuInline constexpr char lower__(char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

template <typename S>
inline void lower(S &s) {
  for (unsigned i = 0, n = s.length(); i < n; ++i) s[i] = lower__(s[i]);
}

template <typename S>
inline void lower(S &s, ZuCSpan src) {
  s.length(src.length());
  char *dst = s.data();
  const char *ptr = src.data();
  for (unsigned i = 0, n = src.length(); i < n; ++i) dst[i] = lower__(ptr[i]);
}

inline bool ieq(ZuCSpan a, ZuCSpan b) {
  if (a.length() != b.length()) return false;
  for (unsigned i = 0, n = a.length(); i < n; ++i)
    if (lower__(a[i]) != lower__(b[i])) return false;
  return true;
}

struct Forward {
  HdrString	host;
  HdrString	url;
};

struct Options {
  HdrString		root;
  HdrString		addr{"0.0.0.0"};
  unsigned		port = 8080;
  HdrString		cert;
  HdrString		key;
  HdrString		keyLog;
  HdrString		index{"index.html"};
  HdrString		mimetypes;
  HdrString		defaultMimetype{"application/octet-stream"};
  ZtArray<HdrString >	forward;
  ZtArray<Forward>	forwards;
  HdrString		forwardAll;
  HdrString		auth;
  HdrString		authUser;
  HdrString		authPass;
  HdrString		logPath{"-"};
  HdrString		pidfile;
  unsigned		maxconn = 0;
  unsigned		timeout = 30;
  bool			ipv6 = false;
  bool			daemon = false;
  bool			syslog = false;
  bool			noListing = false;
  bool			noKeepalive = false;
  bool			singleFile = false;
  bool			hideDotfiles = false;
  bool			forwardHttps = false;
  bool			noServerID = false;
  bool			http = true;
  bool			https = false;
  bool			http3 = false;
  Http2Mode::T		http2 = Http2Mode::prefer;
  bool			debug = false;
  bool			frag = false;
  bool			yield = false;
  HdrString		quicMigration{"passive"};
  uint32_t		quicHeartbeat = 0;
  unsigned		quicMigrationCIDReserve = 1;
  bool			quicMigrationCloseOnFailure = false;
#ifdef ZiMultiplex_FILTER
  HdrString		quicRxDrop;
  HdrString		quicTxDrop;
#endif
#ifdef Zquic_DEBUG
  uint32_t		quicDiag = 0;
#endif
  uint32_t		memDiag = 0;
  bool			help = false;
};

ZfStruct((Options, CLI),
  (((root),            (CLI::Arg<1>)),                           (String)),
  (((addr),            (CLI::Long<"addr">)),                     (String, "0.0.0.0")),
  (((port),            (CLI::Long<"port">)),                     (UInt32, 8080)),
  (((cert),            (CLI::Long<"cert">)),                     (String)),
  (((key),             (CLI::Long<"key">)),                      (String)),
  (((keyLog),          (CLI::Long<"key-log">)),                  (String)),
  (((index),           (CLI::Long<"index">)),                    (String, "index.html")),
  (((mimetypes),       (CLI::Long<"mimetypes">)),                (String)),
  (((defaultMimetype),
    (CLI::Long<"default-mimetype">)),
    (String, "application/octet-stream")),
  (((forward),         (CLI::Long<"forward">)),                  (StringVec)),
  (((forwardAll),      (CLI::Long<"forward-all">)),              (String)),
  (((auth),            (CLI::Long<"auth">)),                     (String)),
  (((logPath),         (CLI::Long<"log">)),                      (String, "-")),
  (((pidfile),         (CLI::Long<"pidfile">)),                  (String)),
  (((maxconn),         (CLI::Long<"maxconn">)),                  (UInt32)),
  (((timeout),         (CLI::Long<"timeout">)),                  (UInt32, 30)),
  (((ipv6),            (CLI::Long<"ipv6">)),                     (Bool)),
  (((daemon),          (CLI::Long<"daemon">)),                   (Bool)),
  (((syslog),          (CLI::Long<"syslog">)),                   (Bool)),
  (((noListing),       (CLI::Long<"no-listing">)),               (Bool)),
  (((noKeepalive),     (CLI::Long<"no-keepalive">)),             (Bool)),
  (((singleFile),      (CLI::Long<"single-file">)),              (Bool)),
  (((hideDotfiles),    (CLI::Long<"hide-dotfiles">)),            (Bool)),
  (((forwardHttps),    (CLI::Long<"forward-https">)),            (Bool)),
  (((noServerID),      (CLI::Long<"no-server-id">)),             (Bool)),
  (((http),            (CLI::Long<"http">)),                     (Bool, true)),
  (((https),           (CLI::Long<"https">)),                    (Bool)),
  (((http3),           (CLI::Long<"http3">)),                    (Bool)),
  (((http2),           (Enum<Http2Mode::Map>, CLI::Long<"http2">)),
								 (Int8, Http2Mode::prefer)),
  (((debug),           (CLI::Long<"debug">)),                    (Bool)),
  (((frag),            (CLI::Long<"frag">)),                     (Bool)),
  (((yield),           (CLI::Long<"yield">)),                    (Bool)),
  (((quicMigration),   (CLI::Long<"quic-migration">)),           (String, "passive")),
  (((quicHeartbeat),   (CLI::Long<"quic-heartbeat">)),           (UInt32)),
  (((quicMigrationCIDReserve),
    (CLI::Long<"quic-migration-cid-reserve">)),                  (UInt32, 1)),
  (((quicMigrationCloseOnFailure),
    (CLI::Long<"quic-migration-close-on-failure">)),             (Bool)),
#ifdef ZiMultiplex_FILTER
  (((quicRxDrop),      (CLI::Long<"quic-rx-drop">)),             (String)),
  (((quicTxDrop),      (CLI::Long<"quic-tx-drop">)),             (String)),
#endif
#ifdef Zquic_DEBUG
  (((quicDiag),        (CLI::Long<"quic-diag">)),                (UInt32)),
#endif
  (((memDiag),         (CLI::Long<"mem-diag">)),                 (UInt32)),
  (((help),            (CLI::Flag<'h'>, CLI::Long<"help">)),     (Bool)));

using RequestData = Zhttp::RequestInfo;

struct ResponsePlan {
  ResponsePlan() = default;
  ResponsePlan(const ResponsePlan &resp) :
    status{resp.status},
    reason{resp.reason},
    contentType{resp.contentType},
    location{resp.location},
    wwwAuthenticate{resp.wwwAuthenticate},
    lastModified{resp.lastModified},
    date{resp.date},
    etag{resp.etag},
    contentRange{resp.contentRange},
    allow{resp.allow},
    connection{resp.connection},
    server{resp.server},
    body{resp.body},
    filePath{resp.filePath},
    contentLength{resp.contentLength},
    fileOffset{resp.fileOffset},
    fileLength{resp.fileLength},
    sendBody{resp.sendBody},
    close{resp.close},
    file{resp.file},
    generated{resp.generated}
  {
    if (resp.fileHandle)
      fileHandle.dup(resp.fileHandle, ZiFile::GC);
  }
  ResponsePlan &operator =(const ResponsePlan &resp) {
    if (this != &resp) {
      this->~ResponsePlan();
      new (this) ResponsePlan{resp};
    }
    return *this;
  }
  ResponsePlan(ResponsePlan &&) = default;
  ResponsePlan &operator =(ResponsePlan &&) = default;

  unsigned		status = 500;
  HdrString		reason{"Internal Server Error"};
  HdrString		contentType;
  HdrString		location;
  HdrString		wwwAuthenticate;
  HdrString		lastModified;
  HdrString		date;
  HdrString		etag;
  HdrString		contentRange;
  HdrString		allow;
  HdrString		connection;
  HdrString		server;
  HdrString		body;
  ZiFile		fileHandle;
  HdrString		filePath;
  uint64_t		contentLength = 0;
  uint64_t		fileOffset = 0;
  uint64_t		fileLength = 0;
  bool			sendBody = false;
  bool			close = false;
  bool			file = false;
  bool			generated = false;
};

struct MimeMap {
  using String = ZtString<ZtStringHeapID<"Zhttpd.Mime.String">>;
  using Map = ZmHashKV<String, String, ZmHashHeapID<"Zhttpd.Mime">>;
  using Builtins = ZuTypeList<
    ZuTypeList<ZuStringT<"html">, ZuStringT<"text/html">>,
    ZuTypeList<ZuStringT<"htm">, ZuStringT<"text/html">>,
    ZuTypeList<ZuStringT<"txt">, ZuStringT<"text/plain">>,
    ZuTypeList<ZuStringT<"css">, ZuStringT<"text/css">>,
    ZuTypeList<ZuStringT<"js">, ZuStringT<"application/javascript">>,
    ZuTypeList<ZuStringT<"json">, ZuStringT<"application/json">>,
    ZuTypeList<ZuStringT<"png">, ZuStringT<"image/png">>,
    ZuTypeList<ZuStringT<"jpg">, ZuStringT<"image/jpeg">>,
    ZuTypeList<ZuStringT<"jpeg">, ZuStringT<"image/jpeg">>,
    ZuTypeList<ZuStringT<"gif">, ZuStringT<"image/gif">>,
    ZuTypeList<ZuStringT<"svg">, ZuStringT<"image/svg+xml">>,
    ZuTypeList<ZuStringT<"ico">, ZuStringT<"image/x-icon">>,
    ZuTypeList<ZuStringT<"wasm">, ZuStringT<"application/wasm">>,
    ZuTypeList<ZuStringT<"pdf">, ZuStringT<"application/pdf">>,
    ZuTypeList<ZuStringT<"mp3">, ZuStringT<"audio/mpeg">>,
    ZuTypeList<ZuStringT<"mp4">, ZuStringT<"video/mp4">>,
    ZuTypeList<ZuStringT<"webp">, ZuStringT<"image/webp">>>;

  ZmRef<Map>		map{new Map};

  void add(ZuCSpan ext, ZuCSpan mime) {
    String ext_{ext};
    lower(ext_);
    map->del(ext_);
    map->add(ZuMv(ext_), String{mime});
  }

  void init(const Options &options) {
    ZuUnroll::all<Builtins>([this]<typename KV>() {
      add(ZuType<0, KV>{}(), ZuType<1, KV>{}());
    });
    if (!options.mimetypes) return;
    ZiFile f;
    if (f.open(options.mimetypes, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      return;
    auto size = f.size();
    if (size <= 0 || size > MimeFileMax) return;
    auto text = ZtScratch(
      HdrString, unsigned(size), unsigned(size) + 1);
    int n = f.read(text.data(), text.length());
    if (n <= 0) return;
    text.length(unsigned(n));
    const char *p = text.data();
    const char *e = p + text.length();
    while (p < e) {
      while (p < e && isspace__(*p) && *p != '\r' && *p != '\n') ++p;
      if (p >= e) break;
      if (*p == '#' || *p == '\r' || *p == '\n') {
	while (p < e && *p != '\n') ++p;
	if (p < e) ++p;
	continue;
      }
      const char *b = p;
      while (p < e && !isspace__(*p)) ++p;
      ZuCSpan mime{b, unsigned(p - b)};
      while (p < e && *p != '\r' && *p != '\n') {
	while (p < e && isspace__(*p) && *p != '\r' && *p != '\n') ++p;
	if (p >= e || *p == '\r' || *p == '\n') break;
	b = p;
	while (p < e && !isspace__(*p)) ++p;
	add(ZuCSpan{b, unsigned(p - b)}, mime);
      }
      while (p < e && *p != '\n') ++p;
      if (p < e) ++p;
    }
  }

  ZuCSpan lookup(const Options &options, ZuCSpan path) const {
    int slash = -1, dot = -1;
    for (unsigned i = 0, n = path.length(); i < n; ++i) {
      if (path[i] == '/' || path[i] == '\\') slash = int(i);
      else if (path[i] == '.') dot = int(i);
    }
    if (dot < 0 || (slash >= 0 && dot < slash))
      return options.defaultMimetype;
    auto ext = ZtScratch(
      String, path.length() - unsigned(dot));
    lower(ext, ZuCSpan{path.data() + dot + 1,
      path.length() - unsigned(dot) - 1});
    if (auto node = map->find(ext)) return node->val();
    return options.defaultMimetype;
  }
};

struct LogSink {
  bool init(const Options &) { return true; }
  void final() { }
  void write(const RequestData &req, const ResponsePlan &resp, ZuCSpan remote) {
    auto target = escaped(req.target);
    auto referer = escaped(req.referer);
    auto agent = escaped(req.userAgent);
    auto date = logDate();
    ZiLOG(Info, "zhttpd.access", ([
      remote = ZeString{remote}, date = ZuMv(date), target = ZuMv(target),
      referer = ZuMv(referer), agent = ZuMv(agent), status = resp.status,
      length = resp.contentLength
    ](auto &s) mutable {
      s << remote << " - - [" << date << "] \"" << target << "\" " <<
	status << ' ' << length << " \"" <<
	(referer ? ZuCSpan{referer} : ZuCSpan{"-"}) << "\" \"" <<
	(agent ? ZuCSpan{agent} : ZuCSpan{"-"}) << '"';
    }));
  }

  static ZeString escaped(ZuCSpan s) {
    ZeString out;
    out.ensure(s.length());
    for (unsigned i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c == '"' || c == '\\') { out << '\\' << c; }
      else if (uint8_t(c) < 32) out << '?';
      else out << c;
    }
    return out;
  }
  static ZeString logDate() {
    auto buf = ZtScratch(HdrString, DateBufSize);
    time_t now = time(nullptr);
    struct tm tm_;
#ifdef _WIN32
    gmtime_s(&tm_, &now);
#else
    gmtime_r(&now, &tm_);
#endif
    size_t n = strftime(
      buf.data(), DateBufSize, "%d/%b/%Y:%H:%M:%S +0000", &tm_);
    buf.length(unsigned(n));
    return buf;
  }
};

struct State {
  Options		options;
  MimeMap		mime;
  LogSink		log;
  ZiFile		rootDir;
  ZiFile		rootFile;
  ZiFile::Stat		rootFileStat;
  ZmAtomic<uint64_t>	requests = 0;
  ZmAtomic<uint64_t>	errors = 0;
};

template <typename S>
inline void httpDate(S &s, time_t t) {
  auto buf = ZtScratch(HdrString, DateBufSize);
  struct tm tm_;
#ifdef _WIN32
  gmtime_s(&tm_, &t);
#else
  gmtime_r(&t, &tm_);
#endif
  size_t n = strftime(
    buf.data(), DateBufSize, "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  buf.length(unsigned(n));
  s.length(0);
  s << buf;
}

inline bool parseHTTPDate(ZuCSpan s, time_t &out) {
  auto in = ZtScratch(HdrString, s.length() + 1);
  in << s;
  struct tm tm_;
  memset(&tm_, 0, sizeof(tm_));
  char *p = strptime(in.ndata(), "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  if (!p || *p) return false;
#ifdef _WIN32
  out = _mkgmtime(&tm_);
#else
  out = timegm(&tm_);
#endif
  return out != time_t(-1);
}

inline void splitTarget(ZuCSpan target, ZuCSpan &path, ZuCSpan &query) {
  int64_t q = target.find([](auto c) { return c == '?'; });
  path = q < 0 ? target : ZuCSpan{target.data(), unsigned(q)};
  if (q < 0)
    query = {};
  else
    query = {target.data() + q + 1,
      target.length() - unsigned(q) - 1};
}

inline ZuCSpan pathComponent(ZuCSpan path, unsigned &offset)
{
  while (offset < path.length() && path[offset] == '/') ++offset;
  unsigned begin = offset;
  while (offset < path.length() && path[offset] != '/') ++offset;
  return {path.data() + begin, offset - begin};
}

inline bool decodeNormalizePath(
  ZuCSpan path_, bool hideDotfiles, auto &path, ZuCSpan &err)
{
  unsigned decodedSize = path_.length() + 2;
  auto decoded = ZtScratch(HdrString, decodedSize);
  if (!path_ || path_[0] != '/') decoded << '/';
  decoded << path_;
  auto offsets = ZtScratch(PathOffsets, (decoded.length() + 1)>>1);
  path.length(0);
  path << '/';
  auto append = [&path, &offsets, hideDotfiles, &err](ZuCSpan part) {
    if (!part || part == ".") return true;
    if (part == "..") {
      if (!offsets.length()) { err = "path traversal"; return false; }
      path.length(offsets.pop());
      return true;
    }
    if (hideDotfiles && part[0] == '.') {
      err = "hidden path";
      return false;
    }
    offsets.push(path.length());
    if (path.length() > 1) path << '/';
    path << part;
    return true;
  };
  ZuSpan<char> input{decoded};
  while (input) {
    auto scan = ZfURI::eoc(input);
    int n = scan.template p<0>();
    if (n < 0) { err = "bad percent escape"; return false; }
    ZuCSpan part{input.data(), unsigned(n)};
    for (unsigned i = 0; i < part.length(); ++i)
      if (!part[i]) { err = "NUL in path"; return false; }
    input.offset(scan.template p<1>());
    unsigned offset = 0;
    for (;;) {
      auto component = pathComponent(part, offset);
      if (!component) break;
      if (!append(component)) return false;
    }
  }
  return true;
}

template <typename S>
inline void staticPath(S &path, const Options &options, ZuCSpan clean)
{
  path << options.root;
  if (clean.length() > 1) {
    unsigned n = path.length();
    if (n && path[n - 1] != '/' && path[n - 1] != '\\') path << '/';
    path << ZuCSpan{clean.data() + 1, clean.length() - 1};
  }
}

template <typename S>
inline void htmlEsc(S &out, ZuCSpan s) {
  for (unsigned i = 0, n = s.length(); i < n; ++i) {
    char c = s[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out << c; break;
    }
  }
}

template <typename S>
inline void hostName(S &host, ZuCSpan host_) {
  if (host_ && host_[0] == '[') {
    auto p = host_.find([](auto c) { return c == ']'; });
    if (p >= 0) {
      lower(host, ZuCSpan{host_.data() + 1, unsigned(p - 1)});
      return;
    }
  }
  int p = -1, colons = 0;
  for (unsigned i = 0, n = host_.length(); i < n; ++i)
    if (host_[i] == ':') { p = int(i); ++colons; }
  lower(host, colons == 1 ? ZuCSpan{host_.data(), unsigned(p)} : host_);
}

inline bool constTimeEqual(ZuCSpan a, ZuCSpan b) {
  if (a.length() != b.length()) return false;
  unsigned char d = 0;
  for (unsigned i = 0; i < a.length(); ++i) d |= a[i] ^ b[i];
  return !d;
}

template <typename S>
inline void basicAuthValue(S &out, const Options &options) {
  auto raw = ZtScratch(
    HdrString, options.authUser.length() + options.authPass.length() + 2);
  raw << options.authUser << ':' << options.authPass;
  out << "Basic ";
  unsigned offset = out.length();
  out.length(offset + ZuBase64::enclen(raw.length()));
  auto n = ZuBase64::encode(ZuSpan<uint8_t>{out}.offset(offset), raw);
  out.length(offset + n);
}

struct StaticPlanner {
  StaticPlanner(State *state_) : state{state_} { }

  ResponsePlan plan(const RequestData &req) const {
    ResponsePlan resp;
    httpDate(resp.date, time(nullptr));
    if (!state->options.noServerID) resp.server = "zhttpd";
    if (state->options.noKeepalive || ieq(req.connection, "close") ||
	(req.http10 && !ieq(req.connection, "keep-alive"))) {
      resp.close = true;
      resp.connection = "close";
    }

    if (state->options.forwardAll)
      return redirect(req, state->options.forwardAll, resp);
    auto host = ZtScratch(HdrString, req.host.length() + 1);
    if (state->options.forwards.length()) hostName(host, req.host);
    for (unsigned i = 0; i < state->options.forwards.length(); ++i)
      if (host == state->options.forwards[i].host)
	return redirect(req, state->options.forwards[i].url, resp);
    if (state->options.forwardHttps && !req.secure) {
      auto url = ZtScratch(
        HdrString, req.host.length() + req.target.length() + 9);
      url << "https://" << req.host << req.target;
      return redirect(req, url, resp, false);
    }

    if (state->options.authUser) {
      auto expect = ZtScratch(HdrString,
        ZuBase64::enclen(state->options.authUser.length() +
	  state->options.authPass.length() + 1) + 7);
      basicAuthValue(expect, state->options);
      if (!constTimeEqual(req.authorization, expect)) {
	resp.status = 401;
	resp.reason = "Unauthorized";
	resp.wwwAuthenticate = "Basic realm=\"zhttpd\"";
	resp.contentType = "text/plain";
	resp.body = "Unauthorized\n";
	resp.contentLength = resp.body.length();
	resp.sendBody = req.method != Zhttp::Method::HEAD;
	resp.generated = true;
	return resp;
      }
    }

    if (req.method != Zhttp::Method::GET && req.method != Zhttp::Method::HEAD) {
      resp.status = 405;
      resp.reason = "Method Not Allowed";
      resp.allow = "GET, HEAD";
      resp.contentType = "text/plain";
      resp.body = "Method Not Allowed\n";
      resp.contentLength = resp.body.length();
      resp.sendBody = true;
      resp.generated = true;
      return resp;
    }

    ZuCSpan path, query;
    splitTarget(req.target, path, query);
    auto clean = ZtScratch(HdrString, path.length() + 2);
    ZuCSpan err;
    if (!decodeNormalizePath(path, state->options.hideDotfiles, clean, err))
      return error(resp, 400, "Bad Request", err, req.method);
    if (path.length() > 1 && path[path.length() - 1] == '/' &&
	clean != "/")
      clean << '/';

    if (state->options.singleFile) return singleFile(req, clean, resp);
    return fileOrDir(req, clean, query, resp);
  }

  ResponsePlan redirect(
    const RequestData &req, ZuCSpan base, ResponsePlan resp,
    bool appendTarget = true) const {
    resp.status = 301;
    resp.reason = "Moved Permanently";
    resp.location = base;
    if (appendTarget) resp.location << req.target;
    resp.contentType = "text/plain";
    resp.body = "Moved Permanently\n";
    resp.contentLength = resp.body.length();
    resp.generated = true;
    resp.sendBody = req.method != Zhttp::Method::HEAD;
    return resp;
  }

  ResponsePlan error(
    ResponsePlan resp, unsigned status, ZuCSpan reason, ZuCSpan text,
    Zhttp::Method::T method) const {
    resp.status = status;
    resp.reason = reason;
    resp.contentType = "text/plain";
    resp.body << text << "\n";
    resp.contentLength = resp.body.length();
    resp.generated = true;
    resp.sendBody = method != Zhttp::Method::HEAD && status != 304;
    return resp;
  }

  ResponsePlan singleFile(
    const RequestData &req, ZuCSpan clean, ResponsePlan resp) const {
    auto leaf = ZtScratch(HdrString, state->options.root.length() + 2);
    leaf << '/' << ZiFile::leafname(state->options.root);
    if (clean != "/" && clean != leaf) return notFound(resp, req.method);
    ZiFile file;
    if (file.dup(state->rootFile, ZiFile::GC) != Zi::OK)
      return error(resp, 403, "Forbidden", "Forbidden", req.method);
    return regularFile(req, ZuMv(file), state->rootFileStat,
      state->options.root, resp);
  }

  ResponsePlan fileOrDir(
    const RequestData &req, ZuCSpan clean, ZuCSpan query,
    ResponsePlan resp) const {
    ZiFile dir;
    if (openDir(clean, dir) == Zi::OK) {
      if (!clean || clean[clean.length() - 1] != '/') {
	resp.location = clean;
	resp.location << '/';
	if (query) {
	  resp.location << '?' << query;
	}
	resp.status = 301;
	resp.reason = "Moved Permanently";
	resp.contentType = "text/plain";
	resp.body = "Moved Permanently\n";
	resp.contentLength = resp.body.length();
	resp.generated = true;
	resp.sendBody = req.method != Zhttp::Method::HEAD;
	return resp;
      }
      ZiFile index;
      if (index.openAt(dir, state->options.index,
	  ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) == Zi::OK) {
	ZiFile::Stat stat;
	if (index.fstat(stat) == Zi::OK && stat.regular) {
	  auto indexPath = ZtScratch(Zi::Path,
	    state->options.root.length() + clean.length() +
	      state->options.index.length() + 2);
	  staticPath(indexPath, state->options, clean);
	  if (indexPath && indexPath[indexPath.length() - 1] != '/' &&
	      state->options.index)
	    indexPath << '/';
	  indexPath << state->options.index;
	  return regularFile(req, ZuMv(index), stat, indexPath, resp);
	}
      }
      if (state->options.noListing)
	return error(resp, 403, "Forbidden", "Forbidden", req.method);
      auto path = ZtScratch(
	Zi::Path, state->options.root.length() + clean.length() + 1);
      staticPath(path, state->options, clean);
      return listing(req, path, clean, resp);
    }
    if (clean && clean[clean.length() - 1] == '/') return notFound(resp, req.method);
    ZiFile file;
    ZiFile::Stat stat;
    int rc = openFile(clean, file, stat);
    if (rc != Zi::OK) return notFound(resp, req.method);
    if (!stat.regular)
      return error(resp, 403, "Forbidden", "Forbidden", req.method);
    auto path = ZtScratch(
      Zi::Path, state->options.root.length() + clean.length() + 1);
    staticPath(path, state->options, clean);
    return regularFile(req, ZuMv(file), stat, path, resp);
  }

  ResponsePlan notFound(ResponsePlan resp, Zhttp::Method::T method) const {
    return error(resp, 404, "Not Found", "Not Found", method);
  }

  ResponsePlan regularFile(
    const RequestData &req, ZiFile file, const ZiFile::Stat &stat,
    ZuCSpan path,
    ResponsePlan resp) const {
    uint64_t size = stat.size;
    time_t mtime = stat.mtime ? stat.mtime.as_time_t() : time(nullptr);
    time_t ims;
    if (parseHTTPDate(req.ifModifiedSince, ims) && mtime <= ims) {
      resp.status = 304;
      resp.reason = "Not Modified";
      httpDate(resp.lastModified, mtime);
      resp.contentLength = 0;
      resp.sendBody = false;
      return resp;
    }

    uint64_t first = 0, length = size;
    bool ranged = false, unsat = false;
    parseRange(req.range, size, first, length, ranged, unsat);
    if (unsat) {
      resp.status = 416;
      resp.reason = "Range Not Satisfiable";
      resp.contentRange << "bytes */" << size;
      resp.contentLength = 0;
      resp.sendBody = false;
      return resp;
    }

    resp.status = ranged ? 206 : 200;
    resp.reason = ranged ? "Partial Content" : "OK";
    resp.contentType = state->mime.lookup(state->options, path);
    httpDate(resp.lastModified, mtime);
    resp.fileHandle = ZuMv(file);
    resp.filePath = path;
    resp.contentLength = length;
    resp.fileOffset = first;
    resp.fileLength = length;
    resp.file = true;
    resp.sendBody = req.method != Zhttp::Method::HEAD && length > 0;
    if (ranged) {
      resp.contentRange << "bytes " << first << '-' <<
	(first + length - 1) << '/' << size;
    }
    return resp;
  }

  int openDir(ZuCSpan clean, ZiFile &dir) const {
    ZiFile cur;
    if (cur.dup(state->rootDir, ZiFile::GC) != Zi::OK) return Zi::IOError;
    unsigned offset = 0;
    while (auto part = pathComponent(clean, offset)) {
      ZiFile next;
      if (next.openAt(cur, part,
	  ZiFile::ReadOnly | ZiFile::Directory |
	    ZiFile::NoFollow | ZiFile::GC) != Zi::OK)
	return Zi::IOError;
      cur = ZuMv(next);
    }
    dir = ZuMv(cur);
    return Zi::OK;
  }

  int openFile(
    ZuCSpan clean, ZiFile &file, ZiFile::Stat &stat) const {
    unsigned offset = 0;
    ZuCSpan part = pathComponent(clean, offset);
    if (!part) return Zi::IOError;
    ZiFile dir;
    if (dir.dup(state->rootDir, ZiFile::GC) != Zi::OK) return Zi::IOError;
    for (;;) {
      ZuCSpan nextPart = pathComponent(clean, offset);
      if (!nextPart) break;
      ZiFile next;
      if (next.openAt(dir, part,
	  ZiFile::ReadOnly | ZiFile::Directory |
	    ZiFile::NoFollow | ZiFile::GC) != Zi::OK)
	return Zi::IOError;
      dir = ZuMv(next);
      part = nextPart;
    }
    ZiFile out;
    if (out.openAt(dir, part,
	ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) != Zi::OK)
      return Zi::IOError;
    if (out.fstat(stat) != Zi::OK) return Zi::IOError;
    file = ZuMv(out);
    return Zi::OK;
  }

  static void parseRange(
    ZuCSpan range_, uint64_t size, uint64_t &first, uint64_t &length,
    bool &ranged, bool &unsat) {
    ZuCSpan range{range_};
    if (!range.match<"bytes=">()) return;
    ZuCSpan spec{range.data() + 6, range.length() - 6};
    if (spec.find([](auto c) { return c == ','; }) >= 0) return;
    auto dash = spec.find([](auto c) { return c == '-'; });
    if (dash < 0) return;
    ZuCSpan a{spec.data(), unsigned(dash)};
    ZuCSpan b{spec.data() + dash + 1, spec.length() - unsigned(dash) - 1};
    auto isnum = [](ZuCSpan s) {
      if (!s) return false;
      for (unsigned i = 0, n = s.length(); i < n; ++i)
	if (s[i] < '0' || s[i] > '9') return false;
      return true;
    };
    auto toU64 = [](ZuCSpan s, uint64_t &v) {
      v = 0;
      for (unsigned i = 0; i < s.length(); ++i) {
	unsigned digit = unsigned(s[i] - '0');
	if (v > (UINT64_MAX - digit) / 10) return false;
	v = v * 10 + digit;
      }
      return true;
    };
    if (!a) {
      if (!isnum(b)) return;
      uint64_t suffix;
      if (!toU64(b, suffix)) return;
      if (!suffix) { unsat = true; return; }
      if (suffix >= size) { first = 0; length = size; }
      else { first = size - suffix; length = suffix; }
      ranged = true;
      return;
    }
    if (!isnum(a) || (b && !isnum(b))) return;
    uint64_t start;
    if (!toU64(a, start)) return;
    if (start >= size) { unsat = true; return; }
    uint64_t end;
    if (b) {
      if (!toU64(b, end)) return;
    } else
      end = size - 1;
    if (end < start) { unsat = true; return; }
    if (end >= size) end = size - 1;
    first = start;
    length = end - start + 1;
    ranged = true;
  }

  ResponsePlan listing(
    const RequestData &req, const Zi::Path &path,
    ZuCSpan clean, ResponsePlan resp) const {
    struct Entry {
      HdrString name;
      bool dir = false;
      uint64_t size = 0;
      time_t mtime = 0;
    };
    using Entries = ZtArray<Entry,
      ZtArrayHeapID<"zhttpd.DirEntries">>;
    auto entries = ZtScratch(Entries, DirEntriesBuiltin);
    ZiDir dir;
    auto name = ZtScratch(Zi::Path, DirNameBuiltin);
    if (dir.open(path) == Zi::OK) {
      while (dir.read(name) == Zi::OK) {
      if (name == "." || name == "..") continue;
      if (state->options.hideDotfiles && name && name[0] == '.')
	continue;
      auto *entry = new (entries.push()) Entry();
      entry->name = name;
      auto child = ZtScratch(
	Zi::Path, path.length() + name.length() + 2);
      child << path;
#ifndef _WIN32
      child << '/';
#else
      child << L"\\";
#endif
      child << name;
      ZiFile file;
      ZiFile::Stat stat;
      if (file.open(child,
	  ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) == Zi::OK &&
	  file.fstat(stat) == Zi::OK) {
	entry->dir = stat.directory;
	entry->size = stat.size;
	entry->mtime = stat.mtime ? stat.mtime.as_time_t() : 0;
      } else if (file.open(child,
	  ZiFile::ReadOnly | ZiFile::Directory |
	    ZiFile::NoFollow | ZiFile::GC) == Zi::OK &&
	  file.fstat(stat) == Zi::OK) {
	entry->dir = stat.directory;
	entry->size = stat.size;
	entry->mtime = stat.mtime ? stat.mtime.as_time_t() : 0;
      }
      }
    }
    ZuSort(entries.data(), entries.length(),
      [](const Entry &a, const Entry &b) {
	return ZuCmp<HdrString>::cmp(a.name, b.name);
      });
    auto &html = resp.body;
    html += "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of ";
    htmlEsc(html, clean);
    html += "</title></head><body><h1>Index of ";
    htmlEsc(html, clean);
    html += "</h1><pre>";
    if (clean != "/") html += "<a href=\"../\">../</a>\n";
    for (unsigned i = 0, n = entries.length(); i < n; ++i) {
      auto &e = entries[i];
      html += "<a href=\"";
      ZfURI::PathQuote::quote(html, e.name);
      if (e.dir) html << '/';
      html += "\">";
      htmlEsc(html, e.name);
      if (e.dir) html << '/';
      html += "</a>";
      if (!e.dir) html << ' ' << e.size;
      if (e.mtime) {
	auto date = ZtScratch(HdrString, DateBufSize);
	httpDate(date, e.mtime);
	html << ' ' << date;
      }
      html += "\n";
    }
    html += "</pre>";
    if (!state->options.noServerID)
      html += "<hr><address>zhttpd</address>";
    html += "</body></html>\n";
    resp.status = 200;
    resp.reason = "OK";
    resp.contentType = "text/html";
    resp.contentLength = resp.body.length();
    resp.generated = true;
    resp.sendBody = req.method != Zhttp::Method::HEAD;
    return resp;
  }

  State	*state = nullptr;
};

inline bool parseForward(Options &options, ZuCSpan host, ZuCSpan url) {
  if (!host || !url) return false;
  auto *f = new (options.forwards.push()) Forward();
  lower(f->host, host);
  f->url = url;
  return true;
}

inline bool parseForward(Options &options, ZuCSpan spec) {
  auto comma = spec.find([](auto c) { return c == ','; });
  if (comma <= 0 || unsigned(comma) + 1 >= spec.length()) return false;
  return parseForward(options,
    ZuCSpan{spec.data(), unsigned(comma)},
    ZuCSpan{spec.data() + comma + 1, spec.length() - unsigned(comma) - 1});
}

inline bool parseAuth(Options &options, ZuCSpan auth) {
  auto colon = auth.find([](auto c) { return c == ':'; });
  if (colon <= 0) return false;
  options.authUser = ZuCSpan{auth.data(), unsigned(colon)};
  options.authPass = ZuCSpan{auth.data() + colon + 1,
    auth.length() - unsigned(colon) - 1};
  return true;
}

inline bool loadOptions(
  Options &options, int argc, const char *const *argv, bool &help) {
  bool httpSet = false;
  for (int i = 1; i < argc; ++i) {
    ZuCSpan arg{argv[i]};
    if (arg == "--http") httpSet = true;
  }
  int argc_ = ZfCLI::load(options, argc, argv);
  help = options.help;
  if (help) return true;
  if (argc_ != 2) return false;
  if (options.port > 65535) return false;
  if (options.http2 < 0 || options.http2 >= Http2Mode::N) return false;
  for (unsigned i = 0, n = options.forward.length(); i < n; ++i)
    if (!parseForward(options, options.forward[i])) return false;
  if (options.auth && !parseAuth(options, options.auth)) return false;
  if (!httpSet && (options.https || options.http3)) options.http = false;
  return true;
}

template <typename S>
inline bool initFileState(State &state, S &error) {
  if (state.options.singleFile) {
    if (state.rootFile.open(state.options.root,
	ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) != Zi::OK) {
      error = "failed to open root file";
      return false;
    }
    if (state.rootFile.fstat(state.rootFileStat) != Zi::OK ||
	!state.rootFileStat.regular) {
      error = "root is not a regular file";
      return false;
    }
    return true;
  }
  if (state.rootDir.open(state.options.root,
      ZiFile::ReadOnly | ZiFile::Directory | ZiFile::GC) != Zi::OK) {
    error = "failed to open root directory";
    return false;
  }
  ZiFile::Stat stat;
  if (state.rootDir.fstat(stat) != Zi::OK || !stat.directory) {
    error = "root is not a directory";
    return false;
  }
  return true;
}

template <typename S>
inline bool validate(Options &options, S &error) {
  if (!options.root) { error = "root required"; return false; }
  if ((options.https || options.http3) && (!options.cert || !options.key)) {
    error = "--https/--http3 require --cert and --key";
    return false;
  }
  ZiFile root;
  ZiFile::Stat stat;
  if (options.singleFile) {
    if (root.open(options.root,
	  ZiFile::ReadOnly | ZiFile::NoFollow | ZiFile::GC) != Zi::OK ||
	root.fstat(stat) != Zi::OK || !stat.regular) {
      error = "--single-file root is not a readable regular file";
      return false;
    }
  } else {
    if (root.open(options.root,
	  ZiFile::ReadOnly | ZiFile::Directory | ZiFile::GC) != Zi::OK ||
	root.fstat(stat) != Zi::OK || !stat.directory) {
      error = "root is not a directory";
      return false;
    }
  }
  return true;
}

int run(int argc, const char *const *argv);

} // namespace Zhttpd

#endif /* Zhttpd_HH */
