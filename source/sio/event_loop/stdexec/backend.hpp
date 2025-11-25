#pragma once

#include "./details.h"
#include "./fd_close.h"
#include "./fd_read.h"
#include "./fd_write.h"
#include "./file_open.h"
#include "./socket_open.h"
#include "./socket_connect.h"
#include "./socket_sendmsg.h"
#include "./socket_accept.h"
#include "../concepts.hpp"

#include <exec/linux/io_uring_context.hpp>
#include <stdexec/execution.hpp>

#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include <filesystem>
#include <system_error>
#include <utility>

namespace sio::event_loop::stdexec_backend {

class backend {
 public:
  using native_context_type = exec::io_uring_context;
  using native_handle_type = int;
  using env = stdexec_backend::env;
  using close_sender = stdexec_backend::close_sender;

  using file_state = basic_fd;
  using seekable_file_state = basic_fd;

  template <class Protocol>
  using socket_state = socket_fd<Protocol>;

  template <class Protocol>
  using acceptor_state = socket_fd<Protocol>;

  backend() = default;

  explicit backend(unsigned int queue_depth)
    : context_{queue_depth} {
  }

  auto get_scheduler() noexcept {
    return context_.get_scheduler();
  }

  native_context_type& native_context() noexcept {
    return context_;
  }

  const native_context_type& native_context() const noexcept {
    return context_;
  }

  auto run(exec::until mode = exec::until::stopped) {
    return context_.run(mode);
  }

  void run_until_empty() {
    context_.run_until_empty();
  }

  void request_stop() {
    context_.request_stop();
  }

  auto close(basic_fd& state) noexcept {
    return close_sender{&context_, state.native_handle()};
  }

  auto read_some(basic_fd& state, sio::mutable_buffer_span buffers) {
    return stdexec_backend::read_sender{context_, buffers, state.native_handle()};
  }

  auto read_some(basic_fd& state, sio::mutable_buffer buffer) {
    return stdexec_backend::read_sender_single{context_, buffer, state.native_handle()};
  }

  auto write_some(basic_fd& state, sio::const_buffer_span buffers) {
    return stdexec_backend::write_sender{context_, buffers, state.native_handle()};
  }

  auto write_some(basic_fd& state, sio::const_buffer buffer) {
    return stdexec_backend::write_sender_single{context_, buffer, state.native_handle()};
  }

  auto read(basic_fd& state, sio::mutable_buffer_span buffers) {
    return reduce(
      buffered_sequence(stdexec_backend::read_factory{&context_, state}, buffers), 0ull);
  }

  auto read(basic_fd& state, sio::mutable_buffer buffer) {
    auto buffered = buffered_sequence(stdexec_backend::read_factory{&context_, state}, buffer);
    return reduce(std::move(buffered), 0ull);
  }

  auto write(basic_fd& state, sio::const_buffer_span buffers) {
    return reduce(buffered_sequence(stdexec_backend::write_factory{&context_, state}, buffers), 0ull);
  }

  auto write(basic_fd& state, sio::const_buffer buffer) {
    return reduce(buffered_sequence(stdexec_backend::write_factory{&context_, state}, buffer), 0ull);
  }

  auto read_some(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
    return stdexec_backend::read_sender{context_, buffers, state.native_handle(), offset};
  }

  auto read_some(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
    return stdexec_backend::read_sender_single{context_, buffer, state.native_handle(), offset};
  }

  auto write_some(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
    return stdexec_backend::write_sender{context_, buffers, state.native_handle(), offset};
  }

  auto write_some(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
    return stdexec_backend::write_sender_single{context_, buffer, state.native_handle(), offset};
  }

  auto read(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::read_factory{&context_, state},
        buffers,
        offset),
      0ull);
  }

  auto read(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
    auto buffered = buffered_sequence(stdexec_backend::read_factory{&context_, state}, buffer, offset);
    return reduce(std::move(buffered), 0ull);
  }

  auto write(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::write_factory{&context_, state},
        buffers,
        offset),
      0ull);
  }

  auto write(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::write_factory{&context_, state},
        buffer,
        offset),
      0ull);
  }

  auto open_file(
    std::filesystem::path path,
    async::mode mode = async::mode::read,
    async::creation creation = async::creation::open_existing,
    async::caching caching = async::caching::unchanged,
    int dirfd = AT_FDCWD) {
    stdexec_backend::open_data data{
      static_cast<std::filesystem::path&&>(path),
      dirfd,
      to_open_flags(mode, creation),
      to_mode(mode)};
    return stdexec_backend::file_open_sender<file_state>{
      context_,
      static_cast<stdexec_backend::open_data&&>(data)};
  }

  auto open_seekable_file(
    std::filesystem::path path,
    async::mode mode = async::mode::read,
    async::creation creation = async::creation::open_existing,
    async::caching caching = async::caching::unchanged,
    int dirfd = AT_FDCWD) {
    stdexec_backend::open_data data{
      static_cast<std::filesystem::path&&>(path),
      dirfd,
      to_open_flags(mode, creation),
      to_mode(mode)};
    return stdexec_backend::file_open_sender<seekable_file_state>{
      context_,
      static_cast<stdexec_backend::open_data&&>(data)};
  }

  template <class Protocol>
  auto open_socket(Protocol protocol) {
    return stdexec_backend::open_sender<Protocol>{&context_, protocol};
  }

  template <class Protocol>
  auto connect(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
    return stdexec_backend::connect_sender<Protocol>{&context_, state.native_handle(), endpoint};
  }

  template <class Protocol>
  void bind(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
    auto addr = reinterpret_cast<const sockaddr*>(endpoint.data());
    if (::bind(state.native_handle(), addr, endpoint.size()) == -1) {
      throw std::system_error(errno, std::system_category());
    }
  }

  template <class Protocol>
  auto read_some(socket_state<Protocol>& state, sio::mutable_buffer_span buffers) {
    return read_some(static_cast<basic_fd&>(state), buffers);
  }

  template <class Protocol>
  auto read_some(socket_state<Protocol>& state, sio::mutable_buffer buffer) {
    return read_some(static_cast<basic_fd&>(state), buffer);
  }

  template <class Protocol>
  auto write_some(socket_state<Protocol>& state, sio::const_buffer_span buffers) {
    return write_some(static_cast<basic_fd&>(state), buffers);
  }

  template <class Protocol>
  auto write_some(socket_state<Protocol>& state, sio::const_buffer buffer) {
    return write_some(static_cast<basic_fd&>(state), buffer);
  }

  template <class Protocol>
  auto write(socket_state<Protocol>& state, sio::const_buffer_span buffers) {
    return write(static_cast<basic_fd&>(state), buffers);
  }

  template <class Protocol>
  auto write(socket_state<Protocol>& state, sio::const_buffer buffer) {
    return write(static_cast<basic_fd&>(state), buffer);
  }

  template <class Protocol>
  auto open_acceptor(Protocol protocol, typename Protocol::endpoint endpoint) {
    return ::stdexec::then(
      open_socket(protocol),
      [this, endpoint = std::move(endpoint)](socket_state<Protocol> state) mutable {
        int fd = state.native_handle();
        int one = 1;
        if (
          ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &one,
            static_cast<socklen_t>(sizeof(one)))
          == -1) {
          int err = errno;
          ::close(fd);
          throw std::system_error(err, std::system_category());
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
        return acceptor_state<Protocol>{fd};
      });
  }

  template <class Protocol>
  auto accept_once(acceptor_state<Protocol>& state) {
    return stdexec_backend::accept_sender<Protocol>{&context_, state.native_handle()};
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

} // namespace sio::event_loop::stdexec
