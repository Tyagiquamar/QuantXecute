include(TestHelpers)

set(QX_TEST_LINK_LIBS quantxecute_core)

qx_add_test(types_test SRC types_test.cpp)
qx_add_test(book_test SRC book_test.cpp)
qx_add_test(decoder_test SRC decoder_test.cpp)
qx_add_test(sequence_test SRC sequence_test.cpp)
qx_add_test(execution_test SRC execution_test.cpp)
qx_add_test(replay_test SRC replay_test.cpp)
qx_add_test(parity_test SRC parity_test.cpp)
qx_add_test(metrics_test SRC metrics_test.cpp)
