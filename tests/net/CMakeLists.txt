set(SIO_TEST_NET_SOURCES
  test_async_accept.cpp
  test_can_endpoint.cpp
  # TODO(andrphi): Need make sure test env does have CAN interface
  # test_can_socket.cpp
  test_address.cpp
  test_endpoint.cpp
  test_resolve.cpp
  test_socket_handle.cpp
  test_unix_socket.cpp
)

sio_add_test_target(sio_tests_net ${SIO_TEST_NET_SOURCES})
