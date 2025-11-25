#pragma once

#include "details.h"

#include <filesystem>

#include <fcntl.h>

namespace sio::event_loop::stdexec_backend {
  struct open_data {
    std::filesystem::path path_{};
    int dirfd_{AT_FDCWD};
    int flags_{0};
    ::mode_t mode_{0};
  };

  struct open_submission {
    open_data data_{};

    explicit open_submission(open_data data) noexcept
      : data_{static_cast<open_data&&>(data)} {
    }

    ~open_submission() = default;

    static constexpr std::false_type ready() noexcept {
      return {};
    }

    void submit(::io_uring_sqe& sqe) const noexcept {
      ::io_uring_sqe sqe_{};
      sqe_.opcode = IORING_OP_OPENAT;
      sqe_.fd = data_.dirfd_;
      sqe_.addr = std::bit_cast<__u64>(data_.path_.c_str());
      sqe_.open_flags = data_.flags_;
      sqe_.len = data_.mode_;
      sqe = sqe_;
    }
  };

  template <class Receiver, class State>
  struct file_open_operation_base
    : stoppable_op_base<Receiver>
    , open_submission {

    file_open_operation_base(
      open_data data,
      exec::io_uring_context& context,
      Receiver&& receiver)
      : stoppable_op_base<Receiver>{context, static_cast<Receiver&&>(receiver)}
      , open_submission{static_cast<open_data&&>(data)} {
    }

    void complete(const ::io_uring_cqe& cqe) noexcept {
      if (cqe.res >= 0) {
        ::stdexec::set_value(static_cast<Receiver&&>(this->__receiver_), State{cqe.res});
      } else {
        SIO_ASSERT(cqe.res < 0);
        ::stdexec::set_error(
          static_cast<Receiver&&>(this->__receiver_),
          std::error_code(-cqe.res, std::system_category()));
      }
    }
  };

  template <class Receiver, class State>
  using file_open_operation = stoppable_task_facade<file_open_operation_base<Receiver, State>>;

  template <class State>
  struct file_open_sender {
    using sender_concept = ::stdexec::sender_t;

    using completion_signatures = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(State),
      ::stdexec::set_error_t(std::error_code),
      ::stdexec::set_stopped_t()>;

    exec::io_uring_context* context_{};
    open_data data_{};

    file_open_sender(exec::io_uring_context& context, open_data data) noexcept
      : context_{&context}
      , data_{static_cast<open_data&&>(data)} {
    }

    template <class Receiver>
    auto connect(Receiver rcvr) const -> file_open_operation<Receiver, State> {
      return {
        std::in_place,
        open_data{data_},
        *context_,
        static_cast<Receiver&&>(rcvr)};
    }
  };
} // namespace sio::event_loop::stdexec_backend
