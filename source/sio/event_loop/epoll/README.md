# Epoll Backend (design)

This directory will describe how to adapt Linux `epoll` to the generic event
loop facade.

## Open Questions

- How to surface a scheduler compatible with `stdexec` abstractions while using
  `epoll_wait`/`eventfd`.
- What state needs to be stored in the socket/file handles (e.g. non-blocking
  fd wrappers, buffer management).
- Which operations we can realistically support (accept/connect/read/write,
  timers, custom notifications).

Implementation notes and detailed design will be added once the facade is
complete.
