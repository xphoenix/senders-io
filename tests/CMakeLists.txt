cmake_minimum_required(VERSION 3.22)

project(senders-io_tests CXX)
include(CTest)

if (PROJECT_IS_TOP_LEVEL)
  find_package(sio REQUIRED)
elseif (NOT TARGET sio::sio)
  message(FATAL_ERROR "sio::sio target is not defined. Please use add_subdirectory to add sio to your project")
endif ()

set(SIO_TEST_BACKEND_LIBRARIES)
if (TARGET sio::sio_backend_stdexec)
  list(APPEND SIO_TEST_BACKEND_LIBRARIES sio::sio_backend_stdexec)
endif()
if (TARGET sio::sio_backend_iouring)
  list(APPEND SIO_TEST_BACKEND_LIBRARIES sio::sio_backend_iouring)
endif()

if (SIO_TEST_BACKEND_LIBRARIES STREQUAL "")
  message(FATAL_ERROR "Tests require at least one sio backend to be enabled.")
endif()

set(SIO_TEST_DEFINES
  SIO_TEST_HAS_STDEXEC=$<BOOL:${SIO_ENABLE_BACKEND_STDEXEC}>
  SIO_TEST_HAS_IOURING=$<BOOL:${SIO_ENABLE_BACKEND_IOURING}>
)

function(sio_add_test_target target_name)
  add_executable(${target_name} ${ARGN})
  target_include_directories(${target_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${target_name}
    PRIVATE
      sio::sio
      Catch2::Catch2WithMain
      ${SIO_TEST_BACKEND_LIBRARIES})
  target_compile_definitions(${target_name} PRIVATE ${SIO_TEST_DEFINES})
  add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

set(SIO_TEST_FILE_SOURCES
  file/test_file_handle.cpp
  file/test_read_batched.cpp
)

set(SIO_TEST_NET_SOURCES
  net/test_async_accept.cpp
  net/test_can_endpoint.cpp
  net/test_can_socket.cpp
  net/test_address.cpp
  net/test_endpoint.cpp
  net/test_resolve.cpp
  net/test_socket_handle.cpp
)

set(SIO_TEST_LIB_SOURCES
  sequence/test_first.cpp
  sequence/test_last.cpp
  sequence/test_fork.cpp
  sequence/test_merge_each.cpp
  sequence/test_scan.cpp
  sequence/test_transform_each.cpp
  sequence/test_repeat.cpp
  sequence/test_zip.cpp
  sequence/test_finally.cpp
  sequence/test_buffered_sequence.cpp
  # test_async_channel.cpp
  test_async_resource.cpp
  test_async_mutex.cpp
  test_const_buffer_subspan.cpp
  test_memory_pool.cpp
  test_tap.cpp
)

sio_add_test_target(sio_tests_file ${SIO_TEST_FILE_SOURCES})
sio_add_test_target(sio_tests_lib ${SIO_TEST_LIB_SOURCES})
sio_add_test_target(sio_tests_net ${SIO_TEST_NET_SOURCES})
