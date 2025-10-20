include(FetchContent)
FetchContent_Declare(
  stdexec
  GIT_REPOSITORY https://github.com/NVIDIA/stdexec.git
  GIT_TAG nvhpc-25.09
)

set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "close stdexec examples")
message(STATUS "Downloading stdexec")
FetchContent_MakeAvailable(stdexec)

