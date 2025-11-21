# io_uring Backend (liburing)

This backend will talk directly to [liburing](https://github.com/axboe/liburing)
while exposing the same contract as the existing `stdexec` io_uring backend.
The dependency surface stays minimal and the facade (`backend`, state objects,
senders) remains interchangeable.

## High-Level Goals

- Reuse the current event loop facade exactly so higher layers can swap the
  backend alias without code changes.
- Provide a `stdexec::scheduler`-conforming scheduler implemented on top of a
  bespoke `io_uring` loop.
- Preserve cancellation, stop tokens, and `std::error_code` propagation exactly
  as the stdexec-backed version.
- Keep ownership rules crystal clear: the backend owns the ring, handle states
  borrow descriptors, and cleanup flows through explicit `close` senders.

## Planned Components

| Type | Purpose |
| --- | --- |
| `io_context` | RAII wrapper around `::io_uring`, wake-up fd, submission helpers, and the dispatch loop (`run`, `run_some`, `run_until_empty`). |
| `scheduler` / `schedule_sender` | Minimal scheduler that queues a NOP CQE and satisfies `stdexec::scheduler`. |
| `backend` | User-facing facade mirroring `stdexec_backend::backend`; exposes run/stop, state factories, IO senders. |
| `basic_fd` / `socket_fd` tokens | Trivial descriptor wrappers exchanged with handles. All runtime metadata lives inside the backend. |
| `acceptor_state<Protocol>` | Thin alias of the descriptor token used for listeners; backend stores any wait queues internally. |
| `completion_slot` | Per-operation bookkeeping (SQE prep, receiver pointer, cancellation token management). |
| `stop_callback` | Bridges `stdexec::stop_token` to `io_uring_prep_cancel` and guards against races. |
| `fd_read_sender_span` / `fd_read_sender_single` / `fd_write_sender_span` / `fd_write_sender_single` | Issue `IORING_OP_READ(V)`/`WRITE(V)` with completion dispatch. |
| `socket_connect_sender` / `socket_accept_sender` / `socket_sendmsg_sender` | Socket primitives mapped to the corresponding `io_uring` ops. |
| `fd_close_sender` | Wraps `IORING_OP_CLOSE` (or fallbacks) for deterministic descriptor teardown. |

## Implementation Plan

1. Introduce `io_context` with `run`, `run_some`, `run_until_empty`, `request_stop`,
   and a CQE dispatch loop that invokes stored completion functors.
2. Layer `scheduler` and `schedule_sender` on top of the context; add tests that
   a scheduled task resumes after `run()`.
3. Port the file descriptor primitives (`read_some`, `write_some`, `close`) by
   reusing the stdexec sender interfaces but swapping in the new operation base.
4. Expand to socket primitives (`connect`, `accept`, `sendmsg`) and ensure they
   match the `sio::event_loop::stdexec_backend` surface.
5. Harden cancellation: hook `stop_token` callbacks, issue `io_uring_prep_cancel`,
   and guarantee race-free destruction of receivers.
6. Add integration coverage that parameterises the existing tests over both
   backends to check parity for read/write and socket rendezvous scenarios.

## Design Decisions

- **Cancellation strategy**  
  Each pending operation installs a stop callback that queues `io_uring_prep_cancel`
  against its SQE user data. We fall back to cooperative flags if the kernel
  reports `-EALREADY`.
- **Submission policy**  
  Start simple with one `io_uring_submit()` per operation to guarantee timely
  wake-ups; revisit batching once instrumentation is in place.
- **Socket scope**  
  Initial drop supports single-shot `connect`/`accept`/`sendmsg`. Multi-shot
  variants remain future work once parity tests pass.
- **Error mapping**  
  Always translate negative CQE `res` values into `std::error_code` via
  `std::system_category()` so behaviour aligns with the stdexec backend.
