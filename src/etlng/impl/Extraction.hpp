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

// fetches the data in gRPC and transforms to local representation
class Extractor : public ExtractorInterface {
    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;

    util::Logger log_{"ETL"};

private:
    // TODO: move all this to .cpp if possible
    //
    auto
    makeExtractor(uint32_t seq)
    {
        return [this, seq](auto&& data) {
            auto header = ::util::deserializeHeader(ripple::makeSlice(data.ledger_header()));
            return std::make_optional<model::Batch>({
                .transactions =
                    extractTxs(std::move(*data.mutable_transactions_list()->mutable_transactions()), header.seq),
                .objects = extractObjs(std::move(*data.mutable_ledger_objects()->mutable_objects())),
                .header = header,
                .rawHeader = std::move(*data.mutable_ledger_header()),
                .seq = seq,
                .areNeighborsIncluded = data.object_neighbors_included(),
            });
        };
    }

public:
    Extractor(std::shared_ptr<etl::LedgerFetcherInterface> fetcher) : fetcher_(std::move(fetcher))
    {
    }

    std::optional<model::Batch>
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

    std::optional<model::Batch>
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

private:
    // TODO: move all this to .cpp if possible
    //
    std::vector<model::Transaction>
    extractTxs(auto&& transactions, uint32_t seq)
    {
        namespace rg = std::ranges;
        namespace vs = std::views;

        // TODO: should be simplified with ranges::to<> when available
        std::vector<model::Transaction> output;
        output.reserve(transactions.size());

        rg::move(
            transactions | vs::transform([this, seq](auto&& tx) { return this->extractTx(tx, seq); }),
            std::back_inserter(output)
        );
        return output;
    }

    model::Transaction
    extractTx(auto&& tx, uint32_t seq)
    {
        auto raw = std::move(*tx.mutable_transaction_blob());
        ripple::SerialIter it{raw.data(), raw.size()};
        ripple::STTx const sttx{it};
        ripple::TxMeta meta{sttx.getTransactionID(), seq, tx.metadata_blob()};

        static constexpr std::size_t KEY_SIZE = 32;
        std::string keyStr{reinterpret_cast<char const*>(sttx.getTransactionID().data()), KEY_SIZE};

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

    std::vector<model::Object>
    extractObjs([[maybe_unused]] auto&& objects)
    {
        namespace rg = std::ranges;
        namespace vs = std::views;

        // TODO: should be simplified with ranges::to<> when available
        std::vector<model::Object> output;
        output.reserve(objects.size());

        rg::move(
            objects | vs::transform([this](auto&& obj) { return this->extractObj(obj); }), std::back_inserter(output)
        );
        return output;
    }

    model::Object
    extractObj(auto&& obj)
    {
        auto key = ripple::uint256::fromVoidChecked(obj.key());
        ASSERT(key.has_value(), "Failed to deserialize key from void");

        return {
            .key = std::move(*key),
            .data = {obj.mutable_data()->begin(), obj.mutable_data()->end()},
            .type = extractModType(obj.mod_type()),
        };
    }

    model::Object::ModType
    extractModType(auto&& type)
    {
        switch (type) {
            case org::xrpl::rpc::v1::RawLedgerObject::UNSPECIFIED:
                return model::Object::ModType::UNSPECIFIED;
            case org::xrpl::rpc::v1::RawLedgerObject::CREATED:
                return model::Object::ModType::CREATED;
            case org::xrpl::rpc::v1::RawLedgerObject::MODIFIED:
                return model::Object::ModType::MODIFIED;
            case org::xrpl::rpc::v1::RawLedgerObject::DELETED:
                return model::Object::ModType::DELETED;
            default:  // some gRPC system values that we don't care about
                ASSERT(false, "Oops");
        }
        std::unreachable();
    }
};

}  // namespace etlng::impl
