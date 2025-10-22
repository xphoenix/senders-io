#pragma once

#include "./details.h"

namespace sio::event_loop::stdexec_backend {
  namespace se = ::stdexec;
  template <class Protocol, class Receiver>
  struct connect_operation_base : stoppable_op_base<Receiver> {
    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_;

    connect_operation_base(
      socket_state<Protocol>& state,
      typename Protocol::endpoint peer_endpoint,
      Receiver rcvr) noexcept
      : stoppable_op_base<Receiver>{state.context(), static_cast<Receiver&&>(rcvr)}
      , state_{&state}
      , peer_endpoint_{peer_endpoint} {
    }

    static std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CONNECT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(peer_endpoint_.data());
      sqe_.off = peer_endpoint_.size();
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res == 0) {
        se::set_value(static_cast<connect_operation_base&&>(*this).receiver());
      } else {
        se::set_error(
          static_cast<connect_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Protocol, class Receiver>
  using connect_operation =
    stoppable_task_facade<connect_operation_base<Protocol, Receiver>>;

  template <class Protocol>
  struct connect_sender {
    using sender_concept = se::sender_t;

    using completion_signatures = se::completion_signatures<
      se::set_value_t(),
      se::set_error_t(std::error_code),
      se::set_stopped_t()>;

    socket_state<Protocol>* state_{};
    typename Protocol::endpoint peer_endpoint_;

    template <class Receiver>
    auto connect(Receiver rcvr) const
      -> connect_operation<Protocol, Receiver> {
      return {
        std::in_place,
        *state_,
        peer_endpoint_,
        static_cast<Receiver&&>(rcvr)};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };

} // namespace sio::event_loop::stdexec_backend
