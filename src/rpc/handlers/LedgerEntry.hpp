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
        // Validator only works in this handler
        // The accounts array must have two different elements
        // Each element must be a valid address
        static constexpr auto kRIPPLE_STATE_ACCOUNTS_VALIDATOR =
            spec::CustomValidator{[](auto const& f) -> rpc::MaybeError {
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
            spec::withCustomError(spec::uint256Hex, rpc::ClioError::RpcMalformedRequest);

        static constexpr auto kMALFORMED_REQUEST_INT_VALIDATOR =
            spec::withCustomError(spec::type<uint32_t>, rpc::ClioError::RpcMalformedRequest);

        static constexpr auto kBRIDGE_JSON_VALIDATOR = spec::withCustomError(
            spec::ifObject(
                spec::section(
                    spec::field("LockingChainDoor", spec::required, spec::accountBase58),
                    spec::field("IssuingChainDoor", spec::required, spec::accountBase58),
                    spec::field("LockingChainIssue", spec::required, spec::currencyIssue),
                    spec::field("IssuingChainIssue", spec::required, spec::currencyIssue)
                )
            ),
            rpc::ClioError::RpcMalformedRequest
        );

        static constexpr auto kRPC_SPEC = spec::RpcSpec{
            spec::field(JS(binary), spec::type<bool>),
            spec::field(JS(ledger_hash), spec::uint256Hex),
            spec::field(JS(ledger_index), spec::ledgerIndex),
            spec::field(JS(index), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(account_root), spec::accountBase58),
            spec::field(JS(did), spec::accountBase58),
            spec::field(JS(check), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(
                JS(deposit_preauth),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(owner),
                            spec::required,
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedOwner
                            )
                        ),
                        spec::field(JS(authorized), spec::accountBase58),
                        spec::field(JS(authorized_credentials), spec::authorizeCredential)
                    )
                )
            ),
            spec::field(
                JS(directory),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(JS(owner), spec::accountBase58),
                        spec::field(JS(dir_root), spec::uint256Hex),
                        spec::field(JS(sub_index), kMALFORMED_REQUEST_INT_VALIDATOR)
                    )
                )
            ),
            spec::field(
                JS(escrow),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(owner),
                            spec::required,
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedOwner
                            )
                        ),
                        spec::field(JS(seq), spec::required, kMALFORMED_REQUEST_INT_VALIDATOR)
                    )
                )
            ),
            spec::field(
                JS(offer),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(JS(account), spec::required, spec::accountBase58),
                        spec::field(JS(seq), spec::required, kMALFORMED_REQUEST_INT_VALIDATOR)
                    )
                )
            ),
            spec::field(JS(payment_channel), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(
                JS(ripple_state),
                spec::type<spec::JsonObject>,
                spec::section(
                    spec::field(JS(accounts), spec::required, kRIPPLE_STATE_ACCOUNTS_VALIDATOR),
                    spec::field(JS(currency), spec::required, spec::currency)
                )
            ),
            spec::field(
                JS(ticket),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(JS(account), spec::required, spec::accountBase58),
                        spec::field(
                            JS(ticket_seq), spec::required, kMALFORMED_REQUEST_INT_VALIDATOR
                        )
                    )
                )
            ),
            spec::field(JS(nft_page), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(
                JS(amm),
                spec::anyType<std::string, spec::JsonObject>(),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(asset),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<spec::JsonObject>, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::currencyIssue
                        ),
                        spec::field(
                            JS(asset2),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<spec::JsonObject>, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::currencyIssue
                        )
                    )
                )
            ),
            spec::field(
                JS(bridge),
                spec::withCustomError(
                    spec::type<spec::JsonObject>, rpc::ClioError::RpcMalformedRequest
                ),
                kBRIDGE_JSON_VALIDATOR
            ),
            spec::field(
                JS(bridge_account),
                spec::withCustomError(spec::accountBase58, rpc::ClioError::RpcMalformedRequest)
            ),
            spec::field(
                JS(xchain_owned_claim_id),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                kBRIDGE_JSON_VALIDATOR,
                spec::withCustomError(
                    spec::ifObject(
                        spec::section(
                            spec::field(
                                JS(xchain_owned_claim_id), spec::required, spec::type<uint32_t>
                            )
                        )
                    ),
                    rpc::ClioError::RpcMalformedRequest
                )
            ),
            spec::field(
                JS(xchain_owned_create_account_claim_id),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                kBRIDGE_JSON_VALIDATOR,
                spec::withCustomError(
                    spec::ifObject(
                        spec::section(
                            spec::field(
                                JS(xchain_owned_create_account_claim_id),
                                spec::required,
                                spec::type<uint32_t>
                            )
                        )
                    ),
                    rpc::ClioError::RpcMalformedRequest
                )
            ),
            spec::field(
                JS(oracle),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(spec::withCustomError(
                    kMALFORMED_REQUEST_HEX_STRING_VALIDATOR, rpc::ClioError::RpcMalformedAddress
                )),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(account),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        ),
                        // note: Unlike `rippled`, Clio only supports UInt as input, no string, no
                        // `null`, etc.:
                        spec::field(
                            JS(oracle_document_id),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::anyType<uint32_t, std::string>(),
                                rpc::ClioError::RpcMalformedOracleDocumentId
                            ),
                            spec::withCustomError(
                                spec::toNumber, rpc::ClioError::RpcMalformedOracleDocumentId
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(credential),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(spec::withCustomError(
                    kMALFORMED_REQUEST_HEX_STRING_VALIDATOR, rpc::ClioError::RpcMalformedAddress
                )),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(subject),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        ),
                        spec::field(
                            JS(issuer),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        ),
                        spec::field(
                            JS(credential_type),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<std::string>, rpc::ClioError::RpcMalformedRequest
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(mpt_issuance),
                spec::withCustomError(spec::uint192Hex, rpc::ClioError::RpcMalformedRequest)
            ),
            spec::field(
                JS(mptoken),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(account),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        ),
                        spec::field(
                            JS(mpt_issuance_id),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::uint192Hex, rpc::ClioError::RpcMalformedRequest
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(permissioned_domain),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(seq),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<uint32_t>, rpc::ClioError::RpcMalformedRequest
                            )
                        ),
                        spec::field(
                            JS(account),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(vault),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(seq),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<uint32_t>, rpc::ClioError::RpcMalformedRequest
                            )
                        ),
                        spec::field(
                            JS(owner),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedOwner
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(loan_broker),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(seq),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<uint32_t>, rpc::ClioError::RpcMalformedRequest
                            )
                        ),
                        spec::field(
                            JS(owner),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedOwner
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(loan),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(loan_seq),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::type<uint32_t>, rpc::ClioError::RpcMalformedRequest
                            )
                        ),
                        spec::field(
                            JS(loan_broker_id),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::uint256Hex, rpc::ClioError::RpcMalformedRequest
                            )
                        )
                    )
                )
            ),
            spec::field(
                JS(delegate),
                spec::withCustomError(
                    spec::anyType<std::string, spec::JsonObject>(),
                    rpc::ClioError::RpcMalformedRequest
                ),
                spec::ifType<std::string>(kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
                spec::ifObject(
                    spec::section(
                        spec::field(
                            JS(account),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        ),
                        spec::field(
                            JS(authorize),
                            spec::withCustomError(
                                spec::required, rpc::ClioError::RpcMalformedRequest
                            ),
                            spec::withCustomError(
                                spec::accountBase58, rpc::ClioError::RpcMalformedAddress
                            )
                        )
                    )
                )
            ),
            spec::field(JS(amendments), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(fee), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(hashes), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(nft_offer), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(nunl), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(signer_list), kMALFORMED_REQUEST_HEX_STRING_VALIDATOR),
            spec::field(JS(ledger), spec::deprecated),
            spec::field("include_deleted", spec::type<bool>),
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
