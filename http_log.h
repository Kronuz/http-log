// http-log — an http::HttpHandler middleware that logs each HTTP request and
// response through Kronuz/logger, with term-color, recovering the rich request/
// response logging Xapiand once had (Request::to_text / Response::to_text, deleted
// in Xapiand commit 602dcdd33 "Leg 2 (stage 3d): delete Xapiand's Response class").
//
// What it renders, faithful to that original:
//   - a per-HTTP-method color palette for the request (GET/SEARCH/COUNT/DUMP blue,
//     POST/PUT/PATCH/UPDATE/UPSERT/RESTORE orange, DELETE red, COMMIT teal,
//     OPEN/CLOSE pink, OPTIONS/INFO/HEAD purple), and a per-status palette for the
//     response (2xx green, 3xx teal, 404 tan, 4xx orange-red, 5xx red);
//   - prettified request/response bodies via a pluggable hook (Xapiand injects its
//     MsgPack/JSON/YAML pretty-printer; a plain app injects a JSON reindenter);
//   - iTerm2 inline image previews for image/document bodies (gated on verbosity);
//   - large-body truncation to a "<body N bytes>" summary and an escaped repr for
//     binary; timing; and the exception path (an unhandled throw is logged with a
//     backtrace via the logger's hook, then handed to the inner handler's error
//     mapping).
//
// Colors are term-color stacked escapes; the logger sink resolves them per
// --color / NO_COLOR / terminal depth. Header-only; depends on Kronuz/http (the
// HttpHandler seam), Kronuz/logger, and Kronuz/term-color (via logger).

#pragma once

#include <chrono>
#include <cctype>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "http_handler.h"   // http::HttpHandler / Request / ResponseWriter
#include "http_message.h"   // http::reason_phrase / http::iequal / http::Headers
#include "colors.h"         // term-color: rgb()/rgba()/brgb()/NO_COLOR/CLEAR_COLOR
#include "logger.h"         // Kronuz/logger: Logging, LogConfig, the L_* macros

namespace http_log {

// ---------------------------------------------------------------------------
// Small self-contained utilities (base64 for the iTerm2 image escape, an escaped
// repr for binary, and a human byte size). Kept inline so the lib needs no extra
// dependency.
// ---------------------------------------------------------------------------
inline std::string b64encode(std::string_view in) {
	static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((in.size() + 2) / 3 * 4);
	std::size_t i = 0;
	for (; i + 2 < in.size(); i += 3) {
		unsigned n = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
		out += T[n >> 18 & 63]; out += T[n >> 12 & 63]; out += T[n >> 6 & 63]; out += T[n & 63];
	}
	if (i < in.size()) {
		unsigned n = (unsigned char)in[i] << 16;
		if (i + 1 < in.size()) n |= (unsigned char)in[i + 1] << 8;
		out += T[n >> 18 & 63]; out += T[n >> 12 & 63];
		out += (i + 1 < in.size()) ? T[n >> 6 & 63] : '=';
		out += '=';
	}
	return out;
}

// A printable, length-capped rendering of arbitrary bytes: printable ASCII passes
// through, everything else is \xNN. For the binary-body summary in the log.
inline std::string repr(std::string_view s, std::size_t max = 500) {
	std::string out;
	std::size_t n = s.size() < max ? s.size() : max;
	out.reserve(n + 8);
	static const char* H = "0123456789abcdef";
	for (std::size_t i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)s[i];
		if (c == '\n') { out += "\\n"; }
		else if (c == '\t') { out += "\\t"; }
		else if (c == '\r') { out += "\\r"; }
		else if (c >= 0x20 && c < 0x7f) { out += (char)c; }
		else { out += "\\x"; out += H[c >> 4]; out += H[c & 0xf]; }
	}
	if (s.size() > max) { out += "..."; }
	return out;
}

inline std::string human_bytes(std::size_t n) {
	static const char* U[] = {"B", "KiB", "MiB", "GiB", "TiB"};
	double v = (double)n;
	int u = 0;
	while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
	char buf[32];
	if (u == 0) std::snprintf(buf, sizeof buf, "%zu B", n);
	else std::snprintf(buf, sizeof buf, "%.1f %s", v, U[u]);
	return buf;
}

// ---------------------------------------------------------------------------
// Color palettes (term-color stacked escapes). head = bold, headers = dimmed
// (alpha 0.6), text = plain. Ported verbatim from Xapiand's to_text().
// ---------------------------------------------------------------------------
struct Palette {
	std::string head, headers, text;
};

namespace detail {
inline std::string sv(auto&& c) { return std::string(std::string_view(c)); }
inline Palette pal(auto&& h, auto&& hd, auto&& t) { return {sv(h), sv(hd), sv(t)}; }
}  // namespace detail

inline const std::string& reset() {
	static const std::string s = detail::sv(CLEAR_COLOR);
	return s;
}

inline const Palette& method_palette(std::string_view m) {
	static const Palette purple = detail::pal(brgb(100, 64, 131), rgba(100, 64, 131, 0.6), rgb(100, 64, 131));
	static const Palette blue   = detail::pal(brgb(34, 113, 191),  rgba(34, 113, 191, 0.6),  rgb(34, 113, 191));
	static const Palette orange = detail::pal(brgb(158, 90, 28),   rgba(158, 90, 28, 0.6),   rgb(158, 90, 28));
	static const Palette red    = detail::pal(brgb(158, 56, 28),   rgba(158, 56, 28, 0.6),   rgb(158, 56, 28));
	static const Palette teal   = detail::pal(brgb(51, 136, 116),  rgba(51, 136, 116, 0.6),  rgb(51, 136, 116));
	static const Palette pink   = detail::pal(brgb(158, 28, 71),   rgba(158, 28, 71, 0.6),   rgb(158, 28, 71));
	static const Palette none   = {"", "", ""};
	if (m == "OPTIONS" || m == "INFO" || m == "HEAD") return purple;
	if (m == "GET" || m == "SEARCH" || m == "COUNT" || m == "DUMP") return blue;
	if (m == "POST" || m == "RESTORE" || m == "PATCH" || m == "UPDATE" || m == "UPSERT" || m == "PUT") return orange;
	if (m == "DELETE") return red;
	if (m == "COMMIT") return teal;
	if (m == "OPEN" || m == "CLOSE") return pink;
	return none;
}

inline const Palette& status_palette(int status) {
	static const Palette green = detail::pal(brgb(68, 136, 68),   rgba(68, 136, 68, 0.6),   rgb(68, 136, 68));
	static const Palette tgreen = detail::pal(brgb(68, 136, 120), rgba(68, 136, 120, 0.6),  rgb(68, 136, 120));
	static const Palette tan   = detail::pal(brgb(116, 100, 77),  rgba(116, 100, 77, 0.6),  rgb(116, 100, 77));
	static const Palette ored  = detail::pal(brgb(183, 70, 17),   rgba(183, 70, 17, 0.6),   rgb(183, 70, 17));
	static const Palette red   = detail::pal(brgb(190, 30, 10),   rgba(190, 30, 10, 0.6),   rgb(190, 30, 10));
	static const Palette none  = {"", "", ""};
	if (status >= 200 && status <= 299) return green;
	if (status >= 300 && status <= 399) return tgreen;
	if (status == 404) return tan;
	if (status >= 400 && status <= 499) return ored;
	if (status >= 500 && status <= 599) return red;
	return none;
}

// ---------------------------------------------------------------------------
// Options / hooks.
// ---------------------------------------------------------------------------

// Render a body for the log given its content-type and raw bytes, or nullopt to
// fall back to the built-in handling (raw text / escaped repr). A consumer injects
// the fidelity it wants: Xapiand a MsgPack/JSON/YAML pretty-printer, a plain app a
// JSON reindenter.
using Prettify = std::function<std::optional<std::string>(std::string_view ct, std::string_view body)>;

// Whether a content-type can be previewed inline on iTerm2. Default (when unset):
// image/* plus the document set Xapiand allowed (pdf / eps / postscript / psd).
using CanPreview = std::function<bool(std::string_view ct)>;

inline bool default_can_preview(std::string_view ct) {
	if (ct.substr(0, 6) == "image/") return true;
	static const char* docs[] = {
		"application/pdf", "application/eps", "application/x-eps", "application/postscript",
		"application/x-pdf", "application/x-gzpdf", "application/x-bzpdf",
		"application/photoshop", "application/x-photoshop", "application/psd",
	};
	for (const char* d : docs) if (ct == d) return true;
	return false;
}

struct Options {
	Prettify prettify;                     // structured-body pretty-printer (optional)
	CanPreview can_preview;                // previewable content types (default set if unset)
	std::size_t body_limit = 10 * 1024;    // bodies larger than this become "<body N>"
	bool images = true;                    // iTerm2 inline image previews
	// Log levels, matching Xapiand. Each request/response is one block logged at one
	// level; the body detail (image vs prettify vs summary) is gated inside the
	// renderer by the running log level. A consumer can raise these to make the
	// blocks visible at a lower verbosity (e.g. a demo).
	int request_level = LOG_DEBUG;         // the request block
	int level_2xx = LOG_DEBUG;             // 2xx / 3xx / 404 responses
	int level_4xx = LOG_INFO;              // 4xx responses
	int level_5xx = LOG_NOTICE;            // 5xx responses
	int image_level = LOG_DEBUG + 1;       // above this: previewable bodies as iTerm2 images
};

// ---------------------------------------------------------------------------
// Rendering.
// ---------------------------------------------------------------------------
inline bool looks_texty(std::string_view ct) {
	if (ct.empty()) return true;
	if (ct.substr(0, 5) == "text/") return true;
	for (const char* k : {"json", "yaml", "xml", "html", "javascript", "x-www-form"})
		if (ct.find(k) != std::string_view::npos) return true;
	return false;
}

// The body block, reproducing Xapiand to_text()'s decode branch (log_request /
// log_response always call it decoded): an iTerm2 image when previewable and
// verbose enough, else a size summary for an oversized body, else the prettify
// hook, else raw text (texty) or an escaped repr (binary).
inline std::string render_body(std::string_view ct, std::string_view body, const Options& opts,
	int log_level, std::string_view kind) {
	if (body.empty()) return {};
	const CanPreview& cp = opts.can_preview ? opts.can_preview : CanPreview(default_can_preview);
	if (opts.images && log_level > opts.image_level && cp(ct)) {
		std::string b64 = b64encode(body);
		// iTerm2 inline image (https://iterm2.com/documentation-images.html).
		return "\033]1337;File=name=;inline=1;size=" + std::to_string(b64.size()) + ";width=20%:" + b64 + "\a";
	}
	if (body.size() > opts.body_limit) {
		return "<" + std::string(kind) + " " + human_bytes(body.size()) + ">";
	}
	if (opts.prettify) {
		if (auto p = opts.prettify(ct, body)) return std::move(*p);
	}
	if (looks_texty(ct)) return std::string(body);
	return "<" + std::string(kind) + " " + repr(body) + ">";
}

// The header block: one "Key: Value" per line. The section color is applied once
// around the whole block by the caller (as Xapiand's to_text did), not per line.
inline std::string headers_block(const http::Headers& headers) {
	std::string out;
	for (const auto& [k, v] : headers) { out += k; out += ": "; out += v; out += '\n'; }
	return out;
}

// The per-status emoji prefix and log level, ported from Xapiand's log_response:
// 2xx 💊, 3xx 💫, 404 🕸, 4xx 💥, 5xx 🔥; a request is 🌎.
inline std::string_view status_prefix(int status) {
	if (status >= 300 && status <= 399) return " \U0001F4AB  ";  // 💫
	if (status == 404) return " \U0001F578  ";                   // 🕸
	if (status >= 400 && status <= 499) return " \U0001F4A5  ";  // 💥
	if (status >= 500 && status <= 599) return " \U0001F525  ";  // 🔥
	return " \U0001F48A  ";                                      // 💊 (2xx / other)
}
inline int status_level(const Options& o, int status) {
	if (status >= 500 && status <= 599) return o.level_5xx;
	if (status >= 400 && status <= 499) return o.level_4xx;
	return o.level_2xx;
}

// Indent continuation lines (after each newline) by 4 spaces; the first line
// follows the emoji prefix. Matches Xapiand's string::indent(text, ' ', 4, false).
inline std::string indent4(std::string_view s) {
	std::string out;
	out.reserve(s.size() + 16);
	for (char c : s) { out += c; if (c == '\n') out += "    "; }
	return out;
}

// ---------------------------------------------------------------------------
// A ResponseWriter that captures the status, content-type, headers, and body while
// forwarding everything to the real writer, so the middleware can render the
// response after the inner handler completes.
// ---------------------------------------------------------------------------
class CapturingWriter : public http::ResponseWriter {
	http::ResponseWriter& real_;
public:
	int code = 200;
	std::string content_type = "text/plain; charset=utf-8";
	http::Headers headers;
	std::string body;
	bool started = false;
	explicit CapturingWriter(http::ResponseWriter& r) : real_(r) {}
	void status(int c) override { code = c; started = true; real_.status(c); }
	void set_header(std::string_view n, std::string_view v) override {
		if (http::iequal(n, "Content-Type")) content_type = std::string(v);
		headers.emplace_back(std::string(n), std::string(v));
		real_.set_header(n, v);
	}
	void write(std::string_view c) override { started = true; body.append(c); real_.write(c); }
	void end() override { real_.end(); }
	void set_close() override { real_.set_close(); }
};

// ---------------------------------------------------------------------------
// The middleware. Wraps an inner HttpHandler, logs each request and response, maps
// an unhandled exception to the inner handler's error mapping (after logging it
// with a backtrace via the logger's hook), and forwards the rest of the seam.
// ---------------------------------------------------------------------------
class AccessLog : public http::HttpHandler {
	http::HttpHandler& inner_;
	Options opts_;

	// One request/response = one log call: an emoji prefix + the block, with
	// continuation lines indented 4 spaces (Xapiand's log_request / log_response).
	void emit(int level, std::string_view prefix, std::string&& block) {
		std::string msg = std::string(prefix) + indent4(block);
		Logging::do_log(false, std::chrono::steady_clock::now(), level >= ASYNC_LOG_LEVEL,
			true, 0, level, std::exception_ptr{}, std::source_location::current(), std::move(msg));
	}

	// The request block: 🌎 + "METHOD path HTTP/v" (method-colored), headers, body.
	void log_request(const http::Request& req) {
		if (Logging::config.log_level < opts_.request_level) return;
		const Palette& p = method_palette(req.method);
		std::string head = req.method + " " + req.path + " HTTP/" +
			std::to_string(req.http_major) + "." + std::to_string(req.http_minor);
		std::string block = p.head + head + "\n" + p.headers + headers_block(req.headers);
		std::string b = render_body(req.content_type(), req.body, opts_, Logging::config.log_level, "body");
		if (!b.empty()) { block += p.text; block += b; }
		block += reset();
		emit(opts_.request_level, " \U0001F30E  ", std::move(block));   // 🌎
	}

	// The response block: the status emoji + "HTTP/v STATUS reason" (status-colored),
	// the response headers (timing rides in a Response-Time header the app sets), body.
	void log_response(const CapturingWriter& cap, int http_major, int http_minor) {
		int level = status_level(opts_, cap.code);
		if (Logging::config.log_level < level) return;
		const Palette& p = status_palette(cap.code);
		std::string head = "HTTP/" + std::to_string(http_major) + "." + std::to_string(http_minor) +
			" " + std::to_string(cap.code) + " " + http::reason_phrase(cap.code);
		std::string block = p.head + head + "\n" + p.headers + headers_block(cap.headers);
		std::string b = render_body(cap.content_type, cap.body, opts_, Logging::config.log_level, "body");
		if (!b.empty()) { block += p.text; block += b; }
		block += reset();
		emit(level, status_prefix(cap.code), std::move(block));
	}

public:
	explicit AccessLog(http::HttpHandler& inner, Options opts = {}) : inner_(inner), opts_(std::move(opts)) {}

	void handle(const http::Request& req, http::ResponseWriter& response) override {
		log_request(req);
		CapturingWriter cap(response);
		try {
			inner_.handle(req, cap);
		} catch (...) {
			// The exception's description and (when enabled) its backtrace come from
			// the logger's hooks; a consumer points Logging::hooks.backtrace at its
			// traceback provider. Rethrow so the framework's backstop hands it to the
			// inner handler's on_error() for the real status mapping.
			L_EXC(std::string("unhandled exception in ") + req.method + " " + req.path);
			throw;
		}
		log_response(cap, req.http_major, req.http_minor);
	}

	void on_error(std::exception_ptr error, const http::Request& req, http::ResponseWriter& response) override {
		// The framework calls this after handle() threw. Capture the response the
		// inner handler's error mapping writes, so the error response is logged too.
		// If the inner handler leaves it unanswered, the framework writes a generic
		// 500 fallback (past this capture), so reflect that in the log.
		CapturingWriter cap(response);
		inner_.on_error(error, req, cap);
		if (!cap.started) {
			cap.code = 500;
			cap.content_type = "text/plain; charset=utf-8";
		}
		log_response(cap, req.http_major, req.http_minor);
	}

	std::unique_ptr<http::BodySink> on_request_body(http::Request& req) override {
		return inner_.on_request_body(req);
	}
	bool wants_body_stream(const http::Request& req) override {
		return inner_.wants_body_stream(req);
	}
	std::unique_ptr<http::RequestExtension> create_extension(const http::Request& req) override {
		return inner_.create_extension(req);
	}
	bool should_offload(const http::Request& req) const override {
		return inner_.should_offload(req);
	}
};

}  // namespace http_log
