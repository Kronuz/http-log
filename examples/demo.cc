#include "http_log.h"
#include <iostream>

// Runs a coroutine to completion on a private loop.
static void drive(asio::awaitable<void> task) {
	asio::io_context io;
	std::exception_ptr failure;
	asio::co_spawn(io, std::move(task), [&](std::exception_ptr e) { failure = e; });
	io.run();
	if (failure) { std::rethrow_exception(failure); }
}

struct Application : http::HttpHandler {
	asio::awaitable<void> handle(http::Request&, http::ResponseWriter& response) override {
		response.send(200, "hello\n");
		co_return;
	}
};

struct ConsoleResponse : http::ResponseWriter {
	void status(int) override {}
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view bytes) override { std::cout << bytes; }
	void end() override {}
	void set_close() override {}
};

int main() {
	Logging::config.log_level = LOG_DEBUG;
	Logging::config.tint = true;
	Logging::add_handler(std::make_unique<StderrLogger>());
	Application application;
	http_log::Options options;
	options.metadata_only = true;
	http_log::AccessLog access(application, options);
	http::Request request;
	request.method = "GET";
	request.path = "/hello?omitted=value";
	ConsoleResponse response;
	drive(access.handle(request, response));
	Logging::finish();
	Logging::join();
}
