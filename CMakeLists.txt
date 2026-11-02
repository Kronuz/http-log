cmake_minimum_required(VERSION 3.14)
project(http_log LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# The HTTP application framework: the HttpHandler seam this middleware decorates,
# plus Request / ResponseWriter / reason_phrase. Each dependency is guarded so a
# consumer that already brought it in is reused rather than fetched twice.
if(NOT TARGET http::http)
  FetchContent_Declare(http_lib GIT_REPOSITORY https://github.com/Kronuz/http.git GIT_TAG 679c0f814917389ac7f6cd24c4c8b95967bd7404)
  FetchContent_MakeAvailable(http_lib)
endif()

# The deferred, colored logger (brings term-color, whose rgb()/rgba()/brgb() build
# the palettes and whose sink resolves the stacked escapes).
if(NOT TARGET logger::logger)
  FetchContent_Declare(logger GIT_REPOSITORY https://github.com/Kronuz/logger.git GIT_TAG 801105c8c4938d06c15c730df94118ea60364111)
  FetchContent_MakeAvailable(logger)
endif()

# strings (from_bytes / indent / format) and repr — the same utilities Xapiand's
# to_text used, so the rendering is verbatim. repr is declared before strings:
# FetchContent keeps the first declaration of a name, and strings pins its own repr.
if(NOT TARGET repr::repr)
  FetchContent_Declare(repr GIT_REPOSITORY https://github.com/Kronuz/repr.git GIT_TAG 87450b43c6ff113a321e06d811d9b7a4857c9351)
  FetchContent_MakeAvailable(repr)
endif()
if(NOT TARGET strings::strings)
  FetchContent_Declare(strings GIT_REPOSITORY https://github.com/Kronuz/strings.git GIT_TAG 1be38483a333d13544fbf6b0937409295c8ea3e0)
  FetchContent_MakeAvailable(strings)
endif()

# cppcodec: a compact, dedicated base64 codec (the base64_rfc4648 Xapiand's to_text
# used). Header-only; SOURCE_SUBDIR points at a non-existent dir so its CMakeLists
# (which requests an old CMake policy) is not run -- we only need the headers.
# Pinned to the commit its master names (2022-09-06; the last tag, v0.2, is from 2018).
if(NOT DEFINED cppcodec_SOURCE_DIR)
  FetchContent_Declare(cppcodec
    GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
    GIT_TAG        8019b8b580f8573c33c50372baec7039dfe5a8ce
    SOURCE_SUBDIR  do-not-configure)
  FetchContent_MakeAvailable(cppcodec)
endif()

add_library(http_log INTERFACE)
target_include_directories(http_log INTERFACE ${CMAKE_CURRENT_SOURCE_DIR} ${cppcodec_SOURCE_DIR})
target_compile_features(http_log INTERFACE cxx_std_20)
target_link_libraries(http_log INTERFACE http::http logger::logger strings::strings repr::repr)
add_library(http_log::http_log ALIAS http_log)

# Tests and the demo build only for the standalone project.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(http_log_test test/test.cc)
  target_link_libraries(http_log_test PRIVATE http_log)
  add_test(NAME http_log COMMAND http_log_test)
  add_executable(http_log_completion_test test/completion_test.cc)
  target_link_libraries(http_log_completion_test PRIVATE http_log)
  add_test(NAME http_log_completion COMMAND http_log_completion_test)
  set_tests_properties(http_log_completion PROPERTIES TIMEOUT 60)
  # Every FetchContent dependency is pinned to a full commit hash.
  add_test(NAME http_log_pins COMMAND ${CMAKE_COMMAND} -DFILE=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_pins.cmake)
  add_executable(http_log_demo examples/demo.cc)
  target_link_libraries(http_log_demo PRIVATE http_log)
endif()
