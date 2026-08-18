# ADR-0003: Length-bounded byte-oriented parsing

- Status: Accepted
- Date: 2026-08-12

## Context

Serial and TCP reads produce arbitrary byte sequences with an explicit byte
count. Treating them as C strings can scan beyond the received frame or require
writing a terminator outside a completely filled buffer. Reinterpreting wire
bytes as integers also introduces alignment and host-endian dependencies.

## Decision

Pass received data as `std::span` or `std::string_view` and preserve its length
through dispatch and parsing. Never append a NUL to a full receive buffer.
Serialize and decode multibyte wire fields explicitly.

The TCP receive capacity is `tcp_message::MaxMessageSize`; impossible transfer
counts are rejected after being clamped to the storage boundary. Parser fuzz
targets run under libFuzzer, ASan, and UBSan in CI.

## Consequences

- Embedded NUL bytes and exact-capacity reads are handled without out-of-bounds
  access.
- Parsers must not call unbounded C string functions on network data.
- Each new wire parser should expose a bounded, GUI-free entry point suitable
  for regression tests and fuzzing.

## Evidence

- TCP boundary: [`TcpMessageParser.cpp`](../../src/TcpMessageParser.cpp),
  [`Session.cpp`](../../src/Session.cpp)
- Exact-capacity tests: [`TcpMessageParserTests.cpp`](../../tests/TcpMessageParserTests.cpp)
- Fuzz harnesses: [`NetworkMessageFuzz.cpp`](../../fuzz/NetworkMessageFuzz.cpp),
  [`ProtocolParsersFuzz.cpp`](../../fuzz/ProtocolParsersFuzz.cpp)
