#pragma once

#include "./details.h"

namespace sio::event_loop::stdexec_backend {
  namespace se = ::stdexec;

  template <class Protocol>
  struct acceptor_state;

  template <class Protocol, class Receiver>
  struct accept_operation_base : stoppable_op_base<Receiver> {
    acceptor_state<Protocol>* state_{};
    socklen_t addrlen_{};

    accept_operation_base(acceptor_state<Protocol>& state, Receiver receiver) noexcept
      : stoppable_op_base<Receiver>{state.context(), static_cast<Receiver&&>(receiver)}
      , state_{&state}
      , addrlen_(state.local_endpoint().size()) {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = state_->native_handle();
      sqe_.addr = std::bit_cast<__u64>(state_->local_endpoint().data());
      sqe_.addr2 = std::bit_cast<__u64>(&addrlen_);
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        socket_state<Protocol> state{
          state_->context(),
          cqe.res,
          state_->protocol()};
        se::set_value(
          static_cast<accept_operation_base&&>(*this).receiver(),
          std::move(state));
      } else {
        se::set_error(
          static_cast<accept_operation_base&&>(*this).receiver(),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Protocol, class Receiver>
  using accept_operation =
    stoppable_task_facade<accept_operation_base<Protocol, Receiver>>;

  template <class Protocol>
  struct accept_sender {
    using sender_concept = se::sender_t;

    using completion_signatures = se::completion_signatures<
      se::set_value_t(socket_state<Protocol>),
      se::set_error_t(std::error_code),
      se::set_stopped_t()>;

    acceptor_state<Protocol>* state_{};

    template <class Receiver>
    auto connect(Receiver rcvr) const
      -> accept_operation<Protocol, Receiver> {
      return {
        std::in_place,
        *state_,
        static_cast<Receiver&&>(rcvr)};
    }

    env get_env() const noexcept {
      return {state_->context().get_scheduler()};
    }
  };
} // namespace sio::event_loop::stdexec_backend
