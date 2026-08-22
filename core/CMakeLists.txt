add_library(quantxecute_core INTERFACE)
add_library(qx::core ALIAS quantxecute_core)

target_include_directories(quantxecute_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/third_party)

target_compile_features(quantxecute_core INTERFACE cxx_std_20)
