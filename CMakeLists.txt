cmake_minimum_required(VERSION 3.22)
project(senders-io
  DESCRIPTION "An adaption of Senders/Receivers for async networking and I/O"
  HOMEPAGE_URL "https://github.com/maikel/senders-io"
  LANGUAGES CXX)

# Configure cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FetchStdexec)
include(CMakeDependentOption)
include(GNUInstallDirs)

function(sio_install_backend target file_set)
  if (CMAKE_SKIP_INSTALL_RULES)
    return()
  endif()

  install(
    TARGETS ${target}
    EXPORT sio-targets
    FILE_SET ${file_set} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endfunction()

# Setup build tools
find_package(Doxygen)
if (DOXYGEN_FOUND)
  set(DOXYGEN_GENERATE_HTML YES)
  set(DOXYGEN_GENERATE_MAN YES)
  doxygen_add_docs(
      doxygen
      ${CMAKE_CURRENT_SOURCE_DIR}/source
  )
endif()

# Setup project
set(CMAKE_EXPORT_COMPILE_COMMANDS true)
set(SIO_PLATFORM_LINUX OFF)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(SIO_PLATFORM_LINUX ON)
endif()

option(SIO_FETCH_CONTENT "Use FetchContent to get dependencies" ON)
option(BUILD_TESTING "Build tests" ${PROJECT_IS_TOP_LEVEL})
option(SIO_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(SIO_ENABLE_BACKEND_STDEXEC "Enable stdexec-backed event loop integration" ON)
cmake_dependent_option(SIO_ENABLE_BACKEND_IOURING "Enable io_uring-backed event loop integration" ON "SIO_PLATFORM_LINUX" OFF)
cmake_dependent_option(SIO_ENABLE_BACKEND_EPOLL "Enable epoll-backed event loop integration" ON "SIO_PLATFORM_LINUX" OFF)
option(SIO_ENABLE_ASAN "Enable address sanitizer" OFF)
option(SIO_ENABLE_TEST_COVERAGE "Enable compiler instrumentation for coverage reports" OFF)

if (SIO_ENABLE_ASAN)
  set(SIO_ASAN_FLAGS "-fsanitize=address -fno-omit-frame-pointer" CACHE STRING "ASan compile flags")
  set(SIO_ASAN_LINK_FLAGS "-fsanitize=address" CACHE STRING "ASan link flags")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SIO_ASAN_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${SIO_ASAN_LINK_FLAGS}")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${SIO_ASAN_LINK_FLAGS}")
endif()

if(SIO_FETCH_CONTENT)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
endif()

if(SIO_FETCH_CONTENT)
  include(FetchStdexec)
else()
  find_package(STDEXEC REQUIRED)
endif()

if (SIO_ENABLE_TEST_COVERAGE)
  if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(SIO_COVERAGE_COMPILE_FLAGS "--coverage" CACHE STRING "Coverage compile flags")
    set(SIO_COVERAGE_LINK_FLAGS "--coverage" CACHE STRING "Coverage link flags")
  elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(SIO_COVERAGE_COMPILE_FLAGS "-fprofile-instr-generate;-fcoverage-mapping" CACHE STRING "Coverage compile flags")
    set(SIO_COVERAGE_LINK_FLAGS "-fprofile-instr-generate;-fcoverage-mapping" CACHE STRING "Coverage link flags")
  else()
    message(WARNING "SIO_ENABLE_TEST_COVERAGE is only supported with GCC or Clang.")
  endif()

  if (SIO_COVERAGE_COMPILE_FLAGS)
    add_compile_options(${SIO_COVERAGE_COMPILE_FLAGS})
  endif()
  if (SIO_COVERAGE_LINK_FLAGS)
    add_link_options(${SIO_COVERAGE_LINK_FLAGS})
  endif()
endif()

# Core lib
add_library(sio
  source/sio/const_buffer_span.cpp
  source/sio/mutable_buffer_span.cpp
  source/sio/memory_pool.cpp)
add_library(sio::sio ALIAS sio)
target_include_directories(sio
  PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/source>
      $<INSTALL_INTERFACE:include>)
target_sources(sio PUBLIC
  FILE_SET sio_headers
  TYPE HEADERS
  BASE_DIRS source
  FILES
    source/sio/ip/address.hpp
    source/sio/ip/endpoint.hpp
    source/sio/ip/resolve.hpp
    source/sio/ip/tcp.hpp
    source/sio/ip/udp.hpp
    source/sio/local/stream_protocol.hpp
    source/sio/local/endpoint.hpp
    source/sio/sequence/any_sequence_of.hpp
    source/sio/sequence/buffered_sequence.hpp
    source/sio/sequence/empty_sequence.hpp
    source/sio/sequence/first.hpp
    source/sio/sequence/fork.hpp
    source/sio/sequence/ignore_all.hpp
    source/sio/sequence/iterate.hpp
    source/sio/sequence/last.hpp
    source/sio/sequence/let_value_each.hpp
    source/sio/sequence/merge_each.hpp
    source/sio/sequence/reduce.hpp
    source/sio/sequence/repeat.hpp
    source/sio/sequence/scan.hpp
    source/sio/sequence/sequence_concepts.hpp
    source/sio/sequence/then_each.hpp
    source/sio/sequence/transform_each.hpp
    source/sio/sequence/finally.hpp
    source/sio/sequence/zip.hpp
    source/sio/event_loop/concepts.hpp
    source/sio/event_loop/file_handle.hpp
    source/sio/event_loop/socket_handle.hpp
    source/sio/assert.hpp
    source/sio/async_allocator.hpp
    source/sio/async_channel.hpp
    source/sio/async_mutex.hpp
    source/sio/async_resource.hpp
    source/sio/buffer.hpp
    source/sio/buffer_algorithms.hpp
    source/sio/concepts.hpp
    source/sio/const_buffer_span.hpp
    source/sio/const_buffer.hpp
    source/sio/deferred.hpp
    source/sio/intrusive/list.hpp
    source/sio/intrusive/queue.hpp
    source/sio/intrusive/heap.hpp
    source/sio/io_concepts.hpp
    source/sio/mutable_buffer.hpp
    source/sio/mutable_buffer_span.hpp
    source/sio/memory_pool.hpp
    source/sio/net_concepts.hpp
    source/sio/read_batched.hpp
    source/sio/tap.hpp
    source/sio/with_env.hpp)
target_link_libraries(sio PUBLIC STDEXEC::stdexec)
target_compile_features(sio PUBLIC cxx_std_20)
install( TARGETS sio EXPORT sio-targets FILE_SET sio_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})


if (SIO_ENABLE_BACKEND_STDEXEC)
  add_library(sio_backend_stdexec INTERFACE)
  add_library(sio::sio_backend_stdexec ALIAS sio_backend_stdexec)
  target_link_libraries(sio_backend_stdexec INTERFACE sio)
  target_sources(sio_backend_stdexec
    INTERFACE
      FILE_SET stdexec_backend_headers
      TYPE HEADERS
      BASE_DIRS source
      FILES
        source/sio/event_loop/stdexec/backend.hpp
        source/sio/event_loop/stdexec/details.h
        source/sio/event_loop/stdexec/fd_close.h
        source/sio/event_loop/stdexec/fd_read.h
        source/sio/event_loop/stdexec/fd_write.h
        source/sio/event_loop/stdexec/file_open.h
        source/sio/event_loop/stdexec/socket_accept.h
        source/sio/event_loop/stdexec/socket_connect.h
        source/sio/event_loop/stdexec/socket_open.h
        source/sio/event_loop/stdexec/socket_sendmsg.h)
  sio_install_backend(sio_backend_stdexec stdexec_backend_headers)
endif()

if (SIO_ENABLE_BACKEND_IOURING)
  find_package(liburing 2.5 REQUIRED)
  add_library(sio_backend_iouring INTERFACE)
  add_library(sio::sio_backend_iouring ALIAS sio_backend_iouring)
  target_link_libraries(
    sio_backend_iouring
    INTERFACE
      sio
      liburing::liburing)
  target_sources(sio_backend_iouring
    INTERFACE
      FILE_SET iouring_backend_headers
      TYPE HEADERS
      BASE_DIRS source
      FILES
        source/sio/event_loop/iouring/backend.hpp
        source/sio/event_loop/iouring/context.hpp
        source/sio/event_loop/iouring/details.hpp
        source/sio/event_loop/iouring/fd_close.hpp
        source/sio/event_loop/iouring/fd_read.hpp
        source/sio/event_loop/iouring/fd_write.hpp
        source/sio/event_loop/iouring/file_open.hpp
        source/sio/event_loop/iouring/socket_accept.hpp
        source/sio/event_loop/iouring/socket_connect.hpp
        source/sio/event_loop/iouring/socket_open.hpp
        source/sio/event_loop/iouring/socket_sendmsg.hpp
        source/sio/event_loop/iouring/submission_operation.hpp
        source/sio/event_loop/iouring/run_sender.hpp
        source/sio/event_loop/iouring/scheduler.hpp)
  sio_install_backend(sio_backend_iouring iouring_backend_headers)
endif()

if (SIO_ENABLE_BACKEND_EPOLL)
  add_library(sio_backend_epoll INTERFACE)
  add_library(sio::sio_backend_epoll ALIAS sio_backend_epoll)
  target_link_libraries(
    sio_backend_epoll
    INTERFACE
      sio)
  target_sources(sio_backend_epoll
    INTERFACE
      FILE_SET epoll_backend_headers
      TYPE HEADERS
      BASE_DIRS source
      FILES
        source/sio/event_loop/epoll/backend.hpp
        source/sio/event_loop/epoll/context.hpp
        source/sio/event_loop/epoll/details.hpp
        source/sio/event_loop/epoll/fd_close.hpp
        source/sio/event_loop/epoll/fd_read.hpp
        source/sio/event_loop/epoll/fd_write.hpp
        source/sio/event_loop/epoll/file_open.hpp
        source/sio/event_loop/epoll/socket_accept.hpp
        source/sio/event_loop/epoll/socket_connect.hpp
        source/sio/event_loop/epoll/socket_open.hpp
        source/sio/event_loop/epoll/socket_sendmsg.hpp
        source/sio/event_loop/epoll/run_sender.hpp
        source/sio/event_loop/epoll/scheduler.hpp)
  sio_install_backend(sio_backend_epoll epoll_backend_headers)
endif()

if (BUILD_TESTING)
  if(SIO_FETCH_CONTENT)
    include(FetchCatch2)
  else()
    find_package(Catch2 REQUIRED)
  endif()
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

if (SIO_EXAMPLES)
  add_subdirectory(examples)
endif()
