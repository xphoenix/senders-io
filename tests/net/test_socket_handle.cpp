#include "sio/async_resource.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/ip/tcp.hpp"
#include "backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

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

  auto sender = let_value(socket.open(), [](auto&& handle) {
    CHECK(handle.protocol().type() == SOCK_STREAM);
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
