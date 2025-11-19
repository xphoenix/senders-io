set(SIO_TEST_INTRUSIVE_SOURCES
  test_list.cpp
  test_queue.cpp
  test_heap.cpp)

sio_add_test_target(sio_tests_intrusive ${SIO_TEST_INTRUSIVE_SOURCES})
