#include "sio/async_resource.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/event_loop/stdexec/backend.hpp"
#include "sio/ip/tcp.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/timed_scheduler.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <chrono>

 using backend = sio::event_loop::stdexec_backend::backend;

template <stdexec::sender Sender>
void sync_wait(backend& context, Sender&& sender) {
  stdexec::sync_wait(
    exec::when_any(std::forward<Sender>(sender), context.run(exec::until::stopped)));
}

TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
  backend context{};
  sio::event_loop::socket<backend, sio::ip::tcp> socket{&context, sio::ip::tcp::v4()};

  sync_wait(
    context,
    stdexec::let_value(
      socket.open(),
      [](auto&& handle) {
        CHECK(handle.protocol().type() == SOCK_STREAM);
        return sio::async::close(handle);
      }));
}

TEST_CASE("acceptor_handle - Open and close", "[socket_handle][acceptor]") {
  backend context{};
  const sio::ip::endpoint endpoint{sio::ip::address_v4::loopback(), 4242};

  sio::event_loop::acceptor<backend, sio::ip::tcp> acceptor{&context, sio::ip::tcp::v4(), endpoint};

  sync_wait(
    context,
    stdexec::let_value(
      acceptor.open(),
      [](auto handle) {
        return handle.close();
      }));
}
