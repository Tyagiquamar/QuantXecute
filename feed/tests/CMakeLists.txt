include(TestHelpers)

set(QX_TEST_LINK_LIBS quantxecute_feed)

qx_add_test(feed_test SRC feed_test.cpp)
