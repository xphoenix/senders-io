#pragma once

#include "submission_operation.hpp"
#include "../concepts.hpp"

#include <sys/socket.h>

namespace sio::event_loop::iouring {

  template <class Protocol, class Receiver>
  class socket_accept_operation
    : public submission_operation<socket_accept_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_accept_operation(io_context& ctx, int fd, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          socket_accept_operation<Protocol, Receiver>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , fd_{fd} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = fd_;
      sqe_.addr = 0;
      sqe_.addr2 = 0;
      sqe = sqe_;
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
      } else if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), socket_fd<Protocol>{cqe.res});
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    int fd_{-1};
  };

  template <class Protocol>
  struct socket_accept_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_fd<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    int fd_{-1};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_accept_operation<Protocol, Receiver>{
        *context_, fd_, static_cast<Receiver&&>(receiver)};
    }
  };

} // namespace sio::event_loop::iouring
