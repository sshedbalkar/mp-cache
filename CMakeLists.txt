cmake_minimum_required(VERSION 3.20)

project(mp_cache VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_subdirectory(native/mp_logger)

add_library(mp_cache_core
  internal/cache/cache.c
  internal/config/config.c
  internal/httpserver/http_server.c
  internal/observability/log.c
  internal/platform/fs.c
  internal/runtime/runtime.c
)

target_include_directories(mp_cache_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(mp_cache_core
  PUBLIC
    MP_CACHE_VERSION="${PROJECT_VERSION}"
)

target_compile_options(mp_cache_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(mp_cache_core PUBLIC mp_logger Threads::Threads)

add_executable(mp-cache-server cmd/mp-cache-server/main.c)
target_link_libraries(mp-cache-server PRIVATE mp_cache_core)
target_compile_options(mp-cache-server PRIVATE -Wall -Wextra -Wpedantic)

add_executable(mp-cachectl cmd/mp-cachectl/main.c)
target_link_libraries(mp-cachectl PRIVATE mp_cache_core)
target_compile_options(mp-cachectl PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()

add_executable(mp_cache_config_tests tests/unit/config_tests.c)
target_link_libraries(mp_cache_config_tests PRIVATE mp_cache_core)
target_compile_options(mp_cache_config_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME mp_cache_config COMMAND mp_cache_config_tests)

add_executable(mp_cache_store_tests tests/unit/cache_tests.c)
target_link_libraries(mp_cache_store_tests PRIVATE mp_cache_core)
target_compile_options(mp_cache_store_tests PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME mp_cache_store COMMAND mp_cache_store_tests)
