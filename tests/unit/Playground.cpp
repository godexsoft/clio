//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2023, the clio developers.

    Permission to use, copy, modify, and distribute this software for any
    purpose with or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL,  DIRECT,  INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

/*
 * Use this file for temporary tests and implementations.
 * Note: Please don't push your temporary work to the repo.
 */

#include "data/LedgerCache.hpp"
#include "data/Types.hpp"
#include "etlng/Models.hpp"
#include "util/MockPrometheus.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>

#include <vector>

struct PlaygroundTest : util::prometheus::WithPrometheus {};

TEST_F(PlaygroundTest, LedgerCacheGetDeletedAfterUpdate)
{
    data::LedgerCache cache;
    {
        std::vector<etlng::model::Object> updates{
            etlng::model::Object{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
                .keyRaw = "0000000000000000000000000000000000000000000000000000000000000001",
                .data = {1, 2, 3},
                .dataRaw = "123",
                .successor = "",
                .predecessor = "",
                .type = etlng::model::Object::ModType::Created,
            },
            etlng::model::Object{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
                .keyRaw = "0000000000000000000000000000000000000000000000000000000000000002",
                .data = {1, 3, 3},
                .dataRaw = "133",
                .successor = "",
                .predecessor = "",
                .type = etlng::model::Object::ModType::Created,
            }
        };

        cache.update(updates, 1);
    }

    {
        std::vector<etlng::model::Object> updates{etlng::model::Object{
            .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
            .keyRaw = "0000000000000000000000000000000000000000000000000000000000000001",
            .data = {},
            .dataRaw = "",
            .successor = "",
            .predecessor = "",
            .type = etlng::model::Object::ModType::Deleted,
        }};

        cache.update(updates, 2);
    }

    EXPECT_TRUE(cache.getDeleted(ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"}, 1)
                    .has_value());
}

TEST_F(PlaygroundTest, LedgerCacheGetDeletedAfterCacheLoad)
{
    data::LedgerCache cache;
    {
        std::vector<data::LedgerObject> updates{
            data::LedgerObject{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
                .blob = {1, 2, 3},
            },
            data::LedgerObject{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
                .blob = {1, 3, 3},
            }
        };

        cache.update(updates, 1);
    }

    {
        std::vector<data::LedgerObject> updates{
            data::LedgerObject{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"},
                .blob = {},
            },
            data::LedgerObject{
                .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
                .blob = {1, 2, 3},
            }
        };

        cache.update(updates, 2);
    }

    cache.setFull();  // finished loading

    {
        std::vector<etlng::model::Object> updates{etlng::model::Object{
            .key = ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000002"},
            .keyRaw = "0000000000000000000000000000000000000000000000000000000000000002",
            .data = {},
            .dataRaw = "",
            .successor = "",
            .predecessor = "",
            .type = etlng::model::Object::ModType::Deleted,
        }};

        cache.update(updates, 3);
    }

    EXPECT_TRUE(cache.getDeleted(ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000001"}, 2)
                    .has_value());
    EXPECT_TRUE(cache.getDeleted(ripple::uint256{"0000000000000000000000000000000000000000000000000000000000000002"}, 2)
                    .has_value());
}
