#pragma once

#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

#if SIO_TEST_HAS_STDEXEC
#include "sio/event_loop/stdexec/backend.hpp"
#endif

#if SIO_TEST_HAS_IOURING
#include "sio/event_loop/iouring/backend.hpp"
#endif

#define SIO_TEST_BACKEND_TYPES ::sio::test::available_backend_tuple

namespace sio::test {

  template <class... Ts>
  struct TypeList { };

  namespace detail {

    template <class T, class List>
    struct prepend;

    template <class T, class... Ts>
    struct prepend<T, TypeList<Ts...>> {
      using type = TypeList<T, Ts...>;
    };

    template <class List>
    struct filter_void;

    template <>
    struct filter_void<TypeList<>> {
      using type = TypeList<>;
    };

    template <class Head, class... Tail>
    struct filter_void<TypeList<Head, Tail...>> {
      using rest = typename filter_void<TypeList<Tail...>>::type;
      using type =
        std::conditional_t<std::is_void_v<Head>, rest, typename prepend<Head, rest>::type>;
    };

    template <class List>
    struct to_tuple;

    template <class... Ts>
    struct to_tuple<TypeList<Ts...>> {
      using type = std::tuple<Ts...>;
    };

    template <class Tuple, class Fn, std::size_t... Indices>
    void for_each_backend_impl(Fn&& fn, std::index_sequence<Indices...>) {
      (fn.template operator()<std::tuple_element_t<Indices, Tuple>>(), ...);
    }

  } // namespace detail

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
    using loop_type = void;
  };
#endif

#if SIO_TEST_HAS_IOURING
  struct iouring_backend {
    static constexpr std::string_view name = "iouring";
    static constexpr bool available = true;

    using loop_type = sio::event_loop::iouring::backend;
    using native_context_type = typename loop_type::native_context_type;
    using run_mode = sio::event_loop::iouring::run_mode;
    using read_factory = sio::event_loop::iouring::fd_read_factory;
    using write_factory = sio::event_loop::iouring::fd_write_factory;

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
    using loop_type = void;
  };
#endif

  using available_backends = TypeList<
    std::conditional_t<stdexec_backend::available, stdexec_backend, void>,
    std::conditional_t<iouring_backend::available, iouring_backend, void>>;

  using enabled_backends = typename detail::filter_void<available_backends>::type;
  using available_backend_tuple = typename detail::to_tuple<enabled_backends>::type;

  template <class Fn>
  void for_each_backend(Fn&& fn) {
    detail::for_each_backend_impl<available_backend_tuple>(
      std::forward<Fn>(fn), std::make_index_sequence<std::tuple_size_v<available_backend_tuple>>{});
  }

} // namespace sio::test
