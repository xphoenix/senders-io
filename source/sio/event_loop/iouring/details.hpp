#pragma once

#include <liburing.h>

#include "../../assert.hpp"
#include "../../const_buffer.hpp"
#include "../../const_buffer_span.hpp"
#include "../../io_concepts.hpp"
#include "../../mutable_buffer.hpp"
#include "../../mutable_buffer_span.hpp"
#include "../../sequence/buffered_sequence.hpp"
#include "../../sequence/reduce.hpp"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::iouring {
  class io_context;
  class scheduler;

  struct completion_base {
    completion_base(
      io_context& ctx,
      void (*complete_fn)(completion_base*, const ::io_uring_cqe&) noexcept) noexcept
      : context{ctx}
      , complete_fn{complete_fn} {
    }

    virtual ~completion_base() = default;

    void complete(const ::io_uring_cqe& cqe) noexcept {
      complete_fn(this, cqe);
    }

    void request_cancel() noexcept;

    io_context& context;
    void (*complete_fn)(completion_base*, const ::io_uring_cqe&) noexcept;
    std::atomic<bool> cancelled{false};
  };

  inline void completion_base::request_cancel() noexcept {
    if (!cancelled.exchange(true, std::memory_order_acq_rel)) {
      context.cancel(*this);
    }
  }

  struct env;

  namespace buffered_sequence_ {
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
  } // namespace buffered_sequence_

  struct fd_state {
    io_context* context_{nullptr};
    int fd_{-1};

    fd_state() = default;

    fd_state(io_context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    io_context& context() const noexcept {
      return *context_;
    }

    int native_handle() const noexcept {
      return fd_;
    }

    void reset(int fd = -1) noexcept {
      fd_ = fd;
    }
  };

  struct file_state_base : fd_state {
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};

    file_state_base() = default;

    file_state_base(
      io_context& context,
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

    socket_state(io_context& context, int fd, Protocol protocol) noexcept
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

    void bind(endpoint local_endpoint) const {
      if (
        ::bind(native_handle(), reinterpret_cast<sockaddr*>(local_endpoint.data()), local_endpoint.size()) == -1) {
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

    acceptor_state(io_context& context, int fd, Protocol protocol, endpoint endpoint_value) noexcept
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
} // namespace sio::event_loop::iouring
