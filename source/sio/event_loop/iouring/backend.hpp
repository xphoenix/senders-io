#pragma once

#include "context.hpp"
#include "operations.hpp"
#include "run_sender.hpp"
#include "scheduler.hpp"

#include "../../sequence/buffered_sequence.hpp"
#include "../../sequence/reduce.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <system_error>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::iouring {
  class backend {
   public:
    using native_context_type = io_context;
    using env = iouring::env;
    using close_sender = iouring::close_sender;

    using fd_state = iouring::fd_state;
    using file_state = iouring::file_state;
    using seekable_file_state = iouring::seekable_file_state;

    template <class Protocol>
    using socket_state = iouring::socket_state<Protocol>;

    template <class Protocol>
    using acceptor_state = iouring::acceptor_state<Protocol>;

    backend() = default;

    explicit backend(unsigned int queue_depth)
      : context_{queue_depth} {
    }

    scheduler get_scheduler() noexcept {
      return context_.get_scheduler();
    }

    native_context_type& native_context() noexcept {
      return context_;
    }

    const native_context_type& native_context() const noexcept {
      return context_;
    }

    auto run(run_mode mode = run_mode::stopped) {
      return context_.run(mode);
    }

    void run_until_empty() {
      context_.run_until_empty();
    }

    void request_stop() {
      context_.request_stop();
    }

    auto close(fd_state& state) noexcept {
      return close_sender{&state.context(), state.native_handle()};
    }

    auto read_some(fd_state& state, sio::mutable_buffer_span buffers) {
      return read_sender{state.context(), buffers, state.native_handle()};
    }

    auto read_some(fd_state& state, sio::mutable_buffer buffer) {
      return read_sender_single{state.context(), buffer, state.native_handle()};
    }

    auto write_some(fd_state& state, sio::const_buffer_span buffers) {
      return write_sender{state.context(), buffers, state.native_handle()};
    }

    auto write_some(fd_state& state, sio::const_buffer buffer) {
      return write_sender_single{state.context(), buffer, state.native_handle()};
    }

    auto read(fd_state& state, sio::mutable_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(read_factory{&state.context(), state.native_handle()}, buffers),
        0ull);
    }

    auto read(fd_state& state, sio::mutable_buffer buffer) {
      auto buffered = ::sio::buffered_sequence(
        read_factory{&state.context(), state.native_handle()}, buffer);
      return ::sio::reduce(std::move(buffered), 0ull);
    }

    auto write(fd_state& state, sio::const_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(write_factory{&state.context(), state.native_handle()}, buffers),
        0ull);
    }

    auto write(fd_state& state, sio::const_buffer buffer) {
      return ::sio::reduce(
        ::sio::buffered_sequence(write_factory{&state.context(), state.native_handle()}, buffer),
        0ull);
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
      return read_sender{state.context(), buffers, state.native_handle(), offset};
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
      return read_sender_single{state.context(), buffer, state.native_handle(), offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
      return write_sender{state.context(), buffers, state.native_handle(), offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
      return write_sender_single{state.context(), buffer, state.native_handle(), offset};
    }

    auto read(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(
          read_factory{&state.context(), state.native_handle()}, buffers, offset),
        0ull);
    }

    auto read(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
      auto buffered = ::sio::buffered_sequence(
        read_factory{&state.context(), state.native_handle()}, buffer, offset);
      return ::sio::reduce(std::move(buffered), 0ull);
    }

    auto write(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(
          write_factory{&state.context(), state.native_handle()}, buffers, offset),
        0ull);
    }

    auto write(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(
          write_factory{&state.context(), state.native_handle()}, buffer, offset),
        0ull);
    }

    auto open_file(
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) {
      open_data data{
        static_cast<std::filesystem::path&&>(path),
        dirfd,
        to_open_flags(mode, creation),
        to_mode(mode)};
      return file_open_sender<file_state>{
        context_, static_cast<open_data&&>(data), mode, creation, caching};
    }

    auto open_seekable_file(
      std::filesystem::path path,
      async::mode mode = async::mode::read,
      async::creation creation = async::creation::open_existing,
      async::caching caching = async::caching::unchanged,
      int dirfd = AT_FDCWD) {
      open_data data{
        static_cast<std::filesystem::path&&>(path),
        dirfd,
        to_open_flags(mode, creation),
        to_mode(mode)};
      return file_open_sender<seekable_file_state>{
        context_, static_cast<open_data&&>(data), mode, creation, caching};
    }

    template <class Protocol>
    auto open_socket(Protocol protocol) {
      return open_sender<Protocol>{&context_, protocol};
    }

    template <class Protocol>
    auto close_socket(socket_state<Protocol>& state) noexcept {
      return close(static_cast<fd_state&>(state));
    }

    template <class Protocol>
    auto connect_socket(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      return connect_sender<Protocol>{&state, endpoint};
    }

    template <class Protocol>
    void bind(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      state.bind(endpoint);
    }

    template <class Protocol>
    auto read_some(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::buffers_type buffers) {
      return read_some(static_cast<fd_state&>(state), buffers);
    }

    template <class Protocol>
    auto read_some(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::buffer_type buffer) {
      return read_some(static_cast<fd_state&>(state), buffer);
    }

    template <class Protocol>
    auto write_some(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::const_buffers_type buffers) {
      return write_some(static_cast<fd_state&>(state), buffers);
    }

    template <class Protocol>
    auto write_some(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::const_buffer_type buffer) {
      return write_some(static_cast<fd_state&>(state), buffer);
    }

    template <class Protocol>
    auto write(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::const_buffers_type buffers) {
      return write(static_cast<fd_state&>(state), buffers);
    }

    template <class Protocol>
    auto write(
      socket_state<Protocol>& state,
      typename socket_state<Protocol>::const_buffer_type buffer) {
      return write(static_cast<fd_state&>(state), buffer);
    }

    template <class Protocol>
    auto open_acceptor(Protocol protocol, typename Protocol::endpoint endpoint) {
      return ::stdexec::then(
        open_socket(protocol), [this, endpoint](socket_state<Protocol> state) mutable {
          Protocol proto = state.consume_protocol();
          int one = 1;
          if (
            ::setsockopt(
              state.native_handle(),
              SOL_SOCKET,
              SO_REUSEADDR,
              &one,
              static_cast<socklen_t>(sizeof(one)))
            == -1) {
            throw std::system_error(errno, std::system_category());
          }
          state.bind(endpoint);
          if (::listen(state.native_handle(), 16) == -1) {
            throw std::system_error(errno, std::system_category());
          }
          return acceptor_state<Protocol>{
            context_, state.native_handle(), std::move(proto), std::move(endpoint)};
        });
    }

    template <class Protocol>
    auto accept_once(acceptor_state<Protocol>& state) {
      return accept_sender<Protocol>{&state};
    }

    template <class Protocol>
    auto close_acceptor(acceptor_state<Protocol>& state) noexcept {
      return close(static_cast<fd_state&>(state));
    }

   private:
    // TODO(andrphi): Move into utils.h
    static int to_open_flags(async::mode mode, async::creation creation) noexcept {
      int flags = O_CLOEXEC;
      switch (mode) {
      case async::mode::write:
      case async::mode::attr_write:
        flags |= O_WRONLY;
        break;
      case async::mode::append:
        flags |= O_WRONLY | O_APPEND;
        break;
      default:
        flags |= O_RDONLY;
        break;
      }

      switch (creation) {
      case async::creation::if_needed:
        flags |= O_CREAT;
        break;
      case async::creation::always_new:
        flags |= O_CREAT | O_EXCL;
        break;
      case async::creation::truncate_existing:
        flags |= O_TRUNC;
        break;
      default:
        break;
      }

      if (mode == async::mode::write || mode == async::mode::attr_write) {
        flags |= O_CREAT;
      }

      return flags;
    }

    // TODO(andrphi): Move into utils.h
    static ::mode_t to_mode(async::mode mode) noexcept {
      switch (mode) {
      case async::mode::write:
      case async::mode::attr_write:
      case async::mode::append:
        return static_cast<::mode_t>(0644);
      default:
        return static_cast<::mode_t>(0);
      }
    }

    native_context_type context_{};
  };
} // namespace sio::event_loop::iouring
