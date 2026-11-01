# http-log

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/http-log/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/http-log/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only HTTP access-log middleware for C++20: colored, prettified request and response blocks for http services.
`http_log::AccessLog` wraps any [http](https://github.com/Kronuz/http) handler and logs each
exchange through [logger](https://github.com/Kronuz/logger) as two readable blocks: the
request line and headers, then the status, headers and a prettified body. Reach for it while
developing or debugging a service, when you want to read the traffic.

## ✨ Highlights

- **A color per method and per status.** Verbs from `GET` to `DELETE` and custom ones like
  `RESTORE` each have a tint, and so do `2xx`, `3xx`, `404`, `4xx` and `5xx`.
- **Prettified bodies.** Your hook renders JSON, MsgPack or YAML; binary shows as an
  escaped, capped `repr`, and a large body as a `<body 2.4 MiB>` summary.
- **Inline image previews.** Image and document bodies preview inline on iTerm2 at high
  verbosity.
- **The exception path.** A handler that throws gets its own error mapping, and a `5xx` is
  logged with a stack trace when the logger's backtrace hook is set.
- **Metadata-only mode.** `Options::metadata_only` logs method, escaped path and status,
  and omits headers, bodies and query values.
- **Transparent.** Streamed request bodies and extensions pass through to your handler.

## 🚀 Quick start

```cpp
#include "http_asio.h"
#include "http_log.h"

#include <chrono>
#include <memory>
#include <thread>

struct App : http::HttpHandler {
	asio::awaitable<void> handle(http::Request&, http::ResponseWriter& resp) override {
		resp.send(200, "hello\n");
		co_return;
	}
};

int main() {
	Logging::config.log_level = LOG_DEBUG;  // requests log at DEBUG
	Logging::add_handler(std::make_unique<StderrLogger>());

	App app;
	http_log::AccessLog access(app);  // default options
	http::HttpAsioService service(access, /*reactors=*/2, /*in_flight_limit=*/64);
	service.start(8080);  // curl localhost:8080/ logs a GET block and a 200 block to stderr
	std::this_thread::sleep_for(std::chrono::minutes(1));
	service.stop();
}
```

[`examples/demo.cc`](examples/demo.cc) logs a metadata-only exchange without a network
listener, and [prism](https://github.com/Kronuz/prism) is a complete service that uses it.

## 📦 Installation

Requires a C++20 compiler and CMake 3.16 or newer; it fetches
[http](https://github.com/Kronuz/http), [logger](https://github.com/Kronuz/logger),
[strings](https://github.com/Kronuz/strings) and [repr](https://github.com/Kronuz/repr) at
pinned commits, and cppcodec's headers. CI builds it on Linux (x86_64, arm64), macOS (arm64,
x86_64), and FreeBSD, plus ASan, UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(http_log
  GIT_REPOSITORY https://github.com/Kronuz/http-log.git
  GIT_TAG        d2315b86ee0483c0b2fa8b25f82e79e64e40b33d)  # or main, to track the tip
FetchContent_MakeAvailable(http_log)

target_link_libraries(my_app PRIVATE http_log::http_log)
```

```cpp
#include "http_log.h"
```

## 🎯 When to use it

Use it during development and debugging of an http service, or as an access log people
read.

Not for:

- **Machine-parsed access logs.** The output is for people: colors, emoji and indentation.
- **Deferred or streamed responses.** `AccessLog` logs the response when `handle()` returns;
  do not wrap a handler that hands its response off through `defer()` or `stream()`.
- **Proof of delivery.** The response block is the handler's response before transport
  transforms, not what reached the socket.

## 📚 Documentation

- [docs/API.md](docs/API.md): `AccessLog`, every `Options` field, metadata-only mode and the
  error path.
- [ARCHITECTURE.md](ARCHITECTURE.md): the palettes, the levels, the exception path, and the
  Xapiand rendering it reproduces.
- [CHANGELOG.md](CHANGELOG.md): dated changes.
- [AGENTS.md](AGENTS.md): file map, streaming and invariants.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries): the catalog.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

## 🤝 Contributing

Read [AGENTS.md](AGENTS.md) and the [catalog's rules](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md)
first.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo and (c) 2015-2020 Dubalu LLC. See
[LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand).
