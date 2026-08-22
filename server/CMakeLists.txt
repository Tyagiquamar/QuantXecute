add_executable(quantxecute-server
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ApiServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HttpServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/WebSocket.cpp)

target_include_directories(quantxecute-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(quantxecute-server PRIVATE quantxecute_feed)

find_package(Threads REQUIRED)
target_link_libraries(quantxecute-server PRIVATE Threads::Threads)

include(Warnings)
qx_apply_warnings(quantxecute-server)
