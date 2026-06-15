//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static file server support for zhttpd

#ifndef Zhttpd_HH
#define Zhttpd_HH

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <zlib/ZuBase64.hh>
#include <zlib/ZuPercent.hh>
#include <zlib/ZuSort.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmHash.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtCLI.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiDir.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zhttp.hh>

ZtCLIConfig(CLI,
  (ZtCLI_ArrayFmt<ZtCLI::Delimited, ZtCLI_Delimiter<';'>>));

namespace Zhttpd {

constexpr unsigned FileChunk = 16<<10;
constexpr unsigned MimeFileMax = 16<<20;

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

inline ZtString<> lower(ZuCSpan s_) {
  ZtString<> s{s_};
  for (unsigned i = 0, n = s.length(); i < n; ++i)
    if (s[i] >= 'A' && s[i] <= 'Z') s[i] += 'a' - 'A';
  return s;
}

struct Forward {
  ZtString<>	host;
  ZtString<>	url;
};

struct Options {
  ZtString<>		root;
  ZtString<>		addr{"0.0.0.0"};
  unsigned		port = 8080;
  ZtString<>		cert;
  ZtString<>		key;
  ZtString<>		index{"index.html"};
  ZtString<>		mimetypes;
  ZtString<>		defaultMimetype{"application/octet-stream"};
  ZtArray<ZtString<> >	forward;
  ZtArray<Forward>	forwards;
  ZtString<>		forwardAll;
  ZtString<>		auth;
  ZtString<>		authUser;
  ZtString<>		authPass;
  ZtString<>		logPath{"-"};
  ZtString<>		pidfile;
  ZtString<>		uid;
  ZtString<>		gid;
  unsigned		maxconn = 0;
  unsigned		timeout = 30;
  bool			ipv6 = false;
  bool			daemon = false;
  bool			syslog = false;
  bool			noListing = false;
  bool			chroot = false;
  bool			noKeepalive = false;
  bool			singleFile = false;
  bool			hideDotfiles = false;
  bool			forwardHttps = false;
  bool			noServerID = false;
  bool			http = true;
  bool			https = false;
  bool			http3 = false;
  bool			debug = false;
  bool			help = false;
};

ZtStruct((Options, CLI),
  (((root),            (CLI::Arg<1>)),                           (String)),
  (((addr),            (CLI::Long<"addr">)),                     (String, "0.0.0.0")),
  (((port),            (CLI::Long<"port">)),                     (UInt32, 8080)),
  (((cert),            (CLI::Long<"cert">)),                     (String)),
  (((key),             (CLI::Long<"key">)),                      (String)),
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
  (((uid),             (CLI::Long<"uid">)),                      (String)),
  (((gid),             (CLI::Long<"gid">)),                      (String)),
  (((maxconn),         (CLI::Long<"maxconn">)),                  (UInt32)),
  (((timeout),         (CLI::Long<"timeout">)),                  (UInt32, 30)),
  (((ipv6),            (CLI::Long<"ipv6">)),                     (Bool)),
  (((daemon),          (CLI::Long<"daemon">)),                   (Bool)),
  (((syslog),          (CLI::Long<"syslog">)),                   (Bool)),
  (((noListing),       (CLI::Long<"no-listing">)),               (Bool)),
  (((chroot),          (CLI::Long<"chroot">)),                   (Bool)),
  (((noKeepalive),     (CLI::Long<"no-keepalive">)),             (Bool)),
  (((singleFile),      (CLI::Long<"single-file">)),              (Bool)),
  (((hideDotfiles),    (CLI::Long<"hide-dotfiles">)),            (Bool)),
  (((forwardHttps),    (CLI::Long<"forward-https">)),            (Bool)),
  (((noServerID),      (CLI::Long<"no-server-id">)),             (Bool)),
  (((http),            (CLI::Long<"http">)),                     (Bool, true)),
  (((https),           (CLI::Long<"https">)),                    (Bool)),
  (((http3),           (CLI::Long<"http3">)),                    (Bool)),
  (((debug),           (CLI::Long<"debug">)),                    (Bool)),
  (((help),            (CLI::Flag<'h'>, CLI::Long<"help">)),     (Bool)));

struct RequestData {
  Zhttp::Method::T	method = -1;
  ZtString<>		target;
  ZtString<>		path;
  ZtString<>		query;
  ZtString<>		host;
  ZtString<>		authorization;
  ZtString<>		range;
  ZtString<>		ifModifiedSince;
  ZtString<>		connection;
  ZtString<>		referer;
  ZtString<>		userAgent;
  bool			h3 = false;
  bool			tls = false;
  bool			http10 = false;
};

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
  ZtString<>		reason{"Internal Server Error"};
  ZtString<>		contentType;
  ZtString<>		location;
  ZtString<>		wwwAuthenticate;
  ZtString<>		lastModified;
  ZtString<>		date;
  ZtString<>		etag;
  ZtString<>		contentRange;
  ZtString<>		allow;
  ZtString<>		connection;
  ZtString<>		server;
  ZtString<>		body;
  ZiFile		fileHandle;
  ZtString<>		filePath;
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
  using Map = ZmHashKV<String, String,
    ZmHashHeapID<"Zhttpd.Mime">>;
  Map			map;

  void add(ZuCSpan ext, ZuCSpan mime) {
    String ext_{lower(ext)};
    map.del(ext_);
    map.add(ZuMv(ext_), String{mime});
  }

  void init(const Options &options) {
    static const struct { const char *ext; const char *mime; } builtins[] = {
      {"html", "text/html"}, {"htm", "text/html"},
      {"txt", "text/plain"}, {"css", "text/css"},
      {"js", "application/javascript"}, {"json", "application/json"},
      {"png", "image/png"}, {"jpg", "image/jpeg"},
      {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
      {"svg", "image/svg+xml"}, {"ico", "image/x-icon"},
      {"wasm", "application/wasm"}, {"pdf", "application/pdf"},
      {"mp3", "audio/mpeg"}, {"mp4", "video/mp4"},
      {"webp", "image/webp"}
    };
    for (auto &m : builtins) add(m.ext, m.mime);
    if (!options.mimetypes) return;
    ZiFile f;
    if (f.open(options.mimetypes, ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      return;
    auto size = f.size();
    if (size <= 0 || size > MimeFileMax) return;
    ZtString<> text;
    text.length(unsigned(size));
    int n = f.read(text.data(), text.length());
    if (n <= 0) return;
    text.length(unsigned(n));
    const char *p = text.data();
    const char *e = p + text.length();
    while (p < e) {
      while (p < e && (*p == ' ' || *p == '\t')) ++p;
      if (p >= e) break;
      if (*p == '#' || *p == '\r' || *p == '\n') {
	while (p < e && *p != '\n') ++p;
	if (p < e) ++p;
	continue;
      }
      const char *b = p;
      while (p < e && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
	++p;
      ZtString<> mime{ZuCSpan{b, unsigned(p - b)}};
      while (p < e && *p != '\r' && *p != '\n') {
	while (p < e && (*p == ' ' || *p == '\t')) ++p;
	if (p >= e || *p == '\r' || *p == '\n') break;
	b = p;
	while (p < e && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
	  ++p;
	add(ZuCSpan{b, unsigned(p - b)}, mime);
      }
      while (p < e && *p != '\n') ++p;
      if (p < e) ++p;
    }
  }

  ZtString<> lookup(const Options &options, ZuCSpan path) const {
    int slash = -1, dot = -1;
    for (unsigned i = 0, n = path.length(); i < n; ++i) {
      if (path[i] == '/' || path[i] == '\\') slash = int(i);
      else if (path[i] == '.') dot = int(i);
    }
    if (dot < 0 || (slash >= 0 && dot < slash))
      return options.defaultMimetype;
    String ext{lower(ZuCSpan{path.data() + dot + 1,
      path.length() - unsigned(dot) - 1})};
    if (auto node = map.find(ext)) return ZuCSpan{node->val()};
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
      remote = ZtString<>{remote}, date = ZuMv(date), target = ZuMv(target),
      referer = ZuMv(referer), agent = ZuMv(agent), status = resp.status,
      length = resp.contentLength
    ](auto &s) mutable {
      s << remote << " - - [" << date << "] \"" << target << "\" " <<
	status << ' ' << length << " \"" <<
	(referer ? ZuCSpan{referer} : ZuCSpan{"-"}) << "\" \"" <<
	(agent ? ZuCSpan{agent} : ZuCSpan{"-"}) << '"';
    }));
  }

  static ZtString<> escaped(ZuCSpan s) {
    ZtString<> out;
    out.ensure(s.length());
    for (unsigned i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c == '"' || c == '\\') { out << '\\' << c; }
      else if (uint8_t(c) < 32) out << '?';
      else out << c;
    }
    return out;
  }
  static ZtString<> logDate() {
    char buf[64];
    time_t now = time(nullptr);
    struct tm tm_;
#ifdef _WIN32
    gmtime_s(&tm_, &now);
#else
    gmtime_r(&now, &tm_);
#endif
    size_t n = strftime(buf, sizeof(buf), "%d/%b/%Y:%H:%M:%S +0000", &tm_);
    return ZuCSpan{buf, unsigned(n)};
  }
};

struct State {
  Options		options;
  MimeMap		mime;
  LogSink		log;
  ZiFile		rootDir;
  ZiFile		rootFile;
  ZiFile::Stat		rootFileStat;
  ZmSemaphore		done;
  ZmAtomic<unsigned>	active = 0;
  ZmAtomic<uint64_t>	requests = 0;
  ZmAtomic<uint64_t>	errors = 0;
};

struct PathPolicy : ZuPercent::NoPlus {
  static constexpr bool term(uint8_t) { return false; }
  static constexpr bool esc(uint8_t) { return false; }
};

struct HrefPolicy : ZuPercent::NoTerm, ZuPercent::NoPlus {
  static constexpr bool esc(uint8_t c) {
    return !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
      c == '~' || c == '/');
  }
};

inline ZtString<> httpDate(time_t t) {
  char buf[64];
  struct tm tm_;
#ifdef _WIN32
  gmtime_s(&tm_, &t);
#else
  gmtime_r(&t, &tm_);
#endif
  size_t n = strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  return ZuCSpan{buf, unsigned(n)};
}

inline bool parseHTTPDate(ZuCSpan s, time_t &out) {
  ZtString<> in{s};
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

inline bool appendUInt(ZtString<> &out, ZuCSpan s) {
  if (!s) return false;
  for (unsigned i = 0; i < s.length(); ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  out << s;
  return true;
}

inline void splitTarget(RequestData &req) {
  ZuCSpan target{req.target};
  int64_t q = target.find([](auto c) { return c == '?'; });
  req.path = q < 0 ? target : ZuCSpan{target.data(), unsigned(q)};
  if (q < 0)
    req.query = {};
  else
    req.query = ZuCSpan{target.data() + q + 1,
      target.length() - unsigned(q) - 1};
}

inline bool decodeNormalizePath(
  ZuCSpan path_, bool hideDotfiles, ZtString<> &path, ZtString<> &err)
{
  ZtString<> decoded;
  if (!path_ || path_[0] != '/') decoded << '/';
  decoded << path_;
  auto scan = ZuPercent::Codec<PathPolicy>::decode(
    ZuSpan<char>{decoded.data(), unsigned(decoded.length())});
  if (!scan) { err = "bad percent escape"; return false; }
  decoded.length(scan.out);
  for (unsigned i = 0, n = decoded.length(); i < n; ++i)
    if (!decoded[i]) { err = "NUL in path"; return false; }

  ZtArray<ZtString<> > parts;
  unsigned i = 0;
  while (i < decoded.length()) {
    while (i < decoded.length() && decoded[i] == '/') ++i;
    unsigned b = i;
    while (i < decoded.length() && decoded[i] != '/') ++i;
    if (b == i) continue;
    ZuCSpan part{decoded.data() + b, i - b};
    if (part == ".") continue;
    if (part == "..") {
      if (!parts.length()) { err = "path traversal"; return false; }
      parts.length(parts.length() - 1);
      continue;
    }
    if (hideDotfiles && part && part[0] == '.') {
      err = "hidden path";
      return false;
    }
    parts.push(part);
  }
  path = "/";
  for (unsigned n = 0; n < parts.length(); ++n) {
    if (n) path << '/';
    path << parts[n];
  }
  return true;
}

inline void pathComponents(ZuCSpan clean, ZtArray<Zi::Path> &parts)
{
  parts.clear();
  unsigned i = 0;
  while (i < clean.length()) {
    while (i < clean.length() && clean[i] == '/') ++i;
    unsigned b = i;
    while (i < clean.length() && clean[i] != '/') ++i;
    if (b < i) parts.push(ZuCSpan{clean.data() + b, i - b});
  }
}

inline Zi::Path staticPath(const Options &options, ZuCSpan clean)
{
  Zi::Path path{options.root};
  if (clean.length() > 1) {
    unsigned n = path.length();
    if (n && path[n - 1] != '/' && path[n - 1] != '\\') path << '/';
    path << ZuCSpan{clean.data() + 1, clean.length() - 1};
  }
  return path;
}

inline ZtString<> hrefEncode(ZuCSpan s) {
  ZtString<> out;
  out.length(ZuPercent::Codec<HrefPolicy>::len(
    ZuBSpan{s.data(), unsigned(s.length())}));
  auto n = ZuPercent::Codec<HrefPolicy>::encode(
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(out.data()),
      unsigned(out.length())},
    ZuBSpan{s.data(), unsigned(s.length())});
  out.length(n);
  return out;
}

inline void htmlEsc(ZtString<> &out, ZuCSpan s) {
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

inline ZtString<> hostName(ZuCSpan host_) {
  ZtString<> host{host_};
  if (host && host[0] == '[') {
    auto p = host.find([](auto c) { return c == ']'; });
    if (p >= 0)
      return lower(ZuCSpan{host.data() + 1, unsigned(p - 1)});
  }
  int p = -1;
  for (unsigned i = 0, n = host.length(); i < n; ++i)
    if (host[i] == ':') p = int(i);
  if (p >= 0) host.length(p);
  return lower(host);
}

inline bool constTimeEqual(ZuCSpan a, ZuCSpan b) {
  if (a.length() != b.length()) return false;
  unsigned char d = 0;
  for (unsigned i = 0; i < a.length(); ++i) d |= a[i] ^ b[i];
  return !d;
}

inline ZtString<> basicAuthValue(const Options &options) {
  ZtString<> raw;
  raw << options.authUser << ':' << options.authPass;
  ZtString<> out;
  out << "Basic ";
  unsigned offset = out.length();
  out.length(offset + ZuBase64::enclen(raw.length()));
  auto n = ZuBase64::encode(
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(out.data() + offset),
      unsigned(out.length() - offset)},
    ZuBSpan{raw.data(), unsigned(raw.length())});
  out.length(offset + n);
  return out;
}

struct StaticPlanner {
  StaticPlanner(State *state_) : state{state_} { }

  ResponsePlan plan(const RequestData &req) const {
    ResponsePlan resp;
    resp.date = httpDate(time(nullptr));
    if (!state->options.noServerID) resp.server = "zhttpd";
    auto connection = lower(req.connection);
    if (state->options.noKeepalive || connection == "close" ||
	(req.http10 && connection != "keep-alive")) {
      resp.close = true;
      resp.connection = "close";
    }

    if (state->options.forwardAll)
      return redirect(req, state->options.forwardAll, resp);
    for (unsigned i = 0; i < state->options.forwards.length(); ++i)
      if (hostName(req.host) == lower(state->options.forwards[i].host))
	return redirect(req, state->options.forwards[i].url, resp);
    if (state->options.forwardHttps && !req.tls) {
      ZtString<> url;
      url << "https://" << req.host << req.target;
      return redirect(req, url, resp, false);
    }

    if (state->options.authUser) {
      auto expect = basicAuthValue(state->options);
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

    RequestData req_ = req;
    splitTarget(req_);
    ZtString<> clean, err;
    if (!decodeNormalizePath(req_.path, state->options.hideDotfiles, clean, err))
      return error(resp, 400, "Bad Request", err, req.method);
    if (req_.path.length() > 1 && req_.path[req_.path.length() - 1] == '/' &&
	clean != "/")
      clean << '/';

    if (state->options.singleFile) return singleFile(req_, clean, resp);
    return fileOrDir(req_, clean, resp);
  }

  ResponsePlan redirect(
    const RequestData &req, ZuCSpan base, ResponsePlan resp,
    bool appendTarget = true) const {
    resp.status = 301;
    resp.reason = "Moved Permanently";
    ZtString<> location{base};
    if (appendTarget) location << req.target;
    resp.location = ZuMv(location);
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
    ZtString<> leaf;
    leaf << '/' << ZiFile::leafname(state->options.root);
    if (clean != "/" && clean != leaf) return notFound(resp, req.method);
    ZiFile file;
    if (file.dup(state->rootFile, ZiFile::GC) != Zi::OK)
      return error(resp, 403, "Forbidden", "Forbidden", req.method);
    return regularFile(req, ZuMv(file), state->rootFileStat,
      state->options.root, resp);
  }

  ResponsePlan fileOrDir(
    const RequestData &req, ZuCSpan clean, ResponsePlan resp) const {
    ZiFile dir;
    if (openDir(clean, dir) == Zi::OK) {
      if (!clean || clean[clean.length() - 1] != '/') {
	ZtString<> loc{clean};
	loc << '/';
	if (req.query) {
	  loc << '?' << req.query;
	}
	resp.status = 301;
	resp.reason = "Moved Permanently";
	resp.location = ZuMv(loc);
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
	  Zi::Path indexPath = staticPath(state->options, clean);
	  if (indexPath && indexPath[indexPath.length() - 1] != '/' &&
	      state->options.index)
	    indexPath << '/';
	  indexPath << state->options.index;
	  return regularFile(req, ZuMv(index), stat, indexPath, resp);
	}
      }
      if (state->options.noListing)
	return error(resp, 403, "Forbidden", "Forbidden", req.method);
      return listing(req, staticPath(state->options, clean), clean, resp);
    }
    if (clean && clean[clean.length() - 1] == '/') return notFound(resp, req.method);
    ZiFile file;
    ZiFile::Stat stat;
    int rc = openFile(clean, file, stat);
    if (rc != Zi::OK) return notFound(resp, req.method);
    if (!stat.regular)
      return error(resp, 403, "Forbidden", "Forbidden", req.method);
    return regularFile(req, ZuMv(file), stat,
      staticPath(state->options, clean), resp);
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
      resp.lastModified = httpDate(mtime);
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
    resp.lastModified = httpDate(mtime);
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
    ZtArray<Zi::Path> parts;
    pathComponents(clean, parts);
    for (unsigned i = 0, n = parts.length(); i < n; ++i) {
      ZiFile next;
      if (next.openAt(cur, parts[i],
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
    ZtArray<Zi::Path> parts;
    pathComponents(clean, parts);
    if (!parts.length()) return Zi::IOError;
    ZiFile dir;
    if (dir.dup(state->rootDir, ZiFile::GC) != Zi::OK) return Zi::IOError;
    for (unsigned i = 0; i + 1 < parts.length(); ++i) {
      ZiFile next;
      if (next.openAt(dir, parts[i],
	  ZiFile::ReadOnly | ZiFile::Directory |
	    ZiFile::NoFollow | ZiFile::GC) != Zi::OK)
	return Zi::IOError;
      dir = ZuMv(next);
    }
    ZiFile out;
    if (out.openAt(dir, parts[parts.length() - 1],
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
    if (!range.starts("bytes=")) return;
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
    auto toU64 = [](ZuCSpan s) {
      ZtString<> tmp{s};
      return strtoull(tmp.ndata(), nullptr, 10);
    };
    if (!a) {
      if (!isnum(b)) return;
      uint64_t suffix = toU64(b);
      if (!suffix) { unsat = true; return; }
      if (suffix >= size) { first = 0; length = size; }
      else { first = size - suffix; length = suffix; }
      ranged = true;
      return;
    }
    if (!isnum(a) || (b && !isnum(b))) return;
    uint64_t start = toU64(a);
    if (start >= size) { unsat = true; return; }
    uint64_t end = !b ? size - 1 : toU64(b);
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
      ZtString<> name;
      bool dir = false;
      uint64_t size = 0;
      time_t mtime = 0;
    };
    ZtArray<Entry> entries;
    ZiDir dir;
    Zi::Path name;
    if (dir.open(path) == Zi::OK) {
      while (dir.read(name) == Zi::OK) {
      if (name == "." || name == "..") continue;
      if (state->options.hideDotfiles && name && name[0] == '.')
	continue;
      auto *entry = new (entries.push()) Entry();
      entry->name = name;
      Zi::Path child = ZiFile::append(path, name);
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
	return ZuCmp<ZtString<>>::cmp(a.name, b.name);
      });
    ZtString<> html;
    html += "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of ";
    htmlEsc(html, clean);
    html += "</title></head><body><h1>Index of ";
    htmlEsc(html, clean);
    html += "</h1><pre>";
    if (clean != "/") html += "<a href=\"../\">../</a>\n";
    for (unsigned i = 0, n = entries.length(); i < n; ++i) {
      auto &e = entries[i];
      ZtString<> label{e.name};
      if (e.dir) label << '/';
      auto href = hrefEncode(label);
      html += "<a href=\"";
      html += href;
      html += "\">";
      htmlEsc(html, label);
      html += "</a>";
      if (!e.dir) html << ' ' << e.size;
      if (e.mtime) html << ' ' << httpDate(e.mtime);
      html += "\n";
    }
    html += "</pre>";
    if (!state->options.noServerID)
      html += "<hr><address>zhttpd</address>";
    html += "</body></html>\n";
    resp.status = 200;
    resp.reason = "OK";
    resp.contentType = "text/html";
    resp.body = ZuMv(html);
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
  f->host = host;
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

inline bool loadOptions(Options &options, int argc, char **argv, bool &help) {
  bool httpSet = false;
  for (int i = 1; i < argc; ++i) {
    ZuCSpan arg{argv[i]};
    if (arg == "--http") httpSet = true;
  }
  int argc_ = ZtCLI::load(options, argc, const_cast<const char *const *>(argv));
  if (argc_ < 0) return false;
  help = options.help;
  if (help) return true;
  if (argc_ != 2) return false;
  if (options.port > 65535) return false;
  for (unsigned i = 0, n = options.forward.length(); i < n; ++i)
    if (!parseForward(options, options.forward[i])) return false;
  if (options.auth && !parseAuth(options, options.auth)) return false;
#ifndef _WIN32
  if (options.port == 8080 && !geteuid()) options.port = 80;
#endif
  if (!httpSet && (options.https || options.http3)) options.http = false;
  return true;
}

inline bool initFileState(State &state, ZtString<> &error) {
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

inline bool validate(Options &options, ZtString<> &error) {
  if (!options.root) { error = "root required"; return false; }
  if ((options.https || options.http3) && (!options.cert || !options.key)) {
    error = "--https/--http3 require --cert and --key";
    return false;
  }
#ifdef _WIN32
  if (options.chroot || options.uid || options.gid) {
    error = "--chroot/--uid/--gid are unsupported on Windows";
    return false;
  }
#endif
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

} // namespace Zhttpd

#endif /* Zhttpd_HH */
