// http-log — an http::HttpHandler middleware that logs each HTTP request and
// response through Kronuz/logger, in color. It recovers the request/response
// logging Xapiand had in Request::to_text / Response::to_text before the
// decomposition deleted them (Xapiand commit 602dcdd33 "Leg 2 (stage 3d): delete
// Xapiand's Response class"), brought over close to verbatim: the same compile-time
// term-color palettes (rgb / rgba / brgb), the same cppcodec base64 for the iTerm2
// image escape, the same strings::from_bytes / strings::indent, and the same repr
// for binary bodies. The one Xapiand-specific piece, decoding a body to a MsgPack
// and rendering it indented, is a hook a consumer injects.
//
// Colors are term-color stacked escapes; the logger sink resolves them per
// --color / NO_COLOR / terminal depth. Header-only; depends on Kronuz/http (the
// HttpHandler seam), Kronuz/logger (+ term-color), Kronuz/strings, Kronuz/repr, and
// cppcodec (base64).

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "cppcodec/base64_rfc4648.hpp"   // base64 for the iTerm2 image escape (as Xapiand)
#include "http_handler.h"                // http::HttpHandler / Request / ResponseWriter
#include "http_message.h"                // http::reason_phrase / http::iequal / http::Headers
#include "colors.h"                      // term-color: rgb()/rgba()/brgb()/NO_COLOR/CLEAR_COLOR
#include "logger.h"                      // Kronuz/logger: Logging, LogConfig, the L_* macros
#include "repr.hh"                       // Kronuz/repr: repr() for binary bodies
#include "strings.hh"                    // Kronuz/strings: from_bytes / indent / format

namespace http_log {

// ---------------------------------------------------------------------------
// Color palettes: head bold (brgb), headers dimmed (rgba alpha 0.6), text plain
// (rgb). Compile-time constants selected at runtime by method / status, ported
// verbatim from Xapiand's Request::to_text / Response::to_text. The c_str() point
// at function-local static constexpr static_strings (program-lifetime storage).
// ---------------------------------------------------------------------------
struct Palette {
	const char* head;
	const char* headers;
	const char* text;
};

inline const Palette& method_palette(std::string_view m) {
	static constexpr auto nc = NO_COLOR;
	// OPTIONS / INFO / HEAD — rgb(100, 64, 131) purple
	static constexpr auto ph = brgb(100, 64, 131);
	static constexpr auto phd = rgba(100, 64, 131, 0.6);
	static constexpr auto pt = rgb(100, 64, 131);
	// GET / SEARCH / COUNT / DUMP — rgb(34, 113, 191) blue
	static constexpr auto bh = brgb(34, 113, 191);
	static constexpr auto bhd = rgba(34, 113, 191, 0.6);
	static constexpr auto bt = rgb(34, 113, 191);
	// POST / RESTORE / PATCH / UPDATE / UPSERT / PUT — rgb(158, 90, 28) orange
	static constexpr auto oh = brgb(158, 90, 28);
	static constexpr auto ohd = rgba(158, 90, 28, 0.6);
	static constexpr auto ot = rgb(158, 90, 28);
	// DELETE — rgb(158, 56, 28) red
	static constexpr auto rh = brgb(158, 56, 28);
	static constexpr auto rhd = rgba(158, 56, 28, 0.6);
	static constexpr auto rt = rgb(158, 56, 28);
	// COMMIT — rgb(51, 136, 116) teal
	static constexpr auto th = brgb(51, 136, 116);
	static constexpr auto thd = rgba(51, 136, 116, 0.6);
	static constexpr auto tt = rgb(51, 136, 116);
	// OPEN / CLOSE — rgb(158, 28, 71) pink
	static constexpr auto kh = brgb(158, 28, 71);
	static constexpr auto khd = rgba(158, 28, 71, 0.6);
	static constexpr auto kt = rgb(158, 28, 71);
	static const Palette purple{ph.c_str(), phd.c_str(), pt.c_str()};
	static const Palette blue{bh.c_str(), bhd.c_str(), bt.c_str()};
	static const Palette orange{oh.c_str(), ohd.c_str(), ot.c_str()};
	static const Palette red{rh.c_str(), rhd.c_str(), rt.c_str()};
	static const Palette teal{th.c_str(), thd.c_str(), tt.c_str()};
	static const Palette pink{kh.c_str(), khd.c_str(), kt.c_str()};
	static const Palette none{nc.c_str(), nc.c_str(), nc.c_str()};
	if (m == "OPTIONS" || m == "INFO" || m == "HEAD") return purple;
	if (m == "GET" || m == "SEARCH" || m == "COUNT" || m == "DUMP") return blue;
	if (m == "POST" || m == "RESTORE" || m == "PATCH" || m == "UPDATE" || m == "UPSERT" || m == "PUT") return orange;
	if (m == "DELETE") return red;
	if (m == "COMMIT") return teal;
	if (m == "OPEN" || m == "CLOSE") return pink;
	return none;
}

inline const Palette& status_palette(int status) {
	static constexpr auto nc = NO_COLOR;
	// 2xx — rgb(68, 136, 68) green
	static constexpr auto gh = brgb(68, 136, 68);
	static constexpr auto ghd = rgba(68, 136, 68, 0.6);
	static constexpr auto gt = rgb(68, 136, 68);
	// 3xx — rgb(68, 136, 120) teal-green
	static constexpr auto th = brgb(68, 136, 120);
	static constexpr auto thd = rgba(68, 136, 120, 0.6);
	static constexpr auto tt = rgb(68, 136, 120);
	// 404 — rgb(116, 100, 77) tan
	static constexpr auto ah = brgb(116, 100, 77);
	static constexpr auto ahd = rgba(116, 100, 77, 0.6);
	static constexpr auto at = rgb(116, 100, 77);
	// 4xx — rgb(183, 70, 17) orange-red
	static constexpr auto oh = brgb(183, 70, 17);
	static constexpr auto ohd = rgba(183, 70, 17, 0.6);
	static constexpr auto ot = rgb(183, 70, 17);
	// 5xx — rgb(190, 30, 10) red
	static constexpr auto rh = brgb(190, 30, 10);
	static constexpr auto rhd = rgba(190, 30, 10, 0.6);
	static constexpr auto rt = rgb(190, 30, 10);
	static const Palette green{gh.c_str(), ghd.c_str(), gt.c_str()};
	static const Palette tgreen{th.c_str(), thd.c_str(), tt.c_str()};
	static const Palette tan{ah.c_str(), ahd.c_str(), at.c_str()};
	static const Palette ored{oh.c_str(), ohd.c_str(), ot.c_str()};
	static const Palette red{rh.c_str(), rhd.c_str(), rt.c_str()};
	static const Palette none{nc.c_str(), nc.c_str(), nc.c_str()};
	if (status >= 200 && status <= 299) return green;
	if (status >= 300 && status <= 399) return tgreen;
	if (status == 404) return tan;
	if (status >= 400 && status <= 499) return ored;
	if (status >= 500 && status <= 599) return red;
	return none;
}

inline const char* reset() {
	static constexpr auto c = CLEAR_COLOR;
	return c.c_str();
}

// ---------------------------------------------------------------------------
// Options / hooks.
// ---------------------------------------------------------------------------

// Render a body for the log given its content-type and raw bytes, or nullopt to
// fall back to the built-in handling (raw text / escaped repr). A consumer injects
// the fidelity it wants: Xapiand decodes to a MsgPack and renders it indented, a
// plain app a JSON reindenter.
using Prettify = std::function<std::optional<std::string>(std::string_view ct, std::string_view body)>;

// Whether a content-type can be previewed inline on iTerm2. Default (when unset):
// image/* plus the document set Xapiand's can_preview() allowed.
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
	std::size_t capture_limit = 1024 * 1024;  // max response bytes the middleware retains
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
// log_response always call it decoded): an iTerm2 image (cppcodec base64) when
// previewable and verbose enough, else a size summary for an oversized or
// incompletely-captured body, else the prettify hook, else raw text (texty) or an
// escaped repr (binary). `body` is what was retained (possibly capped at
// Options::capture_limit); `total_size` is the true body length. When the two
// differ (a streamed / very large body was truncated) or the body exceeds
// body_limit, only a "<body N bytes>" summary is rendered -- the middleware never
// prettifies or images a body it does not hold in full, so it stays O(capture_limit).
inline std::string render_body(std::string_view ct, std::string_view body, std::size_t total_size,
	const Options& opts, int log_level, std::string_view kind) {
	if (total_size == 0) return {};
	bool complete = body.size() >= total_size;   // we retained every byte
	const CanPreview& cp = opts.can_preview ? opts.can_preview : CanPreview(default_can_preview);
	if (opts.images && log_level > opts.image_level && complete && total_size <= opts.capture_limit && cp(ct)) {
		// From https://iterm2.com/documentation-images.html (as Xapiand's to_text).
		auto b64 = cppcodec::base64_rfc4648::encode(body.data(), body.size());
		std::string out = strings::format("\033]1337;File=name=;inline=1;size={};width=20%:", b64.size());
		out += b64;
		out += '\a';
		return out;
	}
	if (total_size > opts.body_limit || !complete) {
		return "<" + std::string(kind) + " " + strings::from_bytes(total_size) + ">";
	}
	if (opts.prettify) {
		if (auto p = opts.prettify(ct, body)) return std::move(*p);
	}
	if (looks_texty(ct)) return std::string(body);
	return "<" + std::string(kind) + " " + repr(body, true, '\'', 500) + ">";
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
	if (status == 404) return o.level_2xx;   // Xapiand keeps 404 at DEBUG, like 2xx/3xx
	if (status >= 400 && status <= 499) return o.level_4xx;
	return o.level_2xx;
}

// ---------------------------------------------------------------------------
// A ResponseWriter that captures the status, content-type, headers, and body while
// forwarding everything to the real writer, so the middleware can render the
// response after the inner handler completes. The retained body is capped at
// `cap_` bytes (Options::capture_limit) while the true length is tracked in
// `body_size`, so a large or streamed response never grows the middleware's memory
// past the cap -- render_body summarizes anything it did not retain in full.
class CapturingWriter : public http::ResponseWriter {
	http::ResponseWriter& real_;
	std::size_t cap_;
public:
	int code = 200;
	std::string content_type = "text/plain; charset=utf-8";
	http::Headers headers;
	std::string body;
	std::size_t body_size = 0;   // total bytes written (>= body.size() when capped)
	bool started = false;
	CapturingWriter(http::ResponseWriter& r, std::size_t cap) : real_(r), cap_(cap) {}
	void status(int c) override { code = c; started = true; real_.status(c); }
	void set_header(std::string_view n, std::string_view v) override {
		if (http::iequal(n, "Content-Type")) content_type = std::string(v);
		headers.emplace_back(std::string(n), std::string(v));
		real_.set_header(n, v);
	}
	void write(std::string_view c) override {
		started = true;
		body_size += c.size();
		if (body.size() < cap_) {
			std::size_t take = cap_ - body.size();
			body.append(c.data(), c.size() < take ? c.size() : take);
		}
		real_.write(c);
	}
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
	// continuation lines indented 4 spaces (Xapiand's log_request / log_response,
	// strings::indent(text, ' ', 4, false)).
	void emit(int level, std::string_view prefix, const std::string& block) {
		std::string msg = std::string(prefix) + strings::indent(block, ' ', 4, false);
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
		std::string b = render_body(req.content_type(), req.body, req.body.size(), opts_, Logging::config.log_level, "body");
		if (!b.empty()) { block += p.text; block += b; }
		block += reset();
		emit(opts_.request_level, " \U0001F30E  ", block);   // 🌎
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
		std::string b = render_body(cap.content_type, cap.body, cap.body_size, opts_, Logging::config.log_level, "body");
		if (!b.empty()) { block += p.text; block += b; }
		block += reset();
		emit(level, status_prefix(cap.code), block);
	}

public:
	explicit AccessLog(http::HttpHandler& inner, Options opts = {}) : inner_(inner), opts_(std::move(opts)) {}

	void handle(const http::Request& req, http::ResponseWriter& response) override {
		log_request(req);
		CapturingWriter cap(response, opts_.capture_limit);
		try {
			inner_.handle(req, cap);
		} catch (...) {
			// The exception's description and (when enabled) its backtrace come from
			// the logger's hooks; a consumer points Logging::hooks.backtrace at its
			// traceback provider.
			L_EXC(std::string("unhandled exception in ") + req.method + " " + req.path);
			// Run the inner handler's error mapping HERE, with the same CapturingWriter
			// that is still alive on the stack, rather than rethrowing and letting the
			// framework call on_error with a fresh writer. A handler may have stored a
			// pointer to `cap` during handle() (Xapiand's request.response_writer), so
			// its on_error must write through that same object -- rethrowing would
			// destroy `cap` first and leave the stored pointer dangling. If it leaves
			// the response unanswered, write the generic 500 fallback ourselves.
			try {
				inner_.on_error(std::current_exception(), req, cap);
			} catch (...) {}
			if (!cap.started) { cap.send(500, "Internal Server Error\n"); }
			log_response(cap, req.http_major, req.http_minor);
			return;
		}
		log_response(cap, req.http_major, req.http_minor);
	}

	// Not overridden on purpose: handle() catches and maps errors itself (so the
	// inner handler's stored response_writer stays valid), and never rethrows, so the
	// framework's on_error backstop is not used.

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
