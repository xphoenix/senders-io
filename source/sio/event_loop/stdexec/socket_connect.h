#pragma once

#include "./details.h"

namespace sio::event_loop::stdexec_backend {
  namespace se = ::stdexec;
  template <class Protocol, class Receiver>
  struct connect_operation_base : stoppable_op_base<Receiver> {
    exec::io_uring_context* context_{};
    int fd_{-1};
    typename Protocol::endpoint peer_endpoint_;

    connect_operation_base(
      exec::io_uring_context& context,
      int fd,
      typename Protocol::endpoint peer_endpoint,
      Receiver rcvr) noexcept
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(rcvr)}
      , context_{&context}
      , fd_{fd}
      , peer_endpoint_{peer_endpoint} {
    }

    static std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_CONNECT;
      sqe_.fd = fd_;
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

    exec::io_uring_context* context_{};
    int fd_{-1};
    typename Protocol::endpoint peer_endpoint_;

    template <class Receiver>
    auto connect(Receiver rcvr) const
      -> connect_operation<Protocol, Receiver> {
      return {
        std::in_place,
        *context_,
        fd_,
        peer_endpoint_,
        static_cast<Receiver&&>(rcvr)};
    }
  };

} // namespace sio::event_loop::stdexec_backend
