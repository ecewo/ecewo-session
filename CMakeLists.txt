cmake_minimum_required(VERSION 3.14)
project(ecewo_session VERSION 0.1.0 LANGUAGES C)

option(BUILD_SHARED_LIBS "Build ecewo_session as a shared library" OFF)

if(BUILD_SHARED_LIBS)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

add_library(ecewo_session
  src/ecewo-session.c
)

add_library(ecewo::session ALIAS ecewo_session)

include(GenerateExportHeader)
generate_export_header(ecewo_session
  BASE_NAME ECEWO_SESSION
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/ecewo-session-export.h"
)

# ABI hygiene matching ecewo: version the shared object and hide every symbol
# that is not explicitly marked ECEWO_SESSION_EXPORT.
set_target_properties(ecewo_session PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(ecewo_session PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if (PROJECT_IS_TOP_LEVEL AND NOT TARGET ecewo::ecewo)
  include(FetchContent)

  FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/ecewo/ecewo.git
    GIT_TAG v4
    GIT_SHALLOW TRUE
  )

  FetchContent_MakeAvailable(ecewo)
endif()

if (PROJECT_IS_TOP_LEVEL AND TARGET ecewo::ecewo)
  ecewo_add(
    cookie@dev
    mock@dev
  )

  enable_testing()

  add_executable(ecewo_session_test
    tests/test.c
    tests/runner.c
  )

  target_include_directories(ecewo_session_test
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )

  target_link_libraries(ecewo_session_test
    PRIVATE
      ecewo::ecewo
      ecewo::cookie
      ecewo::session
      ecewo::mock
  )

  if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ecewo_session_test PRIVATE
      -Wall
      -Wextra
      -Werror
      -Wstrict-prototypes
      -Wincompatible-pointer-types
      -Wno-unused-parameter
    )
  endif()

  add_test(
    NAME ecewo_session_all
    COMMAND ecewo_session_test
  )
endif()

target_link_libraries(ecewo_session PUBLIC
  ecewo::ecewo
  ecewo::cookie
)
