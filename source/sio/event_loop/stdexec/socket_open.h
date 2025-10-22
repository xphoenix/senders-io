#pragma once

#include "./details.h"

namespace sio::event_loop::stdexec_backend {
  namespace se = ::stdexec;

  template <class Protocol, class Receiver>
  struct open_operation_base {
    exec::io_uring_context* context_{};
    Protocol protocol_;
    [[no_unique_address]] Receiver receiver_;

    open_operation_base(exec::io_uring_context& context, Protocol protocol, Receiver receiver) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{&context}
      , protocol_{std::move(protocol)}
      , receiver_{static_cast<Receiver&&>(receiver)} {
    }

    static std::true_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe&) noexcept {
    }

    void complete(const ::io_uring_cqe&) noexcept;

    auto context() const noexcept -> exec::io_uring_context& {
      return *context_;
    }
  };

  template <class Protocol, class Receiver>
  using open_operation = io_task_facade<open_operation_base<Protocol, Receiver>>;

  template <class Protocol>
  struct open_sender {
    using sender_concept = se::sender_t;
    using completion_signatures = se::completion_signatures<
      se::set_value_t(socket_state<Protocol>),
      se::set_error_t(std::error_code)>;

    exec::io_uring_context* context_{};
    Protocol protocol_;

    template <class Receiver>
    auto connect(Receiver rcvr) const
      -> open_operation<Protocol, Receiver> {
      return {
        std::in_place,
        *context_,
        protocol_,
        static_cast<Receiver&&>(rcvr)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };

  template <class Protocol, class Receiver>
  inline void
    open_operation_base<Protocol, Receiver>::complete(const ::io_uring_cqe&) noexcept {
    int rc = ::socket(protocol_.family(), protocol_.type(), protocol_.protocol());
    if (rc == -1) {
      se::set_error(
        static_cast<Receiver&&>(receiver_), std::error_code(errno, std::system_category()));
    } else {
      se::set_value(
        static_cast<Receiver&&>(receiver_),
        socket_state<Protocol>{*context_, rc, std::move(protocol_)});
    }
  }
} // namespace sio::event_loop::stdexec_backend
