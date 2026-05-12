cmake_minimum_required(VERSION 3.20)

project(mp_cache VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(native/mp_logger)

function(mp_cache_enable_warnings target_name)
  target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_library(mp_cache_core
  internal/cache/cache.c
  internal/config/config.c
  internal/crypto/crypto.c
  internal/httpserver/http_server.c
  internal/observability/log.c
  internal/platform/fs.c
  internal/runtime/runtime.c
  internal/security/security.c
  internal/storage/storage.c
)

target_include_directories(mp_cache_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(mp_cache_core
  PUBLIC
    MP_CACHE_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(
  mp_cache_core
  PUBLIC
    mp_logger
    Threads::Threads
)
mp_cache_enable_warnings(mp_cache_core)

add_executable(mp-cache-server cmd/mp-cache-server/main.c)
target_link_libraries(mp-cache-server PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp-cache-server)

add_executable(mp-cachectl cmd/mp-cachectl/main.c)
target_link_libraries(mp-cachectl PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp-cachectl)

enable_testing()

add_executable(mp_cache_config_tests tests/unit/config_tests.c)
target_link_libraries(mp_cache_config_tests PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_config_tests)
add_test(NAME mp_cache_config COMMAND mp_cache_config_tests)

add_executable(mp_cache_store_tests tests/unit/cache_tests.c)
target_link_libraries(mp_cache_store_tests PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_store_tests)
add_test(NAME mp_cache_store COMMAND mp_cache_store_tests)

add_executable(mp_cache_security_tests tests/unit/security_tests.c)
target_link_libraries(mp_cache_security_tests PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_security_tests)
add_test(NAME mp_cache_security COMMAND mp_cache_security_tests)

add_executable(mp_cache_storage_tests tests/unit/storage_tests.c)
target_link_libraries(mp_cache_storage_tests PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_storage_tests)
add_test(NAME mp_cache_storage COMMAND mp_cache_storage_tests)

add_executable(mp_cache_http_tests tests/unit/http_tests.c)
target_link_libraries(mp_cache_http_tests PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_http_tests)
add_test(NAME mp_cache_http COMMAND mp_cache_http_tests)

add_executable(mp_cache_benchmark tests/bench/cache_benchmark.c)
target_link_libraries(mp_cache_benchmark PRIVATE mp_cache_core)
mp_cache_enable_warnings(mp_cache_benchmark)
