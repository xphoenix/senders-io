#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace sio::event_loop::epoll {
  struct open_data {
    std::filesystem::path path_{};
    int dirfd_{AT_FDCWD};
    int flags_{0};
    ::mode_t mode_{0};
  };

  template <class Receiver, class State>
  class file_open_operation {
   public:
    file_open_operation(
      context& ctx,
      open_data data,
      async::mode mode,
      async::creation creation,
      async::caching caching,
      Receiver&& rcvr) noexcept(std::is_nothrow_move_constructible_v<Receiver>)
      : context_{ctx}
      , data_{static_cast<open_data&&>(data)}
      , mode_{mode}
      , creation_{creation}
      , caching_{caching}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    friend void tag_invoke(stdexec::start_t, file_open_operation& op) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(op.receiver_));
        return;
      }

      int fd = ::openat(op.data_.dirfd_, op.data_.path_.c_str(), op.data_.flags_, op.data_.mode_);
      if (fd == -1) {
        auto ec = std::error_code(errno, std::system_category());
        stdexec::set_error(std::move(op.receiver_), std::move(ec));
        return;
      }

      State state{op.context_, fd, op.mode_, op.creation_, op.caching_};
      stdexec::set_value(std::move(op.receiver_), std::move(state));
    }

   private:
    context& context_;
    open_data data_;
    async::mode mode_;
    async::creation creation_;
    async::caching caching_;
    Receiver receiver_;
  };

  template <class State>
  struct file_open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(State),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    open_data data_{};
    async::mode mode_{async::mode::read};
    async::creation creation_{async::creation::open_existing};
    async::caching caching_{async::caching::unchanged};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return file_open_operation<Receiver, State>{
        *context_,
        open_data{data_},
        mode_,
        creation_,
        caching_,
        static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };
} // namespace sio::event_loop::epoll

