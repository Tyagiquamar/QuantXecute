cmake_minimum_required(VERSION 3.21)
project(TradeSimulator)

# Use modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set vcpkg triplet (optional but recommended)
set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "Vcpkg target triplet")

# Use vcpkg toolchain file for dependency management
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

# Find required packages
find_package(glfw3 CONFIG REQUIRED)
            # Use 'glfw' not 'glfw3'
find_package(OpenGL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(ixwebsocket CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Source files
file(GLOB_RECURSE MODEL_SRC src/models/*.cpp)
file(GLOB_RECURSE UI_SRC src/ui/*.cpp)
file(GLOB_RECURSE BACKEND_SRC src/ui/backends/*.cpp)
set(SRC_FILES src/main.cpp ${MODEL_SRC} ${UI_SRC} ${BACKEND_SRC})

# Add executable
add_executable(TradeSimulator ${SRC_FILES})

# Link libraries
target_link_libraries(TradeSimulator PRIVATE 
    glfw
    OpenGL::GL
    nlohmann_json::nlohmann_json
    ixwebsocket::ixwebsocket
    imgui::imgui
)

# Include directories
target_include_directories(TradeSimulator PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/ui/backend
)

# Optional: Show source groups in Visual Studio
source_group(TREE ${CMAKE_SOURCE_DIR}/src FILES ${SRC_FILES})