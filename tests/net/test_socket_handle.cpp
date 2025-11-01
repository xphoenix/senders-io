#include "sio/async_resource.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/ip/tcp.hpp"
#include "backend_matrix.hpp"

#include <catch2/catch_all.hpp>

#include <exec/finally.hpp>
#include <exec/timed_scheduler.hpp>
#include <stdexec/execution.hpp>

#include <chrono>
#include <string>

using namespace stdexec;

TEST_CASE("socket_handle - Open a socket", "[socket_handle]") {
  sio::test::for_each_backend([]<class Backend>() {
    if constexpr (!Backend::available) {
      SUCCEED();
      return;
    }

    DYNAMIC_SECTION(std::string(Backend::name)) {
      using loop_type = typename Backend::loop_type;
      if constexpr (sio::event_loop::socket_loop_for<loop_type, sio::ip::tcp>) {
        loop_type context{};
        sio::event_loop::socket<loop_type, sio::ip::tcp> socket{&context, sio::ip::tcp::v4()};

        Backend::sync_wait(
          context,
          let_value(
            socket.open(),
            [](auto&& handle) {
              CHECK(handle.protocol().type() == SOCK_STREAM);
              return sio::async::close(handle);
            }));
      } else {
        SUCCEED();
      }
    }
  });
}

TEST_CASE("acceptor_handle - Open and close", "[socket_handle][acceptor]") {
  sio::test::for_each_backend([]<class Backend>() {
    if constexpr (!Backend::available) {
      SUCCEED();
      return;
    }

    DYNAMIC_SECTION(std::string(Backend::name)) {
      using loop_type = typename Backend::loop_type;
      if constexpr (sio::event_loop::socket_loop_for<loop_type, sio::ip::tcp>) {
        loop_type context{};
        const sio::ip::endpoint endpoint{sio::ip::address_v4::loopback(), 4242};

        sio::event_loop::acceptor<loop_type, sio::ip::tcp> acceptor{
          &context, sio::ip::tcp::v4(), endpoint};

        Backend::sync_wait(
          context,
          let_value(
            acceptor.open(),
            [](auto handle) {
              return handle.close();
            }));
      } else {
        SUCCEED();
      }
    }
  });
}
