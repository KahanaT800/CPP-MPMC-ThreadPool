#include <gtest/gtest.h>

TEST (SmokeTest, BasicAssertions) {
    EXPECT_TRUE(true);
    EXPECT_EQ(1 + 1, 2);
    EXPECT_STREQ("hello", "hello");
}