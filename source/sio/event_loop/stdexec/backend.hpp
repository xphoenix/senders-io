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

#include <exec/linux/io_uring_context.hpp>
#include <stdexec/execution.hpp>

#include <sys/socket.h>
#include <fcntl.h>

#include <filesystem>
#include <system_error>
#include <utility>

namespace sio::event_loop::stdexec_backend {
struct fd_state;
struct file_state;
struct seekable_file_state;
template <class Protocol>
struct socket_state;
template <class Protocol>
struct acceptor_state;

class backend {
 public:
 using native_context_type = exec::io_uring_context;
  using env = stdexec_backend::env;
  using close_sender = stdexec_backend::close_sender;

  using fd_state = stdexec_backend::fd_state;
  using file_state = stdexec_backend::file_state;
  using seekable_file_state = stdexec_backend::seekable_file_state;

  template <class Protocol>
  using socket_state = stdexec_backend::socket_state<Protocol>;

  template <class Protocol>
  using acceptor_state = stdexec_backend::acceptor_state<Protocol>;

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

  auto close(fd_state& state) noexcept {
    return close_sender{&state.context(), state.native_handle()};
  }

  auto read_some(fd_state& state, sio::mutable_buffer_span buffers) {
    return stdexec_backend::read_sender{state.context(), buffers, state.native_handle()};
  }

  auto read_some(fd_state& state, sio::mutable_buffer buffer) {
    return stdexec_backend::read_sender_single{state.context(), buffer, state.native_handle()};
  }

  auto write_some(fd_state& state, sio::const_buffer_span buffers) {
    return stdexec_backend::write_sender{state.context(), buffers, state.native_handle()};
  }

  auto write_some(fd_state& state, sio::const_buffer buffer) {
    return stdexec_backend::write_sender_single{state.context(), buffer, state.native_handle()};
  }

  auto read(fd_state& state, sio::mutable_buffer_span buffers) {
    return reduce(
      buffered_sequence(stdexec_backend::read_factory{&state.context(), state.native_handle()}, buffers),
      0ull);
  }

  auto read(fd_state& state, sio::mutable_buffer buffer) {
    auto buffered =
      buffered_sequence(stdexec_backend::read_factory{&state.context(), state.native_handle()}, buffer);
    return reduce(std::move(buffered), 0ull);
  }

  auto write(fd_state& state, sio::const_buffer_span buffers) {
    return reduce(
      buffered_sequence(stdexec_backend::write_factory{&state.context(), state.native_handle()}, buffers),
      0ull);
  }

  auto write(fd_state& state, sio::const_buffer buffer) {
    return reduce(
      buffered_sequence(stdexec_backend::write_factory{&state.context(), state.native_handle()}, buffer),
      0ull);
  }

  auto read_some(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
    return stdexec_backend::read_sender{state.context(), buffers, state.native_handle(), offset};
  }

  auto read_some(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
    return stdexec_backend::read_sender_single{state.context(), buffer, state.native_handle(), offset};
  }

  auto write_some(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
    return stdexec_backend::write_sender{state.context(), buffers, state.native_handle(), offset};
  }

  auto write_some(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
    return stdexec_backend::write_sender_single{state.context(), buffer, state.native_handle(), offset};
  }

  auto read(seekable_file_state& state, sio::mutable_buffer_span buffers, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::read_factory{&state.context(), state.native_handle()},
        buffers,
        offset),
      0ull);
  }

  auto read(seekable_file_state& state, sio::mutable_buffer buffer, ::off_t offset) {
    auto buffered = buffered_sequence(
      stdexec_backend::read_factory{&state.context(), state.native_handle()},
      buffer,
      offset);
    return reduce(std::move(buffered), 0ull);
  }

  auto write(seekable_file_state& state, sio::const_buffer_span buffers, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::write_factory{&state.context(), state.native_handle()},
        buffers,
        offset),
      0ull);
  }

  auto write(seekable_file_state& state, sio::const_buffer buffer, ::off_t offset) {
    return reduce(
      buffered_sequence(
        stdexec_backend::write_factory{&state.context(), state.native_handle()},
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
      static_cast<stdexec_backend::open_data&&>(data),
      mode,
      creation,
      caching};
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
      static_cast<stdexec_backend::open_data&&>(data),
      mode,
      creation,
      caching};
  }

  template <class Protocol>
  auto open_socket(Protocol protocol) {
    return stdexec_backend::open_sender<Protocol>{&context_, protocol};
  }

  template <class Protocol>
  auto connect(socket_state<Protocol>& state, typename Protocol::endpoint endpoint) {
    return stdexec_backend::connect_sender<Protocol>{&state, endpoint};
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
  auto read_some(socket_state<Protocol>& state, typename socket_state<Protocol>::buffer_type buffer) {
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
      open_socket(protocol),
      [this, endpoint](stdexec_backend::socket_state<Protocol> state) mutable {
        Protocol proto = state.consume_protocol();
        int one = 1;
        if (::setsockopt(
              state.native_handle(),
              SOL_SOCKET,
              SO_REUSEADDR,
              &one,
              static_cast<socklen_t>(sizeof(int)))
            == -1) {
          throw std::system_error(errno, std::system_category());
        }
        state.bind(endpoint);
        if (::listen(state.native_handle(), 16) == -1) {
          throw std::system_error(errno, std::system_category());
        }
        return stdexec_backend::acceptor_state<Protocol>{
          context_,
          state.native_handle(),
          std::move(proto),
          std::move(endpoint)};
      });
  }

  template <class Protocol>
  auto accept_once(acceptor_state<Protocol>& state) {
    return stdexec_backend::accept_sender<Protocol>{&state};
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
