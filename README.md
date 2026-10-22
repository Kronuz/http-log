# http-log

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/http-log/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/http-log/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Colored request and response logs for any [http](https://github.com/Kronuz/http)
service, in C++20. Wrap your handler, and every exchange is logged as one readable
block: the request line, headers, status, timing, and a prettified body.

## ✨ Highlights

- 🎨 **A color per method and per status**, from verbs like `GET` and `DELETE` to
  `5xx`.
- 🪄 **Prettified bodies** through your own hook (JSON, MsgPack, YAML); binary is
  shown as an escaped, capped `repr`, and large bodies as a `<body 2.4 MiB>` summary.
- 🖼️ **Inline image previews** on iTerm2 at high verbosity.
- 🧯 **Exceptions logged with a stack trace** when the logger's backtrace hook is set.
- 🔌 **Transparent**: forwards the whole `HttpHandler` seam, including streamed
  bodies and offload, to your handler.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer. It brings
[http](https://github.com/Kronuz/http) and [logger](https://github.com/Kronuz/logger).
CI builds it on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan,
UBSan, and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(http_log
  GIT_REPOSITORY https://github.com/Kronuz/http-log.git
  GIT_TAG        main)
FetchContent_MakeAvailable(http_log)

target_link_libraries(my_app PRIVATE http_log::http_log)
```

## 🚀 Quick start

```cpp
#include "http_asio.h"
#include "http_log.h"

#include <chrono>
#include <thread>

struct App : http::HttpHandler {
	void handle(const http::Request&, http::ResponseWriter& resp) override { resp.send(200, "hello\n"); }
};

int main() {
	App app;
	http_log::AccessLog access(app);   // default options
	http::HttpAsioService service(access, /*reactors=*/2, /*workers=*/2, /*queue_limit=*/64);
	service.start(8080);
	std::this_thread::sleep_for(std::chrono::minutes(1));
	service.stop();
}
```

Requests log at `DEBUG`; raise the logger's level, or the `Options` levels, to see
them. [prism](https://github.com/Kronuz/prism) is a complete example.

Set `Options::metadata_only = true` for method, escaped path and status blocks without headers, bodies, query values, image previews or exception descriptions. Method and path labels are bounded to 32 and 256 input bytes. Response blocks include the handler's body byte count. The default retains Xapiand's detailed rendering. Logging/rendering failures do not replace a successful application response.

`examples/demo.cc` shows metadata-only logging without a network listener. Build the standalone `http_log_demo` target and run it in a terminal to see the shared method/status colors; redirected output follows the logger's normal color policy.

`AccessLog` currently supports synchronous responses only. Do not wrap a handler that transfers response ownership through `defer()` or `stream()`; those require transport completion observation rather than logging when `handle()` returns. The synchronous response block describes the handler's response before transport transforms, not confirmed socket delivery.

Error mapping follows successful `end()`, not whether a status was set. An unfinished handler that throws gets its normal error mapping and a 500 fallback if mapping remains unfinished; exceptions after completion do not remap the response. Accurate buffered completion requires the HTTP version pinned here; custom buffered writers should override `response_ended()` rather than rely on its legacy started approximation.

## ⚙️ Configuration

| `http_log::Options` | Default | Meaning |
|---|---|---|
| `prettify` | none | `(content_type, body) -> optional<string>` for structured bodies |
| `can_preview` | images and documents | which content types preview inline |
| `body_limit` | 10 KiB | larger bodies log as a size summary |
| `capture_limit` | 1 MiB | most response bytes the middleware retains |
| `images` | `true` | iTerm2 inline previews |
| `client_colors` | `false` | lead each request with its client's address (`Request::remote`), in a color stable per address |
| `request_level` | `LOG_DEBUG` | the request block |
| `level_2xx` | `LOG_DEBUG` | 2xx, 3xx, and 404 responses |
| `level_4xx` / `level_5xx` | `LOG_INFO` / `LOG_NOTICE` | error responses |
| `image_level` | `LOG_DEBUG + 1` | previews appear above this level |

## 🎯 When to use it

- ✅ Development and debugging of an http service; a readable access log.
- ❌ Machine-parsed access logs: the output is for people (colors, indentation).

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the palettes, levels, and exception path, and
  the Xapiand rendering it recovers.
- [AGENTS.md](AGENTS.md): file map, streaming, and invariants.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) first. CI must
pass on every platform.

## 📄 License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE). Extracted from
[Xapiand](https://github.com/Kronuz/Xapiand).
