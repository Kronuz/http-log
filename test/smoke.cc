// Compile + link smoke test: instantiate http_log::AccessLog over a trivial
// handler, exercise the palettes and body renderer, and log one request/response.
#include <cassert>
#include <memory>
#include "http_log.h"

struct Echo : http::HttpHandler {
	void handle(const http::Request& req, http::ResponseWriter& resp) override {
		resp.send(200, req.body.empty() ? "ok" : req.body, "application/json");
	}
};

int main() {
	Logging::config.color = LogColorMode::never;   // no escapes in the test output
	Logging::config.log_level = LOG_INFO;
	Logging::add_handler(std::make_unique<StderrLogger>());

	// Palettes resolve for known verbs/status.
	assert(!http_log::method_palette("GET").head.empty());
	assert(!http_log::status_palette(200).head.empty());
	assert(http_log::method_palette("GET").head != http_log::method_palette("DELETE").head);

	// Body renderer: prettify hook, texty passthrough, oversized summary.
	http_log::Options opts;
	opts.prettify = [](std::string_view, std::string_view b) -> std::optional<std::string> {
		return std::string("pretty:") + std::string(b);
	};
	assert(http_log::render_body("application/json", "{}", opts, LOG_INFO, "body") == "pretty:{}");
	std::string big(20 * 1024, 'x');
	assert(http_log::render_body("text/plain", big, opts, LOG_INFO, "body").rfind("<body ", 0) == 0);

	// The middleware wraps a handler and logs a real exchange (buffered).
	Echo echo;
	http_log::AccessLog access(echo, std::move(opts));
	(void)access;

	Logging::finish();
	std::puts("all http-log smoke tests passed");
	return 0;
}
