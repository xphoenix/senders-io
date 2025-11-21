#pragma once

#include "./concepts.hpp"

#include "../net_concepts.hpp"

#include <stdexec/execution.hpp>

#include <type_traits>
#include <utility>

namespace sio::event_loop {
  template <class Loop, class Protocol>
    requires socket_loop_for<Loop, Protocol>
  struct socket_handle {
    using loop_type = std::remove_cvref_t<Loop>;
    using protocol_type = Protocol;
    using state_type = socket_state_t<loop_type, Protocol>;
    using buffer_type = sio::mutable_buffer;
    using buffers_type = sio::mutable_buffer_span;
    using const_buffer_type = sio::const_buffer;
    using const_buffers_type = sio::const_buffer_span;
    using native_handle_type = typename loop_type::native_handle_type;

    loop_type* context_{nullptr};
    state_type state_{};

    socket_handle() = default;

    socket_handle(loop_type& context, state_type&& state) noexcept
      : context_{&context}
      , state_{static_cast<state_type&&>(state)}
    {
    }

    loop_type& context() const noexcept {
      SIO_ASSERT(context_ != nullptr);
      return *context_;
    }

    bool is_open() const noexcept {
      return state_.is_valid();
    }

    native_handle_type native_handle() const noexcept {
      return state().native_handle();
    }

    auto close() const noexcept {
      return context().close(const_cast<state_type&>(state()));
    }

    auto connect(typename Protocol::endpoint endpoint) const {
      return context().connect(const_cast<state_type&>(state()), endpoint);
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
      SIO_ASSERT(is_open());
      return state_;
    }

    const state_type& state() const noexcept {
      SIO_ASSERT(is_open());
      return state_;
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
           | ::stdexec::then([pc = &context_](state_type&& state) {
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
    using native_handle_type = typename loop_type::native_handle_type;

    loop_type* context_{nullptr};
    typename Protocol::endpoint endpoint_{};
    mutable state_type state_{};

    acceptor_handle() = default;

    acceptor_handle(
      loop_type& context,
      state_type&& state,
      typename Protocol::endpoint endpoint) noexcept
      : context_{&context}
      , endpoint_{endpoint}
      , state_{static_cast<state_type&&>(state)} {
    }

    loop_type& context() const noexcept {
      SIO_ASSERT(context_ != nullptr);
      return *context_;
    }

    auto accept_once() const {
      return ::stdexec::then(
        context().accept_once(const_cast<state_type&>(state())),
        [this](socket_state_t<loop_type, protocol_type>&& state) {
          return socket_handle<loop_type, protocol_type>{
            context(), static_cast<socket_state_t<loop_type, protocol_type>&&>(state)};
        });
    }

    auto close() const noexcept {
      return context().close(const_cast<state_type&>(state()));
    }

    native_handle_type native_handle() const noexcept {
      return state().native_handle();
    }

   private:
    friend loop_type;

    state_type& state() noexcept {
      SIO_ASSERT(state_.is_valid());
      return state_;
    }

    const state_type& state() const noexcept {
      SIO_ASSERT(state_.is_valid());
      return state_;
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
           | ::stdexec::then([pc = &context_, ep = endpoint_](state_type&& state) {
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
