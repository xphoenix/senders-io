#[=======================================================================[.rst:
Findliburing
------------

Locate the liburing library and headers.

Imported Targets
^^^^^^^^^^^^^^^^

``liburing::liburing`` - The liburing library.

Result Variables
^^^^^^^^^^^^^^^^

``liburing_FOUND`` - True if liburing was found.
``liburing_VERSION`` - Detected version string.
``liburing_INCLUDE_DIR`` - Directory containing ``liburing.h``.
``liburing_LIBRARY`` - Path to the liburing library.

This module honours ``pkg-config`` if available and falls back to manual
search otherwise.
#]=======================================================================]

if (DEFINED liburing_FOUND)
  return()
endif()

find_package(PkgConfig QUIET)

if (PkgConfig_FOUND)
  if (liburing_FIND_VERSION)
    pkg_check_modules(PC_LIBURING QUIET liburing>=${liburing_FIND_VERSION})
  else()
    pkg_check_modules(PC_LIBURING QUIET liburing)
  endif()
endif()

if (PC_LIBURING_FOUND)
  set(_liburing_include_hints ${PC_LIBURING_INCLUDE_DIRS})
  set(_liburing_library_hints ${PC_LIBURING_LIBRARY_DIRS})
  set(liburing_VERSION ${PC_LIBURING_VERSION})
  set(_liburing_cflags ${PC_LIBURING_CFLAGS_OTHER})
  set(_liburing_dep_libs ${PC_LIBURING_LINK_LIBRARIES})
else()
  unset(_liburing_include_hints)
  unset(_liburing_library_hints)
  unset(_liburing_cflags)
  unset(_liburing_dep_libs)
endif()

find_path(liburing_INCLUDE_DIR
  NAMES liburing.h
  HINTS ${_liburing_include_hints})

find_library(liburing_LIBRARY
  NAMES uring
  HINTS ${_liburing_library_hints})

if (NOT liburing_VERSION AND liburing_INCLUDE_DIR)
  set(_liburing_version_header "${liburing_INCLUDE_DIR}/liburing/version.h")
  if (EXISTS "${_liburing_version_header}")
    file(READ "${_liburing_version_header}" _liburing_version_contents)
    string(REGEX MATCH "#define[ \t]+LIBURING_VERSION[ \t]+\"([0-9]+\\.[0-9]+(\\.[0-9]+)?)\""
      _liburing_version_match "${_liburing_version_contents}")
    if (_liburing_version_match)
      set(liburing_VERSION "${CMAKE_MATCH_1}")
    endif()
  endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(liburing
  REQUIRED_VARS liburing_LIBRARY liburing_INCLUDE_DIR
  VERSION_VAR liburing_VERSION)

if (liburing_FOUND)
  set(liburing_INCLUDE_DIRS ${liburing_INCLUDE_DIR})
  set(liburing_LIBRARIES ${liburing_LIBRARY})

  if (NOT TARGET liburing::liburing)
    add_library(liburing::liburing UNKNOWN IMPORTED)
    set_target_properties(liburing::liburing PROPERTIES
      IMPORTED_LOCATION "${liburing_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${liburing_INCLUDE_DIRS}")

    if (_liburing_cflags)
      separate_arguments(_liburing_cflags NATIVE_COMMAND)
      set_target_properties(liburing::liburing PROPERTIES
        INTERFACE_COMPILE_OPTIONS "${_liburing_cflags}")
    endif()

    if (_liburing_dep_libs)
      set(_liburing_link_deps)
      foreach(_dep IN LISTS _liburing_dep_libs)
        if (_dep MATCHES "^-luring$")
          continue()
        endif()
        list(APPEND _liburing_link_deps "${_dep}")
      endforeach()
      if (_liburing_link_deps)
        set_target_properties(liburing::liburing PROPERTIES
          INTERFACE_LINK_LIBRARIES "${_liburing_link_deps}")
      endif()
    endif()
  endif()
endif()

mark_as_advanced(liburing_INCLUDE_DIR liburing_LIBRARY)
