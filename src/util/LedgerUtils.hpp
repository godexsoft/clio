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
    static constexpr auto const& kTABLE = rpc::spec::detail::kLedgerTypesTable;

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
        std::array<xrpl::LedgerEntryType, kDELETION_BLOCKERS_COUNT> res{};
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
     * @brief Returns the xrpl::LedgerEntryType from the given string.
     *
     * @param entryName The name or canonical name (case-insensitive) of the ledger entry type for
     * all categories
     * @return The xrpl::LedgerEntryType of the given string, returns ltANY if not found.
     */
    static xrpl::LedgerEntryType
    getLedgerEntryTypeFromStr(std::string const& entryName);

    /**
     * @brief Returns the xrpl::LedgerEntryType from the given string.
     *
     * @param entryName The name or canonical name (case-insensitive) of the ledger entry type for
     * account owned category
     * @return The xrpl::LedgerEntryType of the given string, returns ltANY if not found.
     */
    static xrpl::LedgerEntryType
    getAccountOwnedLedgerTypeFromStr(std::string const& entryName);
};

/**
 * @brief Deserializes a xrpl::LedgerHeader from xrpl::Slice of data.
 *
 * @param data The slice to deserialize
 * @return The deserialized xrpl::LedgerHeader
 */
inline xrpl::LedgerHeader
deserializeHeader(xrpl::Slice data)
{
    return xrpl::deserializeHeader(data, /* hasHash = */ true);
}

/**
 * @brief A helper function that converts a xrpl::LedgerHeader to a string representation.
 *
 * @param info The ledger header
 * @return The string representation of the supplied ledger header
 */
inline std::string
toString(xrpl::LedgerHeader const& info)
{
    return fmt::format(
        "LedgerHeader {{Sequence: {}, Hash: {}, TxHash: {}, AccountHash: {}, ParentHash: {}}}",
        info.seq,
        xrpl::strHex(info.hash),
        strHex(info.txHash),
        xrpl::strHex(info.accountHash),
        strHex(info.parentHash)
    );
}

}  // namespace util
