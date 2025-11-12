#include "backend_matrix.hpp"

#include "sio/async_resource.hpp"
#include "sio/can/raw_protocol.hpp"
#include "sio/event_loop/socket_handle.hpp"

#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/let_value_each.hpp"
#include "sio/sequence/zip.hpp"

#include <catch2/catch_all.hpp>
#include <exec/when_any.hpp>
#include <stdexec/execution.hpp>

#include <span>
#include <arpa/inet.h>

using namespace sio;
using namespace stdexec;

TEST_CASE("can - Create raw protocol", "[can]") {
  sio::can::raw_protocol protocol{};
  CHECK(protocol.type() == SOCK_RAW);
  CHECK(protocol.protocol() == CAN_RAW);
  CHECK(protocol.family() == PF_CAN);
}

TEMPLATE_LIST_TEST_CASE("can - Create socket and bind it", "[can]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, sio::can::raw_protocol>) {
    SKIP("Backend does not support CAN raw sockets");
    return;
  }

  loop_type context{};
  sio::event_loop::socket sock{&context, sio::can::raw_protocol{}};

  auto use_socket = zip(sio::async::use(sock), just(::can_frame{})) //
                  | let_value_each([](auto handle, ::can_frame& frame) {
                      frame.can_id = ::htons(0x1234);
                      frame.len = 1;
                      frame.data[0] = 0x42;
                      sio::bind(handle, can::endpoint{5});
                      return sio::async::write(handle, sio::const_buffer{&frame, sizeof(frame)});
                    })
                  | ignore_all();

  stdexec::sync_wait(exec::when_any(std::move(use_socket), context.run()));
}
