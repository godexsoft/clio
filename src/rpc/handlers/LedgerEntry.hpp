#pragma once

#include "data/BackendInterface.hpp"
#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/common/Types.hpp"
#include "rpc/common/spec/Aliases.hpp"
#include "rpc/common/spec/FieldSpec.hpp"
#include "rpc/common/spec/RpcSpec.hpp"
#include "rpc/common/spec/RpcSpecView.hpp"
#include "rpc/common/spec/Types.hpp"
#include "rpc/common/spec/Validators.hpp"
#include "util/AccountUtils.hpp"

#include <boost/json/array.hpp>
#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/core/LexicalCast.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/tokens.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace rpc {

/**
 * @brief The ledger_entry method returns a single ledger object from the XRP Ledger in its raw
 * format.
 *
 * For more details see: https://xrpl.org/ledger_entry.html
 */
class LedgerEntryHandler {
    std::shared_ptr<BackendInterface> sharedPtrBackend_;

public:
    /**
     * @brief A struct to hold the output data of the command
     */
    struct Output {
        std::string index;
        uint32_t ledgerIndex;
        std::string ledgerHash;
        std::optional<boost::json::object> node;
        std::optional<std::string> nodeBinary;
        std::optional<uint32_t> deletedLedgerIndex;
        bool validated = true;
    };

    /**
     * @brief A struct to hold the input data for the command
     */
    struct Input {
        std::optional<std::string> ledgerHash;
        std::optional<uint32_t> ledgerIndex;
        bool binary = false;
        // id of this ledger entry: 256 bits hex string
        std::optional<std::string> index;
        // index can be extracted from payment_channel, check, escrow, offer
        // etc, expectedType is used to save the type of index
        ripple::LedgerEntryType expectedType = ripple::ltANY;
        // account id to address account root object
        std::optional<std::string> accountRoot;
        // account id to address did object
        std::optional<std::string> did;
        // mpt issuance id to address mptIssuance object
        std::optional<std::string> mptIssuance;
        // TODO: extract into custom objects, remove json from Input
        std::optional<boost::json::object> directory;
        std::optional<boost::json::object> offer;
        std::optional<boost::json::object> rippleStateAccount;
        std::optional<boost::json::object> escrow;
        std::optional<boost::json::object> depositPreauth;
        std::optional<boost::json::object> ticket;
        std::optional<boost::json::object> amm;
        std::optional<boost::json::object> mptoken;
        std::optional<boost::json::object> permissionedDomain;
        std::optional<boost::json::object> vault;
        std::optional<boost::json::object> loanBroker;
        std::optional<boost::json::object> loan;
        std::optional<ripple::STXChainBridge> bridge;
        std::optional<std::string> bridgeAccount;
        std::optional<uint32_t> chainClaimId;
        std::optional<uint32_t> createAccountClaimId;
        std::optional<ripple::uint256> oracleNode;
        std::optional<ripple::uint256> credential;
        std::optional<boost::json::object> delegate;
        bool includeDeleted = false;
    };

    using Result = HandlerReturnType<Output>;

    /**
     * @brief Construct a new LedgerEntryHandler object
     *
     * @param sharedPtrBackend The backend to use
     */
    LedgerEntryHandler(std::shared_ptr<BackendInterface> sharedPtrBackend)
        : sharedPtrBackend_(std::move(sharedPtrBackend))
    {
    }

    /**
     * @brief Returns the API specification for the command
     *
     * @param apiVersion The api version to return the spec for
     * @return The spec for the given apiVersion
     */
    static rpc::spec::RpcSpecConstRef
    spec([[maybe_unused]] uint32_t apiVersion)
    {
        using namespace spec;

        // Validator only works in this handler
        // The accounts array must have two different elements
        // Each element must be a valid address
        static constexpr auto kRIPPLE_STATE_ACCOUNTS_VALIDATOR =
            CustomValidator{[](auto const& f) -> rpc::MaybeError {
                if (!f.isArray() || f.arraySize() != 2) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "malformedAccounts"}
                    };
                }
                auto const elem0 = f.element(0);
                auto const elem1 = f.element(1);
                if (!elem0.isString() || !elem1.isString() ||
                    elem0.asString() == elem1.asString()) {
                    return std::unexpected{
                        rpc::Status{rpc::RippledError::rpcINVALID_PARAMS, "malformedAccounts"}
                    };
                }
                auto const id1 =
                    util::parseBase58Wrapper<ripple::AccountID>(std::string{elem0.asString()});
                auto const id2 =
                    util::parseBase58Wrapper<ripple::AccountID>(std::string{elem1.asString()});
                if (!id1 || !id2) {
                    return std::unexpected{
                        rpc::Status{rpc::ClioError::RpcMalformedAddress, "malformedAddresses"}
                    };
                }
                return {};
            }};

        static constexpr auto kMALFORMED_REQUEST_HEX_STRING_VALIDATOR =
            withCustomError(uint256Hex, rpc::ClioError::RpcMalformedRequest);

        static constexpr auto kMALFORMED_REQUEST_INT_VALIDATOR =
            withCustomError(type<uint32_t>, rpc::ClioError::RpcMalformedRequest);

        static constexpr auto kBRIDGE_JSON_VALIDATOR = withCustomError(
            ifObject(section(
                field("LockingChainDoor", required, accountBase58),
                field("IssuingChainDoor", required, accountBase58),
                field("LockingChainIssue", required, currencyIssue),
                field("IssuingChainIssue", required, currencyIssue)
            )),
            rpc::ClioError::RpcMalformedRequest
        );

        static constexpr auto kRPC_SPEC = spec::RpcSpec{
            field(JS(binary), type<bool>),
            field(JS(ledger_hash), uint256Hex),
            field(JS(ledger_index), ledgerIndex),
            field(JS(index), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(account_root), accountBase58),
            field(JS(did), accountBase58),
            field(JS(check), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(
                JS(deposit_preauth),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(owner),
                        required,
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedOwner)
                    ),
                    field(JS(authorized), accountBase58),
                    field(JS(authorized_credentials), authorizeCredential)
                ))
            ),
            field(
                JS(directory),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(JS(owner), accountBase58),
                    field(JS(dir_root), uint256Hex),
                    field(JS(sub_index), kMALFORMED_REQUEST_INT_VALIDATOR)
                ))
            ),
            field(
                JS(escrow),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(owner),
                        required,
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedOwner)
                    ),
                    field(JS(seq), required, kMALFORMED_REQUEST_INT_VALIDATOR)
                ))
            ),
            field(
                JS(offer),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(JS(account), required, accountBase58),
                    field(JS(seq), required, kMALFORMED_REQUEST_INT_VALIDATOR)
                ))
            ),
            field(JS(payment_channel), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(
                JS(ripple_state),
                type<JsonObject>,
                section(
                    field(JS(accounts), required, kRIPPLE_STATE_ACCOUNTS_VALIDATOR),
                    field(JS(currency), required, currency)
                )
            ),
            field(
                JS(ticket),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(JS(account), required, accountBase58),
                    field(JS(ticket_seq), required, kMALFORMED_REQUEST_INT_VALIDATOR)
                ))
            ),
            field(JS(nft_page), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(
                JS(amm),
                type<std::string, JsonObject>,
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(asset),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<JsonObject>, rpc::ClioError::RpcMalformedRequest),
                        currencyIssue
                    ),
                    field(
                        JS(asset2),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<JsonObject>, rpc::ClioError::RpcMalformedRequest),
                        currencyIssue
                    )
                ))
            ),
            field(
                JS(bridge),
                withCustomError(type<JsonObject>, rpc::ClioError::RpcMalformedRequest),
                kBRIDGE_JSON_VALIDATOR
            ),
            field(
                JS(bridge_account),
                withCustomError(accountBase58, rpc::ClioError::RpcMalformedRequest)
            ),
            field(
                JS(xchain_owned_claim_id),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                kBRIDGE_JSON_VALIDATOR,
                withCustomError(
                    ifObject(section(field(JS(xchain_owned_claim_id), required, type<uint32_t>))),
                    rpc::ClioError::RpcMalformedRequest
                )
            ),
            field(
                JS(xchain_owned_create_account_claim_id),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                kBRIDGE_JSON_VALIDATOR,
                withCustomError(
                    ifObject(section(
                        field(JS(xchain_owned_create_account_claim_id), required, type<uint32_t>)
                    )),
                    rpc::ClioError::RpcMalformedRequest
                )
            ),
            field(
                JS(oracle),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(withCustomError(
                    kMALFORMED_REQUEST_HEX_STRING_VALIDATOR, rpc::ClioError::RpcMalformedAddress
                )),
                ifObject(section(
                    field(
                        JS(account),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    ),
                    // note: Unlike `rippled`, Clio only supports UInt as input, no string, no
                    // `null`, etc.:
                    field(
                        JS(oracle_document_id),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(
                            type<uint32_t, std::string>,
                            rpc::ClioError::RpcMalformedOracleDocumentId
                        ),
                        withCustomError(toNumber, rpc::ClioError::RpcMalformedOracleDocumentId)
                    )
                ))
            ),
            field(
                JS(credential),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(withCustomError(
                    kMALFORMED_REQUEST_HEX_STRING_VALIDATOR, rpc::ClioError::RpcMalformedAddress
                )),
                ifObject(section(
                    field(
                        JS(subject),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    ),
                    field(
                        JS(issuer),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    ),
                    field(
                        JS(credential_type),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<std::string>, rpc::ClioError::RpcMalformedRequest)
                    )
                ))
            ),
            field(
                JS(mpt_issuance), withCustomError(uint192Hex, rpc::ClioError::RpcMalformedRequest)
            ),
            field(
                JS(mptoken),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(account),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    ),
                    field(
                        JS(mpt_issuance_id),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(uint192Hex, rpc::ClioError::RpcMalformedRequest)
                    )
                ))
            ),
            field(
                JS(permissioned_domain),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(seq),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<uint32_t>, rpc::ClioError::RpcMalformedRequest)
                    ),
                    field(
                        JS(account),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    )
                ))
            ),
            field(
                JS(vault),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(seq),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<uint32_t>, rpc::ClioError::RpcMalformedRequest)
                    ),
                    field(
                        JS(owner),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedOwner)
                    )
                ))
            ),
            field(
                JS(loan_broker),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(seq),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<uint32_t>, rpc::ClioError::RpcMalformedRequest)
                    ),
                    field(
                        JS(owner),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedOwner)
                    )
                ))
            ),
            field(
                JS(loan),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(loan_seq),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(type<uint32_t>, rpc::ClioError::RpcMalformedRequest)
                    ),
                    field(
                        JS(loan_broker_id),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(uint256Hex, rpc::ClioError::RpcMalformedRequest)
                    )
                ))
            ),
            field(
                JS(delegate),
                withCustomError(type<std::string, JsonObject>, rpc::ClioError::RpcMalformedRequest),
                ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                ifObject(section(
                    field(
                        JS(account),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    ),
                    field(
                        JS(authorize),
                        withCustomError(required, rpc::ClioError::RpcMalformedRequest),
                        withCustomError(accountBase58, rpc::ClioError::RpcMalformedAddress)
                    )
                ))
            ),
            field(JS(amendments), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(fee), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(hashes), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(nft_offer), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(nunl), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(signer_list), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            field(JS(ledger), deprecated),
            field("include_deleted", spec::type<bool>),
        };

        return rpc::spec::RpcSpecView{kRPC_SPEC};
    }

    /**
     * @brief Process the LedgerEntry command
     *
     * @param input The input data for the command
     * @param ctx The context of the request
     * @return The result of the operation
     */
    [[nodiscard]] Result
    process(Input const& input, Context const& ctx) const;

private:
    // dir_root and owner can not be both empty or filled at the same time
    // This function will return an error if this is the case
    static std::expected<ripple::uint256, Status>
    composeKeyFromDirectory(boost::json::object const& directory) noexcept;

    /**
     * @brief Convert the Output to a JSON object
     *
     * @param [out] jv The JSON object to convert to
     * @param output The output to convert
     */
    friend void
    tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Output const& output);

    /**
     * @brief Convert a JSON object to Input type
     *
     * @param jv The JSON object to convert
     * @return Input parsed from the JSON object
     */
    friend Input
    tag_invoke(boost::json::value_to_tag<Input>, boost::json::value const& jv);
};

}  // namespace rpc
