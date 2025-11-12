#pragma once

#include "../../assert.hpp"
#include "../../const_buffer_span.hpp"
#include "../../io_concepts.hpp"
#include "../../mutable_buffer.hpp"
#include "../../mutable_buffer_span.hpp"
#include "../../sequence/buffered_sequence.hpp"
#include "../../sequence/reduce.hpp"

#include <exec/linux/io_uring_context.hpp>
#include <stdexec/execution.hpp>

#include <sys/socket.h>

#include <bit>
#include <cerrno>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>

namespace sio::buffered_sequence_ {
  inline mutable_buffer to_buffer_sequence(const mutable_buffer& buffer) {
    return buffer;
  }

  inline const_buffer to_buffer_sequence(const const_buffer& buffer) {
    return buffer;
  }

  inline mutable_buffer_span to_buffer_sequence(const std::span<mutable_buffer>& buffers) {
    return mutable_buffer_span(buffers.data(), buffers.size());
  }

  inline const_buffer_span to_buffer_sequence(const std::span<const_buffer>& buffers) {
    return const_buffer_span(buffers.data(), buffers.size());
  }

  inline mutable_buffer_span to_buffer_sequence(const mutable_buffer_span& buffers) {
    return buffers;
  }

  inline const_buffer_span to_buffer_sequence(const const_buffer_span& buffers) {
    return buffers;
  }
} // namespace sio::buffered_sequence_

namespace sio::event_loop::stdexec_backend {
  class backend;

  struct fd_state {
    exec::io_uring_context* context_{nullptr};
    int fd_{-1};

    fd_state() = default;

    fd_state(exec::io_uring_context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    exec::io_uring_context& context() const noexcept {
      return *context_;
    }

    int native_handle() const noexcept {
      return fd_;
    }

    void reset(int fd = -1) noexcept {
      fd_ = fd;
    }
  };

  struct env {
    exec::io_uring_scheduler scheduler;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
      -> exec::io_uring_scheduler {
      return scheduler;
    }
  };

  template <class Tp>
  using io_task_facade = exec::__io_uring::__io_task_facade<Tp>;

  template <class Tp>
  using stoppable_op_base = exec::__io_uring::__stoppable_op_base<Tp>;

  template <class Tp>
  using stoppable_task_facade = exec::__io_uring::__stoppable_task_facade_t<Tp>;

  struct file_state_base : fd_state {
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};

    file_state_base() = default;

    file_state_base(
      exec::io_uring_context& context,
      int fd,
      async::mode mode,
      async::creation creation,
      async::caching caching) noexcept
      : fd_state{context, fd}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching} {
    }

    async::mode mode() const noexcept {
      return mode_;
    }

    async::creation creation() const noexcept {
      return creation_;
    }

    async::caching caching() const noexcept {
      return caching_;
    }
  };

  struct file_state : file_state_base {
    using file_state_base::file_state_base;
  };

  struct seekable_file_state : file_state_base {
    using file_state_base::file_state_base;
  };

  template <class Protocol>
  struct socket_state : fd_state {
    using endpoint = typename Protocol::endpoint;
    using buffer_type = sio::mutable_buffer;
    using buffers_type = sio::mutable_buffer_span;
    using const_buffer_type = sio::const_buffer;
    using const_buffers_type = sio::const_buffer_span;

    socket_state() = default;

    socket_state(exec::io_uring_context& context, int fd, Protocol protocol) noexcept
      : fd_state{context, fd}
      , protocol_{std::move(protocol)} {
    }

    const Protocol& protocol() const noexcept {
      return *protocol_;
    }

    Protocol consume_protocol() noexcept {
      Protocol value = std::move(*protocol_);
      protocol_.reset();
      return value;
    }

    void bind(endpoint local_endpoint) {
      auto addr = reinterpret_cast<const sockaddr*>(local_endpoint.data());
      if (::bind(native_handle(), addr, local_endpoint.size()) == -1) {
        throw std::system_error(errno, std::system_category());
      }
    }

    endpoint local_endpoint() const;
    endpoint remote_endpoint() const;

   private:
    [[no_unique_address]] std::optional<Protocol> protocol_{};
  };

  template <class Protocol>
  struct acceptor_state : fd_state {
    using endpoint = typename Protocol::endpoint;

    acceptor_state() = default;

    acceptor_state(
      exec::io_uring_context& context,
      int fd,
      Protocol protocol,
      endpoint endpoint_value) noexcept
      : fd_state{context, fd}
      , protocol_{std::move(protocol)}
      , local_endpoint_{std::move(endpoint_value)} {
    }

    const Protocol& protocol() const noexcept {
      return *protocol_;
    }

    Protocol consume_protocol() noexcept {
      Protocol value = std::move(*protocol_);
      protocol_.reset();
      return value;
    }

    const endpoint& local_endpoint() const noexcept {
      return local_endpoint_;
    }

   private:
    [[no_unique_address]] std::optional<Protocol> protocol_{};
    endpoint local_endpoint_{};
  };
} // namespace sio::event_loop::stdexec_backend
