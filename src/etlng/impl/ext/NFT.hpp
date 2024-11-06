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
#include "etl/NFTHelpers.hpp"
#include "etlng/Models.hpp"
#include "util/log/Logger.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace etlng::impl {

class NFTExt {
    std::shared_ptr<BackendInterface> backend_;
    util::Logger log_{"ETL"};

public:
    // using spec = Spec<
    //     ripple::TxType::ttNFTOKEN_MINT,
    //     ripple::TxType::ttNFTOKEN_BURN,
    //     ripple::TxType::ttNFTOKEN_ACCEPT_OFFER,
    //     ripple::TxType::ttNFTOKEN_CANCEL_OFFER,
    //     ripple::TxType::ttNFTOKEN_CREATE_OFFER>;

    NFTExt(std::shared_ptr<BackendInterface> backend) : backend_(std::move(backend))
    {
    }

    void
    onLedgerData(model::LedgerData const& data) const
    {
        writeNFTs(data);
    }

    // FIXME: this approach currently does not work for nft because we need to sort and unique
    // the nft data at the end. so we just process the entire batch as before.

    // void
    // onTransaction(model::Transaction const& tx) const
    // {
    //     // TODO: save nft data
    // }

    void
    onInitialObject(uint32_t seq, model::Object const& obj) const
    {
        LOG(log_.trace()) << "got initial object with key = " << obj.key;
        backend_->writeNFTs(etl::getNFTDataFromObj(seq, obj.keyRaw, obj.dataRaw));
    }

    void
    onInitialData(model::LedgerData const& data) const
    {
        LOG(log_.trace()) << "got initial TXS cnt = " << data.transactions.size();
        writeNFTs(data);
    }

private:
    void
    writeNFTs(model::LedgerData const& data) const
    {
        std::vector<NFTsData> nfts;
        std::vector<NFTTransactionsData> nftTxs;

        for (auto const& tx : data.transactions) {
            auto const [txs, maybeNFT] = etl::getNFTDataFromTx(tx.meta, tx.sttx);
            nftTxs.insert(nftTxs.end(), txs.begin(), txs.end());
            if (maybeNFT)
                nfts.push_back(*maybeNFT);
        }

        // TODO: consider pulling these backend functions into this extension instead
        backend_->writeNFTs(etl::getUniqueNFTsDatas(nfts)
        );  // this is uniqued so that we only write latest modification. maybe we don't need that. then we can use the
            // below approach instead. alternatively we could have state in the extension and onStart(seq) onFinish(seq)
            // hooks to cleanup and send uniqued data to backend.
        backend_->writeNFTTransactions(nftTxs);
    }
};

}  // namespace etlng::impl
