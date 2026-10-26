# Scope note:
# This is the standalone CMake entry used by the existing HarmonyOS/OHOS
# framework workflow for gpu-avs-workload. The Android/HarmonyOS dual-framework
# integration has a separate CMake implementation. Do not replace or represent
# that dual-framework build with this file; changes must be synchronized there
# deliberately and validated through its own entry point.

cmake_minimum_required(VERSION 3.16)
project(gpu_avs_workload CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Detect the standalone OpenHarmony build used by the current device workflow.
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS" OR DEFINED OHOS_NDK_HOME)
    set(BUILDING_FOR_OHOS TRUE)
else()
    set(BUILDING_FOR_OHOS FALSE)
endif()

message(STATUS
    "gpu-avs-workload CMake scope: standalone OHOS-framework build; "
    "not the Android/OHOS dual-framework integration")

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    message(WARNING
        "CMAKE_BUILD_TYPE is empty. Pass -DCMAKE_BUILD_TYPE=Release for "
        "a traceable release build.")
endif()

option(GPU_AVS_ENABLE_NULL "Enable Null backend" ON)
option(GPU_AVS_ENABLE_GLES "Enable GLES backend" OFF)
option(GPU_AVS_ENABLE_VULKAN "Enable Vulkan backend" OFF)
option(GPU_AVS_ENABLE_OPENCL "Enable OpenCL backend" OFF)

add_executable(gpu-avs-workload
    src/main.cpp
    src/config.cpp
    src/profile.cpp
    src/logger.cpp
    src/heartbeat.cpp
    src/metrics.cpp
    src/runner.cpp
    src/verifier.cpp
    src/crc32.cpp
    src/utils.cpp
    src/backend_factory.cpp
)

target_include_directories(gpu-avs-workload PRIVATE
    include
)

find_package(Threads REQUIRED)
target_link_libraries(gpu-avs-workload PRIVATE Threads::Threads)

# ================== NULL Backend ==================
if(GPU_AVS_ENABLE_NULL)
    target_sources(gpu-avs-workload PRIVATE
        src/backends/null/null_backend.cpp
    )
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_NULL=1)
else()
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_NULL=0)
endif()

# ================== GLES Backend ==================
if(GPU_AVS_ENABLE_GLES)
    target_sources(gpu-avs-workload PRIVATE
        src/backends/gles/gles_context.cpp
        src/backends/gles/gles_backend.cpp
    )

    if(BUILDING_FOR_OHOS)
        find_library(EGL_LIBRARY EGL)
        find_library(GLESV3_LIBRARY GLESv3)
    else()
        # Desktop fallback for standalone development only. The Android/OHOS
        # dual-framework build remains a separate CMake implementation.
        find_library(EGL_LIBRARY EGL)
        find_library(GLESV2_LIBRARY GLESv2)
    endif()

    if(NOT EGL_LIBRARY)
        message(FATAL_ERROR "EGL library not found")
    endif()

    if(BUILDING_FOR_OHOS AND NOT GLESV3_LIBRARY)
        message(FATAL_ERROR "GLESv3 library not found in OHOS NDK")
    endif()
    if(BUILDING_FOR_OHOS)
        target_link_libraries(gpu-avs-workload PRIVATE
            ${EGL_LIBRARY}
            ${GLESV3_LIBRARY}
        )
    else()
        target_link_libraries(gpu-avs-workload PRIVATE
            ${EGL_LIBRARY}
            ${GLESV2_LIBRARY}
        )
    endif()

    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_GLES=1)
else()
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_GLES=0)
endif()

# ================== VULKAN Backend ==================
if(GPU_AVS_ENABLE_VULKAN)
    target_sources(gpu-avs-workload PRIVATE
        src/backends/vulkan/vulkan_backend.cpp
        src/backends/vulkan/vulkan_graphics_backend.cpp
        src/backends/vulkan/vulkan_shader_loader.cpp
    )
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_VULKAN_GRAPHICS=1)
    if(BUILDING_FOR_OHOS)
        # OHOS FindVulkan.cmake may not resolve the platform library correctly.
        find_library(VULKAN_LIBRARY vulkan)
        if(NOT VULKAN_LIBRARY)
            message(FATAL_ERROR "Vulkan library not found in OHOS NDK")
        endif()
        target_link_libraries(gpu-avs-workload PRIVATE ${VULKAN_LIBRARY})
    else()
        # Desktop fallback for standalone development only.
        find_package(Vulkan REQUIRED)
        target_link_libraries(gpu-avs-workload PRIVATE Vulkan::Vulkan)
    endif()

    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_VULKAN=1)

    # Shader compilation runs on the host even for an OHOS cross build.
    find_program(GLSLANG_VALIDATOR glslangValidator)
    if(NOT GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "glslangValidator not found, required for Vulkan shader compilation")
    endif()

    set(VULKAN_SHADER_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders/vulkan)
    file(MAKE_DIRECTORY ${VULKAN_SHADER_OUT_DIR})

    set(VULKAN_SHADER_OUTPUTS
        ${VULKAN_SHADER_OUT_DIR}/workload.comp.spv
        ${VULKAN_SHADER_OUT_DIR}/fullscreen.vert.spv
        ${VULKAN_SHADER_OUT_DIR}/workload.frag.spv
    )

    add_custom_command(OUTPUT ${VULKAN_SHADER_OUT_DIR}/workload.comp.spv
        COMMAND ${GLSLANG_VALIDATOR} -V
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/workload.comp
            -o ${VULKAN_SHADER_OUT_DIR}/workload.comp.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/workload.comp
        COMMENT "Compiling Vulkan compute shader: workload.comp"
        VERBATIM
    )

    add_custom_command(OUTPUT ${VULKAN_SHADER_OUT_DIR}/fullscreen.vert.spv
        COMMAND ${GLSLANG_VALIDATOR} -V
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/fullscreen.vert
            -o ${VULKAN_SHADER_OUT_DIR}/fullscreen.vert.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/fullscreen.vert
        COMMENT "Compiling Vulkan vertex shader: fullscreen.vert"
        VERBATIM
    )

    add_custom_command(OUTPUT ${VULKAN_SHADER_OUT_DIR}/workload.frag.spv
        COMMAND ${GLSLANG_VALIDATOR} -V
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/workload.frag
            -o ${VULKAN_SHADER_OUT_DIR}/workload.frag.spv
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vulkan/workload.frag
        COMMENT "Compiling Vulkan fragment shader: workload.frag"
        VERBATIM
    )

    add_custom_target(vulkan_shaders DEPENDS ${VULKAN_SHADER_OUTPUTS})
    add_dependencies(gpu-avs-workload vulkan_shaders)

    target_compile_definitions(gpu-avs-workload PRIVATE
        GPU_AVS_VULKAN_SHADER_DIR="${VULKAN_SHADER_OUT_DIR}")
else()
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_VULKAN=0)
endif()

# ================== OPENCL Backend ==================
if(GPU_AVS_ENABLE_OPENCL)
    target_sources(gpu-avs-workload PRIVATE
        src/backends/opencl/opencl_backend.cpp
    )

    if(BUILDING_FOR_OHOS)
        find_library(OPENCL_LIBRARY OpenCL)
        if(NOT OPENCL_LIBRARY)
            message(FATAL_ERROR "OpenCL library not found in OHOS NDK")
        endif()
        target_link_libraries(gpu-avs-workload PRIVATE ${OPENCL_LIBRARY})
    else()
        # Desktop fallback for standalone development only.
        find_package(OpenCL REQUIRED)
        target_link_libraries(gpu-avs-workload PRIVATE OpenCL::OpenCL)
    endif()

    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_OPENCL=1)
else()
    target_compile_definitions(gpu-avs-workload PRIVATE GPU_AVS_ENABLE_OPENCL=0)
endif()

# ================== Compile options ==================
if(MSVC)
    target_compile_options(gpu-avs-workload PRIVATE /W4)
else()
    target_compile_options(gpu-avs-workload PRIVATE
        -Wall
        -Wextra
        -Werror=return-type
    )
endif()
