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
#include "sio/event_loop/socket_handle.hpp"
#include "sio/event_loop/stdexec/backend.hpp"
#include "sio/ip/address.hpp"
#include "sio/ip/endpoint.hpp"
#include "sio/ip/tcp.hpp"
#include "sio/sequence/ignore_all.hpp"
#include "sio/sequence/let_value_each.hpp"
#include "sio/net_concepts.hpp"

#include "common/test_receiver.hpp"

#include <catch2/catch_all.hpp>
#include <sys/socket.h>

#include <exec/finally.hpp>
#include <exec/sequence_senders.hpp>
#include <exec/when_any.hpp>

#include <utility>


using namespace sio;
 using backend = sio::event_loop::stdexec_backend::backend;

template <stdexec::sender Sender>
void sync_wait(backend& context, Sender&& sender) {
  stdexec::sync_wait(exec::when_any(std::forward<Sender>(sender), context.run(exec::until::stopped)));
}

TEST_CASE("async_accept concept", "[async_accept]") {
  using handle = sio::event_loop::acceptor_handle<backend, ip::tcp>;
  using sequence = decltype(sio::async::accept(std::declval<handle&>()));
  STATIC_REQUIRE(exec::sequence_sender<sequence, stdexec::env<>>);
  SUCCEED();
}

TEST_CASE("async_accept should work", "[async_accept]") {
  backend ctx;

  sio::event_loop::acceptor<backend, ip::tcp> acceptor{
    &ctx, ip::tcp::v4(), ip::endpoint{ip::address_v4::any(), 2080}};
  stdexec::sender auto accept = stdexec::let_value(
    acceptor.open(),
    [](auto&& acceptor_handle) mutable {
      auto sequence = sio::async::accept(acceptor_handle)
                    | let_value_each([](auto client) {
                        return sio::async::close(client);
                      })
                    | sio::ignore_all();
      auto close_sender = acceptor_handle.close();
      return exec::finally(std::move(sequence), std::move(close_sender));
    });

  sio::event_loop::socket<backend, ip::tcp> sock{&ctx, ip::tcp::v4()};
  stdexec::sender auto connect = stdexec::let_value(
    sock.open(),
    [](auto&& client) {
      auto connect_sender = sio::async::connect(client, ip::endpoint{ip::address_v4::loopback(), 2080});
      auto close_sender = sio::async::close(client);
      return exec::finally(std::move(connect_sender), std::move(close_sender));
    });

  ::sync_wait(ctx, exec::when_any(accept, connect));
}
