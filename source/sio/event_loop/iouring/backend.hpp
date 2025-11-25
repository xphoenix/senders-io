#pragma once

#include "context.hpp"
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
#include "../concepts.hpp"

#include "../../sequence/buffered_sequence.hpp"
#include "../../sequence/reduce.hpp"
#include "../../local/socket_options.hpp"
#include "../../local/stream_protocol.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <system_error>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::iouring {
  class backend {
   public:
    using native_context_type = io_context;
    using native_handle_type = int;
    using env = iouring::env;
    using file_state = basic_fd;
    using seekable_file_state = basic_fd;

    using fd_close_sender = iouring::fd_close_sender;
    using fd_read_sender_span = iouring::fd_read_sender_span;
    using fd_read_sender_single = iouring::fd_read_sender_single;
    using fd_read_factory = iouring::fd_read_factory;
    using fd_write_sender_span = iouring::fd_write_sender_span;
    using fd_write_sender_single = iouring::fd_write_sender_single;
    using fd_write_factory = iouring::fd_write_factory;

    template <class Protocol>
    using socket_state = socket_fd<Protocol>;

    template <class Protocol>
    using acceptor_state = socket_fd<Protocol>;

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

    template <class Protocol>
      requires std::is_same_v<std::remove_cvref_t<Protocol>, ::sio::local::stream_protocol>
    auto close(socket_state<Protocol>& state) noexcept {
      auto path = std::move(state.unix_path);
      const bool should_unlink = state.unlink_on_close && !path.empty();
      state.unlink_on_close = false;
      return fd_close_sender{&context_, state.native_handle()}
           | ::stdexec::then([should_unlink, path = std::move(path)]() mutable {
               if (should_unlink) {
                 ::unlink(path.c_str());
               }
             });
    }

    auto close(basic_fd& state) noexcept {
      return fd_close_sender{&context_, state.native_handle()};
    }

    auto read_some(basic_fd& state, sio::mutable_buffer_span buffers) {
      return fd_read_sender_span{context_, buffers, state.native_handle()};
    }

    auto read_some(basic_fd& state, sio::mutable_buffer buffer) {
      return fd_read_sender_single{context_, buffer, state.native_handle()};
    }

    auto write_some(basic_fd& state, sio::const_buffer_span buffers) {
      return fd_write_sender_span{context_, buffers, state.native_handle()};
    }

    auto write_some(basic_fd& state, sio::const_buffer buffer) {
      return fd_write_sender_single{context_, buffer, state.native_handle()};
    }

    auto read(basic_fd& state, sio::mutable_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffers), 0ull);
    }

    auto read(basic_fd& state, sio::mutable_buffer buffer) {
      auto buffered = ::sio::buffered_sequence(fd_read_factory{&context_, state}, buffer);
      return ::sio::reduce(std::move(buffered), 0ull);
    }

    auto write(basic_fd& state, sio::const_buffer_span buffers) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffers), 0ull);
    }

    auto write(basic_fd& state, sio::const_buffer buffer) {
      return ::sio::reduce(
        ::sio::buffered_sequence(fd_write_factory{&context_, state}, buffer), 0ull);
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
      return fd_read_sender_span{context_, buffers, state.native_handle(), offset};
    }

    auto read_some(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
      return fd_read_sender_single{context_, buffer, state.native_handle(), offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
      return fd_write_sender_span{context_, buffers, state.native_handle(), offset};
    }

    auto write_some(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
      return fd_write_sender_single{context_, buffer, state.native_handle(), offset};
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
      return file_open_sender<file_state>{&context_, static_cast<open_data&&>(data)};
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
      return file_open_sender<seekable_file_state>{&context_, static_cast<open_data&&>(data)};
    }

    template <class Protocol>
    auto open_socket(Protocol protocol) {
      return socket_open_sender<Protocol>{&context_, protocol};
    }

    template <class Protocol>
    auto open_socket(Protocol protocol, ::sio::local::socket_options options) {
      if constexpr (std::is_same_v<std::remove_cvref_t<Protocol>, ::sio::local::stream_protocol>) {
        return ::stdexec::then(open_socket(protocol), [options](socket_state<Protocol> state) {
          state.unlink_on_close = options.unlink_on_close;
          return state;
        });
      } else {
        (void)options;
        return open_socket(protocol);
      }
    }

    template <class Protocol>
    auto open_acceptor(Protocol protocol, typename Protocol::endpoint endpoint) {
      return ::stdexec::then(
        open_socket(protocol),
        [this, endpoint = std::move(endpoint), protocol](socket_state<Protocol> state) mutable {
          int fd = state.native_handle();
          if (protocol.family() == AF_INET || protocol.family() == AF_INET6) {
            int one = 1;
            if (
              ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, static_cast<socklen_t>(sizeof(one)))
              == -1) {
              int err = errno;
              ::close(fd);
              throw std::system_error(err, std::system_category());
            }
          }
          auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
          if (::bind(fd, addr, endpoint.size()) == -1) {
            int err = errno;
            ::close(fd);
            throw std::system_error(err, std::system_category());
          }
          if (::listen(fd, 16) == -1) {
            int err = errno;
            ::close(fd);
            throw std::system_error(err, std::system_category());
          }
          state.unlink_on_close = false;
          return acceptor_state<Protocol>{fd};
        });
    }

    template <class Protocol>
    auto open_acceptor(
      Protocol protocol,
      typename Protocol::endpoint endpoint,
      ::sio::local::socket_options options) {
      if constexpr (std::is_same_v<std::remove_cvref_t<Protocol>, ::sio::local::stream_protocol>) {
        return ::stdexec::then(
          open_socket(protocol, options),
          [endpoint = std::move(endpoint), options, protocol](socket_state<Protocol> state) mutable {
            int fd = state.native_handle();
            if (protocol.family() == AF_INET || protocol.family() == AF_INET6) {
              int one = 1;
              if (
                ::setsockopt(
                  fd, SOL_SOCKET, SO_REUSEADDR, &one, static_cast<socklen_t>(sizeof(one)))
                == -1) {
                int err = errno;
                ::close(fd);
                throw std::system_error(err, std::system_category());
              }
            }
            auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
            if (::bind(fd, addr, endpoint.size()) == -1) {
              int err = errno;
              ::close(fd);
              throw std::system_error(err, std::system_category());
            }
            if (::listen(fd, 16) == -1) {
              int err = errno;
              ::close(fd);
              throw std::system_error(err, std::system_category());
            }
            acceptor_state<Protocol> acceptor{fd};
            if (options.unlink_on_close && endpoint.is_filesystem()) {
              acceptor.unlink_on_close = true;
              acceptor.unix_path = std::string(endpoint.path());
            }
            return acceptor;
          });
      } else {
        (void)options;
        return open_acceptor(std::move(protocol), std::move(endpoint));
      }
    }

    template <class Protocol>
    auto connect(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      return socket_connect_sender<Protocol>{&context_, state.native_handle(), endpoint};
    }

    template <class Protocol>
    void bind(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
      auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
      if (::bind(state.native_handle(), addr, endpoint.size()) == -1) {
        throw std::system_error(errno, std::system_category());
      }
      if constexpr (std::is_same_v<std::remove_cvref_t<Protocol>, ::sio::local::stream_protocol>) {
        if (state.unlink_on_close && endpoint.is_filesystem()) {
          state.unix_path = std::string(endpoint.path());
        } else {
          state.unix_path.clear();
        }
      }
    }

    template <class Protocol>
    auto accept_once(acceptor_state<Protocol>& state) {
      return socket_accept_sender<Protocol>{&context_, state.native_handle()};
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
