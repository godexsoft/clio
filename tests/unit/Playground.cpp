/*
 * Use this file for temporary tests and implementations.
 * Note: Please don't push your temporary work to the repo.
 */

#include <boost/json/basic_parser_impl.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <string>

using namespace testing;

namespace {

// Hard element-count limit per array nesting level.
// Returning false from any handler callback aborts the parse immediately —
// no further bytes are consumed, so the caller can stop feeding chunks.
static constexpr std::size_t kMaxArrayElements = 5;

// Custom error: reuse an existing boost::json error category for simplicity.
// In production code you'd define a custom error_category.
static boost::system::error_code
arrayTooLargeError()
{
    return boost::json::error::array_too_large;
}

struct LimitedArrayHandler {
    // Limits declared here are checked by basic_parser before calling our
    // callbacks, providing a second safety net beyond our own counting.
    static constexpr std::size_t max_array_size = kMaxArrayElements;
    static constexpr std::size_t max_object_size = std::size_t(-1);
    static constexpr std::size_t max_string_size = std::size_t(-1);
    static constexpr std::size_t max_key_size = std::size_t(-1);

    // Track how many top-level array elements we've seen so far.
    // Each value-completing event (string/number/bool/null/nested end) that
    // fires while we're inside the root array increments this.
    std::size_t depth_ = 0;
    std::size_t rootElementCount_ = 0;
    std::size_t bytesConsumedAtAbort_ = 0;  // for demo purposes

    bool
    checkLimit(boost::system::error_code& ec)
    {
        if (depth_ == 1 && rootElementCount_ > kMaxArrayElements) {
            ec = arrayTooLargeError();
            return false;
        }
        return true;
    }

    // Called after every complete scalar/nested-value while inside any array.
    // depth_ == 1 means we're directly inside the root array.
    void
    countElement()
    {
        if (depth_ == 1)
            ++rootElementCount_;
    }

    bool
    on_document_begin(boost::system::error_code&)
    {
        return true;
    }
    bool
    on_document_end(boost::system::error_code&)
    {
        return true;
    }

    bool
    on_array_begin(boost::system::error_code&)
    {
        ++depth_;
        return true;
    }

    bool
    on_array_end(std::size_t /*n*/, boost::system::error_code& ec)
    {
        --depth_;
        countElement();  // the nested array itself is one element of the parent
        return checkLimit(ec);
    }

    bool
    on_object_begin(boost::system::error_code&)
    {
        return true;
    }

    bool
    on_object_end(std::size_t /*n*/, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_string_part(boost::json::string_view, std::size_t, boost::system::error_code&)
    {
        return true;
    }

    bool
    on_string(boost::json::string_view, std::size_t, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_key_part(boost::json::string_view, std::size_t, boost::system::error_code&)
    {
        return true;
    }
    bool
    on_key(boost::json::string_view, std::size_t, boost::system::error_code&)
    {
        return true;
    }
    bool
    on_number_part(boost::json::string_view, boost::system::error_code&)
    {
        return true;
    }

    bool
    on_int64(std::int64_t, boost::json::string_view, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_uint64(std::uint64_t, boost::json::string_view, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_double(double, boost::json::string_view, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_bool(bool, boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_null(boost::system::error_code& ec)
    {
        countElement();
        return checkLimit(ec);
    }

    bool
    on_comment_part(boost::json::string_view, boost::system::error_code&)
    {
        return true;
    }
    bool
    on_comment(boost::json::string_view, boost::system::error_code&)
    {
        return true;
    }
};

// Feed the JSON in small chunks and return how many bytes were consumed before
// the parser either finished or aborted. The caller never needs to buffer the
// full input in memory.
struct ParseResult {
    boost::system::error_code ec;
    std::size_t bytesConsumed;
    std::size_t elementsSeen;
};

ParseResult
parseInChunks(std::string const& json, std::size_t chunkSize = 4)
{
    boost::json::basic_parser<LimitedArrayHandler> parser({});
    boost::system::error_code ec;
    std::size_t total = 0;

    for (std::size_t offset = 0; offset < json.size(); offset += chunkSize) {
        bool const isLast = (offset + chunkSize >= json.size());
        auto const chunk = boost::json::string_view(json).substr(offset, chunkSize);

        std::size_t consumed = parser.write_some(!isLast, chunk.data(), chunk.size(), ec);
        total += consumed;

        if (ec)
            break;

        if (isLast)
            break;
    }

    return {ec, total, parser.handler().rootElementCount_};
}

}  // namespace

// Passes: 3 elements ≤ limit of 5
TEST(SaxEarlyBailTest, SmallArrayIsAccepted)
{
    auto [ec, bytes, elements] = parseInChunks("[1, 2, 3]");
    EXPECT_FALSE(ec) << ec.message();
    EXPECT_EQ(elements, 3u);
    EXPECT_EQ(bytes, 9u);  // full input consumed
}

// Passes: exactly at the limit
TEST(SaxEarlyBailTest, ArrayAtLimitIsAccepted)
{
    auto [ec, bytes, elements] = parseInChunks("[1, 2, 3, 4, 5]");
    EXPECT_FALSE(ec) << ec.message();
    EXPECT_EQ(elements, 5u);
}

// Key test: 6 elements > limit of 5 → parser aborts after the 6th element
// is complete, long before the rest of the (potentially huge) array is read.
TEST(SaxEarlyBailTest, LargeArrayIsRejectedEarly)
{
    // Imagine this continues for millions of elements — we stop after #6.
    std::string const input = "[1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12]";
    auto [ec, bytes, elements] = parseInChunks(input, /*chunkSize=*/4);

    EXPECT_TRUE(ec);
    EXPECT_EQ(ec, boost::json::error::array_too_large);

    // We bailed long before consuming the full 41-byte input.
    EXPECT_LT(bytes, input.size());

    // basic_parser enforces max_array_size=5 before calling our callback for the
    // 6th element, so our counter stops at 5.
    EXPECT_EQ(elements, 5u);
}

// Nested arrays: the nested array counts as one element of the root.
TEST(SaxEarlyBailTest, NestedArrayCountsAsOneElement)
{
    // Root has 3 elements: [1,2], [3,4], [5,6] — all within the limit.
    auto [ec, bytes, elements] = parseInChunks("[[1,2],[3,4],[5,6]]");
    EXPECT_FALSE(ec) << ec.message();
    EXPECT_EQ(elements, 3u);
}
