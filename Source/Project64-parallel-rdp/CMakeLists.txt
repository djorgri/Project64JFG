cmake_minimum_required(VERSION 3.21)
project(Project64ParallelRDP LANGUAGES C CXX)

get_filename_component(PROJECT64_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(PROJECT64_PLUGIN_ARCHITECTURE "x64")
else()
    set(PROJECT64_PLUGIN_ARCHITECTURE "Win32")
endif()

# Granite's shader compiler uses the static MSVC runtime. Keep every target in
# this build on the same runtime to avoid a mixed-CRT link.
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Parallel RDP brings Granite and its shader compiler as nested CMake targets.
# The integration examples are not needed by the Project64 plugin.
set(RDP_INTEGRATION_EXAMPLE OFF CACHE BOOL "" FORCE)
set(GRANITE_ISPC_TEXTURE_COMPRESSION OFF CACHE BOOL "" FORCE)
set(GRANITE_ASTC_ENCODER_COMPRESSION OFF CACHE BOOL "" FORCE)
add_subdirectory("${PROJECT64_ROOT}/external/parallel-rdp"
                 "${CMAKE_CURRENT_BINARY_DIR}/parallel-rdp")

# The usual CMake target reads shaders from the source checkout. Build the
# upstream slangmosh header instead, so the released DLL is self-contained.
set(PARALLEL_RDP_SOURCE_DIR "${PROJECT64_ROOT}/external/parallel-rdp")
set(PARALLEL_RDP_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PARALLEL_RDP_SHADER_HEADER "${PARALLEL_RDP_GENERATED_DIR}/shaders/slangmosh.hpp")
file(GLOB PARALLEL_RDP_SHADER_SOURCES
    "${PARALLEL_RDP_SOURCE_DIR}/parallel-rdp/shaders/*")
add_custom_command(
    OUTPUT "${PARALLEL_RDP_SHADER_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PARALLEL_RDP_GENERATED_DIR}/shaders"
    COMMAND $<TARGET_FILE:slangmosh> --strip -O --vk13 --namespace RDP
            "${PARALLEL_RDP_SOURCE_DIR}/parallel-rdp/shaders/slangmosh.json"
            --output "${PARALLEL_RDP_SHADER_HEADER}"
    DEPENDS slangmosh "${PARALLEL_RDP_SOURCE_DIR}/parallel-rdp/shaders/slangmosh.json"
            ${PARALLEL_RDP_SHADER_SOURCES}
    WORKING_DIRECTORY "${PARALLEL_RDP_SOURCE_DIR}")
add_custom_target(Project64ParallelRDPShaders DEPENDS "${PARALLEL_RDP_SHADER_HEADER}")
add_dependencies(parallel-rdp Project64ParallelRDPShaders)
target_include_directories(parallel-rdp PRIVATE "${PARALLEL_RDP_GENERATED_DIR}")

# Upstream's default CMake configuration defines PARALLEL_RDP_SHADER_DIR,
# which selects runtime shader files. Remove it from both target interfaces to
# select the embedded slangmosh header generated above.
foreach(property COMPILE_DEFINITIONS INTERFACE_COMPILE_DEFINITIONS)
    get_property(definitions TARGET parallel-rdp PROPERTY ${property})
    list(FILTER definitions EXCLUDE REGEX "^PARALLEL_RDP_SHADER_DIR")
    set_property(TARGET parallel-rdp PROPERTY ${property} "${definitions}")
endforeach()

add_library(Project64-ParallelRDP SHARED Main.cpp Settings.cpp ParallelRDP.rc)
target_compile_features(Project64-ParallelRDP PRIVATE cxx_std_14)
target_compile_definitions(Project64-ParallelRDP PRIVATE NOMINMAX VK_USE_PLATFORM_WIN32_KHR)
target_include_directories(Project64-ParallelRDP PRIVATE
    "${PROJECT64_ROOT}/Source/Project64-plugin-spec"
    "${PROJECT64_ROOT}/external/parallel-rdp/Granite/application/global")
target_link_libraries(Project64-ParallelRDP PRIVATE parallel-rdp granite-application-global-init user32 gdi32 dwmapi comctl32)

set_target_properties(Project64-ParallelRDP PROPERTIES
    OUTPUT_NAME "Project64-ParallelRDP"
    PREFIX ""
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT64_ROOT}/Bin/${PROJECT64_PLUGIN_ARCHITECTURE}/$<CONFIG>/Plugin/GFX")

# Explicit headless GPU regression target; not part of the released plugin.
add_executable(Project64JfgHudLayerTest EXCLUDE_FROM_ALL tests/JfgHudLayerTest.cpp)
target_compile_features(Project64JfgHudLayerTest PRIVATE cxx_std_14)
target_compile_definitions(Project64JfgHudLayerTest PRIVATE NOMINMAX)
target_include_directories(Project64JfgHudLayerTest PRIVATE
    "${PROJECT64_ROOT}/external/parallel-rdp/Granite/application/global")
target_link_libraries(Project64JfgHudLayerTest PRIVATE parallel-rdp granite-application-global-init)
