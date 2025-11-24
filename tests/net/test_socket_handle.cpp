#include "sio/async_resource.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/ip/tcp.hpp"
#include "common/backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>
#include <sys/socket.h>
#include <concepts>

using namespace stdexec;

TEMPLATE_LIST_TEST_CASE("socket_handle - Open a socket", "[socket_handle]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, sio::ip::tcp>) {
    SKIP("Backend does not support sio::ip::tcp sockets");
    return;
  }

  loop_type context{};
  sio::event_loop::socket socket{&context, sio::ip::tcp::v4()};
  using native_handle_type = typename loop_type::native_handle_type;
  auto* native_ctx = &context.native_context();

  auto sender = let_value(socket.open(), [native_ctx](auto&& handle) {
    int type{};
    socklen_t length = sizeof(type);
    auto fd = [&]() {
      if constexpr (std::same_as<native_handle_type, int>) {
        return handle.native_handle();
      } else {
        return native_ctx->native_handle(handle.native_handle());
      }
    }();
    REQUIRE(::getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) == 0);
    CHECK(type == SOCK_STREAM);
    return sio::async::close(handle);
  });
  stdexec::sync_wait(exec::when_any(std::move(sender), context.run()));
}

TEMPLATE_LIST_TEST_CASE("acceptor_handle - Open and close", "[socket_handle][acceptor]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, sio::ip::tcp>) {
    SKIP("Backend does not support sio::ip::tcp sockets");
    return;
  }

  loop_type context{};
  const sio::ip::endpoint endpoint{sio::ip::address_v4::loopback(), 4242};
  sio::event_loop::acceptor acceptor{&context, sio::ip::tcp::v4(), endpoint};

  auto sender = let_value(acceptor.open(), [](auto handle) { return handle.close(); });
  stdexec::sync_wait(exec::when_any(std::move(sender), context.run()));
}
