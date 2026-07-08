# AGENTS

Working notes for `http-log`. For usage read `README.md`.

## What this is

One header, `http_log.h`, namespace `http_log`. It provides `AccessLog`, an
`http::HttpHandler` decorator that logs each request and response through
`Kronuz/logger` with `Kronuz/term-color` colors. It recovers the request/response
logging Xapiand had in `Request::to_text` / `Response::to_text` before the
decomposition deleted them (Xapiand commit `602dcdd33`).

## File map

```
http_log.h        everything: the utilities (b64encode / repr / human_bytes), the
                  per-method and per-status color palettes, Options + the Prettify /
                  CanPreview hooks, the body/header/head rendering, CapturingWriter,
                  and the AccessLog middleware.
CMakeLists.txt    INTERFACE library http_log (+ alias http_log::http_log); FetchContents
                  http + logger, guarded so a consumer that already has them is reused.
```

## Dependencies

- **http** (`http_handler.h`, `http_message.h`) — the `HttpHandler` seam it decorates,
  plus `Request` / `ResponseWriter` / `reason_phrase` / `iequal` / `Headers`.
- **logger** — the sink it logs through (`Logging::do_log`, the levels, `ASYNC_LOG_LEVEL`).
  It brings **term-color**, whose `rgb()`/`rgba()`/`brgb()`/`CLEAR_COLOR` build the
  palettes.

No dependency on Xapiand. The body pretty-printing and the previewable-type set are
hooks (`Options::prettify`, `Options::can_preview`), so a host injects its own fidelity.

## Load-bearing invariants

- **Log through `Logging::do_log`, not the `L_*` macros, for the request/response
  bodies.** `L_DEBUG` compiles to nothing without `LOGGER_DEBUG`, so a Release
  consumer would silently lose the detail lines. `do_log` is runtime-gated by
  `Logging::config.log_level` only. The head lines could use `L_NOTICE`, but they use
  `do_log` too for uniformity and the per-line level.
- **Bold rides inside the color.** Palettes use `brgb(...)` for the bold head color,
  never a separate `\033[1m`, so term-color's `collapse()` sees clean triples (a bare
  SGR next to a color defeats the depth collapse).
- **The middleware forwards the whole seam.** `on_request_body`, `wants_body_stream`,
  `create_extension`, `should_offload`, and `on_error` all delegate to the inner
  handler. Only `handle()` is wrapped. Don't drop a forward when the `HttpHandler`
  interface grows, or a decorated handler loses that capability.
- **The response is captured, not re-derived.** `CapturingWriter` wraps the real
  writer and records status / content-type / headers / body while forwarding, so the
  response is rendered from what was actually sent. It logs the response after the
  inner `handle()` returns (fine for a buffered handler that calls `end()` within
  `handle()`); a handler that defers `end()` past `handle()` return is a known
  limitation.
- **The error path preserves the app's status mapping.** `handle()` logs the thrown
  exception (`L_EXC`, so the description + backtrace come from the logger hooks) and
  *rethrows*, so the framework's backstop calls the inner handler's `on_error` for the
  real status mapping (e.g. a client error → 400). `AccessLog::on_error` captures that
  mapped response and logs it; if the inner leaves it unanswered, it reflects the
  framework's generic 500 fallback.

## Rendering fidelity (from Xapiand's to_text)

- One request or response is one log call: an emoji prefix (🌎 request; 💊 2xx,
  💫 3xx, 🕸 404, 💥 4xx, 🔥 5xx response) plus the block, with continuation lines
  indented 4 spaces (Xapiand's `log_request` / `log_response` +
  `string::indent(text, ' ', 4, false)`). The head is `METHOD path HTTP/v` (request)
  or `HTTP/v STATUS reason` (response); timing rides in a `Response-Time` header the
  app sets, not the log line.
- Per-method palette: OPTIONS/INFO/HEAD purple, GET/SEARCH/COUNT/DUMP blue,
  POST/RESTORE/PATCH/UPDATE/UPSERT/PUT orange, DELETE red, COMMIT teal,
  OPEN/CLOSE pink, else no color. Per-status: 2xx green, 3xx teal, 404 tan, 4xx
  orange-red, 5xx red. The section color is applied once (`head` bold, `headers`
  dimmed, `text`), not per line. These RGB values are ported verbatim; keep them.
- Body branch (matches to_text's decoded path, since `log_*` always decode): an
  iTerm2 image when previewable and `log_level > image_level`, else a size summary
  for an oversized body, else the `prettify` hook, else raw text (texty) or an
  escaped `repr` (binary). The iTerm2 escape is
  `\033]1337;File=name=;inline=1;size=<n>;width=20%:<b64>\a` (Xapiand used `width=20%`).
- Levels are per-status (`Options::request_level` / `level_2xx` / `level_4xx` /
  `level_5xx`), Xapiand-faithful defaults DEBUG / DEBUG / INFO / NOTICE; errors are
  more visible. A consumer raises them to show the blocks at a lower verbosity.

## Build / test

Header-only; there is nothing to compile standalone. Build it through a consumer
(e.g. `Kronuz/prism`, which uses it), or add a smoke `test/` that instantiates
`http_log::AccessLog` over a trivial handler.

## Conventions

C++20. Double quotes in code. No em dashes in docs. MIT header on the source.
