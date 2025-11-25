#include "common/backend_matrix.hpp"

#include "sio/async_resource.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/local/socket_options.hpp"
#include "sio/local/stream_protocol.hpp"

#include <catch2/catch_all.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <filesystem>
#include <string>
#include <string_view>

using namespace stdexec;

TEMPLATE_LIST_TEST_CASE("local stream sockets unlink on close", "[unix][local]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, sio::local::stream_protocol>) {
    SKIP("Backend does not support Unix domain stream sockets");
    return;
  }

  auto socket_path = std::filesystem::temp_directory_path() / "sio_unix_socket_test.sock";
  std::error_code remove_ec;
  std::filesystem::remove(socket_path, remove_ec);
  (void)remove_ec;

  loop_type context{};
  sio::local::endpoint endpoint{socket_path.string()};
  sio::event_loop::acceptor acceptor{&context, sio::local::stream_protocol{}, endpoint};
  sio::event_loop::socket client{&context, sio::local::stream_protocol{}};

  auto server = stdexec::let_value(
    acceptor.open(sio::local::socket_options{.unlink_on_close = true}),
    [&](auto acc_handle) {
      return stdexec::let_value(
        acc_handle.accept_once(),
        [acc_handle](auto peer) mutable {
          auto buf = std::make_shared<std::array<char, 5>>();
          auto read = sio::async::read_some(peer, sio::mutable_buffer{buf->data(), buf->size()});
          return stdexec::let_value(
            std::move(read),
            [buf, peer, acc_handle](std::size_t n) mutable {
              auto cleanup = stdexec::when_all(sio::async::close(peer), acc_handle.close());
              return stdexec::then(std::move(cleanup), [buf, n](auto&&...) mutable {
                return std::string(buf->data(), n);
              });
            });
        });
    });

  auto client_send = stdexec::let_value(
    client.open(),
    [&](auto client_handle) {
      return sio::async::connect(client_handle, endpoint)
           | stdexec::let_value([client_handle]() mutable {
               constexpr std::string_view msg = "hello";
               return sio::async::write(client_handle, sio::const_buffer{msg.data(), msg.size()});
             })
           | stdexec::let_value([client_handle](std::size_t) mutable {
               return sio::async::close(client_handle);
             })
           | stdexec::then([] { return true; });
    });

  auto work = stdexec::when_all(std::move(server), std::move(client_send))
            | stdexec::then([&](std::string server_msg, bool) {
                context.request_stop();
                return server_msg;
              });

  auto run = stdexec::let_stopped(context.run(), [] { return stdexec::just(); });
  auto result = stdexec::sync_wait(stdexec::when_all(std::move(work), std::move(run)));
  REQUIRE(result);
  const auto& server_msg = std::get<0>(*result);
  CHECK(server_msg == "hello");
  CHECK_FALSE(std::filesystem::exists(socket_path));
}
