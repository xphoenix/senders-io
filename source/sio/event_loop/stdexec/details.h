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

} // namespace sio::event_loop::stdexec_backend
