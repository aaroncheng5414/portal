cmake_minimum_required(VERSION 3.20)
project(VulkanDemo LANGUAGES CXX)

option(CMAKE_EXPORT_COMPILE_COMMANDS "Generate compile commands for IntelliSense" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED COMPONENTS glslc)
find_package(glfw3 REQUIRED)

add_executable(main main.cpp)
target_link_libraries(main PRIVATE Vulkan::Vulkan glfw)
if(MINGW)
    target_link_options(main PRIVATE -municode)
endif()
target_compile_definitions(main PRIVATE OBJ_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}")

foreach(stage IN ITEMS vert frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/mesh.${stage}")
    set(shader "${CMAKE_CURRENT_BINARY_DIR}/mesh.${stage}.spv")
    add_custom_command(
        OUTPUT "${shader}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.0 "${source}" -o "${shader}"
        DEPENDS "${source}"
        VERBATIM
    )
    target_sources(main PRIVATE "${shader}")
endforeach()
