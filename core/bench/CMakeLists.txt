add_executable(book_bench book_bench.cpp)
target_link_libraries(book_bench PRIVATE quantxecute_core)
include(Warnings)
qx_apply_warnings(book_bench)

add_executable(replay_bench replay_bench.cpp)
target_link_libraries(replay_bench PRIVATE quantxecute_core)
qx_apply_warnings(replay_bench)
