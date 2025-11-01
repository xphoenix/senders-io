#pragma once

#include "sio/event_loop/stdexec/backend.hpp"

#if SIO_TEST_HAS_IOURING
#include "sio/event_loop/iouring/run_sender.hpp"
#include "sio/event_loop/iouring/backend.hpp"
#endif

#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <string_view>
#include <utility>

namespace sio::test {

  template <class Backend>
  concept enabled_backend = Backend::available;

#if SIO_TEST_HAS_STDEXEC
  struct stdexec_backend {
    static constexpr std::string_view name = "stdexec";
    static constexpr bool available = true;

    using loop_type = sio::event_loop::stdexec_backend::backend;
    using native_context_type = typename loop_type::native_context_type;
    using run_mode = exec::until;
    using read_factory = sio::event_loop::stdexec_backend::read_factory;
    using write_factory = sio::event_loop::stdexec_backend::write_factory;

    static loop_type make_loop() {
      return loop_type{};
    }

    template <stdexec::sender Sender>
    static void sync_wait(loop_type& loop, Sender&& sender) {
      auto guard = exec::when_any(std::forward<Sender>(sender), loop.run(exec::until::stopped));
      stdexec::sync_wait(std::move(guard));
    }
  };
#else
  struct stdexec_backend {
    static constexpr std::string_view name = "stdexec";
    static constexpr bool available = false;
  };
#endif

#if SIO_TEST_HAS_IOURING
  struct iouring_backend {
    static constexpr std::string_view name = "iouring";
    static constexpr bool available = true;

    using loop_type = sio::event_loop::iouring::backend;
    using native_context_type = typename loop_type::native_context_type;
    using run_mode = sio::event_loop::iouring::run_mode;
    using read_factory = sio::event_loop::iouring::read_factory;
    using write_factory = sio::event_loop::iouring::write_factory;

    static loop_type make_loop() {
      return loop_type{};
    }

    template <stdexec::sender Sender>
    static void sync_wait(loop_type& loop, Sender&& sender) {
      auto guard = exec::when_any(
        std::forward<Sender>(sender), loop.run(sio::event_loop::iouring::run_mode::stopped));
      stdexec::sync_wait(std::move(guard));
    }
  };
#else
  struct iouring_backend {
    static constexpr std::string_view name = "iouring";
    static constexpr bool available = false;
  };
#endif

  template <class... Backends>
  struct backend_list {
    template <class Fn>
    static void for_each(Fn&& fn) {
      (fn.template operator()<Backends>(), ...);
    }
  };

  using active_backends = backend_list<stdexec_backend, iouring_backend>;

  template <class Fn>
  void for_each_backend(Fn&& fn) {
    active_backends::for_each(std::forward<Fn>(fn));
  }

} // namespace sio::test

#if SIO_TEST_HAS_IOURING && !SIO_TEST_EXCLUDE_IOURING
#define SIO_TEST_BACKEND_TYPES ::sio::test::stdexec_backend, ::sio::test::iouring_backend
#else
#define SIO_TEST_BACKEND_TYPES ::sio::test::stdexec_backend
#endif
