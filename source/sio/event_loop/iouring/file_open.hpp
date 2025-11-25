#pragma once

#include "submission_operation.hpp"

#include <filesystem>

namespace sio::event_loop::iouring {

  struct open_data {
    std::filesystem::path path_{};
    int dirfd_{AT_FDCWD};
    int flags_{0};
    ::mode_t mode_{0};
  };

  struct open_submission {
    explicit open_submission(open_data data) noexcept
      : data_{static_cast<open_data&&>(data)} {
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

    open_data data_;
  };

  template <class Receiver, class State>
  class file_open_operation
    : public submission_operation<file_open_operation<Receiver, State>, Receiver>
    , public open_submission {
   public:
    file_open_operation(open_data data, io_context& ctx, Receiver&& rcvr)
      : submission_operation<
          file_open_operation<Receiver, State>,
          Receiver>{ctx, static_cast<Receiver&&>(rcvr)}
      , open_submission{static_cast<open_data&&>(data)} {
    }

    void prepare_submission(::io_uring_sqe& sqe) noexcept {
      submit(sqe);
    }

    void on_completion(const ::io_uring_cqe& cqe) noexcept {
      if (this->cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
        stdexec::set_stopped(std::move(*this).receiver());
        return;
      }
      if (cqe.res >= 0) {
        stdexec::set_value(std::move(*this).receiver(), State{cqe.res});
      } else {
        auto ec = std::error_code(-cqe.res, std::system_category());
        stdexec::set_error(std::move(*this).receiver(), std::move(ec));
      }
    }
  };

  template <class State>
  struct file_open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(State),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    io_context* context_{};
    open_data data_;

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return file_open_operation<Receiver, State>{
        open_data{data_}, *context_, static_cast<Receiver&&>(receiver)};
    }
  };

} // namespace sio::event_loop::iouring
