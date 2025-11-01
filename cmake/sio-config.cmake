include(CMakeFindDependencyMacro)
find_dependency(stdexec REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
include("${CMAKE_CURRENT_LIST_DIR}/sio-targets.cmake")

if (TARGET sio::sio_backend_iouring)
  find_dependency(liburing 2.5 REQUIRED)
endif()
