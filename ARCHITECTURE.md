# http-log architecture

What the middleware renders, at which levels, and where the behavior comes from.

The optional metadata-only mode preserves the method/status palettes while omitting raw headers, bodies, query values, images and exception descriptions. It bounds and escapes request labels before rendering. Default detailed rendering is unchanged. Diagnostic rendering failures are contained independently of application error mapping.

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
