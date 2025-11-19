# Test Backend Migration Plan

This document tracks the planned changes for exercising every available
`sio::event_loop` backend through the test suite. The steps below assume no
behavioural differences between backends unless noted otherwise.

## 1. Introduce a shared backend matrix helper
- Add `tests/common/backend_matrix.hpp` that enumerates the usable backend types
  and provides compile-time availability flags.
- Detect `stdexec` support unconditionally.
- Detect `io_uring` support by checking for `<liburing.h>` and optionally a
  `SIO_TEST_HAS_IOURING` compile definition supplied by CMake.
- Expose convenience aliases inside each backend class so tests can query
  `Backend::native_context_type`, `Backend::read_factory`, etc. without spelling
  backend-specific namespaces.
- Provide a generic `sync_wait` helper template that accepts an arbitrary
  backend and sender.

## 2. Refactor existing tests to use the backend matrix
- Replace hard-coded `using backend = sio::event_loop::stdexec_backend::backend`
  declarations (`tests/test_file_handle.cpp`, `tests/test_async_accept.cpp`,
  `tests/test_read_batched.cpp`, `tests/net/test_socket_handle.cpp`) with
  `TEMPLATE_LIST_TEST_CASE` or similar constructs that iterate over the backend
  list from `backend_matrix.hpp`.
- Update sequence tests that currently include
  `sio/event_loop/stdexec/fd_read.h`/`fd_write.h` to instead obtain the relevant
  factory types from the selected backend (`Backend::read_factory`,
  `Backend::write_factory`).
- Ensure backend-specific constraints (e.g. requirements for `/dev/null`,
  memfd) are guarded so they skip when the backend is unavailable on the host.

## 3. Wire backend detection into the build
- Teach `tests/CMakeLists.txt` to set `SIO_TEST_HAS_IOURING=1` when liburing
  headers are found and link dependencies are satisfied; otherwise define it to
  `0`.
- Continue building a single Catch2-based test binary; the templated tests will
  instantiate once per backend automatically.
- Document how to disable io_uring coverage on platforms lacking liburing
  (e.g. `-DSIO_DISABLE_IOURING_TESTS=ON`) and ensure the helper header honours
  that switch.

## 4. CI and follow-up
- Update CI scripts to run the test suite twice if needed: once with both
  backends enabled (default) and once with io_uring explicitly disabled to keep
  conditional code covered.
- After migration, validate locally by running `ctest --output-on-failure` and
  confirming the test report lists each backend variant separately.
