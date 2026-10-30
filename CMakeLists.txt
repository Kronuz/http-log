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
  FetchContent_Declare(http_lib GIT_REPOSITORY https://github.com/Kronuz/http.git GIT_TAG ead53b4e33f2e513702c201d9e9e55685fcb0033)
  FetchContent_MakeAvailable(http_lib)
endif()

# The deferred, colored logger (brings term-color, whose rgb()/rgba()/brgb() build
# the palettes and whose sink resolves the stacked escapes).
if(NOT TARGET logger::logger)
  FetchContent_Declare(logger GIT_REPOSITORY https://github.com/Kronuz/logger.git GIT_TAG 8c5afcd89030586939dd5229ee2ae583069b83dd)
  FetchContent_MakeAvailable(logger)
endif()

# strings (from_bytes / indent / format) and repr — the same utilities Xapiand's
# to_text used, so the rendering is verbatim.
if(NOT TARGET strings::strings)
  FetchContent_Declare(strings GIT_REPOSITORY https://github.com/Kronuz/strings.git GIT_TAG b42d3c4ce1130043a381e487ae0a693248c2c0f5)
  FetchContent_MakeAvailable(strings)
endif()
if(NOT TARGET repr::repr)
  FetchContent_Declare(repr GIT_REPOSITORY https://github.com/Kronuz/repr.git GIT_TAG 0fe9a1e98914a423574bfe675eaa82f3e1cb1df6)
  FetchContent_MakeAvailable(repr)
endif()

# cppcodec: a compact, dedicated base64 codec (the base64_rfc4648 Xapiand's to_text
# used). Header-only; SOURCE_SUBDIR points at a non-existent dir so its CMakeLists
# (which requests an old CMake policy) is not run -- we only need the headers.
if(NOT DEFINED cppcodec_SOURCE_DIR)
  FetchContent_Declare(cppcodec
    GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
    GIT_TAG        master
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
  add_executable(http_log_demo examples/demo.cc)
  target_link_libraries(http_log_demo PRIVATE http_log)
endif()
