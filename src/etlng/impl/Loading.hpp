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

#pragma once

#include "data/BackendInterface.hpp"
#include "data/DBHelpers.hpp"
#include "data/Types.hpp"
#include "etl/LedgerFetcherInterface.hpp"
#include "etl/LoadBalancerInterface.hpp"
#include "etl/NFTHelpers.hpp"
#include "etl/impl/LedgerLoader.hpp"
#include "etlng/LoaderInterface.hpp"
#include "etlng/Models.hpp"
#include "etlng/RegistryInterface.hpp"
#include "util/Assert.hpp"
#include "util/LedgerUtils.hpp"
#include "util/Profiler.hpp"
#include "util/log/Logger.hpp"

#include <org/xrpl/rpc/v1/ledger.pb.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/proto/org/xrpl/rpc/v1/get_ledger.pb.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TxMeta.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace etlng::impl {

class Loader : public LoaderInterface, public InitialLoadObserverInterface {
    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<etl::LoadBalancerInterface> balancer_;
    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;
    std::shared_ptr<RegistryInterface> registry_;

    util::Logger log_{"ETL"};

public:
    using RawLedgerObjectType = org::xrpl::rpc::v1::RawLedgerObject;
    using GetLedgerResponseType = org::xrpl::rpc::v1::GetLedgerResponse;
    using OptionalGetLedgerResponseType = std::optional<GetLedgerResponseType>;

    Loader(
        std::shared_ptr<BackendInterface> backend,
        std::shared_ptr<etl::LoadBalancerInterface> balancer,
        std::shared_ptr<etl::LedgerFetcherInterface> fetcher,
        std::shared_ptr<RegistryInterface> registry
    )
        : backend_(std::move(backend))
        , balancer_(std::move(balancer))
        , fetcher_(std::move(fetcher))
        , registry_(std::move(registry))
    {
    }

    void
    load(model::Batch const& data) override
    {
        LOG(log_.debug()) << "Loading a batch for " << data.seq;

        // backend_->startWrites();
        // LOG(log_.debug()) << "Started writes";

        registry_->dispatch(data);

        // LOG(log_.debug()) << "Committing writes for " << data.seq;
        // backend_->finishWrites(data.seq);
    };

    void
    onInitialLoadGotMoreObjects(uint32_t seq, std::vector<model::Object> const& data) override
    {
        std::string lastKey;

        LOG(log_.debug()) << "On initial load: got more objects for seq " << seq << ". size = " << data.size();
        for (auto const& obj : data) {
            if (!lastKey.empty())
                backend_->writeSuccessor(std::move(lastKey), seq, auto{obj.keyRaw});

            backend_->writeLedgerObject(auto{obj.keyRaw}, seq, auto{obj.dataRaw});
            lastKey = obj.keyRaw;
        }

        registry_->dispatchInitialObjects(seq, data);
    }

    std::optional<ripple::LedgerHeader>
    loadInitialLedger(model::Batch const& data, std::vector<std::string> const& edgeKeys) override
    {
        // check that database is actually empty
        auto rng = backend_->hardFetchLedgerRangeNoThrow();
        if (rng) {
            ASSERT(false, "Database is not empty");
            return std::nullopt;
        }

        LOG(log_.debug()) << "Deserialized ledger header. " << ::util::toString(data.header);
        auto sequence = data.seq;

        backend_->startWrites();
        LOG(log_.debug()) << "Started writes";

        // TODO: think about avoiding copy here
        backend_->writeLedger(data.header, std::string{data.rawHeader});
        LOG(log_.debug()) << "Wrote ledger";

        insertTransactions(data);
        registry_->dispatchInitialTransactions(sequence, data.transactions);
        LOG(log_.debug()) << "Inserted txns";

        ASSERT(backend_->cache().isFull(), "Cache must be full at this point");

        size_t numWrites = 0;
        auto seconds = ::util::timed<std::chrono::seconds>([this, &edgeKeys, sequence, &numWrites]() mutable {
            writeEdgeKeys(sequence, edgeKeys);

            ripple::uint256 prev = data::firstKey;
            while (auto cur = backend_->cache().getSuccessor(prev, sequence)) {
                ASSERT(cur.has_value(), "Successor for key {} must exist", ripple::strHex(prev));
                if (prev == data::firstKey)
                    backend_->writeSuccessor(uint256ToString(prev), sequence, uint256ToString(cur->key));

                if (isBookDir(cur->key, cur->blob)) {
                    auto base = getBookBase(cur->key);

                    // make sure the base is not an actual object
                    if (!backend_->cache().get(base, sequence)) {
                        auto succ = backend_->cache().getSuccessor(base, sequence);
                        ASSERT(succ.has_value(), "Book base {} must have a successor", ripple::strHex(base));

                        if (succ->key == cur->key) {
                            LOG(log_.debug()) << "Writing book successor = " << ripple::strHex(base) << " - "
                                              << ripple::strHex(cur->key);

                            backend_->writeSuccessor(uint256ToString(base), sequence, uint256ToString(cur->key));
                        }
                    }

                    ++numWrites;
                }

                prev = cur->key;
                static constexpr std::size_t LogInterval = 100000uz;
                if (numWrites % LogInterval == 0 && numWrites != 0)
                    LOG(log_.info()) << "Wrote " << numWrites << " book successors";
            }

            backend_->writeSuccessor(uint256ToString(prev), sequence, uint256ToString(data::lastKey));
            ++numWrites;
        });

        LOG(log_.info()) << "Looping through cache and submitting all writes took " << seconds
                         << " seconds. numWrites = " << std::to_string(numWrites);

        LOG(log_.debug()) << "Loaded initial ledger";
        backend_->finishWrites(sequence);

        return {data.header};
    }

    void
    writeEdgeKeys(std::uint32_t seq, auto const& edgeKeys)
    {
        for (auto& key : edgeKeys) {
            LOG(log_.debug()) << "Writing edge key = " << ripple::strHex(key);
            auto succ = backend_->cache().getSuccessor(*ripple::uint256::fromVoidChecked(key), seq);
            if (succ)
                backend_->writeSuccessor(auto{key}, seq, uint256ToString(succ->key));
        }
    }

    void
    insertTransactions(model::Batch const& data)
    {
        for (auto const& txn : data.transactions) {
            LOG(log_.trace()) << "Inserting transaction = " << txn.sttx.getTransactionID();

            backend_->writeAccountTransaction({txn.meta, txn.sttx.getTransactionID()});
            backend_->writeTransaction(
                auto{txn.key},
                data.seq,
                data.header.closeTime.time_since_epoch().count(),
                auto{txn.raw},
                auto{txn.metaRaw}
            );
        }
    }
};

}  // namespace etlng::impl
