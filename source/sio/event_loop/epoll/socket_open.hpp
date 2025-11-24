#pragma once

#include "details.hpp"

#include <stdexec/execution.hpp>

#include <cerrno>
#include <fcntl.h>
#include <system_error>
#include <utility>
#include <new>

#include <unistd.h>

namespace sio::event_loop::epoll {
  inline void set_non_blocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      throw std::system_error(errno, std::system_category());
    }
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
      throw std::system_error(errno, std::system_category());
    }
  }

  template <class Protocol, class Receiver>
  class socket_open_operation {
   public:
    socket_open_operation(context& ctx, Protocol protocol, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : context_{ctx}
      , protocol_{std::move(protocol)}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    void start() noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(receiver_));
        return;
      }

      int type = protocol_.type() | SOCK_CLOEXEC;
      int fd = ::socket(protocol_.family(), type | SOCK_NONBLOCK, protocol_.protocol());
      if (fd == -1 && errno == EINVAL) {
        fd = ::socket(protocol_.family(), type, protocol_.protocol());
        if (fd != -1) {
          try {
            set_non_blocking(fd);
          } catch (const std::system_error& err) {
            ::close(fd);
            stdexec::set_error(std::move(receiver_), err.code());
            return;
          }
        }
      }

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

      stdexec::set_value(std::move(receiver_), socket_state<Protocol>{token});
    }

   private:
    context& context_;
    Protocol protocol_;
    Receiver receiver_;
  };

  template <class Protocol>
  struct socket_open_sender {
    using sender_concept = stdexec::sender_t;

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(socket_state<Protocol>),
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    context* context_{nullptr};
    Protocol protocol_{};

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return socket_open_operation<Protocol, Receiver>{
        *context_, protocol_, static_cast<Receiver&&>(receiver)};
    }

    env get_env() const noexcept {
      return {context_->get_scheduler()};
    }
  };
} // namespace sio::event_loop::epoll
