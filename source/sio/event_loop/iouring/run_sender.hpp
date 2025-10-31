#pragma once

#include "context.hpp"

#include <stdexec/execution.hpp>

#include <system_error>
#include <utility>

namespace sio::event_loop::iouring {
  enum class run_mode {
    stopped,
    drained
  };

  namespace detail {
    template <class Receiver>
    class run_operation {
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

        try {
          if (op.run_loop(stop_token)) {
            stdexec::set_value(std::move(op.receiver_));
          } else {
            stdexec::set_stopped(std::move(op.receiver_));
          }
        } catch (const std::system_error& err) {
          stdexec::set_error(std::move(op.receiver_), err.code());
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
      stdexec::set_error_t(std::error_code),
      stdexec::set_stopped_t()>;

    template <stdexec::receiver Receiver>
    auto connect(Receiver receiver) const {
      return detail::run_operation<Receiver>{*context_, mode_, static_cast<Receiver&&>(receiver)};
    }

    friend auto tag_invoke(stdexec::get_env_t, const run_sender&) noexcept {
      return stdexec::empty_env{};
    }
  };

  inline run_sender io_context::run(run_mode mode) noexcept {
    return run_sender{this, mode};
  }
} // namespace sio::event_loop::iouring
