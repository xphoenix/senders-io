#pragma once

#include "submission_operation.hpp"

namespace sio::event_loop::iouring {

  template <class Protocol, class Receiver>
  class socket_connect_operation
    : public submission_operation<socket_connect_operation<Protocol, Receiver>, Receiver> {
   public:
    socket_connect_operation(
      socket_state<Protocol>& state,
      typename Protocol::endpoint endpoint,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : submission_operation<
          socket_connect_operation<Protocol, Receiver>,
          Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , peer_endpoint_{endpoint} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CONNECT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(peer_endpoint_.data());
      sqe_.addr2 = std::bit_cast<__u64>(peer_endpoint_.size());
      sqe = sqe_;
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

   private:
    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_{};
  };

  template <class Protocol>
  struct socket_connect_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_connect_operation<Protocol, Receiver>{
        *state_, peer_endpoint_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };

} // namespace sio::event_loop::iouring
