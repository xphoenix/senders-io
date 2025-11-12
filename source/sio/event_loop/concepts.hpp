#pragma once

#include <stdexec/execution.hpp>

#include "../const_buffer.hpp"
#include "../const_buffer_span.hpp"
#include "../mutable_buffer.hpp"
#include "../mutable_buffer_span.hpp"

#include <concepts>
#include <sys/types.h>
#include <type_traits>
#include <utility>

namespace sio::event_loop {
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
        loop.read_some(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::buffers_type>())
      };
      {
        loop.read_some(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::buffer_type>())
      };
      {
        loop.write_some(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::const_buffers_type>())
      };
      {
        loop.write_some(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::const_buffer_type>())
      };
      {
        loop.write(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::const_buffers_type>())
      };
      {
        loop.write(
          socket_state,
          std::declval<typename socket_state_t<Loop, Protocol>::const_buffer_type>())
      };
      {
        socket_state.protocol()
      } -> std::same_as<const Protocol&>;
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
} // namespace sio::event_loop
