#pragma once

#include "submission_operation.hpp"

#include <sys/socket.h>

namespace sio::event_loop::iouring {

  template <class Protocol, class Receiver>
  class socket_accept_operation
    : public submission_operation<socket_accept_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_accept_operation(acceptor_state<Protocol>& state, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          socket_accept_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , addrlen_{static_cast<socklen_t>(state.local_endpoint().size())} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(state_->local_endpoint().data());
      sqe_.addr2 = std::bit_cast<__u64>(&addrlen_);
      sqe = sqe_;
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
      } else if (cqe.res >= 0) {
        socket_state<Protocol> state{state_->context(), cqe.res, state_->protocol()};
        stdexec::set_value(std::move(*this).receiver(), std::move(state));
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }

   private:
    acceptor_state<Protocol>* state_{};
    socklen_t addrlen_{};
  };

  template <class Protocol>
  struct socket_accept_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    acceptor_state<Protocol>* state_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_accept_operation<Protocol, Receiver>{*state_, static_cast<Receiver&&>(receiver)};
    }
  };

} // namespace sio::event_loop::iouring
