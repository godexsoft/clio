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
#include "etl/LoadBalancer.hpp"
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

class Loader : public LoaderInterface {
    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<etl::LoadBalancer> balancer_;
    std::shared_ptr<etl::LedgerFetcherInterface> fetcher_;
    std::shared_ptr<RegistryInterface> registry_;

    util::Logger log_{"ETL"};

public:
    using RawLedgerObjectType = org::xrpl::rpc::v1::RawLedgerObject;
    using GetLedgerResponseType = org::xrpl::rpc::v1::GetLedgerResponse;
    using OptionalGetLedgerResponseType = std::optional<GetLedgerResponseType>;

    Loader(
        std::shared_ptr<BackendInterface> backend,
        std::shared_ptr<etl::LoadBalancer> balancer,
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
        registry_->dispatch(data);
    };

    std::optional<ripple::LedgerHeader>
    loadInitialLedger(model::Batch const& data, std::vector<std::string>&& edgeKeys) override
    {
        // check that database is actually empty
        auto rng = backend_->hardFetchLedgerRangeNoThrow();
        if (rng) {
            ASSERT(false, "Database is not empty");
            return std::nullopt;
        }

        LOG(log_.debug()) << "Deserialized ledger header. " << ::util::toString(data.header);
        auto sequence = data.seq;

        auto timeDiff = ::util::timed<std::chrono::duration<double>>([this, &data, &edgeKeys, sequence]() {
            backend_->startWrites();

            LOG(log_.debug()) << "Started writes";

            // TODO: think about avoiding copy here
            backend_->writeLedger(data.header, std::string{data.rawHeader});

            LOG(log_.debug()) << "Wrote ledger";
            insertTransactions(data);
            LOG(log_.debug()) << "Inserted txns";

            size_t numWrites = 0;
            backend_->cache().setFull();

            auto seconds = ::util::timed<std::chrono::seconds>([this, &edgeKeys, sequence, &numWrites]() mutable {
                writeEdgeKeys(edgeKeys);

                ripple::uint256 prev = data::firstKey;
                while (auto cur = backend_->cache().getSuccessor(prev, sequence)) {
                    ASSERT(cur.has_value(), "Succesor for key {} must exist", ripple::strHex(prev));
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
                    static constexpr std::size_t LOG_INTERVAL = 100000;
                    if (numWrites % LOG_INTERVAL == 0 && numWrites != 0)
                        LOG(log_.info()) << "Wrote " << numWrites << " book successors";
                }

                backend_->writeSuccessor(uint256ToString(prev), sequence, uint256ToString(data::lastKey));
                ++numWrites;
            });

            LOG(log_.info()) << "Looping through cache and submitting all writes took " << seconds
                             << " seconds. numWrites = " << std::to_string(numWrites);

            LOG(log_.debug()) << "Loaded initial ledger";

            // if (not state_.get().isStopping) {
            // backend_->writeAccountTransactions(std::move(insertTxResult.accountTxData));
            // backend_->writeNFTs(insertTxResult.nfTokensData);
            // backend_->writeNFTTransactions(insertTxResult.nfTokenTxData);
            // }

            backend_->finishWrites(sequence);
        });

        LOG(log_.debug()) << "Time to download and store ledger = " << timeDiff;
        return data.header;
    }

    void
    writeEdgeKeys(std::uint32_t seq, auto const& edgeKeys)
    {
        for (auto& key : edgeKeys) {
            LOG(log_.debug()) << "Writing edge key = " << ripple::strHex(key);
            auto succ = backend_->cache().getSuccessor(*ripple::uint256::fromVoidChecked(key), seq);
            if (succ)
                backend_->writeSuccessor(std::move(key), seq, uint256ToString(succ->key));
        }
    }

    void
    insertTransactions(model::Batch const& data)
    {
        for (auto const& txn : data.transactions) {
            LOG(log_.trace()) << "Inserting transaction = " << txn.sttx.getTransactionID();

            // TODO: is account tx data core or plugin?
            backend_->writeTransaction(
                auto{txn.key},
                data.seq,
                data.header.closeTime.time_since_epoch().count(),
                auto{txn.raw},
                auto{txn.metaRaw}
            );
        }
    }

    FormattedTransactionsData
    insertTransactions(ripple::LedgerHeader const& ledger, GetLedgerResponseType& data)
    {
        FormattedTransactionsData result;

        for (auto& txn : *(data.mutable_transactions_list()->mutable_transactions())) {
            std::string* raw = txn.mutable_transaction_blob();

            ripple::SerialIter it{raw->data(), raw->size()};
            ripple::STTx const sttx{it};

            LOG(log_.trace()) << "Inserting transaction = " << sttx.getTransactionID();

            ripple::TxMeta txMeta{sttx.getTransactionID(), ledger.seq, txn.metadata_blob()};

            // TODO: this part to be moved to NFT plugin
            auto const [nftTxs, maybeNFT] = etl::getNFTDataFromTx(txMeta, sttx);
            result.nfTokenTxData.insert(result.nfTokenTxData.end(), nftTxs.begin(), nftTxs.end());
            if (maybeNFT)
                result.nfTokensData.push_back(*maybeNFT);

            // TODO: is account tx data core or plugin?
            result.accountTxData.emplace_back(txMeta, sttx.getTransactionID());
            static constexpr std::size_t KEY_SIZE = 32;
            std::string keyStr{reinterpret_cast<char const*>(sttx.getTransactionID().data()), KEY_SIZE};
            backend_->writeTransaction(
                std::move(keyStr),
                ledger.seq,
                ledger.closeTime.time_since_epoch().count(),
                std::move(*raw),
                std::move(*txn.mutable_metadata_blob())
            );
        }

        // TODO: this is also NFT plugin
        // result.nfTokensData = getUniqueNFTsDatas(result.nfTokensData);
        return result;
    }
};

}  // namespace etlng::impl
