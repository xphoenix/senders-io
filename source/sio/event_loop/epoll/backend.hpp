#pragma once

#include "context.hpp"
#include "details.hpp"
#include "fd_close.hpp"
#include "fd_read.hpp"
#include "fd_write.hpp"
#include "file_open.hpp"
#include "run_sender.hpp"
#include "scheduler.hpp"
#include "socket_accept.hpp"
#include "socket_connect.hpp"
#include "socket_open.hpp"
#include "socket_sendmsg.hpp"

#include "../../sequence/buffered_sequence.hpp"
#include "../../sequence/reduce.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/socket.h>

namespace sio::event_loop::epoll {
  class backend {
   public:
    using native_context_type = context;
    using native_handle_type = int;
    using env = epoll::env;
    using file_state = epoll::file_state;
    using seekable_file_state = epoll::seekable_file_state;

    template <class Protocol>
    using socket_state = epoll::socket_state<Protocol>;

    template <class Protocol>
    using acceptor_state = epoll::acceptor_state<Protocol>;

    backend() = default;

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

    template <class State>
    auto close(State& state) noexcept {
      return fd_close_sender{&context_, state};
    }

    auto read_some(file_state& state, sio::mutable_buffer_span buffers) {
      return fd_read_sender_span{context_, state, buffers};
    }

    auto read_some(file_state& state, sio::mutable_buffer buffer) {
      return fd_read_sender_single{context_, state, buffer};
    }

    auto write_some(file_state& state, sio::const_buffer_span buffers) {
      return fd_write_sender_span{context_, state, buffers};
    }

    auto write_some(file_state& state, sio::const_buffer buffer) {
      return fd_write_sender_single{context_, state, buffer};
    }

    auto read(file_state& state, sio::mutable_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffers),
        0ull);
    }

    auto read(file_state& state, sio::mutable_buffer buffer) {
      auto buffered = ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffer);
      return ::sio::reduce(std::move(buffered), 0ull);
    }

    auto write(file_state& state, sio::const_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffers),
        0ull);
    }

    auto write(file_state& state, sio::const_buffer buffer) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffer),
        0ull);
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
      return fd_read_sender_span{context_, state, buffers, offset};
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
      return fd_read_sender_single{context_, state, buffer, offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
      return fd_write_sender_span{context_, state, buffers, offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
      return fd_write_sender_single{context_, state, buffer, offset};
    }

    auto read(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffers, offset),
        0ull);
    }

    auto read(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
      auto buffered = ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffer, offset);
      return ::sio::reduce(std::move(buffered), 0ull);
    }

    auto write(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffers, offset),
        0ull);
    }

    auto write(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffer, offset),
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
      return file_open_sender<file_state>{&context_, static_cast<open_data&&>(data), mode, creation, caching};
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
        &context_, static_cast<open_data&&>(data), mode, creation, caching};
    }

    template <class Protocol>
    auto open_socket(Protocol protocol) {
      return socket_open_sender<Protocol>{&context_, protocol};
    }

    template <class Protocol>
    auto open_acceptor(Protocol protocol, typename Protocol::endpoint endpoint) {
      return ::stdexec::then(
        open_socket(protocol),
        [this, endpoint = std::move(endpoint)](socket_state<Protocol> state) mutable {
          auto cleanup_on_error = [this, state](int err) {
            context_.release_entry(state);
            throw std::system_error(err, std::system_category());
          };

          int fd = context_.native_handle(state);
          int one = 1;
          if (
            ::setsockopt(
              fd,
              SOL_SOCKET,
              SO_REUSEADDR,
              &one,
              static_cast<socklen_t>(sizeof(one)))
            == -1) {
            cleanup_on_error(errno);
          }
          auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
          if (::bind(fd, addr, endpoint.size()) == -1) {
            cleanup_on_error(errno);
          }
          if (::listen(fd, 16) == -1) {
            cleanup_on_error(errno);
          }
          return acceptor_state<Protocol>{state};
        });
    }

    template <class Protocol>
    auto connect(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      return socket_connect_sender<Protocol>{&context_, state, endpoint};
    }

    template <class Protocol>
    void bind(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      int fd = context_.native_handle(state);
      auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
      if (::bind(fd, addr, endpoint.size()) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }

    template <class Protocol>
    auto accept_once(acceptor_state<Protocol>& state) {
      return socket_accept_sender<Protocol>{&context_, state};
    }

    template <class Protocol>
    auto sendmsg(socket_state<Protocol>& state, ::msghdr msg) {
      return socket_sendmsg_sender<Protocol>{&context_, state, msg};
    }

    template <class Protocol>
    auto read_some(socket_state<Protocol>& state, sio::mutable_buffer_span buffers) {
      return fd_read_sender_span{context_, state, buffers};
    }

    template <class Protocol>
    auto read_some(socket_state<Protocol>& state, sio::mutable_buffer buffer) {
      return fd_read_sender_single{context_, state, buffer};
    }

    template <class Protocol>
    auto write_some(socket_state<Protocol>& state, sio::const_buffer_span buffers) {
      return fd_write_sender_span{context_, state, buffers};
    }

    template <class Protocol>
    auto write_some(socket_state<Protocol>& state, sio::const_buffer buffer) {
      return fd_write_sender_single{context_, state, buffer};
    }

    template <class Protocol>
    auto read(socket_state<Protocol>& state, sio::mutable_buffer_span buffers) {
      return read(static_cast<file_state&>(state), buffers);
    }

    template <class Protocol>
    auto read(socket_state<Protocol>& state, sio::mutable_buffer buffer) {
      return read(static_cast<file_state&>(state), buffer);
    }

    template <class Protocol>
    auto write(socket_state<Protocol>& state, sio::const_buffer_span buffers) {
      return write(static_cast<file_state&>(state), buffers);
    }

    template <class Protocol>
    auto write(socket_state<Protocol>& state, sio::const_buffer buffer) {
      return write(static_cast<file_state&>(state), buffer);
    }

   private:
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
} // namespace sio::event_loop::epoll
