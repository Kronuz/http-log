# http-log API

`http_log.h` is the whole library: `http_log::AccessLog`, an `http::HttpHandler` that wraps
another handler, and `http_log::Options`.

## AccessLog

```cpp
http_log::AccessLog access(inner_handler, options);  // options default to http_log::Options{}
```

`AccessLog::handle` awaits the inner handler, then logs the request block and the response
block through [logger](https://github.com/Kronuz/logger). It forwards `on_request_body`,
`wants_body_stream` and `create_extension` to the inner handler, so streamed request bodies
reach it unchanged.

- **Response source.** When the transport's writer is an `http::BufferedResponse`, the
  middleware logs its retained status, headers and body without a copy. Otherwise it wraps
  the writer and keeps up to `capture_limit` response bytes.
- **Synchronous responses only.** The response block describes the handler's response
  when `handle()` returns, before transport transforms, not confirmed socket delivery. Do
  not wrap a handler that transfers response ownership through `defer()` or `stream()`;
  logging those needs transport completion, which this middleware does not observe.
- **Error mapping.** A handler that throws before its response ends gets the inner
  handler's `on_error` mapping with the same writer, and a `500` when the mapping leaves the
  response unfinished. An exception after a successful `end()` does not remap the response.
  A `5xx` from an exception is logged at CRIT, with a stack trace when the logger's
  backtrace hook is set. Custom buffered writers override `response_ended()` so this
  follows `end()`; its default returns `response_started()`.
- **Failures stay in the log.** A logging or rendering failure never replaces a successful
  application response.

## Options

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
| `metadata_only` | `false` | log method, escaped path and status only |

With `metadata_only = true`, a block keeps the method and status colors and omits headers,
bodies, query values, image previews and exception descriptions. The method label is
bounded to 32 input bytes and the path to 256, both escaped, and the response block carries
the handler's body byte count. The default is Xapiand's detailed rendering.

[`examples/demo.cc`](../examples/demo.cc) (`http_log_demo`) logs a metadata-only exchange
without a network listener; run it in a terminal to see the method and status colors.
Redirected output follows the logger's color policy.
