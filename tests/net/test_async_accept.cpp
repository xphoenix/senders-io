/*
 * Copyright (c) 2024 Maikel Nadolski
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sio/async_resource.hpp"
#include "sio/event_loop/concepts.hpp"
#include "sio/event_loop/socket_handle.hpp"
#include "sio/ip/address.hpp"
#include "sio/ip/endpoint.hpp"
#include "sio/ip/tcp.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/let_value_each.hpp"
#include "sio/net_concepts.hpp"
#include "common/backend_matrix.hpp"

#include <catch2/catch_all.hpp>
#include <exec/finally.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/when_any.hpp>

#include <utility>
#include <cstring>
#include <sys/socket.h>

using namespace sio;
using namespace stdexec;

namespace {
  template <class Handle>
  ip::endpoint get_local_endpoint(const Handle& handle) {
    ::sockaddr_storage storage{};
    socklen_t len = sizeof(storage);
    int rc = ::getsockname(
      handle.native_handle(),
      reinterpret_cast<::sockaddr*>(&storage),
      &len);
    REQUIRE(rc == 0);

    ip::endpoint endpoint{};
    std::memcpy(endpoint.data(), &storage, static_cast<std::size_t>(len));
    return endpoint;
  }
} // namespace

TEMPLATE_LIST_TEST_CASE("async_accept concept", "[async_accept]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, ip::tcp>) {
    SKIP("Backend does not support ip::tcp sockets");
    return;
  }

  using handle = sio::event_loop::acceptor_handle<loop_type, ip::tcp>;
  using sequence = decltype(sio::async::accept(std::declval<handle&>()));
  STATIC_REQUIRE(exec::sequence_sender<sequence, stdexec::env<>>);
}

TEMPLATE_LIST_TEST_CASE("async_accept should work", "[async_accept]", SIO_TEST_BACKEND_TYPES) {
  using Backend = TestType;
  using loop_type = typename Backend::loop_type;

  CAPTURE(Backend::name);
  if constexpr (!sio::event_loop::socket_loop_for<loop_type, ip::tcp>) {
    SKIP("Backend does not support ip::tcp sockets");
    return;
  }

  loop_type ctx{};

  sio::event_loop::acceptor acceptor{
    &ctx, ip::tcp::v4(), ip::endpoint{ip::address_v4::any(), 0}};
  sio::event_loop::socket sock{&ctx, ip::tcp::v4()};

  auto test_sender = let_value(acceptor.open(), [&](auto& acceptor_handle) mutable {
    auto endpoint = get_local_endpoint(acceptor_handle);
    auto accept_sequence = sio::async::accept(acceptor_handle)
                         | let_value_each([](auto& client) { return sio::async::close(client); })
                         | exec::ignore_all_values();
    auto accept_sender = exec::finally(std::move(accept_sequence), acceptor_handle.close());

    auto connect_sender = let_value(sock.open(), [endpoint](auto& client) {
      auto connect = sio::async::connect(client, endpoint);
      auto close_sender = sio::async::close(client);
      return exec::finally(std::move(connect), std::move(close_sender));
    });

    auto accept_and_connect = exec::when_any(std::move(accept_sender), std::move(connect_sender));
    return exec::when_any(std::move(accept_and_connect), ctx.run());
  });

  stdexec::sync_wait(std::move(test_sender));
}
