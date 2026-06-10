#include "util/LedgerUtils.hpp"

#include <rpcspec/detail/XrplParse.hpp>
#include <xrpl/protocol/LedgerFormats.h>

#include <string>

namespace util {

ripple::LedgerEntryType
LedgerTypes::getLedgerEntryTypeFromStr(std::string const& entryName)
{
    return rpc::spec::detail::ledgerEntryTypeFromStr(entryName);
}

ripple::LedgerEntryType
LedgerTypes::getAccountOwnedLedgerTypeFromStr(std::string const& entryName)
{
    return rpc::spec::detail::accountOwnedLedgerTypeFromStr(entryName);
}

}  // namespace util
