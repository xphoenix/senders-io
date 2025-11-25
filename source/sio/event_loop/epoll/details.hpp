#pragma once

#include "context.hpp"
#include "scheduler.hpp"

#include "../../async_resource.hpp"
#include "../../const_buffer.hpp"
#include "../../const_buffer_span.hpp"
#include "../../mutable_buffer.hpp"
#include "../../mutable_buffer_span.hpp"

#include <stdexec/execution.hpp>

#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

#include <sys/socket.h>

namespace sio::event_loop::epoll {
  struct env {
    scheduler loop_scheduler_;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept -> scheduler {
      return loop_scheduler_;
    }
  };

  using file_state = descriptor_token;
  using seekable_file_state = descriptor_token;

  template <class Protocol>
  struct socket_state : descriptor_token {
    using descriptor_token::descriptor_token;
    using descriptor_token::operator=;

    socket_state() = default;

    socket_state(descriptor_token token) noexcept
      : descriptor_token{token} {
    }

    bool unlink_on_close{false};
    std::string unix_path{};
  };

  template <class Protocol>
  struct acceptor_state : descriptor_token {
    using descriptor_token::descriptor_token;
    using descriptor_token::operator=;

    acceptor_state() = default;

    acceptor_state(descriptor_token token) noexcept
      : descriptor_token{token} {
    }

    bool unlink_on_close{false};
    std::string unix_path{};
  };

  template <class Derived, class Receiver>
  struct fd_operation_base : public fd_operation_base_common {
    using env_type = stdexec::env_of_t<Receiver&>;
    using stop_token_type = stdexec::stop_token_of_t<env_type>;

    struct on_stop {
      fd_operation_base* self;

      void operator()() noexcept {
        self->request_stop();
      }
    };

    using callback_type = stdexec::stop_callback_for_t<stop_token_type, on_stop>;

    fd_operation_base(class context& ctx, descriptor_token token, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : fd_operation_base_common{ctx, token, &fd_operation_base::dispatch}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    fd_operation_base(fd_operation_base&&) = delete;
    fd_operation_base(const fd_operation_base&) = delete;
    fd_operation_base& operator=(fd_operation_base&&) = delete;
    fd_operation_base& operator=(const fd_operation_base&) = delete;

    void start() noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(receiver_));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(receiver_));
        return;
      }
      if constexpr (stdexec::stoppable_token<stop_token_type>) {
        if (stop_token.stop_possible()) {
          stop_callback_.emplace(stop_token, on_stop{this});
        }
      }
      loop_context().enqueue_task(*this);
    }

    void complete_stopped() noexcept {
      stop_callback_.reset();
      stdexec::set_stopped(std::move(receiver_));
    }

    void complete_error(std::error_code ec) noexcept {
      stop_callback_.reset();
      stdexec::set_error(std::move(receiver_), std::move(ec));
    }

    Receiver& receiver() & noexcept {
      return receiver_;
    }

    Receiver&& receiver() && noexcept {
      return static_cast<Receiver&&>(receiver_);
    }

    void reset_stop_callback() noexcept {
      stop_callback_.reset();
    }

   private:
    static void dispatch(runnable_base& base) noexcept {
      auto& self = static_cast<Derived&>(base);
      self.run_once();
    }

    Receiver receiver_;
    std::optional<callback_type> stop_callback_{};
  };
} // namespace sio::event_loop::epoll
