#pragma once

#include "submission_operation.hpp"

namespace sio::event_loop::iouring {

  struct fd_write_submission_span {
    fd_write_submission_span(sio::const_buffer_span buffers, int fd, ::off_t offset) noexcept
      : buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
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

    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{};
  };

  struct fd_write_submission_single {
    fd_write_submission_single(sio::const_buffer buffer, int fd, ::off_t offset) noexcept
      : buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
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

    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{};
  };

  template <class Submission, class Receiver>
  class fd_write_operation_base
    : public submission_operation<fd_write_operation_base<Submission, Receiver>, Receiver>
    , public Submission {
   public:
    fd_write_operation_base(
      io_context& ctx,
      auto data,
      int fd,
      ::off_t offset,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          fd_write_operation_base<Submission, Receiver>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , Submission{data, fd, offset} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      this->Submission::submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), static_cast<std::size_t>(cqe.res));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  template <class Receiver>
  using fd_write_operation_span = fd_write_operation_base<fd_write_submission_span, Receiver>;

  template <class Receiver>
  using fd_write_operation_single = fd_write_operation_base<fd_write_submission_single, Receiver>;

  struct fd_write_sender_span {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::const_buffer_span buffers_{};
    int fd_{};
    ::off_t offset_{-1};

    fd_write_sender_span(
      io_context& ctx,
      sio::const_buffer_span buffers,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , buffers_{buffers}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_span<Receiver>{
        *context_, buffers_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct fd_write_sender_single {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    sio::const_buffer buffer_{};
    int fd_{};
    ::off_t offset_{-1};

    fd_write_sender_single(
      io_context& ctx,
      sio::const_buffer buffer,
      int fd,
      ::off_t offset = -1) noexcept
      : context_{&ctx}
      , buffer_{buffer}
      , fd_{fd}
      , offset_{offset} {
    }

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_write_operation_single<Receiver>{
        *context_, buffer_, fd_, offset_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  struct fd_write_factory {
    io_context* context_{};
    int fd_{};

    fd_write_sender_span operator()(sio::const_buffer_span data, ::off_t offset) const noexcept {
      return fd_write_sender_span{*context_, data, fd_, offset};
    }

    fd_write_sender_single operator()(sio::const_buffer data, ::off_t offset) const noexcept {
      return fd_write_sender_single{*context_, data, fd_, offset};
    }
  };

} // namespace sio::event_loop::iouring
