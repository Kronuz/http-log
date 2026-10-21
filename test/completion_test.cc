#include "http_asio.h"
#include "http_log.h"
#include <cstdio>
#include <stdexcept>

static void require(bool condition) {
	if (!condition) throw std::runtime_error("HTTP logging completion regression");
}

struct Application : http::HttpHandler {
	unsigned mappings = 0;
	void handle(const http::Request& request, http::ResponseWriter& response) override {
		if (request.path != "/before") response.status(200);
		if (request.path == "/partial") response.write("partial");
		if (request.path == "/ended") response.send(200, "complete");
		throw std::runtime_error("handler refused");
	}
	void on_error(std::exception_ptr, const http::Request& request, http::ResponseWriter& response) override {
		++mappings;
		if (request.path == "/mapped") response.send(404, "mapped");
		if (request.path == "/unfinished-map") response.status(404);
	}
};

struct Writer : http::ResponseWriter {
	int code = 0;
	bool ended = false, refuse_end = false;
	std::string body;
	void status(int value) override { code = value; }
	void set_header(std::string_view, std::string_view) override {}
	void write(std::string_view value) override { body += value; }
	void end() override {
		if (refuse_end) { refuse_end = false; throw std::runtime_error("end refused"); }
		ended = true;
	}
	void set_close() override {}
};

static http_log::Options options() {
	http_log::Options result;
	result.metadata_only = true;
	return result;
}

static void unit_cases() {
	for (const auto& path : {"/before", "/status", "/partial", "/mapped", "/unfinished-map", "/ended"}) {
		http::Request request;
		request.method = "GET";
		request.path = path;
		const bool completed = request.path == "/ended";
		const int expected = completed ? 200 : request.path == "/mapped" ? 404 : 500;
		{
			Application application;
			http_log::AccessLog logging(application, options());
			http::AsioResponseWriter writer(request, nullptr);
			logging.handle(request, writer);
			require(writer.response_code() == expected && writer.response_ended());
			require(application.mappings == (completed ? 0 : 1));
		}
		{
			Application application;
			http_log::AccessLog logging(application, options());
			Writer writer;
			logging.handle(request, writer);
			require(writer.code == expected && writer.ended);
			require(application.mappings == (completed ? 0 : 1));
		}
	}
	http::Request request;
	request.path = "/ended";
	Application application;
	http_log::AccessLog logging(application, options());
	Writer writer;
	writer.refuse_end = true;
	logging.handle(request, writer);
	require(writer.code == 500 && writer.ended && application.mappings == 1);
}

static std::string exchange(http::HttpHandler& handler, std::string_view path) {
	asio::io_context io;
	using asio::ip::tcp;
	tcp::acceptor reservation(io, {asio::ip::address_v4::loopback(), 0});
	const auto port = reservation.local_endpoint().port();
	reservation.close();
	http::HttpAsioService service(handler, 1, 0, 16);
	http::AsioBindOptions bind;
	bind.address = "127.0.0.1";
	service.set_bind_options(bind);
	service.start(port);
	std::string response;
	{
		tcp::socket socket(io);
		socket.connect({asio::ip::address_v4::loopback(), port});
		const auto request = "GET " + std::string(path) + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
		asio::write(socket, asio::buffer(request));
		asio::error_code error;
		asio::read(socket, asio::dynamic_buffer(response), error);
		require(error == asio::error::eof);
	}
	service.stop();
	const auto split = response.find("\r\n\r\n");
	require(split != std::string::npos);
	return response.substr(0, response.find("\r\n")) + response.substr(split);
}

int main() {
	Logging::config.log_level = LOG_EMERG;
	unit_cases();
	for (const auto& path : {"/status", "/partial", "/mapped", "/unfinished-map", "/ended"}) {
		Application direct, wrapped;
		http_log::AccessLog logging(wrapped, options());
		require(exchange(direct, path) == exchange(logging, path));
		require(direct.mappings == wrapped.mappings);
	}
	std::puts("HTTP access logging preserves response completion");
}
