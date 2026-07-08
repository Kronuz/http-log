cmake_minimum_required(VERSION 3.14)
project(http_log LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# The HTTP application framework: the HttpHandler seam this middleware decorates,
# plus Request / ResponseWriter / reason_phrase. Guarded so a consumer that already
# brought it in is reused rather than fetched twice.
if(NOT TARGET http::http)
  FetchContent_Declare(http_lib GIT_REPOSITORY https://github.com/Kronuz/http.git GIT_TAG main)
  FetchContent_MakeAvailable(http_lib)
endif()

# The deferred, colored logger (brings term-color, which resolves the stacked color
# escapes this lib emits). The request/response lines log through it.
if(NOT TARGET logger::logger)
  FetchContent_Declare(logger GIT_REPOSITORY https://github.com/Kronuz/logger.git GIT_TAG main)
  FetchContent_MakeAvailable(logger)
endif()

add_library(http_log INTERFACE)
target_include_directories(http_log INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(http_log INTERFACE cxx_std_20)
target_link_libraries(http_log INTERFACE http::http logger::logger)
add_library(http_log::http_log ALIAS http_log)
