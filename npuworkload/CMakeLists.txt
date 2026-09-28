cmake_minimum_required(VERSION 3.16)

project(npu_avs_workload VERSION 0.1.0 LANGUAGES CXX)

option(NPU_AVS_STATIC_CXX_RUNTIME "Link the compiler C++ runtime statically when supported" ON)

include(CTest)

set(NPU_AVS_SOURCES
    src/main.cpp
    src/backend_factory.cpp
    src/config.cpp
    src/crc32.cpp
    src/heartbeat.cpp
    src/logger.cpp
    src/metrics.cpp
    src/profile.cpp
    src/runner.cpp
    src/tensor.cpp
    src/utils.cpp
    src/verifier.cpp
    src/backends/null/null_backend.cpp
    src/backends/reference_cpu/reference_cpu_backend.cpp
)

add_executable(npu-avs-workload ${NPU_AVS_SOURCES})
target_compile_features(npu-avs-workload PRIVATE cxx_std_17)
target_include_directories(npu-avs-workload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(npu-avs-workload PRIVATE Threads::Threads)

if(MSVC)
    target_compile_options(npu-avs-workload PRIVATE /W4 /permissive- /O2)
else()
    target_compile_options(npu-avs-workload PRIVATE -Wall -Wextra -Wpedantic -O2)
endif()

if(NPU_AVS_STATIC_CXX_RUNTIME AND NOT ANDROID AND NOT OHOS AND
   CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(npu-avs-workload PRIVATE -static-libgcc -static-libstdc++)
endif()

install(TARGETS npu-avs-workload RUNTIME DESTINATION bin)

if(BUILD_TESTING)
    add_test(NAME npu_avs_version COMMAND npu-avs-workload --version)
    set_tests_properties(npu_avs_version PROPERTIES PASS_REGULAR_EXPRESSION "npu-avs-workload 0.1.0")

    add_test(NAME npu_avs_null_smoke COMMAND npu-avs-workload
        --profile null --duration 0 --inferences 2 --warmup-inferences 0 --summary-only)
    set_tests_properties(npu_avs_null_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"result\":\"PASS\"")

    add_test(NAME npu_avs_reference_smoke COMMAND npu-avs-workload
        --profile reference --duration 0 --inferences 3 --warmup-inferences 0 --summary-only)
    set_tests_properties(npu_avs_reference_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"verify_pass\":true")
endif()
