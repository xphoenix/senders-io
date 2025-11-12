#pragma once

#include "./concepts.hpp"

#include "../net_concepts.hpp"

#include <stdexec/execution.hpp>

#include <optional>
#include <type_traits>
#include <utility>

namespace sio::event_loop {
  template <class Loop, class Protocol>
    requires socket_loop_for<Loop, Protocol>
  struct socket_handle {
    using loop_type = std::remove_cvref_t<Loop>;
    using protocol_type = Protocol;
    using state_type = socket_state_t<loop_type, Protocol>;
    using buffer_type = typename state_type::buffer_type;
    using buffers_type = typename state_type::buffers_type;
    using const_buffer_type = typename state_type::const_buffer_type;
    using const_buffers_type = typename state_type::const_buffers_type;

    loop_type* context_{nullptr};
    std::optional<state_type> state_;

    socket_handle() = default;

    socket_handle(loop_type& context, state_type state) noexcept
      : context_{&context}
      , state_{std::in_place, static_cast<state_type&&>(state)}
    {
    }

    loop_type& context() const noexcept {
      return *context_;
    }

    const protocol_type& protocol() const noexcept {
      return state_->protocol();
    }

    auto close() const noexcept {
      return context().close_socket(const_cast<state_type&>(state()));
    }

    auto connect(typename Protocol::endpoint endpoint) const {
      return context().connect_socket(const_cast<state_type&>(state()), endpoint);
    }

    auto bind(typename Protocol::endpoint endpoint) const {
      return context().bind(const_cast<state_type&>(state()), endpoint);
    }

    auto read_some(buffers_type buffers) const {
      return context().read_some(const_cast<state_type&>(state()), buffers);
    }

    auto read_some(buffer_type buffer) const {
      return context().read_some(const_cast<state_type&>(state()), buffer);
    }

    auto write_some(const_buffers_type buffers) const {
      return context().write_some(const_cast<state_type&>(state()), buffers);
    }

    auto write_some(const_buffer_type buffer) const {
      return context().write_some(const_cast<state_type&>(state()), buffer);
    }

    auto write(const_buffers_type buffers) const {
      return context().write(const_cast<state_type&>(state()), buffers);
    }

    auto write(const_buffer_type buffer) const {
      return context().write(const_cast<state_type&>(state()), buffer);
    }

   private:
    friend loop_type;

    state_type& state() noexcept {
      return *state_;
    }

    const state_type& state() const noexcept {
      return *state_;
    }
  };

  template <class Loop, class Protocol>
    requires socket_loop_for<Loop, Protocol>
  struct socket {
    using loop_type = std::remove_cvref_t<Loop>;
    using protocol_type = Protocol;
    using state_type = socket_state_t<loop_type, Protocol>;

    loop_type& context_;
    protocol_type protocol_;

    explicit socket(loop_type& context, protocol_type protocol = protocol_type()) noexcept
      : context_{context}
      , protocol_{protocol} {
    }

    explicit socket(loop_type* context, protocol_type protocol = protocol_type()) noexcept
      : context_{*context}
      , protocol_{protocol} {
    }

    auto open() noexcept {
      return context_.open_socket(protocol_)
           // once state constructed handle could be made
           | ::stdexec::then([pc = &context_](state_type state) {
               return socket_handle<loop_type, protocol_type>{*pc, static_cast<state_type&&>(state)};
             });
    }
  };

  template <class Loop, class Protocol>
    requires socket_loop_for<Loop, Protocol>
  struct acceptor_handle {
    using loop_type = std::remove_cvref_t<Loop>;
    using protocol_type = Protocol;
    using state_type = acceptor_state_t<loop_type, Protocol>;

    loop_type* context_{nullptr};
    typename Protocol::endpoint endpoint_{};
    mutable std::optional<state_type> state_;

    acceptor_handle() = default;

    acceptor_handle(
      loop_type& context,
      state_type state,
      typename Protocol::endpoint endpoint) noexcept
      : context_{&context}
      , endpoint_{endpoint}
      , state_{std::in_place, static_cast<state_type&&>(state)} {
    }

    loop_type& context() const noexcept {
      return *context_;
    }

    const protocol_type& protocol() const noexcept {
      return state().protocol();
    }

    auto accept_once() const {
      return ::stdexec::then(
        context().accept_once(const_cast<state_type&>(state())),
        [this](socket_state_t<loop_type, protocol_type> state) {
          return socket_handle<loop_type, protocol_type>{
            context(), static_cast<socket_state_t<loop_type, protocol_type>&&>(state)};
        });
    }

    auto close() const noexcept {
      return context().close_acceptor(const_cast<state_type&>(state()));
    }

   private:
    friend loop_type;

    state_type& state() noexcept {
      return *state_;
    }

    const state_type& state() const noexcept {
      return *state_;
    }
  };

  template <class Loop, class Protocol>
    requires socket_loop_for<Loop, Protocol>
  struct acceptor {
    using loop_type = std::remove_cvref_t<Loop>;
    using protocol_type = Protocol;
    using state_type = acceptor_state_t<loop_type, Protocol>;

    loop_type& context_;
    protocol_type protocol_;
    typename Protocol::endpoint endpoint_;

    explicit acceptor(
      loop_type& context,
      protocol_type protocol,
      typename Protocol::endpoint endpoint) noexcept
      : context_{context}
      , protocol_{protocol}
      , endpoint_{endpoint} {
    }

    explicit acceptor(
      loop_type* context,
      protocol_type protocol,
      typename Protocol::endpoint endpoint) noexcept
      : context_{*context}
      , protocol_{protocol}
      , endpoint_{endpoint} {
    }

    auto open() noexcept {
      return context_.open_acceptor(protocol_, endpoint_)
           | ::stdexec::then([pc = &context_, ep = endpoint_](state_type state) {
               return acceptor_handle<loop_type, protocol_type>{*pc, static_cast<state_type&&>(state), ep};
             });
    }
  };

  template <class Loop, class Protocol>
    requires socket_loop_for<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>
  socket(Loop& context, Protocol protocol)
    -> socket<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>;

  template <class Loop, class Protocol>
    requires socket_loop_for<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>
  socket(Loop* context, Protocol protocol)
    -> socket<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>;

  template <class Loop, class Protocol>
    requires socket_loop_for<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>
  acceptor(
    Loop& context,
    Protocol protocol,
    typename std::remove_cvref_t<Protocol>::endpoint endpoint)
    -> acceptor<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>;

  template <class Loop, class Protocol>
    requires socket_loop_for<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>
  acceptor(
    Loop* context,
    Protocol protocol,
    typename std::remove_cvref_t<Protocol>::endpoint endpoint)
    -> acceptor<std::remove_reference_t<Loop>, std::remove_cvref_t<Protocol>>;

} // namespace sio::event_loop
