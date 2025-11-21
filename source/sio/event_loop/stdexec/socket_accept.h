#pragma once

#include "../concepts.hpp"
#include "./details.h"

namespace sio::event_loop::stdexec_backend {
  namespace se = ::stdexec;

  template <class Protocol>
  struct acceptor_state;

  template <class Protocol, class Receiver>
  struct accept_operation_base : stoppable_op_base<Receiver> {
    exec::io_uring_context* context_{};
    int listen_fd_{-1};

    accept_operation_base(exec::io_uring_context& context, int listen_fd, Receiver receiver) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , context_{&context}
      , listen_fd_{listen_fd} {
    }

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_ACCEPT;
      sqe_.fd = listen_fd_;
      sqe_.addr = 0;
      sqe_.addr2 = 0;
      sqe = sqe_;
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        se::set_value(
          static_cast<accept_operation_base&&>(*this).receiver(),
          socket_fd<Protocol>{cqe.res});
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
      se::set_value_t(socket_fd<Protocol>),
      se::set_error_t(std::error_code),
      se::set_stopped_t()>;

    exec::io_uring_context* context_{};
    int listen_fd_{-1};

    template <class Receiver>
    auto connect(Receiver rcvr) const
      -> accept_operation<Protocol, Receiver> {
      return {
        std::in_place,
        *context_,
        listen_fd_,
        static_cast<Receiver&&>(rcvr)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };
} // namespace sio::event_loop::stdexec_backend
