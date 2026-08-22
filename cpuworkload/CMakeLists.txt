cmake_minimum_required(VERSION 3.16)

project(cpu_avs_workload VERSION 1.0.0 LANGUAGES CXX)

option(CPU_AVS_STATIC_CXX_RUNTIME "Link the compiler C++ runtime statically when supported" ON)

include(CTest)

set(CPU_AVS_SOURCES
    src/main.cpp
    src/backend_factory.cpp
    src/config.cpp
    src/crc32.cpp
    src/heartbeat.cpp
    src/logger.cpp
    src/metrics.cpp
    src/profile.cpp
    src/runner.cpp
    src/utils.cpp
    src/verifier.cpp
    src/backends/null/null_backend.cpp
    src/backends/integer/integer_backend.cpp
    src/backends/floating_point/floating_point_backend.cpp
    src/backends/matrix/matrix_backend.cpp
    src/backends/memory/memory_backend.cpp
    src/backends/mixed/mixed_backend.cpp
)

add_executable(cpu-avs-workload ${CPU_AVS_SOURCES})
target_compile_features(cpu-avs-workload PRIVATE cxx_std_17)
target_include_directories(cpu-avs-workload
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(cpu-avs-workload PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(cpu-avs-workload PRIVATE /W4 /permissive- /O2)
else()
    target_compile_options(cpu-avs-workload PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

# Android and HarmonyOS select c++_static in their toolchain configuration.
# This fallback covers desktop GCC/Clang without overriding a platform NDK.
if(CPU_AVS_STATIC_CXX_RUNTIME AND NOT ANDROID AND NOT OHOS AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(cpu-avs-workload PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS cpu-avs-workload RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_test(NAME cpu_avs_version COMMAND cpu-avs-workload --version)
    set_tests_properties(cpu_avs_version PROPERTIES PASS_REGULAR_EXPRESSION "cpu-avs-workload 1.0.0")

    add_test(NAME cpu_avs_integer_smoke COMMAND cpu-avs-workload
        --profile integer --duration 0 --batches 2 --warmup 0 --summary-only)
    set_tests_properties(cpu_avs_integer_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"result\":\"PASS\"")

    add_test(NAME cpu_avs_mixed_threads_smoke COMMAND cpu-avs-workload
        --profile mixed --duration 0 --batches 2 --warmup 0 --threads 2 --summary-only)
    set_tests_properties(cpu_avs_mixed_threads_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"verify_pass\":true")
endif()
