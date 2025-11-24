#pragma once

#include "context.hpp"

#include <stdexec/execution.hpp>

#include <atomic>
#include <optional>
#include <system_error>
#include <utility>

namespace sio::event_loop::epoll {
  class scheduler {
   public:
    scheduler() = default;

    explicit scheduler(context* ctx) noexcept
      : context_{ctx} {
    }

    context& ctx() const noexcept {
      return *context_;
    }

    friend bool operator==(scheduler lhs, scheduler rhs) noexcept {
      return lhs.context_ == rhs.context_;
    }

    friend bool operator!=(scheduler lhs, scheduler rhs) noexcept {
      return !(lhs == rhs);
    }

    auto schedule() const noexcept;

    auto get_completion_scheduler(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
      -> scheduler {
      return *this;
    }

   private:
    context* context_{nullptr};
  };

  namespace detail {
    template <class Receiver>
    class schedule_operation : public runnable_base {
     public:
      schedule_operation(context& ctx, Receiver&& rcvr) noexcept(
        std::is_nothrow_move_constructible_v<Receiver>)
        : runnable_base{&schedule_operation::invoke}
        , context_{ctx}
        , receiver_(static_cast<Receiver&&>(rcvr)) {
      }

      schedule_operation(schedule_operation&&) = delete;
      schedule_operation(const schedule_operation&) = delete;
      schedule_operation& operator=(schedule_operation&&) = delete;
      schedule_operation& operator=(const schedule_operation&) = delete;

      void start() noexcept {
        auto stop_token = stdexec::get_stop_token(stdexec::get_env(receiver_));
        if (stop_token.stop_requested()) {
          stdexec::set_stopped(std::move(receiver_));
          return;
        }

        if constexpr (stdexec::stoppable_token<decltype(stop_token)>) {
          if (stop_token.stop_possible()) {
            stop_callback_.emplace(stop_token, on_stop{this});
          }
        }

        context_.enqueue_task(*this);
      }

     private:
      struct on_stop {
        schedule_operation* self;

        void operator()() const noexcept {
          self->cancelled_.store(true, std::memory_order_release);
          self->context_.wake();
        }
      };

      static void invoke(runnable_base& base) noexcept {
        auto& self = static_cast<schedule_operation&>(base);
        self.stop_callback_.reset();
        if (self.cancelled_.load(std::memory_order_acquire)) {
          stdexec::set_stopped(std::move(self.receiver_));
        } else {
          stdexec::set_value(std::move(self.receiver_));
        }
      }

      context& context_;
      std::atomic<bool> cancelled_{false};
      Receiver receiver_;
      using stop_token_type = stdexec::stop_token_of_t<stdexec::env_of_t<Receiver&>>;
      using callback_type = stdexec::stop_callback_for_t<stop_token_type, on_stop>;
      std::optional<callback_type> stop_callback_{};
    };

    struct schedule_env {
      scheduler sched;

      auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept -> scheduler {
        return sched;
      }
    };

    struct schedule_sender {
      using sender_concept = stdexec::sender_t;

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_stopped_t()>;

      context* context_{nullptr};

      template <stdexec::receiver Receiver>
      auto connect(Receiver receiver) const {
        return schedule_operation<Receiver>{*context_, static_cast<Receiver&&>(receiver)};
      }

      auto get_env() const noexcept -> schedule_env {
        return schedule_env{scheduler{context_}};
      }
    };
  } // namespace detail

  inline auto scheduler::schedule() const noexcept {
    return detail::schedule_sender{context_};
  }

  inline scheduler context::get_scheduler() noexcept {
    return scheduler{this};
  }
} // namespace sio::event_loop::epoll
