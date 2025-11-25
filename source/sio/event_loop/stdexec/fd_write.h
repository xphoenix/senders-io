#pragma once

#include "details.h"
#include "../concepts.hpp"

namespace sio::event_loop::stdexec_backend {
  struct write_submission {
    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{};

    write_submission(sio::const_buffer_span buffers, int fd, ::off_t offset) noexcept
      : buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_WRITEV;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffers_.begin());
      sqe_.len = buffers_.size();
      sqe = sqe_;
    }
  };

  struct write_submission_single {
    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{};

    write_submission_single(sio::const_buffer buffer, int fd, ::off_t offset) noexcept
      : buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    ~write_submission_single() = default;

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_WRITE;
      sqe_.fd = fd_;
      sqe_.off = offset_;
      sqe_.addr = std::bit_cast<__u64>(buffer_.data());
      sqe_.len = buffer_.size();
      sqe = sqe_;
    }
  };

  template <class SubmissionBase, class Receiver>
  struct write_operation_base
    : stoppable_op_base<Receiver>
    , SubmissionBase {
    write_operation_base(
      exec::io_uring_context& context,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& receiver) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , SubmissionBase{data, fd, offset} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        ::stdexec::set_value(
          static_cast<write_operation_base&&>(*this).receiver(),
          static_cast<std::size_t>(cqe.res));
      } else {
        STDEXEC_ASSERT(cqe.res < 0);
        ::stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using write_operation = stoppable_task_facade<write_operation_base<write_submission, Receiver>>;

  template <class Receiver>
  using write_operation_single =
    stoppable_task_facade<write_operation_base<write_submission_single, Receiver>>;

  struct write_sender {
    using sender_concept = ::stdexec::sender_t;

    using completion_signatures = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(std::size_t),
      ::stdexec::set_error_t(std::error_code),
      ::stdexec::set_stopped_t()>;

    exec::io_uring_context* context_{};
    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{-1};

    write_sender(
      exec::io_uring_context& context,
      sio::const_buffer_span buffers,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <class Receiver>
    auto connect(Receiver rcvr) const -> write_operation<Receiver> {
      return {
        std::in_place,
        *context_,
        buffers_,
        fd_,
        offset_,
        static_cast<Receiver&&>(rcvr)};
    }
  };

  struct write_sender_single {
    using sender_concept = ::stdexec::sender_t;

    using completion_signatures = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(std::size_t),
      ::stdexec::set_error_t(std::error_code),
      ::stdexec::set_stopped_t()>;

    exec::io_uring_context* context_{};
    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{-1};

    write_sender_single(
      exec::io_uring_context& context,
      sio::const_buffer buffer,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&context}
      , buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    template <class Receiver>
    auto connect(Receiver rcvr) const -> write_operation_single<Receiver> {
      return {
        std::in_place,
        *context_,
        buffer_,
        fd_,
        offset_,
        static_cast<Receiver&&>(rcvr)};
    }
  };

  struct write_factory {
    exec::io_uring_context* context_{};
    basic_fd state_{};

    write_sender operator()(sio::const_buffer_span data, ::off_t offset) const noexcept {
      return write_sender{*context_, data, state_.native_handle(), offset};
    }

    write_sender_single operator()(sio::const_buffer data, ::off_t offset) const noexcept {
      return write_sender_single{*context_, data, state_.native_handle(), offset};
    }
  };
} // namespace sio::event_loop::stdexec_backend
