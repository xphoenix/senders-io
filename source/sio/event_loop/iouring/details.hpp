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

} // namespace sio::event_loop::iouring
