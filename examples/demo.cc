#include "http_log.h"
#include <iostream>

// Runs a task to completion on the calling thread.
static void drive(task::task<void> t) { task::sync_wait(std::move(t)); }

struct Application : http::HttpHandler {
	task::task<void> handle(http::Request&, http::ResponseWriter& response) override {
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
