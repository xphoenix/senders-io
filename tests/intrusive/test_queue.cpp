#include "sio/intrusive/queue.hpp"

#include <catch2/catch_all.hpp>

#include <vector>

namespace {
  struct list_node {
    int value;
    list_node* next = nullptr;
  };

  using node_queue = sio::intrusive::queue<&list_node::next>;
} // namespace

TEST_CASE("intrusive queue maintains FIFO ordering", "[intrusive][queue]") {
  node_queue q;
  list_node n1{1};
  list_node n2{2};
  list_node n3{3};
  list_node n4{4};

  q.push_back(&n1);
  q.push_back(&n2);
  q.push_back(&n3);

  auto* first = q.pop_front();
  REQUIRE(first == &n1);
  REQUIRE(first->next == nullptr);
  q.push_front(first);
  REQUIRE(q.front() == &n1);

  node_queue tail;
  tail.push_back(&n4);
  q.append(std::move(tail));
  REQUIRE(q.back()->value == 4);

  node_queue prefix;
  list_node n0{0};
  prefix.push_back(&n0);
  q.prepend(std::move(prefix));
  std::vector<int> values;
  while (!q.empty()) {
    values.push_back(q.pop_front()->value);
  }
  CHECK(values == std::vector<int>{0, 1, 2, 3, 4});

  n0.next = &n1;
  n1.next = nullptr;
  auto reversed = node_queue::make_reversed(&n0);
  REQUIRE(reversed.pop_front() == &n1);
  REQUIRE(reversed.pop_front() == &n0);
  CHECK(reversed.empty());
}

TEST_CASE("intrusive queue move operations transfer ownership", "[intrusive][queue]") {
  node_queue source;
  list_node n1{1};
  list_node n2{2};
  source.push_back(&n1);
  source.push_back(&n2);

  node_queue moved{std::move(source)};
  REQUIRE(source.empty());
  REQUIRE(moved.front() == &n1);
  REQUIRE(moved.back() == &n2);
  REQUIRE(n1.next == &n2);
  REQUIRE(n2.next == nullptr);

  node_queue dest;
  list_node n3{3};
  dest.push_back(&n3);
  dest = std::move(moved);
  REQUIRE(moved.empty());
  REQUIRE(dest.front() == &n1);
  REQUIRE(dest.back() == &n2);
  REQUIRE(n3.next == nullptr);
}

TEST_CASE("intrusive queue edge cases exercise all branches", "[intrusive][queue]") {
  SECTION("pop_front and push_front handle empty queues") {
    node_queue q;
    CHECK(q.pop_front() == nullptr);

    list_node n1{1};
    q.push_back(&n1);
    auto* popped = q.pop_front();
    REQUIRE(popped == &n1);
    CHECK(q.empty());

    q.push_front(&n1);
    REQUIRE(q.front() == &n1);
    REQUIRE(q.back() == &n1);
  }

  SECTION("append handles empty sources and destinations") {
    node_queue q;
    node_queue empty_tail;
    q.append(std::move(empty_tail));
    CHECK(q.empty());

    list_node n1{1};
    list_node n2{2};
    node_queue suffix;
    suffix.push_back(&n1);
    suffix.push_back(&n2);
    q.append(std::move(suffix));
    CHECK(q.front() == &n1);
    CHECK(q.back() == &n2);

    list_node n3{3};
    node_queue more;
    more.push_back(&n3);
    q.append(std::move(more));
    CHECK(q.back() == &n3);
  }

  SECTION("prepend handles empty sources and empty queues") {
    node_queue q;
    node_queue empty_prefix;
    q.prepend(std::move(empty_prefix));
    CHECK(q.empty());

    list_node n1{1};
    node_queue prefix;
    prefix.push_back(&n1);
    q.prepend(std::move(prefix));
    CHECK(q.front() == &n1);
    CHECK(q.back() == &n1);

    list_node n2{2};
    node_queue more_prefix;
    more_prefix.push_back(&n2);
    q.prepend(std::move(more_prefix));
    CHECK(q.front() == &n2);
  }

  SECTION("make_reversed handles null input and longer chains") {
    auto empty = node_queue::make_reversed(nullptr);
    CHECK(empty.empty());

    list_node n1{1};
    list_node n2{2};
    list_node n3{3};
    n1.next = &n2;
    n2.next = &n3;
    n3.next = nullptr;

    auto reversed = node_queue::make_reversed(&n1);
    REQUIRE(reversed.pop_front() == &n3);
    REQUIRE(reversed.pop_front() == &n2);
    REQUIRE(reversed.pop_front() == &n1);
    CHECK(reversed.empty());
  }
}
