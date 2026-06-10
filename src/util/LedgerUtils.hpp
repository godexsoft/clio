#pragma once

#include <rpcspec/detail/XrplParse.hpp>

#include <fmt/format.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace util {

/**
 * @brief A helper class that provides lists of different ledger type category.
 *
 */
class LedgerTypes {
    static constexpr auto const& kTABLE = rpc::spec::detail::kLEDGER_TYPES_TABLE;

public:
    /**
     * @brief Returns a list of all ledger entry type as string.
     * @return A list of all ledger entry type as string.
     */
    static constexpr auto
    getLedgerEntryTypeStrList()
    {
        std::array<char const*, std::size(kTABLE)> res{};
        std::ranges::transform(kTABLE, std::begin(res), [](auto const& e) {
            return e.rpcName.data();
        });
        return res;
    }

    /**
     * @brief Returns a list of all account deletion blocker's type as string.
     *
     * @return A list of all account deletion blocker's type as string.
     */
    static constexpr auto
    getDeletionBlockerLedgerTypes()
    {
        constexpr auto kFILTER = [](auto const& e) {
            return e.category == rpc::spec::detail::LedgerCategory::DeletionBlocker;
        };

        constexpr auto kDELETION_BLOCKERS_COUNT =
            std::count_if(std::begin(kTABLE), std::end(kTABLE), kFILTER);
        std::array<ripple::LedgerEntryType, kDELETION_BLOCKERS_COUNT> res{};
        auto it = std::begin(res);
        std::ranges::for_each(kTABLE, [&](auto const& e) {
            if (kFILTER(e)) {
                *it = e.type;
                ++it;
            }
        });
        return res;
    }

    /**
     * @brief Returns the ripple::LedgerEntryType from the given string.
     *
     * @param entryName The name or canonical name (case-insensitive) of the ledger entry type for
     * all categories
     * @return The ripple::LedgerEntryType of the given string, returns ltANY if not found.
     */
    static ripple::LedgerEntryType
    getLedgerEntryTypeFromStr(std::string const& entryName);

    /**
     * @brief Returns the ripple::LedgerEntryType from the given string.
     *
     * @param entryName The name or canonical name (case-insensitive) of the ledger entry type for
     * account owned category
     * @return The ripple::LedgerEntryType of the given string, returns ltANY if not found.
     */
    static ripple::LedgerEntryType
    getAccountOwnedLedgerTypeFromStr(std::string const& entryName);
};

/**
 * @brief Deserializes a ripple::LedgerHeader from ripple::Slice of data.
 *
 * @param data The slice to deserialize
 * @return The deserialized ripple::LedgerHeader
 */
inline ripple::LedgerHeader
deserializeHeader(ripple::Slice data)
{
    return ripple::deserializeHeader(data, /* hasHash = */ true);
}

/**
 * @brief A helper function that converts a ripple::LedgerHeader to a string representation.
 *
 * @param info The ledger header
 * @return The string representation of the supplied ledger header
 */
inline std::string
toString(ripple::LedgerHeader const& info)
{
    return fmt::format(
        "LedgerHeader {{Sequence: {}, Hash: {}, TxHash: {}, AccountHash: {}, ParentHash: {}}}",
        info.seq,
        ripple::strHex(info.hash),
        strHex(info.txHash),
        ripple::strHex(info.accountHash),
        strHex(info.parentHash)
    );
}

}  // namespace util
