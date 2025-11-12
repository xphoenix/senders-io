#pragma once

#include "scheduler.hpp"

#include <stdexec/execution.hpp>

#include <exception>
#include <optional>
#include <system_error>
#include <utility>

namespace sio::event_loop::iouring {
  namespace detail {
    template <class Receiver>
    class run_operation {
     private:
      struct on_stop {
        io_context& context;

        void operator()() const noexcept {
          context.request_stop();
        }
      };

     public:

      run_operation(io_context& ctx, run_mode mode, Receiver&& rcvr) noexcept
        : context_{ctx}
        , mode_{mode}
        , receiver_{static_cast<Receiver&&>(rcvr)} {
      }

      run_operation(const run_operation&) = delete;
      run_operation(run_operation&&) = delete;
      run_operation& operator=(const run_operation&) = delete;
      run_operation& operator=(run_operation&&) = delete;

      friend void tag_invoke(stdexec::start_t, run_operation& op) noexcept {
        auto stop_token = stdexec::get_stop_token(stdexec::get_env(op.receiver_));
        if (stop_token.stop_requested()) {
          stdexec::set_stopped(std::move(op.receiver_));
          return;
        }

        using stop_token_type = decltype(stop_token);
        using callback_type = stdexec::stop_callback_for_t<stop_token_type, on_stop>;
        std::optional<callback_type> callback{
          std::in_place,
          stop_token,
          on_stop{op.context_}
        };

        bool completed = false;
        try {
          completed = op.run_loop(stop_token);
        } catch (const std::system_error& err) {
          callback.reset();
          stdexec::set_error(std::move(op.receiver_), std::make_exception_ptr(err));
          return;
        } catch (...) {
          callback.reset();
          stdexec::set_error(std::move(op.receiver_), std::current_exception());
          return;
        }

        callback.reset();

        if (!completed) {
          op.context_.run_until_empty();
          stdexec::set_stopped(std::move(op.receiver_));
        } else {
          stdexec::set_value(std::move(op.receiver_));
        }
      }

     private:
      template <class StopToken>
      bool run_loop(const StopToken& token) {
        switch (mode_) {
          case run_mode::stopped:
            while (!context_.stop_requested()) {
              if (token.stop_requested()) {
                return false;
              }
              context_.run_one();
            }
            return true;
          case run_mode::drained:
            while (context_.run_some() != 0) {
              if (token.stop_requested()) {
                return false;
              }
            }
            return true;
        }
        return true;
      }

      io_context& context_;
      run_mode mode_;
      Receiver receiver_;
    };
  } // namespace detail

  struct run_sender {
    using sender_concept = stdexec::sender_t;

    io_context* context_{};
    run_mode mode_{run_mode::stopped};

    using completion_signatures = stdexec::completion_signatures<
      stdexec::set_value_t(),
      stdexec::set_error_t(std::exception_ptr),
      stdexec::set_stopped_t()>;

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return detail::run_operation<Receiver>{*context_, mode_, static_cast<Receiver&&>(receiver)};
    }

    struct run_env {
      io_context* ctx;

      auto query(stdexec::get_completion_scheduler_t<stdexec::set_value_t>) const noexcept
        -> scheduler {
        return ctx->get_scheduler();
      }
    };

    friend auto tag_invoke(stdexec::get_env_t, const run_sender& sndr) noexcept {
      return run_env{sndr.context_};
    }
  };

  inline run_sender io_context::run(run_mode mode) noexcept {
    return run_sender{this, mode};
  }
} // namespace sio::event_loop::iouring
