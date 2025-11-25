#pragma once

#include <stdexec/execution.hpp>

#include "../assert.hpp"
#include "../const_buffer.hpp"
#include "../const_buffer_span.hpp"
#include "../mutable_buffer.hpp"
#include "../mutable_buffer_span.hpp"

#include <concepts>
#include <sys/types.h>
#include <type_traits>
#include <utility>

namespace sio::event_loop {
  struct basic_fd {
    int fd{-1};

    constexpr basic_fd() = default;
    constexpr explicit basic_fd(int value) noexcept
      : fd{value} {
    }

    constexpr bool is_valid() const noexcept {
      return fd >= 0;
    }

    constexpr int native_handle() const noexcept {
      SIO_ASSERT(is_valid());
      return fd;
    }
  };

  template <class Protocol>
  struct socket_fd : basic_fd {
    using basic_fd::basic_fd;
  };

  template <class Loop>
  concept base_loop = requires(Loop& loop) {
    { loop.get_scheduler() } -> stdexec::scheduler;
    { loop.run() };
  };

  template <class Loop>
  concept timer_loop = base_loop<Loop>;

  template <class Loop, class Protocol>
  using socket_state_t = typename std::remove_cvref_t<Loop>::template socket_state<Protocol>;

  template <class Loop, class Protocol>
  using acceptor_state_t = typename std::remove_cvref_t<Loop>::template acceptor_state<Protocol>;

  template <class Loop>
  using file_state_t = typename std::remove_cvref_t<Loop>::file_state;

  template <class Loop>
  using seekable_file_state_t = typename std::remove_cvref_t<Loop>::seekable_file_state;

  template <class Loop, class Protocol>
  concept socket_loop_for =
    base_loop<Loop> &&
    requires(
      Loop& loop,
      socket_state_t<Loop, Protocol>& socket_state,
      acceptor_state_t<Loop, Protocol>& acceptor_state,
      typename Protocol::endpoint endpoint) {
      {
        loop.close(socket_state)
      };
      {
        loop.connect(socket_state, endpoint)
      };
      {
        loop.bind(socket_state, endpoint)
      };
      {
        loop.read_some(socket_state, sio::mutable_buffer_span{})
      };
      {
        loop.read_some(socket_state, sio::mutable_buffer{})
      };
      {
        loop.write_some(socket_state, sio::const_buffer_span{})
      };
      {
        loop.write_some(socket_state, sio::const_buffer{})
      };
      {
        loop.write(socket_state, sio::const_buffer_span{})
      };
      {
        loop.write(socket_state, sio::const_buffer{})
      };
    };

  template <class Loop>
  concept socket_loop = base_loop<Loop>;

  template <class Loop>
  concept file_loop =
    base_loop<Loop> &&
    requires(
      Loop& loop,
      file_state_t<Loop>& state,
      sio::mutable_buffer_span buffers,
      sio::mutable_buffer buffer,
      sio::const_buffer_span const_buffers,
      sio::const_buffer const_buffer) {
      loop.close(state);
      loop.read_some(state, buffers);
      loop.read_some(state, buffer);
      loop.write_some(state, const_buffers);
      loop.write_some(state, const_buffer);
      loop.read(state, buffers);
      loop.read(state, buffer);
      loop.write(state, const_buffers);
      loop.write(state, const_buffer);
    };

  template <class Loop>
  concept seekable_file_loop =
    file_loop<Loop> &&
    requires(
      Loop& loop,
      seekable_file_state_t<Loop>& state,
      sio::mutable_buffer_span buffers,
      sio::mutable_buffer buffer,
      sio::const_buffer_span const_buffers,
      sio::const_buffer const_buffer,
      ::off_t offset) {
      loop.read_some(state, buffers, offset);
      loop.read_some(state, buffer, offset);
      loop.write_some(state, const_buffers, offset);
      loop.write_some(state, const_buffer, offset);
      loop.read(state, buffers, offset);
      loop.read(state, buffer, offset);
      loop.write(state, const_buffers, offset);
      loop.write(state, const_buffer, offset);
    };

  namespace detail {
    template <class Loop, class State>
    auto resolve_native_handler(Loop& loop, const State& state) {
      if constexpr (requires { loop.native_context().native_handle(state.native_handle()); }) {
        return loop.native_context().native_handle(state.native_handle());
      } else {
        return state.native_handle();
      }
    }
  } // namespace detail
} // namespace sio::event_loop

namespace sio {
  namespace get_native_handle_ {
    template <class Tp>
    concept has_member_cpo = requires(Tp&& t) {
      static_cast<Tp&&>(t).native_handle();
    };

    template <class Tp>
    concept nothrow_has_member_cpo = requires(Tp&& t) {
      { static_cast<Tp&&>(t).native_handle() } noexcept;
    };

    template <class Tp>
    concept has_static_member_cpo = requires(Tp&& t) {
      std::decay_t<Tp>::native_handle(static_cast<Tp&&>(t));
    };

    template <class Tp>
    concept nothrow_has_static_member_cpo = requires(Tp&& t) {
      { std::decay_t<Tp>::native_handle(static_cast<Tp&&>(t)) } noexcept;
    };

    template <class Tp>
    concept has_customization = has_member_cpo<Tp> || has_static_member_cpo<Tp>;

    template <class Tp>
    concept nothrow_has_customization =
      nothrow_has_member_cpo<Tp> || nothrow_has_static_member_cpo<Tp>;

    struct get_native_handle_t {
      template <class Tp>
        requires has_customization<Tp>
      auto operator()(Tp&& t) const noexcept(nothrow_has_customization<Tp>) {
        if constexpr (has_member_cpo<Tp>) {
          return static_cast<Tp&&>(t).native_handle();
        } else {
          return std::decay_t<Tp>::native_handle(static_cast<Tp&&>(t));
        }
      }
    };
  } // namespace get_native_handle_

  inline constexpr get_native_handle_::get_native_handle_t get_native_handle{};
} // namespace sio
