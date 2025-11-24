#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include "../../async_resource.hpp"
#include "../../io_concepts.hpp"

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <utility>
#include <new>

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

    void start() noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(receiver_));
        return;
      }

      int fd = ::openat(data_.dirfd_, data_.path_.c_str(), data_.flags_, data_.mode_);
      if (fd == -1) {
        auto ec = std::error_code(errno, std::system_category());
        stdexec::set_error(std::move(receiver_), std::move(ec));
        return;
      }

      descriptor_token token{};
      try {
        token = context_.register_descriptor(fd);
      } catch (const std::bad_alloc&) {
        ::close(fd);
        auto ec = std::make_error_code(std::errc::not_enough_memory);
        stdexec::set_error(std::move(receiver_), std::move(ec));
        return;
      }

      stdexec::set_value(std::move(receiver_), token);
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
