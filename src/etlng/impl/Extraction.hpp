//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2024, the clio developers.

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

#include "data/DBHelpers.hpp"
#include "data/Types.hpp"
#include "etl/LedgerFetcherInterface.hpp"
#include "etl/impl/LedgerFetcher.hpp"
#include "etlng/ExtractorInterface.hpp"
#include "etlng/Models.hpp"
#include "util/Assert.hpp"
#include "util/LedgerUtils.hpp"
#include "util/Profiler.hpp"
#include "util/log/Logger.hpp"

#include <sys/types.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>
#include <xrpl/proto/org/xrpl/rpc/v1/ledger.pb.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#pragma once

namespace etlng::impl {

// TODO: move all this to .cpp if possible
//

static model::Object::ModType
extractModType(auto&& type)
{
    switch (type) {
        case org::xrpl::rpc::v1::RawLedgerObject::UNSPECIFIED:
            return model::Object::ModType::Unspecified;
        case org::xrpl::rpc::v1::RawLedgerObject::CREATED:
            return model::Object::ModType::Created;
        case org::xrpl::rpc::v1::RawLedgerObject::MODIFIED:
            return model::Object::ModType::Modified;
        case org::xrpl::rpc::v1::RawLedgerObject::DELETED:
            return model::Object::ModType::Deleted;
        default:  // some gRPC system values that we don't care about
            ASSERT(false, "Oops");
    }
    std::unreachable();
}

static model::Transaction
extractTx(auto&& tx, uint32_t seq)
{
    auto raw = std::move(*tx.mutable_transaction_blob());
    ripple::SerialIter it{raw.data(), raw.size()};
    ripple::STTx const sttx{it};
    ripple::TxMeta meta{sttx.getTransactionID(), seq, tx.metadata_blob()};

    static constexpr std::size_t KeySize = 32;
    std::string keyStr{reinterpret_cast<char const*>(sttx.getTransactionID().data()), KeySize};

    return {
        .raw = std::move(raw),
        .metaRaw = std::move(*tx.mutable_metadata_blob()),
        .sttx = sttx,  // trivially copyable
        .meta = std::move(meta),
        .id = sttx.getTransactionID(),
        .key = std::move(keyStr),
        .type = sttx.getTxnType()
    };
}

static std::vector<model::Transaction>
extractTxs(auto&& transactions, uint32_t seq)
{
    namespace rg = std::ranges;
    namespace vs = std::views;

    // TODO: should be simplified with ranges::to<> when available
    std::vector<model::Transaction> output;
    output.reserve(transactions.size());

    rg::move(transactions | vs::transform([seq](auto&& tx) { return extractTx(tx, seq); }), std::back_inserter(output));
    return output;
}

static model::Object
extractObj(org::xrpl::rpc::v1::RawLedgerObject obj)
{
    auto key = ripple::uint256::fromVoidChecked(obj.key());
    ASSERT(key.has_value(), "Failed to deserialize key from void");

    auto value_or = [](std::string const& maybe, std::string fallback) -> std::string {
        if (maybe.empty())
            return fallback;
        return maybe;
    };

    return {
        .key = *key,  // trivially copyable
        .keyRaw = std::move(*obj.mutable_key()),
        .data = {obj.mutable_data()->begin(), obj.mutable_data()->end()},
        .dataRaw = std::move(*obj.mutable_data()),
        .successor = value_or(obj.successor(), uint256ToString(data::firstKey)),
        .predecessor = value_or(obj.predecessor(), uint256ToString(data::lastKey)),
        .type = extractModType(obj.mod_type()),
    };
}

static std::vector<model::Object>
extractObjs(auto&& objects)
{
    namespace rg = std::ranges;
    namespace vs = std::views;

    // TODO: should be simplified with ranges::to<> when available
    std::vector<model::Object> output;
    output.reserve(objects.size());

    rg::move(objects | vs::transform([](auto&& obj) { return extractObj(obj); }), std::back_inserter(output));
    return output;
}

static model::BookSuccessor
extractSuccessor(org::xrpl::rpc::v1::BookSuccessor const& successor)
{
    return {
        .firstBook = successor.first_book(),
        .bookBase = successor.book_base(),
    };
}

static std::optional<std::vector<model::BookSuccessor>>
maybeExtractSuccessors(org::xrpl::rpc::v1::GetLedgerResponse const& data)
{
    namespace rg = std::ranges;
    namespace vs = std::views;

    if (not data.object_neighbors_included())
        return std::nullopt;

    // TODO: should be simplified with ranges::to<> when available
    std::vector<model::BookSuccessor> output;
    output.reserve(data.book_successors_size());

    rg::move(
        data.book_successors() | vs::transform([](auto&& obj) { return extractSuccessor(obj); }),
        std::back_inserter(output)
    );
    return output;
}

// fetches the data in gRPC and transforms to local representation
class Extractor : public ExtractorInterface {
    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;

    util::Logger log_{"ETL"};

private:
    // TODO: move all this to .cpp if possible
    //
    static auto
    makeExtractor(uint32_t seq)
    {
        return [seq](auto&& data) {
            auto header = ::util::deserializeHeader(ripple::makeSlice(data.ledger_header()));

            return std::make_optional<model::LedgerData>({
                .transactions =
                    extractTxs(std::move(*data.mutable_transactions_list()->mutable_transactions()), header.seq),
                .objects = extractObjs(std::move(*data.mutable_ledger_objects()->mutable_objects())),
                .successors = maybeExtractSuccessors(data),
                .edgeKeys = std::nullopt,
                .header = header,
                .rawHeader = std::move(*data.mutable_ledger_header()),
                .seq = seq,
            });
        };
    }

public:
    Extractor(std::shared_ptr<etl::LedgerFetcherInterface> fetcher) : fetcher_(std::move(fetcher))
    {
    }

    std::optional<model::LedgerData>
    extractDiff(uint32_t seq) override
    {
        LOG(log_.debug()) << "Extracting DIFF " << seq;

        auto [batch, time] = ::util::timed<std::chrono::duration<double>>([this, seq] {
            return fetcher_->fetchDataAndDiff(seq).and_then(makeExtractor(seq));
        });

        LOG(log_.debug()) << "Extracted and Transformed diff for " << seq << " in " << time << "ms";

        // can be nullopt. this means that either the server is stopping or another node took over ETL writing.
        return batch;
    }

    std::optional<model::LedgerData>
    extractFull(uint32_t seq) override
    {
        LOG(log_.debug()) << "Extracting FULL " << seq;

        auto [batch, time] = ::util::timed<std::chrono::duration<double>>([this, seq] {
            return fetcher_->fetchData(seq).and_then(makeExtractor(seq));
        });

        LOG(log_.debug()) << "Extracted and Transformed full ledger for " << seq << " in " << time << "ms";

        // can be nullopt. this means that either the server is stopping or another node took over ETL writing.
        return batch;
    }
};

}  // namespace etlng::impl
