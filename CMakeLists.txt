cmake_minimum_required(VERSION 3.16)

project(cpu_avs_workload VERSION 1.0.0 LANGUAGES CXX)

option(CPU_AVS_STATIC_CXX_RUNTIME "Link the compiler C++ runtime statically when supported" ON)

include(CTest)
add_subdirectory(cpu)
