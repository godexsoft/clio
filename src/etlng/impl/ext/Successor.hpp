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
#include "data/LedgerCache.hpp"
#include "data/Types.hpp"
#include "etlng/Models.hpp"
#include "util/Assert.hpp"
#include "util/log/Logger.hpp"

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>

#include <cstdint>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>
#include <vector>

namespace etlng::impl {

class SuccessorExt {
    std::shared_ptr<BackendInterface> backend_;
    data::LedgerCache& cache_;

    util::Logger log_{"ETL"};

public:
    SuccessorExt(std::shared_ptr<BackendInterface> backend, data::LedgerCache& cache)
        : backend_(std::move(backend)), cache_(cache)
    {
    }

    void
    onLedgerData(model::LedgerData const& data) const
    {
        namespace vs = std::views;

        LOG(log_.trace()) << "got objects cnt = " << data.objects.size()
                          << "; got successors = " << data.successors.has_value();

        if (data.successors.has_value()) {
            LOG(log_.trace()) << "object neighbors included";

            for (auto const& successor : data.successors.value())
                writeIncludedSuccessor(data.seq, successor);

            auto filtered = data.objects  //
                | vs::filter([](auto const& obj) { return obj.type != model::Object::ModType::Modified; });
            for (auto const& obj : filtered)
                writeIncludedSuccessor(data.seq, obj);
        } else {
            LOG(log_.trace()) << "object neighbors not included. using cache";
            if (not cache_.isFull() or cache_.latestLedgerSequence() != data.seq)
                throw std::logic_error("Cache is not full, but object neighbors were not included");

            auto filtered = data.objects  //
                | vs::filter([](auto const& obj) { return obj.type != model::Object::ModType::Modified; });
            for (auto const& obj : filtered)
                updateSuccessorFromCache(data.seq, obj);
        }
    }

    void
    onInitialObjects(uint32_t seq, [[maybe_unused]] std::vector<model::Object> const& objs) const
    {
        ripple::uint256 prev = data::firstKey;
        while (auto cur = cache_.getSuccessor(prev, seq)) {
            ASSERT(cur.has_value(), "Successor for key {} must exist", ripple::strHex(prev));
            if (prev == data::firstKey)
                backend_->writeSuccessor(uint256ToString(prev), seq, uint256ToString(cur->key));

            if (isBookDir(cur->key, cur->blob)) {
                auto base = getBookBase(cur->key);

                // make sure the base is not an actual object
                if (not cache_.get(base, seq)) {
                    auto succ = backend_->cache().getSuccessor(base, seq);
                    ASSERT(succ.has_value(), "Book base {} must have a successor", ripple::strHex(base));

                    if (succ->key == cur->key) {
                        LOG(log_.debug())
                            << "Writing book successor = " << ripple::strHex(base) << " - " << ripple::strHex(cur->key);

                        backend_->writeSuccessor(uint256ToString(base), seq, uint256ToString(cur->key));
                    }
                }
            }

            prev = cur->key;
        }

        backend_->writeSuccessor(uint256ToString(prev), seq, uint256ToString(data::lastKey));
    }

    // void
    // onInitialTransactions([[maybe_unused]] uint32_t seq, std::vector<model::Transaction> const& tx) const
    // {
    // }
private:
    void
    writeIncludedSuccessor(uint32_t seq, model::BookSuccessor const& succ) const
    {
        auto firstBook = succ.firstBook;
        if (firstBook.empty())
            firstBook = uint256ToString(data::lastKey);
        LOG(log_.debug()) << "writing book successor " << ripple::strHex(succ.bookBase) << " - "
                          << ripple::strHex(firstBook);

        backend_->writeSuccessor(auto{succ.bookBase}, seq, std::move(firstBook));
    }

    void
    writeIncludedSuccessor(uint32_t seq, model::Object const& obj) const
    {
        ASSERT(obj.type != model::Object::ModType::Modified, "Attempt to write successor for a modified object");

        // TODO: perhaps make these optionals inside of obj and move value_or here
        auto pred = obj.predecessor;
        auto succ = obj.successor;

        if (obj.type == model::Object::ModType::Deleted) {
            LOG(log_.debug()) << "Modifying successors for deleted object " << ripple::strHex(obj.key) << " - "
                              << ripple::strHex(pred) << " - " << ripple::strHex(succ);

            backend_->writeSuccessor(std::move(pred), seq, std::move(succ));
        } else if (obj.type == model::Object::ModType::Created) {
            LOG(log_.debug()) << "adding successor for new object " << ripple::strHex(obj.key) << " - "
                              << ripple::strHex(pred) << " - " << ripple::strHex(succ);

            backend_->writeSuccessor(std::move(pred), seq, auto{obj.keyRaw});
            backend_->writeSuccessor(auto{obj.keyRaw}, seq, std::move(succ));
        }
    }

    void
    updateSuccessorFromCache(uint32_t seq, model::Object const& obj) const
    {
        auto const lb = cache_.getPredecessor(obj.key, seq).value_or(data::LedgerObject{data::firstKey, {}});
        auto const ub = cache_.getSuccessor(obj.key, seq).value_or(data::LedgerObject{data::lastKey, {}});

        auto checkBookBase = false;
        auto const isDeleted = obj.data.empty();

        if (isDeleted) {
            LOG(log_.debug()) << "writing successor for deleted object " << ripple::strHex(obj.key) << " - "
                              << ripple::strHex(lb.key) << " - " << ripple::strHex(ub.key);

            backend_->writeSuccessor(uint256ToString(lb.key), seq, uint256ToString(ub.key));

        } else {
            LOG(log_.debug()) << "writing successor for new object " << ripple::strHex(lb.key) << " - "
                              << ripple::strHex(obj.key) << " - " << ripple::strHex(ub.key);

            backend_->writeSuccessor(uint256ToString(lb.key), seq, uint256ToString(obj.key));
            backend_->writeSuccessor(uint256ToString(obj.key), seq, uint256ToString(ub.key));
        }

        if (isDeleted) {
            auto const old = cache_.getDeleted(obj.key, seq - 1);
            ASSERT(old.has_value(), "Deleted object {} must be in cache", ripple::strHex(obj.key));

            checkBookBase = isBookDir(obj.key, *old);
        } else {
            checkBookBase = isBookDir(obj.key, obj.data);
        }

        if (checkBookBase) {
            LOG(log_.debug()) << "Is book dir. Key = " << ripple::strHex(obj.key);

            auto const current = cache_.get(obj.key, seq);
            auto const bookBase = getBookBase(obj.key);

            if (isDeleted and not current.has_value()) {
                rewireSuccessor(seq, obj, bookBase);
            } else if (current.has_value()) {
                auto const successor = cache_.getSuccessor(bookBase, seq);
                ASSERT(successor.has_value(), "Book base must have a successor for seq = {}", seq);

                if (successor->key == obj.key) {
                    rewireSuccessor(seq, obj, bookBase);
                }
            }
        }
    }

    void
    rewireSuccessor(auto seq, model::Object const& obj, ripple::uint256 const& bookBase) const
    {
        auto const isDeleted = obj.data.empty();

        LOG(log_.debug()) << "Need to recalculate book base successor. base = " << ripple::strHex(bookBase)
                          << " - key = " << ripple::strHex(obj.key) << " - isDeleted = " << isDeleted
                          << " - seq = " << seq;

        if (auto succ = cache_.getSuccessor(bookBase, seq); succ.has_value()) {
            backend_->writeSuccessor(uint256ToString(bookBase), seq, uint256ToString(succ->key));

            LOG(log_.debug()) << "Updating book successor " << ripple::strHex(bookBase) << " - "
                              << ripple::strHex(succ->key);
        } else {
            backend_->writeSuccessor(uint256ToString(bookBase), seq, uint256ToString(data::lastKey));

            LOG(log_.debug()) << "Updating book successor " << ripple::strHex(bookBase) << " - "
                              << ripple::strHex(data::lastKey);
        }
    };
};

}  // namespace etlng::impl
