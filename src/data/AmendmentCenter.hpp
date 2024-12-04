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

#include "data/AmendmentCenterInterface.hpp"
#include "data/BackendInterface.hpp"
#include "data/Types.hpp"

#include <boost/asio/spawn.hpp>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/digest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#define REGISTER(name)                                   \
    inline static impl::WritingAmendmentKey const name = \
        impl::WritingAmendmentKey(std::string(BOOST_PP_STRINGIZE(name)))

namespace data {
namespace impl {

struct WritingAmendmentKey : AmendmentKey {
    explicit WritingAmendmentKey(std::string amendmentName);
};

}  // namespace impl

/**
 * @brief List of supported amendments
 */
struct Amendments {
    // NOTE: if Clio wants to report it supports an Amendment it should be listed here.
    // Whether an amendment is obsolete and/or supported by libxrpl is extracted directly from libxrpl.
    // If an amendment is in the list below it just means Clio did whatever changes needed to support it.
    // Most of the time it's going to be no changes at all.

    /** @cond */
    REGISTER(kOWNER_PAYS_FEE);
    REGISTER(kFLOW);
    REGISTER(kFLOW_CROSS);
    REGISTER(kFIX1513);
    REGISTER(kDEPOSIT_AUTH);
    REGISTER(kCHECKS);
    REGISTER(kFIX1571);
    REGISTER(kFIX1543);
    REGISTER(kFIX1623);
    REGISTER(kDEPOSIT_PREAUTH);
    REGISTER(kFIX1515);
    REGISTER(kFIX1578);
    REGISTER(kMULTI_SIGN_RESERVE);
    REGISTER(kFIX_TAKER_DRY_OFFER_REMOVAL);
    REGISTER(kFIX_MASTER_KEY_AS_REGULAR_KEY);
    REGISTER(kFIX_CHECK_THREADING);
    REGISTER(kFIX_PAY_CHAN_RECIPIENT_OWNER_DIR);
    REGISTER(kDELETABLE_ACCOUNTS);
    REGISTER(kFIX_QUALITY_UPPER_BOUND);
    REGISTER(kREQUIRE_FULLY_CANONICAL_SIG);
    REGISTER(kFIX1781);
    REGISTER(kHARDENED_VALIDATIONS);
    REGISTER(kFIX_AMENDMENT_MAJORITY_CALC);
    REGISTER(kNEGATIVE_UNL);
    REGISTER(kTICKET_BATCH);
    REGISTER(kFLOW_SORT_STRANDS);
    REGISTER(kFIX_ST_AMOUNT_CANONICALIZE);
    REGISTER(kFIX_RM_SMALL_INCREASED_Q_OFFERS);
    REGISTER(kCHECK_CASH_MAKES_TRUST_LINE);
    REGISTER(kEXPANDED_SIGNER_LIST);
    REGISTER(kNON_FUNGIBLE_TOKENS_V1_1);
    REGISTER(kFIX_TRUST_LINES_TO_SELF);
    REGISTER(kFIX_REMOVE_NF_TOKEN_AUTO_TRUST_LINE);
    REGISTER(kIMMEDIATE_OFFER_KILLED);
    REGISTER(kDISALLOW_INCOMING);
    REGISTER(kXRP_FEES);
    REGISTER(kFIX_UNIVERSAL_NUMBER);
    REGISTER(kFIX_NON_FUNGIBLE_TOKENS_V1_2);
    REGISTER(kFIX_NF_TOKEN_REMINT);
    REGISTER(kFIX_REDUCED_OFFERS_V1);
    REGISTER(kCLAWBACK);
    REGISTER(kAMM);
    REGISTER(kX_CHAIN_BRIDGE);
    REGISTER(kFIX_DISALLOW_INCOMING_V1);
    REGISTER(kDID);
    REGISTER(kFIX_FILL_OR_KILL);
    REGISTER(kFIX_NF_TOKEN_RESERVE);
    REGISTER(kFIX_INNER_OBJ_TEMPLATE);
    REGISTER(kFIX_AMM_OVERFLOW_OFFER);
    REGISTER(kPRICE_ORACLE);
    REGISTER(kFIX_EMPTY_DID);
    REGISTER(kFIX_X_CHAIN_REWARD_ROUNDING);
    REGISTER(kFIX_PREVIOUS_TXN_ID);
    REGISTER(kFIX_AM_MV1_1);
    REGISTER(kNF_TOKEN_MINT_OFFER);
    REGISTER(kFIX_REDUCED_OFFERS_V2);
    REGISTER(kFIX_ENFORCE_NF_TOKEN_TRUSTLINE);
    REGISTER(kFIX_INNER_OBJ_TEMPLATE2);
    REGISTER(kFIX_NF_TOKEN_PAGE_LINKS);
    REGISTER(kINVARIANTS_V1_1);
    REGISTER(kMP_TOKENS_V1);
    REGISTER(kFIX_AM_MV1_2);
    REGISTER(kAMM_CLAWBACK);
    REGISTER(kCREDENTIALS);

    // Obsolete but supported by libxrpl
    REGISTER(kCRYPTO_CONDITIONS_SUITE);
    REGISTER(kNON_FUNGIBLE_TOKENS_V1);
    REGISTER(kFIX_NF_TOKEN_DIR_V1);
    REGISTER(kFIX_NF_TOKEN_NEG_OFFER);

    // Retired amendments
    REGISTER(kMULTI_SIGN);
    REGISTER(kTRUST_SET_AUTH);
    REGISTER(kFEE_ESCALATION);
    REGISTER(kPAY_CHAN);
    REGISTER(kFIX1368);
    REGISTER(kCRYPTO_CONDITIONS);
    REGISTER(kESCROW);
    REGISTER(kTICK_SIZE);
    REGISTER(kFIX1373);
    REGISTER(kENFORCE_INVARIANTS);
    REGISTER(kSORTED_DIRECTORIES);
    REGISTER(kFIX1201);
    REGISTER(kFIX1512);
    REGISTER(kFIX1523);
    REGISTER(kFIX1528);
    /** @endcond */
};

#undef REGISTER

/**
 * @brief Knowledge center for amendments within XRPL
 */
class AmendmentCenter : public AmendmentCenterInterface {
    std::shared_ptr<data::BackendInterface> backend_;

    std::map<std::string, Amendment> supported_;
    std::vector<Amendment> all_;

public:
    /**
     * @brief Construct a new AmendmentCenter instance
     *
     * @param backend The backend
     */
    explicit AmendmentCenter(std::shared_ptr<data::BackendInterface> const& backend);

    /**
     * @brief Check whether an amendment is supported by Clio
     *
     * @param key The key of the amendment to check
     * @return true if supported; false otherwise
     */
    [[nodiscard]] bool
    isSupported(AmendmentKey const& key) const final;

    /**
     * @brief Get all supported amendments as a map
     *
     * @return The amendments supported by Clio
     */
    [[nodiscard]] std::map<std::string, Amendment> const&
    getSupported() const final;

    /**
     * @brief Get all known amendments
     *
     * @return All known amendments as a vector
     */
    [[nodiscard]] std::vector<Amendment> const&
    getAll() const final;

    /**
     * @brief Check whether an amendment was/is enabled for a given sequence
     *
     * @param key The key of the amendment to check
     * @param seq The sequence to check for
     * @return true if enabled; false otherwise
     */
    [[nodiscard]] bool
    isEnabled(AmendmentKey const& key, uint32_t seq) const final;

    /**
     * @brief Check whether an amendment was/is enabled for a given sequence
     *
     * @param yield The coroutine context to use
     * @param key The key of the amendment to check
     * @param seq The sequence to check for
     * @return true if enabled; false otherwise
     */
    [[nodiscard]] bool
    isEnabled(boost::asio::yield_context yield, AmendmentKey const& key, uint32_t seq) const final;

    /**
     * @brief Check whether an amendment was/is enabled for a given sequence
     *
     * @param yield The coroutine context to use
     * @param keys The keys of the amendments to check
     * @param seq The sequence to check for
     * @return A vector of bools representing enabled state for each of the given keys
     */
    [[nodiscard]] std::vector<bool>
    isEnabled(boost::asio::yield_context yield, std::vector<AmendmentKey> const& keys, uint32_t seq) const final;

    /**
     * @brief Get an amendment
     *
     * @param key The key of the amendment to get
     * @return The amendment as a const ref; asserts if the amendment is unknown
     */
    [[nodiscard]] Amendment const&
    getAmendment(AmendmentKey const& key) const final;

    /**
     * @brief Get an amendment by its key

     * @param key The amendment key from @see Amendments
     * @return The amendment as a const ref; asserts if the amendment is unknown
     */
    [[nodiscard]] Amendment const&
    operator[](AmendmentKey const& key) const final;

private:
    [[nodiscard]] std::optional<std::vector<ripple::uint256>>
    fetchAmendmentsList(boost::asio::yield_context yield, uint32_t seq) const;
};

}  // namespace data
