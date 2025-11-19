#include "sio/intrusive/list.hpp"

#include <catch2/catch_all.hpp>

#include <vector>

namespace {
  struct list_node {
    int value;
    list_node* next = nullptr;
    list_node* prev = nullptr;
  };

  using node_list = sio::intrusive::list<&list_node::next, &list_node::prev>;
} // namespace

TEST_CASE("intrusive list supports basic operations", "[intrusive][list]") {
  node_list lst;
  list_node n1{1};
  list_node n2{2};
  list_node n3{3};

  lst.push_back(&n1);
  lst.push_back(&n2);
  lst.push_back(&n3);

  int expected = 1;
  for (auto& node: lst) {
    CHECK(node.value == expected++);
  }

  auto* front = lst.pop_front();
  REQUIRE(front == &n1);
  REQUIRE(front->next == nullptr);
  REQUIRE(front->prev == nullptr);
  lst.erase(&n2);
  REQUIRE(lst.front() == &n3);

  lst.push_front(front);
  REQUIRE(lst.front() == &n1);
  REQUIRE(lst.back() == &n3);

  node_list suffix;
  list_node n4{4};
  list_node n5{5};
  suffix.push_back(&n4);
  suffix.push_back(&n5);
  lst.append(std::move(suffix));
  std::vector<int> values;
  for (auto& node: lst) {
    values.push_back(node.value);
  }
  CHECK(values == std::vector<int>{1, 3, 4, 5});

  node_list prefix;
  list_node n0{0};
  prefix.push_back(&n0);
  lst.prepend(std::move(prefix));
  values.clear();
  for (auto& node: lst) {
    values.push_back(node.value);
  }
  CHECK(values == std::vector<int>{0, 1, 3, 4, 5});
}

TEST_CASE("intrusive list move operations keep hooks consistent", "[intrusive][list]") {
  node_list source;
  list_node n1{1};
  list_node n2{2};
  source.push_back(&n1);
  source.push_back(&n2);

  node_list moved{std::move(source)};
  REQUIRE(source.empty());
  REQUIRE(moved.front() == &n1);
  REQUIRE(moved.back() == &n2);
  REQUIRE(n1.prev == nullptr);
  REQUIRE(n1.next == &n2);
  REQUIRE(n2.prev == &n1);
  REQUIRE(n2.next == nullptr);

  node_list target;
  list_node n3{3};
  target.push_back(&n3);
  target = std::move(moved);
  REQUIRE(moved.empty());
  REQUIRE(target.front() == &n1);
  REQUIRE(target.back() == &n2);
  REQUIRE(n3.next == nullptr);
  REQUIRE(n3.prev == nullptr);
}

TEST_CASE("intrusive list edge cases are covered", "[intrusive][list]") {
  SECTION("pop_front handles empty and single element lists") {
    node_list lst;
    CHECK(lst.pop_front() == nullptr);

    list_node only{7};
    lst.push_back(&only);
    auto* removed = lst.pop_front();
    REQUIRE(removed == &only);
    CHECK(lst.empty());

    lst.push_front(&only);
    REQUIRE(lst.front() == &only);
    REQUIRE(lst.back() == &only);
    const node_list& const_ref = lst;
    CHECK(const_ref.front() == &only);
    CHECK(const_ref.back() == &only);
  }

  SECTION("erase handles null, head, middle, and tail nodes") {
    node_list lst;
    list_node n1{1};
    list_node n2{2};
    list_node n3{3};
    list_node n4{4};

    lst.erase(nullptr);

    lst.push_back(&n1);
    lst.push_back(&n2);
    lst.push_back(&n3);
    lst.push_back(&n4);

    lst.erase(nullptr);
    lst.erase(&n1);
    CHECK(lst.front() == &n2);
    CHECK(n2.prev == nullptr);

    lst.erase(&n4);
    CHECK(lst.back() == &n3);
    CHECK(n3.next == nullptr);

    lst.push_back(&n4);
    lst.erase(&n3);
    REQUIRE(n2.next == &n4);
    REQUIRE(n4.prev == &n2);
  }

  SECTION("append and prepend handle empty sources and destinations") {
    node_list target;
    node_list empty_suffix;
    target.append(std::move(empty_suffix));
    CHECK(target.empty());

    list_node a{1};
    list_node b{2};
    node_list suffix;
    suffix.push_back(&a);
    suffix.push_back(&b);
    target.append(std::move(suffix));
    CHECK(target.front() == &a);
    CHECK(target.back() == &b);

    node_list empty_prefix;
    target.prepend(std::move(empty_prefix));
    CHECK(target.front() == &a);

    list_node c{3};
    node_list singleton_prefix;
    singleton_prefix.push_back(&c);
    node_list empty_target;
    empty_target.prepend(std::move(singleton_prefix));
    CHECK(empty_target.front() == &c);
    CHECK(empty_target.back() == &c);

    node_list no_prefix;
    empty_target.prepend(std::move(no_prefix));
    CHECK(empty_target.front() == &c);
  }
}
