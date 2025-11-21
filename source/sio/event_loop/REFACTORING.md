# Event Loop Refactoring Plan

This document consolidates the agreed upon changes for the descriptor/handle refactor before
any code is modified.

## Goals

- Make `*_handle` objects as lightweight as native Linux descriptors: they hold only the loop
  pointer and a trivial descriptor token.
- Keep every piece of runtime state (wait queues, cached protocol/mode data, etc.) inside the
  event loop backend; handles never own it.
- Remove the ad-hoc `adopt` helpers used only by tests/examples and temporarily disable those
  tests until a new public API exists.

## Token Types

- Introduce `struct basic_fd { int fd{-1}; bool is_valid() const noexcept; int native_handle() const noexcept; }`.
- Provide `template<class Protocol> struct socket_fd : basic_fd { using basic_fd::basic_fd; };` to
  make socket types distinct while staying zero-cost (no extra padding beyond the int).
- Define backend aliases:
  - `using file_state = basic_fd;`
  - `using seekable_file_state = basic_fd;`
  - `template<class Protocol> using socket_state = socket_fd<Protocol>;`
  - `template<class Protocol> using acceptor_state = socket_fd<Protocol>;`
- `is_valid()` returns `fd >= 0`. `native_handle()` returns the integer (asserts validity in debug).

## Handle Changes (`file_handle.hpp`, `socket_handle.hpp`)

- Replace `std::optional<state_type>` with a plain `state_type state_{};`.
- Add `bool is_open() const noexcept { return state_.is_valid(); }` and use `SIO_ASSERT(is_open())`
  before dispatching operations.
- Remove `*_handle::adopt`. Update docs/examples/tests to explain the removal and add TODOs for a
  future replacement.

## Backend Surface Contracts

- `file_loop`, `seekable_file_loop`, and `socket_loop_for` concepts now assume state tokens are
  trivial; backends take `basic_fd`/`socket_fd` and perform any descriptor lookups internally.
- `open_file`/`open_socket` senders still receive mode/protocol parameters because those are only
  needed during the open itself. After completion, only the descriptor token remains.
- Runtime metadata queries (protocol, endpoints, modes) must use Linux APIs (`getsockopt`,
  `getsockname`, `fcntl`, etc.). If some data cannot be reconstructed that way, document the gap.

## Backend-Specific Notes

### io_uring (`source/sio/event_loop/iouring`)

- Replace `fd_state`/`file_state_base`/`socket_state` structs with the new token aliases.
- Sender implementations (`fd_read`, `fd_write`, `socket_accept`, `socket_connect`, `socket_sendmsg`,
  `file_open`) now take/return the tokens directly.
- No descriptor registry is required; the raw fd is enough. Closing simply schedules the existing
  close sender on that fd.

### stdexec backend

- Mirror the io_uring changes to keep feature parity.

### epoll backend

- Prefer the same thin tokens externally. Internally, keep any per-fd bookkeeping inside the
  `epoll::context` via an entry map (`fd -> descriptor_entry`). Entries store wait queues and the
  current interest mask.
- Provide helpers inside the backend to create/destroy entries when a descriptor is opened or
  closed; handles still only carry the `basic_fd` token.
- Document any state that must remain per-descriptor even though it is hidden from user code.

## Tests & Examples

- Comment out tests/examples that rely on `*_handle::adopt` (e.g., the stdin/stdout example). Add
  `// TODO` markers describing that they need to be rewritten using only the public API once a
  replacement exists.
- Ensure the backend matrix continues to compile by guarding tests with `if constexpr` as needed.

## Documentation

- Update `docs/migrations/pluggable-event-loop.md` and `epoll/README.md` to describe the new token
  model, the absence of `adopt`, and the expectation that Linux APIs are used for runtime queries.
