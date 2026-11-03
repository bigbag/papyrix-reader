# Ipp

IPP print server core for the Papyrix e-reader. The device acts as a driverless network printer (AirPrint-compatible raster printer).

## Origin

The IPP implementation comes from the [crosspoint-reader](https://github.com/NishantJoshi00/crosspoint-reader) fork by Nishant Joshi. Introducing commit [`df0d5e3`](https://github.com/NishantJoshi00/crosspoint-reader/commit/df0d5e367b08e206ab914dcff10a2d255d67bcdb) ("feat: add printer mode (AirPrint/IPP printing to e-ink)") at revision `f137b35327db4f685ed2803558d74216975031c8`, [`src/network/ipp`](https://github.com/NishantJoshi00/crosspoint-reader/tree/f137b35327db4f685ed2803558d74216975031c8/src/network/ipp).

The base CrossPoint project is MIT, Copyright (c) 2025 Dave Allie. His license covers the fork. Background: [How to build a f\*\*king printer](https://nishantjosh.dev/blogs/how-to-build-a-fking-printer/) by Nishant Joshi.

## Papyrix changes after the port

- Height bound against line-RLE decompression bombs.
- Color space / bit depth validation.
- Upscale row and column replication in the scaler.
- Chunked framing validation: chunk suffix, payload terminator, trailer terminator, bounded trailer lines, and latched framing failures.
- Strict Content-Length parsing.
- Parser enforces the RFC name grammar and group scoping for additional values. It rejects embedded NUL bytes in attribute names, malformed or out-of-range integer values, and unstorable or unprintable document-format values.
- Validate-Job mirrors Print-Job format rejection. Cancel-Job returns not-found or not-possible. Malformed framing after a decoded page returns bad-request.
- The document byte cap applies to the document alone. The transport cap keeps its headroom for IPP attributes.
- URF streams with an unknown page count are accepted. Pages decode up to the configured page cap; the service drains the remainder.

## Layout

- `IppProto.h` — wire constants (RFC 8010/8011, CUPS raster formats).
- `IppTransport.h` — byte transport, buffered reader, HTTP body framing.
- `IppLog.h` — logging shim for firmware and host builds.
- `IppParser` / `IppWriter` — request parse and response encode.
- `PageSink.h` — decoded-page consumer interface.
- `RasterDecoder` — streaming Apple raster (URF) and PWG raster decode.
- `PageScaler` — box downsample + Floyd-Steinberg dither to 1-bit rows.
- `IppPrintService` — capability advertisement and operation handling.
- `HttpIppConnection` — minimal HTTP/1.1 loop for `application/ipp`.

The core has no Arduino types. `IppTransport` is the port point. The test harness implements it over an in-memory buffer.
