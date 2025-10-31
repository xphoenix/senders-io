#pragma once

#include "context.hpp"

#include <stdexec/execution.hpp>

#include <system_error>
#include <utility>

namespace sio::event_loop::iouring {
  class scheduler {
   public:
    scheduler() = default;

    explicit scheduler(io_context* ctx) noexcept
      : context_{ctx} {
    }

    io_context& context() const noexcept {
      return *context_;
    }

    friend bool operator==(scheduler lhs, scheduler rhs) noexcept {
      return lhs.context_ == rhs.context_;
    }

    friend bool operator!=(scheduler lhs, scheduler rhs) noexcept {
      return !(lhs == rhs);
    }

    auto schedule() const noexcept;

   private:
    io_context* context_{nullptr};
  };

  namespace detail {
    template <class Receiver>
    class schedule_operation : public completion_base {
     public:
      schedule_operation(io_context& ctx, Receiver&& rcvr) noexcept
        : completion_base{ctx, &schedule_operation::on_complete}
        , receiver_(static_cast<Receiver&&>(rcvr)) {
      }

      schedule_operation(schedule_operation&&) = delete;
      schedule_operation(const schedule_operation&) = delete;
      schedule_operation& operator=(schedule_operation&&) = delete;
      schedule_operation& operator=(const schedule_operation&) = delete;

      friend void tag_invoke(stdexec::start_t, schedule_operation& op) noexcept {
        auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
        if (stop_token.stop_requested()) {
          stdexec::set_stopped(std::move(op.receiver_));
          return;
        }

        try {
          op.context.with_submission_queue([&](::io_uring_sqe& sqe) {
            ::io_uring_prep_nop(&sqe);
            op.context.register_completion(op, sqe);
          });
        } catch (const std::system_error& err) {
          stdexec::set_error(std::move(op.receiver_), err.code());
        }
      }

      static void on_complete(completion_base* base, const ::io_uring_cqe& cqe) noexcept {
        auto& self = *static_cast<schedule_operation*>(base);
        if (cqe.res < 0 && cqe.res != -ECANCELED) {
          stdexec::set_error(
            std::move(self.receiver_),
            std::error_code(-cqe.res, std::system_category()));
        } else if (self.cancelled.load(std::memory_order_acquire) || cqe.res == -ECANCELED) {
          stdexec::set_stopped(std::move(self.receiver_));
        } else {
          stdexec::set_value(std::move(self.receiver_));
        }
      }

     private:
      Receiver receiver_;
    };

    struct schedule_sender {
      using sender_concept = stdexec::sender_t;

      io_context* context_{};

      using completion_signatures = stdexec::completion_signatures<
        stdexec::set_value_t(),
        stdexec::set_error_t(std::error_code),
        stdexec::set_stopped_t()>;

      template <stdexec::receiver Receiver>
      auto connect(Receiver receiver) const {
        return schedule_operation<Receiver>{*context_, static_cast<Receiver&&>(receiver)};
      }

      friend auto tag_invoke(stdexec::get_env_t, const schedule_sender& sndr) noexcept {
        return stdexec::empty_env{};
      }
    };
  } // namespace detail

  inline auto scheduler::schedule() const noexcept {
    return detail::schedule_sender{context_};
  }

  inline auto get_scheduler(io_context& ctx) noexcept {
    return scheduler{&ctx};
  }

  inline scheduler io_context::get_scheduler() noexcept {
    return scheduler{this};
  }
} // namespace sio::event_loop::iouring
