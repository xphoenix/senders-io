set(SIO_TEST_FILE_SOURCES
  test_file_handle.cpp
  test_read_batched.cpp
)

sio_add_test_target(sio_tests_file ${SIO_TEST_FILE_SOURCES})
