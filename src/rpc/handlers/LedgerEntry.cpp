#include "rpc/handlers/LedgerEntry.hpp"

#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/HandlerForDefs.hpp>
#include <rpcspec/handlers/ledger_entry/Spec.hpp>
#include <rpcspec/handlers/ledger_entry/Types.hpp>
#include "util/Assert.hpp"

#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STXChainBridge.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/UintTypes.h>

#include <cstdint>
#include <expected>
#include <set>
#include <utility>
#include <variant>
#include <vector>

template struct rpc::spec::HandlerFor<rpc::LedgerEntryHandler::Input>;

namespace rpc {

LedgerEntryHandler::Result
LedgerEntryHandler::process(LedgerEntryHandler::Input const& input, Context const& ctx) const
{
    using namespace rpc::spec::handlers::ledger_entry;

    // Build the libxrpl bridge value from the strong-typed spec representation.
    auto const makeBridge = [](BridgeSpec const& b) {
        return xrpl::STXChainBridge{
            b.lockingChainDoor, b.lockingChainIssue, b.issuingChainDoor, b.issuingChainIssue
        };
    };

    xrpl::uint256 key;
    // For locators supplied as a raw ledger-entry hex key, the type is implied and
    // enforced below; a precisely-computed keylet leaves this as ltANY (no check).
    xrpl::LedgerEntryType expectedType = xrpl::ltANY;

    if (input.index) {
        key = *input.index;
        if (key.isZero())
            return Error{Status{RippledError::RpcEntryNotFound}};
    } else if (input.accountRoot) {
        key = xrpl::keylet::account(*input.accountRoot).key;
    } else if (input.did) {
        key = xrpl::keylet::did(*input.did).key;
    } else if (input.check) {
        key = *input.check;
        expectedType = xrpl::ltCHECK;
    } else if (input.paymentChannel) {
        key = *input.paymentChannel;
        expectedType = xrpl::ltPAYCHAN;
    } else if (input.nftPage) {
        key = *input.nftPage;
        expectedType = xrpl::ltNFTOKEN_PAGE;
    } else if (input.nftOffer) {
        key = *input.nftOffer;
        expectedType = xrpl::ltNFTOKEN_OFFER;
    } else if (input.signerList) {
        key = *input.signerList;
        expectedType = xrpl::ltSIGNER_LIST;
    } else if (input.amendments) {
        key = *input.amendments;
        expectedType = xrpl::ltAMENDMENTS;
    } else if (input.fee) {
        key = *input.fee;
        expectedType = xrpl::ltFEE_SETTINGS;
    } else if (input.hashes) {
        key = *input.hashes;
        expectedType = xrpl::ltLEDGER_HASHES;
    } else if (input.nunl) {
        key = *input.nunl;
        expectedType = xrpl::ltNEGATIVE_UNL;
    } else if (input.mptIssuance) {
        key = xrpl::keylet::mptokenIssuance(*input.mptIssuance).key;
    } else if (input.directory) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.directory)) {
            key = *h;
            expectedType = xrpl::ltDIR_NODE;
        } else {
            auto const& d = std::get<DirectoryEntry>(*input.directory);
            // dir_root and owner can not be both empty or filled at the same time.
            if (d.dirRoot && d.owner)
                return Error{
                    Status{RippledError::RpcInvalidParams, "mayNotSpecifyBothDirRootAndOwner"}
                };
            if (!d.dirRoot && !d.owner)
                return Error{Status{RippledError::RpcInvalidParams, "missingOwnerOrDirRoot"}};

            uint64_t const subIndex = d.subIndex.value_or(0);
            if (d.dirRoot) {
                key = xrpl::keylet::page(*d.dirRoot, subIndex).key;
            } else {
                key = xrpl::keylet::page(xrpl::keylet::ownerDir(*d.owner), subIndex).key;
            }
        }
    } else if (input.offer) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.offer)) {
            key = *h;
            expectedType = xrpl::ltOFFER;
        } else {
            auto const& e = std::get<OfferEntry>(*input.offer);
            key = xrpl::keylet::offer(e.account, e.seq).key;
        }
    } else if (input.rippleStateAccount) {
        auto const& rs = *input.rippleStateAccount;
        key = xrpl::keylet::trustLine(rs.accounts[0], rs.accounts[1], rs.currency).key;
    } else if (input.escrow) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.escrow)) {
            key = *h;
            expectedType = xrpl::ltESCROW;
        } else {
            auto const& e = std::get<EscrowEntry>(*input.escrow);
            key = xrpl::keylet::escrow(e.owner, e.seq).key;
        }
    } else if (input.depositPreauth) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.depositPreauth)) {
            key = *h;
            expectedType = xrpl::ltDEPOSIT_PREAUTH;
        } else {
            auto const& dp = std::get<DepositPreauthEntry>(*input.depositPreauth);
            // Exactly one of authorized or authorized_credentials MUST exist.
            if (dp.authorized.has_value() == dp.authorizedCredentials.has_value()) {
                return Error{Status{
                    ClioError::RpcMalformedRequest,
                    "Must have one of authorized or authorized_credentials."
                }};
            }

            if (dp.authorized) {
                key = xrpl::keylet::depositPreauth(dp.owner, *dp.authorized).key;
            } else {
                std::set<std::pair<xrpl::AccountID, xrpl::Slice>> authCreds;
                // Keep the decoded credential-type bytes alive while the Slices
                // that reference them are used to build the keylet.
                std::vector<std::vector<std::uint8_t>> buffers;
                buffers.reserve(dp.authorizedCredentials->size());
                for (auto const& c : *dp.authorizedCredentials) {
                    // credential_type is validated as a hex string by the spec.
                    auto const unhexed = xrpl::strUnHex(c.credentialType);
                    buffers.push_back(*unhexed);
                    authCreds.emplace(
                        c.issuer, xrpl::Slice(buffers.back().data(), buffers.back().size())
                    );
                }

                if (authCreds.size() != dp.authorizedCredentials->size()) {
                    return Error{Status{
                        ClioError::RpcMalformedAuthorizedCredentials, "duplicates in credentials."
                    }};
                }

                key = xrpl::keylet::depositPreauth(dp.owner, authCreds).key;
            }
        }
    } else if (input.ticket) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.ticket)) {
            key = *h;
            expectedType = xrpl::ltTICKET;
        } else {
            auto const& e = std::get<TicketEntry>(*input.ticket);
            key = xrpl::getTicketIndex(e.account, e.ticketSeq);
        }
    } else if (input.amm) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.amm)) {
            key = *h;
            expectedType = xrpl::ltAMM;
        } else {
            auto const& e = std::get<AmmEntry>(*input.amm);
            key = xrpl::keylet::amm(e.asset, e.asset2).key;
        }
    } else if (input.bridge) {
        if (!input.bridgeAccount)
            return Error{Status{ClioError::RpcMalformedRequest}};

        auto const stBridge = makeBridge(*input.bridge);
        auto const& bridgeAccount = *input.bridgeAccount;
        auto const chainType =
            xrpl::STXChainBridge::srcChain(bridgeAccount == input.bridge->lockingChainDoor);

        if (bridgeAccount != stBridge.door(chainType))
            return Error{Status{ClioError::RpcMalformedRequest}};

        key = xrpl::keylet::bridge(stBridge, chainType).key;
    } else if (input.xchainOwnedClaimId) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.xchainOwnedClaimId)) {
            key = *h;
            expectedType = xrpl::ltXCHAIN_OWNED_CLAIM_ID;
        } else {
            auto const& e = std::get<XChainClaimIdEntry>(*input.xchainOwnedClaimId);
            key = xrpl::keylet::xChainClaimID(makeBridge(e.bridge), e.claimId).key;
        }
    } else if (input.xchainOwnedCreateAccountClaimId) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.xchainOwnedCreateAccountClaimId)) {
            key = *h;
            expectedType = xrpl::ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID;
        } else {
            auto const& e = std::get<XChainClaimIdEntry>(*input.xchainOwnedCreateAccountClaimId);
            key = xrpl::keylet::xChainCreateAccountClaimID(makeBridge(e.bridge), e.claimId).key;
        }
    } else if (input.oracle) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.oracle)) {
            key = *h;
            expectedType = xrpl::ltORACLE;
        } else {
            auto const& e = std::get<OracleEntry>(*input.oracle);
            key = xrpl::keylet::oracle(e.account, e.oracleDocumentId).key;
        }
    } else if (input.credential) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.credential)) {
            key = *h;
            expectedType = xrpl::ltCREDENTIAL;
        } else {
            auto const& e = std::get<CredentialEntry>(*input.credential);
            // credential_type is validated as a hex string by the spec.
            auto const credType = xrpl::strUnHex(e.credentialType);
            key = xrpl::keylet::credential(
                      e.subject, e.issuer, xrpl::Slice(credType->data(), credType->size())
            )
                      .key;
        }
    } else if (input.mptoken) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.mptoken)) {
            key = *h;
            expectedType = xrpl::ltMPTOKEN;
        } else {
            auto const& e = std::get<MptokenEntry>(*input.mptoken);
            key = xrpl::keylet::mptoken(e.mptIssuanceId, e.account).key;
        }
    } else if (input.permissionedDomain) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.permissionedDomain)) {
            key = *h;
            expectedType = xrpl::ltPERMISSIONED_DOMAIN;
        } else {
            auto const& e = std::get<PermissionedDomainEntry>(*input.permissionedDomain);
            key = xrpl::keylet::permissionedDomain(e.account, e.seq).key;
        }
    } else if (input.vault) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.vault)) {
            key = *h;
            expectedType = xrpl::ltVAULT;
        } else {
            auto const& e = std::get<VaultEntry>(*input.vault);
            key = xrpl::keylet::vault(e.owner, e.seq).key;
        }
    } else if (input.loanBroker) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.loanBroker)) {
            key = *h;
            expectedType = xrpl::ltLOAN_BROKER;
        } else {
            auto const& e = std::get<LoanBrokerEntry>(*input.loanBroker);
            key = xrpl::keylet::loanBroker(e.owner, e.seq).key;
        }
    } else if (input.loan) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.loan)) {
            key = *h;
            expectedType = xrpl::ltLOAN;
        } else {
            auto const& e = std::get<LoanEntry>(*input.loan);
            key = xrpl::keylet::loan(e.loanBrokerId, e.loanSeq).key;
        }
    } else if (input.delegate) {
        if (auto const* h = std::get_if<xrpl::uint256>(&*input.delegate)) {
            key = *h;
            expectedType = xrpl::ltDELEGATE;
        } else {
            auto const& e = std::get<DelegateEntry>(*input.delegate);
            key = xrpl::keylet::delegate(e.account, e.authorize).key;
        }
    } else {
        // Must specify 1 of the following fields to indicate what type
        if (ctx.apiVersion == 1)
            return Error{Status{ClioError::RpcUnknownOption}};
        return Error{Status{RippledError::RpcInvalidParams, "No ledger_entry params provided."}};
    }

    // check ledger exists
    auto const range = sharedPtrBackend_->fetchLedgerRange();
    ASSERT(range.has_value(), "LedgerEntry's ledger range must be available");
    auto const expectedLgrInfo = getLedgerHeaderFromLedgerSpecifier(
        *sharedPtrBackend_,
        ctx.yield,
        input.ledger,
        range->maxSequence  // NOLINT(bugprone-unchecked-optional-access)
    );

    if (not expectedLgrInfo.has_value())
        return Error{expectedLgrInfo.error()};

    auto const& lgrInfo = *expectedLgrInfo;
    auto output = LedgerEntryHandler::Output{};
    auto ledgerObject = sharedPtrBackend_->fetchLedgerObject(key, lgrInfo.seq, ctx.yield);

    if (!ledgerObject || ledgerObject->empty()) {
        if (not input.includeDeleted)
            return Error{Status{RippledError::RpcEntryNotFound}};
        auto const deletedSeq =
            sharedPtrBackend_->fetchLedgerObjectSeq(key, lgrInfo.seq, ctx.yield);
        if (!deletedSeq)
            return Error{Status{RippledError::RpcEntryNotFound}};
        ledgerObject = sharedPtrBackend_->fetchLedgerObject(key, *deletedSeq - 1, ctx.yield);
        if (!ledgerObject || ledgerObject->empty())
            return Error{Status{RippledError::RpcEntryNotFound}};
        output.deletedLedgerIndex = deletedSeq;
    }

    xrpl::STLedgerEntry const sle{
        xrpl::SerialIter{ledgerObject->data(), ledgerObject->size()}, key
    };

    if (expectedType != xrpl::ltANY && sle.getType() != expectedType)
        return Error{Status{RippledError::RpcUnexpectedLedgerType}};

    output.index = xrpl::strHex(key);
    output.ledgerIndex = lgrInfo.seq;
    output.ledgerHash = xrpl::strHex(lgrInfo.hash);

    if (input.binary) {
        output.nodeBinary = xrpl::strHex(*ledgerObject);
    } else {
        output.node = toJson(sle);
    }

    return output;
}

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    LedgerEntryHandler::Output const& output
)
{
    auto object = boost::json::object{
        {JS(ledger_hash), output.ledgerHash},
        {JS(ledger_index), output.ledgerIndex},
        {JS(validated), output.validated},
        {JS(index), output.index},
    };

    if (output.deletedLedgerIndex)
        object["deleted_ledger_index"] = *(output.deletedLedgerIndex);

    if (output.nodeBinary) {
        object[JS(node_binary)] = *(output.nodeBinary);
    } else {
        object[JS(node)] = *(output.node);  // NOLINT(bugprone-unchecked-optional-access)
    }

    jv = std::move(object);
}

}  // namespace rpc
