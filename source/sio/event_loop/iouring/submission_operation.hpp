#pragma once

#include "details.hpp"
#include "sio/event_loop/iouring/scheduler.hpp"

#include <stdexec/execution.hpp>

#include <optional>
#include <system_error>
#include <type_traits>
#include <utility>

namespace sio::event_loop::iouring {

  struct env {
    scheduler loop_scheduler_;

    auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
      -> scheduler {
      return loop_scheduler_;
    }
  };

  template <
    class StopToken,
    class Callback,
    bool Enable = stdexec::stoppable_token_for<StopToken, Callback>>
  struct stop_callback_storage;

  template <class StopToken, class Callback>
  struct stop_callback_storage<StopToken, Callback, true> {
    using callback_type = stdexec::stop_callback_for_t<StopToken, Callback>;

    void emplace(const StopToken& token, Callback cb) noexcept(
      std::is_nothrow_constructible_v<callback_type, const StopToken&, Callback>) {
      callback_.emplace(token, std::move(cb));
    }

    void reset() noexcept {
      callback_.reset();
    }

   private:
    std::optional<callback_type> callback_{};
  };

  template <class StopToken, class Callback>
  struct stop_callback_storage<StopToken, Callback, false> {
    void emplace(const StopToken&, Callback) noexcept {
    }

    void reset() noexcept {
    }
  };

  template <class Derived, class Receiver>
  class submission_operation : public completion_base {
   protected:
    using receiver_type = Receiver;
    using env_type = stdexec::env_of_t<receiver_type&>;
    using stop_token_type = stdexec::stop_token_of_t<env_type>;

    struct on_stop {
      Derived* self;

      void operator()() noexcept {
        self->on_stop_requested();
      }
    };

    using stop_storage = stop_callback_storage<stop_token_type, on_stop>;

    submission_operation(io_context& ctx, Receiver&& rcvr) noexcept(
      std::is_nothrow_move_constructible_v<Receiver>)
      : completion_base{ctx, &submission_operation::dispatch}
      , receiver_(static_cast<Receiver&&>(rcvr)) {
    }

    ~submission_operation() = default;

    Derived& derived() noexcept {
      return static_cast<Derived&>(*this);
    }

    void on_stop_requested() noexcept {
      this->request_cancel();
    }

    void reset_stop_callback() noexcept {
      stop_callback_.reset();
    }

    Receiver& receiver() & noexcept {
      return receiver_;
    }

    Receiver&& receiver() && noexcept {
      return static_cast<Receiver&&>(receiver_);
    }

    static void dispatch(completion_base* base, const ::io_uring_cqe& cqe) noexcept {
      auto& self = static_cast<Derived&>(*base);
      self.reset_stop_callback();
      self.derived().on_completion(cqe);
    }

   public:
    friend void tag_invoke(stdexec::start_t, Derived& self) noexcept {
      auto stop_token = stdexec::get_stop_token(stdexec::get_env(self.receiver()));
      if (stop_token.stop_requested()) {
        stdexec::set_stopped(std::move(self).receiver());
        return;
      }
      try {
        if constexpr (stdexec::stoppable_token<stop_token_type>) {
          if (stop_token.stop_possible()) {
            self.stop_callback_.emplace(stop_token, on_stop{&self});
          }
        }
        self.context.with_submission_queue([&](::io_uring_sqe& sqe) {
          self.derived().prepare_submission(sqe);
          self.context.register_completion(self, sqe);
        });
      } catch (const std::system_error& err) {
        self.reset_stop_callback();
        auto ec = err.code();
        stdexec::set_error(std::move(self).receiver(), std::move(ec));
      }
    }

   private:
    stop_storage stop_callback_{};
    Receiver receiver_;
  };

} // namespace sio::event_loop::iouring
