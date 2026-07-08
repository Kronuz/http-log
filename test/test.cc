// http-log test suite. A self-contained smoke test using a CHECK macro that aborts
// regardless of NDEBUG (a Release build compiles asserts out, so a test written with
// assert would verify nothing). Covers the pure renderers (palettes, body branches,
// status helpers) and the AccessLog middleware end-to-end through a capturing sink.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include "http_log.h"

static int g_failures = 0;
#define CHECK(cond) do { \
	if (!(cond)) { std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); ++g_failures; } \
} while (0)

// A sink that captures every rendered line, so the test can assert on the log.
struct CaptureSink : Logger {
	std::mutex mu;
	std::string out;
	void log(int /*priority*/, std::string_view str, bool /*with_priority*/, bool with_endl) override {
		std::lock_guard<std::mutex> lk(mu);
		out.append(str);
		if (with_endl) out += '\n';
	}
	std::string take() { std::lock_guard<std::mutex> lk(mu); return out; }
};

// A trivial handler: echoes the request body (or "ok") as application/json, or
// throws on /boom so the exception + error paths can be exercised.
struct Echo : http::HttpHandler {
	void handle(const http::Request& req, http::ResponseWriter& resp) override {
		if (req.path == "/boom") { throw std::runtime_error("boom"); }
		resp.send(200, req.body.empty() ? std::string("{\"ok\":true}") : req.body, "application/json");
	}
};

static bool contains(std::string_view hay, std::string_view needle) {
	return hay.find(needle) != std::string_view::npos;
}

int main() {
	using namespace http_log;

	// --- palettes ---------------------------------------------------------
	CHECK(std::string_view(method_palette("GET").head).size() > 0);
	CHECK(std::string_view(method_palette("DELETE").head) != std::string_view(method_palette("GET").head));
	CHECK(std::string_view(method_palette("COMMIT").head) == std::string_view(method_palette("COMMIT").head));
	// an unknown verb has no color
	CHECK(std::string_view(method_palette("WEIRD").head).empty());
	CHECK(std::string_view(status_palette(200).head).size() > 0);
	CHECK(std::string_view(status_palette(404).head) != std::string_view(status_palette(200).head));
	CHECK(std::string_view(status_palette(500).head) != std::string_view(status_palette(404).head));
	CHECK(std::string_view(status_palette(199).head).empty());

	// --- status prefix + level -------------------------------------------
	CHECK(status_prefix(200) != status_prefix(404));
	CHECK(status_prefix(500) != status_prefix(200));
	Options defaults;
	CHECK(status_level(defaults, 200) == LOG_DEBUG);
	CHECK(status_level(defaults, 404) == LOG_DEBUG);
	CHECK(status_level(defaults, 418) == LOG_INFO);
	CHECK(status_level(defaults, 503) == LOG_NOTICE);

	// --- looks_texty / can_preview ---------------------------------------
	CHECK(looks_texty("text/plain"));
	CHECK(looks_texty("application/json"));
	CHECK(looks_texty(""));
	CHECK(!looks_texty("image/png"));
	CHECK(default_can_preview("image/png"));
	CHECK(default_can_preview("application/pdf"));
	CHECK(!default_can_preview("application/json"));

	// --- render_body branches --------------------------------------------
	Options opts;
	opts.prettify = [](std::string_view, std::string_view b) -> std::optional<std::string> {
		return std::string("PRETTY(") + std::string(b) + ")";
	};
	// empty -> empty
	CHECK(render_body("application/json", "", 0, opts, LOG_INFO, "body").empty());
	// prettify hook applied
	CHECK(render_body("application/json", "{}", 2, opts, LOG_INFO, "body") == "PRETTY({})");
	// texty with no hook -> raw
	Options plain;
	CHECK(render_body("text/plain", "hello", 5, plain, LOG_INFO, "body") == "hello");
	// binary with no hook -> "<body '...'>" via repr
	{
		std::string bin("\x01\x02\x03", 3);
		auto r = render_body("application/octet-stream", bin, bin.size(), plain, LOG_INFO, "body");
		CHECK(contains(r, "<body "));
	}
	// oversized -> a size summary (strings::from_bytes)
	{
		std::string big(20 * 1024, 'x');
		auto r = render_body("text/plain", big, big.size(), opts, LOG_INFO, "body");
		CHECK(r.rfind("<body ", 0) == 0);
		CHECK(contains(r, "KiB"));
	}
	// incompletely captured (body shorter than total_size, e.g. a capped/streamed
	// response) -> a size summary of the TRUE length, never prettified/rendered
	{
		auto r = render_body("application/json", "{\"a\":1", 4096, opts, LOG_INFO, "body");
		CHECK(r.rfind("<body ", 0) == 0);
		CHECK(!contains(r, "PRETTY"));
	}
	// image preview: previewable + verbose enough -> the iTerm2 escape (base64)
	{
		Options iopts;
		iopts.image_level = LOG_WARNING;                 // low, so LOG_INFO > it
		std::string png("\x89PNG\r\n\x1a\n", 8);
		auto r = render_body("image/png", png, png.size(), iopts, LOG_INFO, "body");
		CHECK(contains(r, "\033]1337;File="));
		CHECK(contains(r, "inline=1"));
	}

	// --- headers_block ----------------------------------------------------
	{
		http::Headers h{{"Host", "x"}, {"Accept", "application/json"}};
		auto s = headers_block(h);
		CHECK(contains(s, "Host: x\n"));
		CHECK(contains(s, "Accept: application/json\n"));
	}

	// --- the middleware end-to-end ---------------------------------------
	Logging::config.color = LogColorMode::never;   // resolve away color for the sinks
	Logging::config.log_level = LOG_DEBUG;         // let the DEBUG-level blocks through
	Logging::config.with_timestamp = false;
	auto sink = std::make_unique<CaptureSink>();
	CaptureSink* cap = sink.get();
	Logging::add_handler(std::move(sink));

	Echo echo;
	Options mopts;
	mopts.prettify = [](std::string_view, std::string_view b) -> std::optional<std::string> {
		return std::string(b);   // identity; enough to exercise the hook path
	};
	AccessLog access(echo, std::move(mopts));

	struct MockWriter : http::ResponseWriter {
		void status(int) override {}
		void set_header(std::string_view, std::string_view) override {}
		void write(std::string_view) override {}
		void end() override {}
		void set_close() override {}
	};

	// A normal request + 200 response.
	{
		http::Request req;
		req.method = "GET";
		req.path = "/hello";
		req.headers = {{"Accept", "application/json"}};
		MockWriter w;
		access.handle(req, w);
	}
	// The exception + error path: handle() rethrows, the framework would call on_error.
	{
		http::Request req;
		req.method = "GET";
		req.path = "/boom";
		MockWriter w;
		try {
			access.handle(req, w);
			CHECK(false);   // /boom must throw
		} catch (...) {
			access.on_error(std::current_exception(), req, w);
		}
	}

	Logging::finish();
	std::string logged = cap->take();
	CHECK(contains(logged, "GET /hello HTTP/1.1"));      // request head
	CHECK(contains(logged, "Accept: application/json"));  // request header
	CHECK(contains(logged, "HTTP/1.1 200 OK"));           // response head
	CHECK(contains(logged, "\"ok\":true"));               // prettified response body
	CHECK(contains(logged, "GET /boom"));                 // the failing request
	CHECK(contains(logged, "unhandled exception in GET /boom"));  // the exception line
	CHECK(contains(logged, "HTTP/1.1 500"));              // the error response, logged

	if (g_failures == 0) {
		std::puts("all http-log tests passed");
		return 0;
	}
	std::fprintf(stderr, "%d http-log test(s) failed\n", g_failures);
	return 1;
}
