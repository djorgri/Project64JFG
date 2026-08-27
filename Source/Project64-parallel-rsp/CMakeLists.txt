cmake_minimum_required(VERSION 3.21)

project(Project64ParallelRSP LANGUAGES C CXX)

get_filename_component(PROJECT64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROJECT64_PLUGIN_ARCHITECTURE "x64")
else()
    set(PROJECT64_PLUGIN_ARCHITECTURE "Win32")
endif()

# Parallel-RSP's test suite is useful upstream, but it is not part of the
# Project64 plugin build. Keep the embedded JIT enabled.
set(PARALLEL_RSP_TESTS OFF CACHE BOOL "" FORCE)
set(PARALLEL_RSP_DEBUG_JIT OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT64_ROOT}/external/parallel-rsp"
                 "${CMAKE_CURRENT_BINARY_DIR}/parallel-rsp")

# The upstream runner is a standalone test harness which expects Mupen-owned
# globals. It cannot link in the Project64 integration and is not part of the
# plugin deliverable, so exclude it from normal Win32 builds.
foreach(PARALLEL_RSP_AUXILIARY_TARGET rsp-runner rsp-vu-fuzzer)
    if (TARGET ${PARALLEL_RSP_AUXILIARY_TARGET})
        set_target_properties(${PARALLEL_RSP_AUXILIARY_TARGET}
                              PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()

# Upstream enables this mode in its Mupen adapter. It makes the RSP invoke
# ProcessRdpList immediately when it commits DPC_END, which is required when
# pairing it with Parallel RDP.
target_compile_definitions(parallel-rsp PRIVATE PARALLEL_INTEGRATION)
target_include_directories(parallel-rsp BEFORE PRIVATE "${CMAKE_CURRENT_LIST_DIR}")

# Parallel RSP's vector unit maps directly to host SIMD, and it picks its code
# paths at compile time. The hottest of them is the per-element operand shuffle
# run for every vector instruction: with SSSE3 it is one register-to-register
# pshufb, and without it the fallback stores the vector, rebuilds it through
# sixteen scalar byte accesses and reloads it, paying a store-forwarding stall
# on the way back. Nineteen further sites select blend and clamp paths on
# SSE4.1.
#
# This used to be gated on a four-byte pointer. The x86-64 baseline stops at
# SSE2, so that left the x64 build compiling the slow form of the very code it
# runs most, and made it slower than Win32 rather than faster. Both are
# modern-PC targets and these instructions date from 2006 and 2007.
#
# SSE4.1 is the ceiling worth asking for: the vector core has no AVX path, so
# raising the floor further would cost compatibility and gain nothing.
if (MINGW)
    target_compile_options(parallel-rsp PRIVATE -mssse3 -msse4.1)
endif()

add_library(Project64-ParallelRSP SHARED Main.cpp)
target_compile_features(Project64-ParallelRSP PRIVATE cxx_std_14)
target_compile_definitions(Project64-ParallelRSP PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(Project64-ParallelRSP PRIVATE
    "${PROJECT64_ROOT}/Source/Project64-plugin-spec"
    "${PROJECT64_ROOT}/external/parallel-rsp"
    "${PROJECT64_ROOT}/external/parallel-rsp/arch/simd/rsp")

find_package(Threads REQUIRED)
target_link_libraries(Project64-ParallelRSP PRIVATE parallel-rsp Threads::Threads)

# Keep the distribution self-contained. MinGW needs its runtime explicitly
# linked statically, while MSVC uses the static CRT for the same purpose.
if (MSVC)
    set_property(TARGET Project64-ParallelRSP PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_options(Project64-ParallelRSP PRIVATE -static -static-libgcc -static-libstdc++)
endif()
set_target_properties(Project64-ParallelRSP PROPERTIES
    OUTPUT_NAME "Project64-ParallelRSP"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT64_ROOT}/Bin/${PROJECT64_PLUGIN_ARCHITECTURE}/$<CONFIG>/Plugin/RSP")
