# http-log

An [`http::HttpHandler`](https://github.com/Kronuz/http) middleware that logs every
HTTP request and response through [Kronuz/logger](https://github.com/Kronuz/logger),
in color. Wrap your application handler with it and each exchange is rendered to the
log: the request line and headers, the response status and timing, and the bodies,
prettified. It is the request/response logging Xapiand used to have, pulled out into
a library both Xapiand and other services can share.

## What it renders

- **A color per method and per status.** Requests are tinted by verb (GET/SEARCH/
  COUNT/DUMP blue, POST/PUT/PATCH/UPDATE/UPSERT/RESTORE orange, DELETE red, COMMIT
  teal, OPEN/CLOSE pink, OPTIONS/INFO/HEAD purple); responses by status (2xx green,
  3xx teal, 404 tan, 4xx orange-red, 5xx red).
- **Prettified bodies.** A pluggable hook renders the body for the log: a service
  with a MsgPack/JSON/YAML decoder injects that; a plain app injects a JSON
  reindenter. Unstructured text is shown as-is; binary is an escaped, capped `repr`.
- **iTerm2 inline image previews.** Image (and PDF/EPS/PostScript/PSD) bodies are
  previewed inline in the log on iTerm2, gated on verbosity.
- **Large-body truncation.** Bodies over a limit become a `<body 2.4 MiB>` summary.
- **The exception path.** A handler that throws is logged with its description and,
  when the logger's backtrace hook is set, a stack trace, then handed to the inner
  handler's own error-to-status mapping. The resulting response is logged too.

Colors are [term-color](https://github.com/Kronuz/term-color) stacked escapes; the
logger sink resolves them to the terminal (`--color` / `NO_COLOR` / depth). Each
request or response is logged as **one block** (an emoji prefix plus the head line,
headers, and body, indented), at a per-status level matching Xapiand: requests and
2xx/3xx/404 at `DEBUG`, 4xx at `INFO`, 5xx at `NOTICE`, with inline images above
that. A consumer can raise the levels to make the blocks visible at a lower
verbosity (a demo does this).

## Provenance

Xapiand once rendered each request and response with `Request::to_text` /
`Response::to_text` (per-method and per-status palettes, prettified JSON/YAML/MsgPack
bodies, iTerm2 image previews, truncation). That code was deleted in the decomposition
(Xapiand commit `602dcdd33`, "delete Xapiand's Response class"). This library recovers
that behavior generically: it works over the `http::Request` seam and a body-prettify
hook, so it carries no dependency on Xapiand's types, and Xapiand adopts it by wrapping
its handler and injecting its MsgPack pretty-printer.

## Install

CMake with `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(http_log
  GIT_REPOSITORY https://github.com/Kronuz/http-log.git
  GIT_TAG        main)
FetchContent_MakeAvailable(http_log)

target_link_libraries(your_target PRIVATE http_log::http_log)
```

Header-only (`http_log.h`). It brings [http](https://github.com/Kronuz/http) (the
`HttpHandler` seam) and [logger](https://github.com/Kronuz/logger) (which brings
term-color). Requires C++20.

## Usage

Wrap your handler; the middleware forwards the whole `HttpHandler` seam
(`on_request_body` / `wants_body_stream` / `create_extension` / `should_offload` /
`on_error`) to it, so it is transparent to the framework.

```cpp
#include "http_log.h"

MyApp app;                                   // your http::HttpHandler

http_log::Options opts;
opts.prettify = [](std::string_view ct, std::string_view body)
    -> std::optional<std::string> {
    if (ct.find("json") != std::string_view::npos) return reindent_json(body);
    return std::nullopt;                      // fall back to raw text / repr
};

http_log::AccessLog access(app, std::move(opts));

// Point the logger's backtrace hook at your traceback provider so a thrown
// handler logs a stack trace (optional):
Logging::hooks.backtrace = [] { return traceback::format(""); };

http::HttpAsioService service(access, reactors, workers, queue_limit);
service.start(port);
```

`Options`: `prettify` (the body hook), `can_preview` (which content-types preview
inline; defaults to image/* plus the document set), `body_limit` (default 10 KiB),
`images` (on by default), and the per-status levels (`request_level` / `level_2xx` /
`level_4xx` / `level_5xx`, Xapiand-faithful DEBUG/DEBUG/INFO/NOTICE, plus
`image_level`).

## License

MIT, Copyright (c) 2015-2026 Dubalu LLC. See the per-file header.
