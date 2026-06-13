//  -*- mode:c++; indent-tabs-mode:t; tab-width:8; c-basic-offset:2; -*-
//  vi: noet ts=8 sw=2 cino=+0,(s,l1,m1,g0,N-s,j1,U1,W2,i2

// (c) Copyright 2026 Huw Rogers
// This code is licensed by the MIT license (see LICENSE for details)

// static file server support for zhttpserver

#ifndef ZhttpStaticServer_HH
#define ZhttpStaticServer_HH

#include <algorithm>
#include <errno.h>
#include <filesystem>
#include <string>
#include <string.h>
#include <time.h>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#include <zlib/ZuBase64.hh>
#include <zlib/ZuPercent.hh>
#include <zlib/ZmAtomic.hh>
#include <zlib/ZmGuard.hh>
#include <zlib/ZmLock.hh>
#include <zlib/ZmSemaphore.hh>
#include <zlib/ZtArray.hh>
#include <zlib/ZtString.hh>
#include <zlib/ZiFile.hh>
#include <zlib/ZiLog.hh>
#include <zlib/Zhttp.hh>

namespace ZhttpStatic {

inline std::string str(ZuCSpan s) { return {s.data(), s.length()}; }
inline std::string lower(std::string s) {
  for (auto &c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return s;
}
inline ZtString<> zstr(const std::string &s) {
  ZtString<> out;
  out << ZuCSpan{s.data(), unsigned(s.size())};
  return out;
}

struct Forward {
  ZtString<>	host;
  ZtString<>	url;
};

struct Options {
  ZtString<>		root;
  ZtString<>		addr{"0.0.0.0"};
  uint16_t		port = 8080;
  ZtString<>		cert;
  ZtString<>		key;
  ZtString<>		index{"index.html"};
  ZtString<>		mimetypes;
  ZtString<>		defaultMimetype{"application/octet-stream"};
  ZtArray<Forward>	forwards;
  ZtString<>		forwardAll;
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
};

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
  std::vector<std::pair<std::string, std::string> > map;

  void init(const Options &options) {
    static const std::pair<const char *, const char *> builtins[] = {
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
    for (auto &m : builtins) map.emplace_back(m.first, m.second);
    auto path = str(options.mimetypes);
    if (path.empty()) return;
    ZiFile f;
    if (f.open(path.c_str(), ZiFile::ReadOnly | ZiFile::GC) != Zi::OK)
      return;
    auto size = f.size();
    if (size <= 0 || size > (16<<20)) return;
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
      std::string mime{b, size_t(p - b)};
      while (p < e && *p != '\r' && *p != '\n') {
	while (p < e && (*p == ' ' || *p == '\t')) ++p;
	if (p >= e || *p == '\r' || *p == '\n') break;
	b = p;
	while (p < e && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
	  ++p;
	map.emplace_back(lower(std::string{b, size_t(p - b)}), mime);
      }
      while (p < e && *p != '\n') ++p;
      if (p < e) ++p;
    }
  }

  ZtString<> lookup(const Options &options, const std::string &path) const {
    auto slash = path.find_last_of("/\\");
    auto dot = path.find_last_of('.');
    if (dot == std::string::npos ||
	(slash != std::string::npos && dot < slash))
      return options.defaultMimetype;
    auto ext = lower(path.substr(dot + 1));
    for (auto i = map.rbegin(); i != map.rend(); ++i)
      if (i->first == ext) return zstr(i->second);
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
    ZiLOG(Info, "zhttpserver.access", ([
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

inline std::string httpDate(time_t t) {
  char buf[64];
  struct tm tm_;
#ifdef _WIN32
  gmtime_s(&tm_, &t);
#else
  gmtime_r(&t, &tm_);
#endif
  strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  return buf;
}

inline bool parseHTTPDate(ZuCSpan s, time_t &out) {
  std::string in = str(s);
  struct tm tm_;
  memset(&tm_, 0, sizeof(tm_));
  char *p = strptime(in.c_str(), "%a, %d %b %Y %H:%M:%S GMT", &tm_);
  if (!p || *p) return false;
#ifdef _WIN32
  out = _mkgmtime(&tm_);
#else
  out = timegm(&tm_);
#endif
  return out != time_t(-1);
}

inline bool appendUInt(std::string &out, ZuCSpan s) {
  if (!s) return false;
  for (unsigned i = 0; i < s.length(); ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  out.append(s.data(), s.length());
  return true;
}

inline void splitTarget(RequestData &req) {
  auto target = str(req.target);
  auto q = target.find('?');
  req.path = zstr(q == std::string::npos ? target : target.substr(0, q));
  req.query = q == std::string::npos ? ZtString<>{} : zstr(target.substr(q + 1));
}

inline bool decodeNormalizePath(
  ZuCSpan path_, bool hideDotfiles, std::string &path, std::string &err)
{
  std::string decoded = str(path_);
  if (decoded.empty() || decoded[0] != '/') decoded.insert(decoded.begin(), '/');
  auto scan = ZuPercent::Codec<PathPolicy>::decode(
    ZuSpan<char>{decoded.data(), unsigned(decoded.size())});
  if (!scan) { err = "bad percent escape"; return false; }
  decoded.resize(scan.out);
  for (char c : decoded)
    if (!c) { err = "NUL in path"; return false; }

  std::vector<std::string> parts;
  size_t i = 0;
  while (i < decoded.size()) {
    while (i < decoded.size() && decoded[i] == '/') ++i;
    size_t b = i;
    while (i < decoded.size() && decoded[i] != '/') ++i;
    if (b == i) continue;
    auto part = decoded.substr(b, i - b);
    if (part == ".") continue;
    if (part == "..") {
      if (parts.empty()) { err = "path traversal"; return false; }
      parts.pop_back();
      continue;
    }
    if (hideDotfiles && !part.empty() && part[0] == '.') {
      err = "hidden path";
      return false;
    }
    parts.push_back(part);
  }
  path = "/";
  for (unsigned n = 0; n < parts.size(); ++n) {
    if (n) path.push_back('/');
    path += parts[n];
  }
  return true;
}

inline std::string hrefEncode(const std::string &s) {
  std::string out;
  out.resize(ZuPercent::Codec<HrefPolicy>::len(
    ZuBSpan{s.data(), unsigned(s.size())}));
  auto n = ZuPercent::Codec<HrefPolicy>::encode(
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(out.data()),
      unsigned(out.size())},
    ZuBSpan{s.data(), unsigned(s.size())});
  out.resize(n);
  return out;
}

inline void htmlEsc(std::string &out, const std::string &s) {
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c); break;
    }
  }
}

inline std::string hostName(ZuCSpan host_) {
  auto host = str(host_);
  if (!host.empty() && host[0] == '[') {
    auto p = host.find(']');
    if (p != std::string::npos) return lower(host.substr(1, p - 1));
  }
  auto p = host.rfind(':');
  if (p != std::string::npos) host.resize(p);
  return lower(host);
}

inline bool constTimeEqual(const std::string &a, const std::string &b) {
  if (a.size() != b.size()) return false;
  unsigned char d = 0;
  for (unsigned i = 0; i < a.size(); ++i) d |= a[i] ^ b[i];
  return !d;
}

inline std::string basicAuthValue(const Options &options) {
  std::string raw = str(options.authUser);
  raw.push_back(':');
  raw += str(options.authPass);
  std::string out;
  out.resize(ZuBase64::enclen(raw.size()));
  auto n = ZuBase64::encode(
    ZuSpan<uint8_t>{reinterpret_cast<uint8_t *>(out.data()),
      unsigned(out.size())},
    ZuBSpan{raw.data(), unsigned(raw.size())});
  out.resize(n);
  return "Basic " + out;
}

struct StaticPlanner {
  StaticPlanner(State *state_) : state{state_} { }

  ResponsePlan plan(const RequestData &req) const {
    ResponsePlan resp;
    resp.date = zstr(httpDate(time(nullptr)));
    if (!state->options.noServerID) resp.server = "zhttpserver";
    auto connection = lower(str(req.connection));
    if (state->options.noKeepalive || connection == "close" ||
	(req.http10 && connection != "keep-alive")) {
      resp.close = true;
      resp.connection = "close";
    }

    if (state->options.forwardAll)
      return redirect(req, str(state->options.forwardAll), resp);
    for (unsigned i = 0; i < state->options.forwards.length(); ++i)
      if (hostName(req.host) == lower(str(state->options.forwards[i].host)))
	return redirect(req, str(state->options.forwards[i].url), resp);
    if (state->options.forwardHttps && !req.tls) {
      std::string url = "https://";
      url += str(req.host);
      url += str(req.target);
      return redirect(req, url, resp, false);
    }

    if (state->options.authUser) {
      auto expect = basicAuthValue(state->options);
      if (!constTimeEqual(str(req.authorization), expect)) {
	resp.status = 401;
	resp.reason = "Unauthorized";
	resp.wwwAuthenticate = "Basic realm=\"zhttpserver\"";
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
    std::string clean, err;
    if (!decodeNormalizePath(req_.path, state->options.hideDotfiles, clean, err))
      return error(resp, 400, "Bad Request", err.c_str(), req.method);
    if (req_.path.length() > 1 && req_.path[req_.path.length() - 1] == '/' &&
	clean != "/")
      clean.push_back('/');

    if (state->options.singleFile) return singleFile(req_, clean, resp);
    return fileOrDir(req_, clean, resp);
  }

  ResponsePlan redirect(
    const RequestData &req, const std::string &base, ResponsePlan resp,
    bool appendTarget = true) const {
    resp.status = 301;
    resp.reason = "Moved Permanently";
    std::string location = base;
    if (appendTarget) location += str(req.target);
    resp.location = zstr(location);
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
    const RequestData &req, const std::string &clean, ResponsePlan resp) const {
    std::filesystem::path root{str(state->options.root)};
    auto leaf = "/" + root.filename().string();
    if (clean != "/" && clean != leaf) return notFound(resp, req.method);
    return regularFile(req, root, resp);
  }

  ResponsePlan fileOrDir(
    const RequestData &req, const std::string &clean, ResponsePlan resp) const {
    std::filesystem::path root{str(state->options.root)};
    auto rel = clean.size() > 1 ? clean.substr(1) : std::string{};
    auto path = root / std::filesystem::path(rel);
    std::error_code ec;
    auto st = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::exists(st)) return notFound(resp, req.method);
    if (std::filesystem::is_directory(st)) {
      if (clean.empty() || clean.back() != '/') {
	std::string loc = clean + "/";
	if (req.query) {
	  loc.push_back('?');
	  loc += str(req.query);
	}
	resp.status = 301;
	resp.reason = "Moved Permanently";
	resp.location = zstr(loc);
	resp.contentType = "text/plain";
	resp.body = "Moved Permanently\n";
	resp.contentLength = resp.body.length();
	resp.generated = true;
	resp.sendBody = req.method != Zhttp::Method::HEAD;
	return resp;
      }
      auto index = path / str(state->options.index);
      if (std::filesystem::is_regular_file(index, ec))
	return regularFile(req, index, resp);
      if (state->options.noListing)
	return error(resp, 403, "Forbidden", "Forbidden", req.method);
      return listing(req, path, clean, resp);
    }
    if (!std::filesystem::is_regular_file(st))
      return error(resp, 403, "Forbidden", "Forbidden", req.method);
    return regularFile(req, path, resp);
  }

  ResponsePlan notFound(ResponsePlan resp, Zhttp::Method::T method) const {
    return error(resp, 404, "Not Found", "Not Found", method);
  }

  ResponsePlan regularFile(
    const RequestData &req, const std::filesystem::path &path,
    ResponsePlan resp) const {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec) return error(resp, 403, "Forbidden", "Forbidden", req.method);
    auto mtime_ = std::filesystem::last_write_time(path, ec);
    time_t mtime = time(nullptr);
    if (!ec) {
      auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
	std::chrono::file_clock::to_sys(mtime_));
      mtime = std::chrono::system_clock::to_time_t(sctp);
    }
    time_t ims;
    if (parseHTTPDate(req.ifModifiedSince, ims) && mtime <= ims) {
      resp.status = 304;
      resp.reason = "Not Modified";
      resp.lastModified = zstr(httpDate(mtime));
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
      std::string cr = "bytes */" + std::to_string(size);
      resp.contentRange = zstr(cr);
      resp.contentLength = 0;
      resp.sendBody = false;
      return resp;
    }

    resp.status = ranged ? 206 : 200;
    resp.reason = ranged ? "Partial Content" : "OK";
    resp.contentType = state->mime.lookup(state->options, path.string());
    resp.lastModified = zstr(httpDate(mtime));
    resp.filePath = zstr(path.string());
    resp.contentLength = length;
    resp.fileOffset = first;
    resp.fileLength = length;
    resp.file = true;
    resp.sendBody = req.method != Zhttp::Method::HEAD && length > 0;
    if (ranged) {
      std::string cr = "bytes " + std::to_string(first) + "-" +
	std::to_string(first + length - 1) + "/" + std::to_string(size);
      resp.contentRange = zstr(cr);
    }
    return resp;
  }

  static void parseRange(
    ZuCSpan range_, uint64_t size, uint64_t &first, uint64_t &length,
    bool &ranged, bool &unsat) {
    auto range = str(range_);
    if (range.rfind("bytes=", 0) != 0) return;
    auto spec = range.substr(6);
    if (spec.find(',') != std::string::npos) return;
    auto dash = spec.find('-');
    if (dash == std::string::npos) return;
    auto a = spec.substr(0, dash);
    auto b = spec.substr(dash + 1);
    auto isnum = [](const std::string &s) {
      if (s.empty()) return false;
      for (char c : s) if (c < '0' || c > '9') return false;
      return true;
    };
    if (a.empty()) {
      if (!isnum(b)) return;
      uint64_t suffix = strtoull(b.c_str(), nullptr, 10);
      if (!suffix) { unsat = true; return; }
      if (suffix >= size) { first = 0; length = size; }
      else { first = size - suffix; length = suffix; }
      ranged = true;
      return;
    }
    if (!isnum(a) || (!b.empty() && !isnum(b))) return;
    uint64_t start = strtoull(a.c_str(), nullptr, 10);
    if (start >= size) { unsat = true; return; }
    uint64_t end = b.empty() ? size - 1 : strtoull(b.c_str(), nullptr, 10);
    if (end < start) { unsat = true; return; }
    if (end >= size) end = size - 1;
    first = start;
    length = end - start + 1;
    ranged = true;
  }

  ResponsePlan listing(
    const RequestData &req, const std::filesystem::path &path,
    const std::string &clean, ResponsePlan resp) const {
    struct Entry {
      std::string name;
      bool dir = false;
      uint64_t size = 0;
      time_t mtime = 0;
    };
    std::vector<Entry> entries;
    std::error_code ec;
    for (auto &e : std::filesystem::directory_iterator(path, ec)) {
      auto name = e.path().filename().string();
      if (name == "." || name == "..") continue;
      if (state->options.hideDotfiles && !name.empty() && name[0] == '.')
	continue;
      Entry entry;
      entry.name = name;
      entry.dir = e.is_directory(ec);
      if (!entry.dir) entry.size = e.file_size(ec);
      auto mt = e.last_write_time(ec);
      if (!ec) {
	auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
	  std::chrono::file_clock::to_sys(mt));
	entry.mtime = std::chrono::system_clock::to_time_t(sctp);
      }
      entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
      [](const Entry &a, const Entry &b) { return a.name < b.name; });
    std::string html;
    html += "<!doctype html><html><head><meta charset=\"utf-8\"><title>Index of ";
    htmlEsc(html, clean);
    html += "</title></head><body><h1>Index of ";
    htmlEsc(html, clean);
    html += "</h1><pre>";
    if (clean != "/") html += "<a href=\"../\">../</a>\n";
    for (auto &e : entries) {
      auto label = e.name + (e.dir ? "/" : "");
      auto href = hrefEncode(label);
      html += "<a href=\"";
      html += href;
      html += "\">";
      htmlEsc(html, label);
      html += "</a>";
      if (!e.dir) html += " " + std::to_string(e.size);
      if (e.mtime) html += " " + httpDate(e.mtime);
      html += "\n";
    }
    html += "</pre>";
    if (!state->options.noServerID)
      html += "<hr><address>zhttpserver</address>";
    html += "</body></html>\n";
    resp.status = 200;
    resp.reason = "OK";
    resp.contentType = "text/html";
    resp.body = zstr(html);
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

inline bool parseAuth(Options &options, ZuCSpan auth) {
  auto colon = auth.find([](auto c) { return c == ':'; });
  if (colon <= 0) return false;
  options.authUser = ZuCSpan{auth.data(), unsigned(colon)};
  options.authPass = ZuCSpan{auth.data() + colon + 1,
    auth.length() - unsigned(colon) - 1};
  return true;
}

inline bool readableFile(const std::string &path) {
  ZiFile f;
  return f.open(path.c_str(), ZiFile::ReadOnly | ZiFile::GC) == Zi::OK;
}

inline bool validate(Options &options, std::string &error) {
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
  std::error_code ec;
  auto root = std::filesystem::path{str(options.root)};
  if (!std::filesystem::exists(root, ec)) { error = "root does not exist"; return false; }
  if (options.singleFile) {
    if (!std::filesystem::is_regular_file(root, ec) || !readableFile(root.string())) {
      error = "--single-file root is not a readable regular file";
      return false;
    }
  } else if (!std::filesystem::is_directory(root, ec)) {
    error = "root is not a directory";
    return false;
  }
  return true;
}

} // namespace ZhttpStatic

#endif /* ZhttpStaticServer_HH */
