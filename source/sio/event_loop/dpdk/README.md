# DPDK TCP/IP Backend (design)

The aim for this backend is to explore a custom TCP/IP stack built on
top of [DPDK](https://www.dpdk.org/). For now we only outline the areas that
need research:

- Integrating the DPDK run loop with the generic event loop scheduler.
- Representing sockets/files in terms of queue pairs or mbuf pools.
- Translating facade operations (`open_socket`, `accept_once`, read/write
  senders) to zero-copy, poll-mode flows.
- Managing lifecycle for hardware queues and ensuring clean shutdown.

More details will follow when the abstraction is validated.
