#include "util/requests/impl/SslContext.hpp"

#include <gtest/gtest.h>

using namespace util::requests::impl;

TEST(SslContext, Create)
{
    auto ctx = getClientSslContext();
    EXPECT_TRUE(ctx);
}

TEST(SslContext, IsCached)
{
    auto first = getClientSslContext();
    auto second = getClientSslContext();

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    // Context is shared
    EXPECT_EQ(&first->get(), &second->get());
}
