#include <sio/can/raw_protocol.hpp>
#include <sio/async_resource.hpp>
#include <sio/event_loop/socket_handle.hpp>
#include <sio/event_loop/stdexec/backend.hpp>

#include <sio/sequence/let_value_each.hpp>
#include <sio/sequence/ignore_all.hpp>
#include <sio/sequence/zip.hpp>
#include <exec/when_any.hpp>

#include <catch2/catch_all.hpp>

TEST_CASE("can - Create raw protocol", "[can]") {
  sio::can::raw_protocol protocol{};
  CHECK(protocol.type() == SOCK_RAW);
  CHECK(protocol.protocol() == CAN_RAW);
  CHECK(protocol.family() == PF_CAN);
}

TEST_CASE("can - Create socket and bind it", "[can]") {
  sio::event_loop::stdexec::backend ioc{};
  using namespace sio;
  sio::event_loop::socket<sio::event_loop::stdexec::backend, can::raw_protocol> sock{ioc};
  auto use_socket = zip(async::use(sock), stdexec::just(::can_frame{})) //
                  | let_value_each([](
                                     sio::event_loop::socket_handle<sio::event_loop::stdexec::backend, can::raw_protocol> sock,
                                     ::can_frame& frame) {
                      sock.bind(can::endpoint{5});
                      frame.can_id = ::htons(0x1234);
                      frame.len = 1;
                      frame.data[0] = 0x42;
                      std::span buffer{&frame, 1};
                      return async::write(sock, std::as_bytes(buffer));
                    })
                  | ignore_all();
  stdexec::sync_wait(exec::when_any(use_socket, ioc.run()));
}
