# Native io_uring Backend (liburing)

This backend will provide a clean implementation that talks directly to
[liburing](https://github.com/axboe/liburing). The goal is to expose the same
contract as the `stdexec` io_uring backend while avoiding stdexec-specific
dependencies.

## Scope & Notes

- Map the generic event loop facade operations (`open_socket`, `connect_socket`,
  `accept_once`, `close_socket`, read/write primitives) onto raw liburing APIs.
- Ensure the scheduler exposed by the loop integrates with the rest of the
  library (`stdexec` senders, cancellation, stop tokens).
- Define the concrete socket/file/timer handle state objects reused by the
  facade.
- Untangle resource ownership rules: clarify whether handles borrow or own
  descriptors and when cleanup happens.

Implementation to be added once the facade stabilises.
