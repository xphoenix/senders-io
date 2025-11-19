set(SIO_TEST_SEQUENCE_SOURCES
  test_buffered_sequence.cpp
  test_empty_sequence.cpp
  test_finally.cpp
  test_first.cpp
  test_fork.cpp
  test_ignore_all.cpp
  test_iterate.cpp
  test_last.cpp
  test_merge_each.cpp
  test_repeat.cpp
  test_scan.cpp
  test_transform_each.cpp
  test_zip.cpp
)

sio_add_test_target(sio_tests_sequence ${SIO_TEST_SEQUENCE_SOURCES})
