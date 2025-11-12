#pragma once

#include "submission_operation.hpp"

#include <sys/socket.h>

namespace sio::event_loop::iouring {

  template <class Protocol, class Receiver>
  class socket_sendmsg_operation
    : public submission_operation<socket_sendmsg_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_sendmsg_operation(socket_state<Protocol>& state, Receiver&& rcvr, ::msghdr msg) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          socket_sendmsg_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , msg_{msg} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_SENDMSG;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(&msg_);
      sqe = sqe_;
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

   private:
    socket_state<Protocol>* state_{};
    ::msghdr msg_{};
  };

  template <class Protocol>
  struct socket_sendmsg_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(std::size_t),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{};
    ::msghdr msg_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_sendmsg_operation<Protocol, Receiver>{
        *state_, static_cast<Receiver&&>(receiver), msg_};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };

} // namespace sio::event_loop::iouring
