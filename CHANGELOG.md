# Changelog

Notable changes to `http-log`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no releases, so
entries are dated.

## 2026-11-01

### Fixed

- cppcodec was fetched at `GIT_TAG master`, so a build could change without a commit here.
  It is pinned to `8019b8b580f8573c33c50372baec7039dfe5a8ce`, the commit its master names
  (2022-09-06; the last tag, `v0.2`, is from 2018) and the one `logger` pins.

### Tests

- `http_log_pins` (`test/check_pins.cmake`) reads `CMakeLists.txt` and fails unless every
  `GIT_TAG` is a 40-digit commit hash. With the old file it fails on `GIT_TAG master is not
  a 40-digit commit hash`; the five pins (http, logger, strings, repr, cppcodec) pass.

### Added

- `docs/API.md`: `AccessLog`, the `Options` table, metadata-only mode and the error path,
  moved out of the README.
- This changelog.

### Changed

- The README follows the family README standard: the description as its first sentence,
  Quick start before Installation, a pinned FetchContent tag, and "Not for" limits. It
  names CMake 3.16, the version the fetched http requires, where it said 3.14, and its Quick
  start turns on `DEBUG` logging so the blocks appear.

## 2026-10-30

### Changed

- `HttpHandler::handle` is a coroutine in http, so `AccessLog::handle` is a coroutine that
  awaits the inner handler; `should_offload` is no longer forwarded. http is pinned at
  `fce24c6`.

## 2026-10-20

### Added

- `Options::metadata_only`: method, escaped path and status blocks without headers, bodies,
  query values, image previews or exception descriptions. Method and path labels are bounded
  to 32 and 256 input bytes. Logging and rendering failures no longer replace the
  application's response. `examples/demo.cc` logs a metadata-only exchange.

### Changed

- Error mapping follows a successful `end()` instead of whether a status was set: a
  response that already completed is not remapped. http is pinned at its
  `response_ended()` completion query.

## 2026-10-17

### Added

- `Options::client_colors`: each request block leads with the client's address, in a color
  stable per address.

### Changed

- Fetched libraries are pinned to commits.

## 2026-10-14

### Added

- CI on Linux, macOS and FreeBSD, nightly and on demand.

## 2026-07-08

### Added

- `AccessLog`, an `http::HttpHandler` middleware that reproduces the request and response
  logging Xapiand removed with its `Response` class: per-method and per-status palettes,
  prettified bodies through a hook, iTerm2 inline image previews, 10 KiB truncation and the
  exception path.
- Responses are logged without a copy through `BufferedResponse`, with a capturing writer as
  the fallback.

### Fixed

- The inner handler's `on_error` runs inside `handle()` with the same writer, so a handler
  that stores its writer no longer reads a destroyed one after an exception.
- Only a `5xx` or an unanswered request logs an "unhandled exception".
