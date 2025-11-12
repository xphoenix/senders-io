#pragma once

#include "submission_operation.hpp"

namespace sio::event_loop::iouring {

  struct fd_close_submission {
    int fd_{};

    explicit fd_close_submission(int fd) noexcept
      : fd_{fd} {
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CLOSE;
      sqe_.fd = fd_;
      sqe = sqe_;
    }
  };

  template <class Receiver>
  class fd_close_operation
    : public submission_operation<fd_close_operation<Receiver>, Receiver>
    , public fd_close_submission {
   public:
    fd_close_operation(io_context& ctx, int fd, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<fd_close_operation<Receiver>, Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , fd_close_submission{fd} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res == 0) {
        stdexec::set_value(std::move(*this).receiver());
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  struct fd_close_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    int fd_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return fd_close_operation<Receiver>{*context_, fd_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

} // namespace sio::event_loop::iouring
