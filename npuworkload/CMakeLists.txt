cmake_minimum_required(VERSION 3.16)

project(npu_avs_workload VERSION 0.3.0 LANGUAGES CXX)

option(NPU_AVS_STATIC_CXX_RUNTIME "Link the compiler C++ runtime statically when supported" ON)

include(CTest)

set(NPU_AVS_SOURCES
    src/main.cpp
    src/backend_factory.cpp
    src/config.cpp
    src/crc32.cpp
    src/heartbeat.cpp
    src/json.cpp
    src/logger.cpp
    src/manifest.cpp
    src/metrics.cpp
    src/profile.cpp
    src/runner.cpp
    src/sha256.cpp
    src/tensor.cpp
    src/utils.cpp
    src/verifier.cpp
    src/backends/null/null_backend.cpp
    src/backends/reference_cpu/reference_cpu_backend.cpp
    src/backends/tflite_delegate/tflite_delegate_backend.cpp
)

add_executable(npu-avs-workload ${NPU_AVS_SOURCES})
target_compile_features(npu-avs-workload PRIVATE cxx_std_17)
target_include_directories(npu-avs-workload PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

find_package(Threads REQUIRED)
target_link_libraries(npu-avs-workload PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

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
    add_library(npu-fake-tflite-runtime SHARED tests/fake_tflite_runtime.cpp)
    target_compile_features(npu-fake-tflite-runtime PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(npu-fake-tflite-runtime PRIVATE /W4 /permissive- /O2)
    else()
        target_compile_options(npu-fake-tflite-runtime PRIVATE -Wall -Wextra -Wpedantic -O2)
    endif()

    add_executable(npu-contract-tests
        tests/contract_tests.cpp
        src/json.cpp
        src/manifest.cpp
        src/profile.cpp
        src/sha256.cpp
        src/tensor.cpp
        src/backends/null/null_backend.cpp
    )
    target_compile_features(npu-contract-tests PRIVATE cxx_std_17)
    target_include_directories(npu-contract-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(npu-contract-tests PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(npu-contract-tests PRIVATE /W4 /permissive- /O2)
    else()
        target_compile_options(npu-contract-tests PRIVATE -Wall -Wextra -Wpedantic -O2)
    endif()

    add_test(NAME npu_avs_contract COMMAND npu-contract-tests
        ${CMAKE_CURRENT_SOURCE_DIR}/profiles/framework_smoke/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/profiles/kws01/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/profiles/ic01/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/profiles/ad01/manifest.json
        ${CMAKE_CURRENT_SOURCE_DIR}/profiles/sww01/manifest.json)

    add_test(NAME npu_avs_version COMMAND npu-avs-workload --version)
    set_tests_properties(npu_avs_version PROPERTIES PASS_REGULAR_EXPRESSION "npu-avs-workload 0.3.0")

    add_test(NAME npu_avs_null_smoke COMMAND npu-avs-workload
        --profile framework_smoke --backend null
        --input-manifest ${CMAKE_CURRENT_SOURCE_DIR}/profiles/framework_smoke/manifest.json
        --duration 0 --inferences 2 --warmup-inferences 0 --summary-only)
    set_tests_properties(npu_avs_null_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"result\":\"PASS\"")

    add_test(NAME npu_avs_reference_smoke COMMAND npu-avs-workload
        --profile framework_smoke --backend reference_cpu
        --input-manifest ${CMAKE_CURRENT_SOURCE_DIR}/profiles/framework_smoke/manifest.json
        --duration 0 --inferences 3 --warmup-inferences 0 --summary-only)
    set_tests_properties(npu_avs_reference_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"verify_pass\":true")

    foreach(NPU_PROFILE kws01 ic01 ad01 sww01)
        add_test(NAME npu_avs_${NPU_PROFILE}_contract COMMAND npu-avs-workload
            --profile ${NPU_PROFILE} --backend null
            --input-manifest ${CMAKE_CURRENT_SOURCE_DIR}/profiles/${NPU_PROFILE}/manifest.json
            --duration 0 --inferences 2 --warmup-inferences 0 --summary-only)
        set_tests_properties(npu_avs_${NPU_PROFILE}_contract PROPERTIES
            PASS_REGULAR_EXPRESSION "\"result\":\"PASS\"")
    endforeach()

    add_test(NAME npu_avs_delegate_lifecycle COMMAND npu-avs-workload
        --profile kws01 --backend tflite_delegate
        --input-manifest ${CMAKE_CURRENT_SOURCE_DIR}/profiles/kws01/manifest.json
        --runtime-library $<TARGET_FILE:npu-fake-tflite-runtime>
        --delegate-library fake-vendor-delegate
        --delegate-options device=test
        --duration 0 --inferences 2 --warmup-inferences 1 --summary-only)
    set_tests_properties(npu_avs_delegate_lifecycle PROPERTIES
        PASS_REGULAR_EXPRESSION "\"result\":\"PASS\"")
endif()
