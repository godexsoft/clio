#include "util/LedgerUtils.hpp"

#include <rpcspec/LedgerTypes.hpp>
#include <xrpl/protocol/LedgerFormats.h>

#include <string>

namespace util {

xrpl::LedgerEntryType
LedgerTypes::getLedgerEntryTypeFromStr(std::string const& entryName)
{
    return rpc::spec::ledgerEntryTypeFromStr(entryName);
}

xrpl::LedgerEntryType
LedgerTypes::getAccountOwnedLedgerTypeFromStr(std::string const& entryName)
{
    return rpc::spec::accountOwnedLedgerTypeFromStr(entryName);
}

}  // namespace util
