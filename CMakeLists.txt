cmake_minimum_required(VERSION 3.14)
project(ecewo_session VERSION 0.1.0 LANGUAGES C)

add_library(ecewo_session
  src/ecewo-session.c
)

add_library(ecewo::session ALIAS ecewo_session)

target_include_directories(ecewo_session PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

if (PROJECT_IS_TOP_LEVEL AND NOT TARGET ecewo::ecewo)
  include(FetchContent)

  FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/savashn/ecewo.git
    GIT_TAG        v3.1.0
    GIT_SHALLOW    TRUE
  )

  FetchContent_MakeAvailable(ecewo)
endif()

if (PROJECT_IS_TOP_LEVEL AND TARGET ecewo::ecewo)
  ecewo_plugins(
    cookie
    mock
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
