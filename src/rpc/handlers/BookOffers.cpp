#include "rpc/handlers/BookOffers.hpp"

#include "rpc/Errors.hpp"
#include "rpc/JS.hpp"
#include "rpc/RPCHelpers.hpp"
#include "rpc/common/Types.hpp"
#include <rpcspec/Aliases.hpp>
#include <rpcspec/FieldSpec.hpp>
#include <rpcspec/RpcSpec.hpp>
#include <rpcspec/RpcSpecView.hpp>
#include <rpcspec/Types.hpp>
#include <rpcspec/Validators.hpp>
#include "util/Assert.hpp"
#include "util/JsonUtils.hpp"

#include <boost/json/conversion.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>
#include <boost/json/value_to.hpp>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <string>

namespace rpc {

rpc::spec::RpcSpecView
BookOffersHandler::spec([[maybe_unused]] uint32_t apiVersion)
{
    using namespace spec;

    static constexpr auto kRPC_SPEC = spec::RpcSpec{
        field(
            JS(taker_gets),
            required,
            type<JsonObject>,
            section(
                field(
                    JS(currency),
                    required,
                    withCustomError(currency, RippledError::rpcDST_AMT_MALFORMED)
                ),
                field(JS(issuer), withCustomError(issuer, RippledError::rpcDST_ISR_MALFORMED))
            )
        ),
        field(
            JS(taker_pays),
            required,
            type<JsonObject>,
            section(
                field(
                    JS(currency),
                    required,
                    withCustomError(currency, RippledError::rpcSRC_CUR_MALFORMED)
                ),
                field(JS(issuer), withCustomError(issuer, RippledError::rpcSRC_ISR_MALFORMED))
            )
        ),
        // return INVALID_PARAMS if account format is wrong for "taker"
        field(
            JS(taker),
            withCustomError(account, RippledError::rpcINVALID_PARAMS, "Invalid field 'taker'.")
        ),
        field(
            JS(domain),
            withCustomError(
                type<std::string>, RippledError::rpcDOMAIN_MALFORMED, "Unable to parse domain."
            ),
            withCustomError(
                uint256Hex, RippledError::rpcDOMAIN_MALFORMED, "Unable to parse domain."
            )
        ),
        field(
            JS(limit),
            type<uint32_t>,
            min(uint32_t{kLimitMin}),
            clamp(uint32_t{kLimitMin}, uint32_t{kLimitMax})
        ),
        field(JS(ledger_hash), uint256Hex),
        field(JS(ledger_index), ledgerIndex),
    };

    return RpcSpecView{kRPC_SPEC};
}

BookOffersHandler::Result
BookOffersHandler::process(Input const& input, Context const& ctx) const
{
    auto bookMaybe =
        parseBook(input.paysCurrency, input.paysID, input.getsCurrency, input.getsID, input.domain);
    if (!bookMaybe.has_value())
        return Error{bookMaybe.error()};

    // check ledger
    auto const range = sharedPtrBackend_->fetchLedgerRange();
    ASSERT(range.has_value(), "BookOffer's ledger range must be available");

    auto const expectedLgrInfo = getLedgerHeaderFromHashOrSeq(
        *sharedPtrBackend_,
        ctx.yield,
        input.ledgerHash,
        input.ledgerIndex,
        range->maxSequence  // NOLINT(bugprone-unchecked-optional-access)
    );

    if (not expectedLgrInfo.has_value())
        return Error{expectedLgrInfo.error()};

    auto const& lgrInfo = *expectedLgrInfo;
    auto const book = *bookMaybe;
    auto const bookKey = getBookBase(book);

    // TODO: Add performance metrics if needed in future
    auto [offers, _] =
        sharedPtrBackend_->fetchBookOffers(bookKey, lgrInfo.seq, input.limit, ctx.yield);

    auto output = BookOffersHandler::Output{};
    output.ledgerHash = ripple::strHex(lgrInfo.hash);
    output.ledgerIndex = lgrInfo.seq;
    output.offers = postProcessOrderBook(
        offers,
        book,
        input.taker ? *(input.taker) : beast::zero,
        *sharedPtrBackend_,
        *amendmentCenter_,
        lgrInfo.seq,
        ctx.yield
    );

    return output;
}

void
tag_invoke(
    boost::json::value_from_tag,
    boost::json::value& jv,
    BookOffersHandler::Output const& output
)
{
    jv = boost::json::object{
        {JS(ledger_hash), output.ledgerHash},
        {JS(ledger_index), output.ledgerIndex},
        {JS(offers), output.offers},
    };
}

BookOffersHandler::Input
tag_invoke(boost::json::value_to_tag<BookOffersHandler::Input>, boost::json::value const& jv)
{
    auto input = BookOffersHandler::Input{};
    auto const& jsonObject = jv.as_object();

    ripple::to_currency(
        input.getsCurrency,
        boost::json::value_to<std::string>(jv.at(JS(taker_gets)).as_object().at(JS(currency)))
    );
    ripple::to_currency(
        input.paysCurrency,
        boost::json::value_to<std::string>(jv.at(JS(taker_pays)).as_object().at(JS(currency)))
    );

    if (jv.at(JS(taker_gets)).as_object().contains(JS(issuer))) {
        ripple::to_issuer(
            input.getsID,
            boost::json::value_to<std::string>(jv.at(JS(taker_gets)).as_object().at(JS(issuer)))
        );
    }

    if (jv.at(JS(taker_pays)).as_object().contains(JS(issuer))) {
        ripple::to_issuer(
            input.paysID,
            boost::json::value_to<std::string>(jv.at(JS(taker_pays)).as_object().at(JS(issuer)))
        );
    }

    if (jsonObject.contains(JS(ledger_hash)))
        input.ledgerHash = boost::json::value_to<std::string>(jv.at(JS(ledger_hash)));

    if (jsonObject.contains(JS(ledger_index))) {
        auto const expectedLedgerIndex = util::getLedgerIndex(jv.at(JS(ledger_index)));
        if (expectedLedgerIndex.has_value())
            input.ledgerIndex = *expectedLedgerIndex;
    }

    if (jsonObject.contains(JS(taker)))
        input.taker = accountFromStringStrict(boost::json::value_to<std::string>(jv.at(JS(taker))));

    if (jsonObject.contains(JS(domain)))
        input.domain = boost::json::value_to<std::string>(jv.at(JS(domain)));

    if (jsonObject.contains(JS(limit)))
        input.limit = util::integralValueAs<uint32_t>(jv.at(JS(limit)));

    return input;
}

}  // namespace rpc
