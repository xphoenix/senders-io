# Epoll Backend (design)

This document captures the intended design for an `epoll`-based backend that
plugs into the generic `sio::event_loop` facade. The goal is to provide a
drop-in alternative to the existing io_uring backend while reusing the same
handle abstractions, sender surface area, and scheduler concepts. The design
leans on Linux `epoll_pwait2` so we can coordinate timers and descriptor events
with nanosecond resolution without introducing additional kernel objects.

## Goals & constraints

- Provide `sio::event_loop::epoll::backend` with the same surface as the
  io_uring backend (`backend::run`, `backend::get_scheduler()`, `read_some`,
  `socket_accept`, timers, etc.).
- Drive descriptors in edge-triggered mode to minimize redundant syscalls; the
  backend is intended for a single-threaded loop so we can promptly re-drain
  readiness without risking starvation.
- Assume `epoll_pwait2` is available; no need for runtime fallback paths.
- Use `epoll_pwait2` as the only blocking syscall, feeding it the relative
  timeout computed from the next timer deadline.
- Remain single-threaded inside the event loop. Concurrent submission is
  expressed through `senders`, but actual wakeups happen on the loop thread.
- Do not introduce extra threads; the backend lives entirely on the user-owned
  loop thread, even for blocking setup tasks.
- Integrate with `stdexec` schedulers (work scheduling is `exec::run_loop`
  inspired, cancellation relies on `stop_token` propagation).
- Avoid dynamic allocations whenever possible by relying on intrusive lists/heaps
  wired through non-movable operation_state objects.
- Do not expose multi-shot operations yet; parity with other backends matters
  more than the slightly lower syscall count. We'll revisit this when the API
  evolves across all backends.

## Architecture overview

### `epoll_context`

- Owns:
  - `int epoll_fd;`
  - `int wake_fd;` implemented with `eventfd` for cross-thread wakeups.
  - `timer_heap` that keeps `(deadline, completion*)` pairs in monotonic time
    using an intrusive binary heap (e.g. pairing heap with parent pointers).
  - `intrusive_queue` for ready completions that should run outside of kernel
    dispatch.
- Public API closely mirrors `iouring::io_context`:
  - `scheduler get_scheduler() noexcept;`
  - `run_sender run(run_mode mode = run_mode::stopped);`
  - `std::size_t run_one();`, `run_some();`, `run_until_empty();`
  - `void request_stop();` (writes to `wake_fd` to interrupt `epoll_pwait2`)
- `epoll_pwait2` call pattern:
  1. Determine monotonic now (`clock_gettime(CLOCK_MONOTONIC, ...)`).
  2. Compute earliest deadline from timer heap (if any).
  3. Convert `deadline - now` to `timespec` passed to `epoll_pwait2`.
  4. Flush queued completions before blocking so that already-ready operations
     run even if no kernel events are pending. `run_some()` uses the same drive
     loop but guarantees at most one blocking wait, making it suitable for
     polling scenarios that interleave other work.
- Each registered descriptor uses `epoll_data.ptr` to point to an owning
  `fd_state` so the dispatcher can find the relevant operation queues.

### Scheduler and run sender

- Scheduler model matches `stdexec` expectations: a lightweight token that
  references the context and enqueues lightweight operations onto the loop.
- We can reuse the pattern from `iouring::run_sender`: `schedule()` returns a
  sender whose `operation_state` pushes itself into the context's ready queue
  guarded by a mutex. The loop drains this queue before/after every
  `epoll_pwait2` call.
- `run(run_mode)` returns a sender that blocks the calling thread while
  repeatedly calling `drive(block)`:
  - `run_mode::stopped` drains until `stop_requested`.
  - `run_mode::drained` keeps driving until `stop` _and_ all fds/timers have
    completed.

### Descriptor states & operations

- Shared state layout is compatible with `sio::event_loop::fd_state`:
  - `int native;`
  - bitmask of registered interests (`EPOLLIN`, `EPOLLOUT`, `EPOLLRDHUP`) plus a
    flag indicating the descriptor is currently armed for edge-triggered wakeups.
  - intrusive linked lists of pending read/write operations; each op embeds its
    list node to avoid heap allocations.
- Operation flow:
  1. Sender starts and immediately tries the syscall (e.g. `::read`, `::accept`)
     with `O_NONBLOCK` file descriptors.
  2. If syscall completes, signal `set_value`.
  3. On `EAGAIN`, append operation to the proper queue and ensure the interest
     bit is enabled in the epoll registration.
  4. When `epoll` notifies (edge-triggered), mark the descriptor as unarmed and
     move the queue into the ready list; keep re-driving syscalls until they
     block again, then arm the descriptor once more with `EPOLL_CTL_MOD`.
  5. Cancellation simply removes the node from the pending list and, if the list
     becomes empty, clears the corresponding interest bit with `EPOLL_CTL_MOD`.
- Accept/connect semantics mirror the io_uring backend: `socket_accept` produces
  `acceptor_state` referencing `fd_state`, then `accept_sender` drives `::accept4`
  with `SOCK_NONBLOCK`. Connect operations register for `EPOLLOUT` and rely on
  `getsockopt(SO_ERROR)` once ready.

### Timers

- Timer senders insert into the shared `timer_heap` with a unique key. When a
  timer expires the loop enqueues its completion into the ready queue.
- `epoll_pwait2` timeout equals time until the earliest timer. If no timers,
  pass `nullptr` to wait indefinitely.
- Cancellation removes entries from the heap lazily by writing a tombstone flag;
  the dispatcher skips cancelled timers when they reach the top of the heap.
- When the earliest deadline changes (new earlier timer scheduled) we wake the
  loop thread via `eventfd` so it recomputes the timeout.

### Wakeups & stop handling

- `wake_fd` is registered with `EPOLLIN | EPOLLET`. Writing `uint64_t 1`
  interrupts the wait and drain the readiness queue.
- `request_stop()` sets `stop_requested_ = true` and wakes the loop; once the
  loop observes `stop_requested_` it stops scheduling new work and drains any
  existing completions depending on the requested run mode.

### Thread safety & lifetimes

- Context methods that can be called concurrently (`schedule`, fd operations)
  synchronize through:
  - `std::mutex` for the ready queue.
  - `std::atomic` reference counts on operation states to guard lifetimes.
- Each handle (`file_state`, `socket_state`, etc.) owns its fd via RAII; closing
  a handle automatically issues `epoll_ctl(DEL)` and cancels queued operations.

### Error handling strategy

- All syscalls throw `std::system_error` if they fail unexpectedly. Expected
  transient errors (EINTR, EAGAIN) are handled inline.
- Each sender completes with `set_error(std::error_code)` mirroring the io_uring
  backend contract.
- If `epoll_pwait2` returns `EINTR` we simply retry while recomputing the timeout.

## Implementation plan

1. **Context skeleton**
   - Create `epoll/context.hpp` with RAII wrappers for `epoll_fd`/`wake_fd`,
     timer heap scaffolding (intrusive heap), ready queue, and `run`/`drive`
     loops around `epoll_pwait2`.
2. **Scheduler + run sender**
   - Port the `iouring::scheduler` + `run_sender` pattern, adjusted so the ready
     queue posts lambdas that the loop calls after returning from epoll. Make
     sure `run_some()` is exposed so polling scenarios can drive a bounded
     amount of work without blocking.
3. **Descriptor state layer**
   - Implement `fd_state` base, registration helpers (`ctl_add/ctl_mod`), and
     interest bookkeeping to add/remove `EPOLLIN/EPOLLOUT` lazily.
4. **Core operations**
   - Build sender families for `read_some`, `write_some`, `socket_accept`,
     `socket_connect`, `socket_sendmsg`, `file_open` (direct `::openat2` /
     `::open` invocation kept non-blocking) with the retry/EAGAIN logic.
5. **Timer sender**
   - Implement heap-backed timers, integrate with `run` timeout calculation, and
     expose via `backend::schedule_at/schedule_after` helpers if needed.
6. **Stop & cancellation wiring**
   - Ensure `stop_token` cancellation removes queued operations, wakes the loop,
     and completes ops with `set_stopped`.
7. **Feature parity tests**
   - Add the new backend into the existing `backend_matrix` so the current test
     suite automatically runs against both io_uring and epoll. Duplicate
     examples where needed to exercise TCP accept, stdin echo, read/write paths.

Open questions left for implementation:

- Whether we introduce a background thread pool for operations that aren't
  readiness-based (e.g. file open with path resolution).
- Whether we expose multi-shot operations (re-arming read watchers) in addition
  to the existing single-operation senders.
- Strategy for systems that lack `epoll_pwait2` (left for later; current design
  assumes availability as requested).
