#pragma once

#include "details.h"

namespace sio::event_loop::stdexec_backend {
  struct close_submission {
    exec::io_uring_context* context_{};
    int fd_{};

    close_submission(exec::io_uring_context& context, int fd) noexcept
      : context_{&context}
      , fd_{fd} {
    }

    exec::io_uring_context& context() const noexcept {
      return *context_;
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CLOSE;
      sqe_.fd = fd_;
      sqe = sqe_;
    }
  };

  template <class Receiver>
  struct close_operation_base : close_submission {
    [[no_unique_address]] Receiver receiver_;

    close_operation_base(exec::io_uring_context& context, int fd, Receiver receiver)
      : close_submission{context, fd}
      , receiver_{static_cast<Receiver&&>(receiver)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res == 0) {
        ::stdexec::set_value(static_cast<Receiver&&>(receiver_));
      } else {
        SIO_ASSERT(cqe.res < 0);
        ::stdexec::set_error(
          static_cast<Receiver&&>(receiver_), std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver>
  using close_operation = io_task_facade<close_operation_base<Receiver>>;

  struct close_sender {
    using sender_concept = ::stdexec::sender_t;

    using completion_signatures = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(),
      ::stdexec::set_error_t(std::error_code),
      ::stdexec::set_stopped_t()>;

    exec::io_uring_context* context_{};
    int fd_{};

    template <class Receiver>
    auto connect(Receiver rcvr) const -> close_operation<Receiver> {
      return {
        std::in_place,
        *context_,
        fd_,
        static_cast<Receiver&&>(rcvr)};
    }
  };
} // namespace sio::event_loop::stdexec_backend
