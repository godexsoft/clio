//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2022, the clio developers.

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

#include <backend/BackendInterface.h>
#include <config/Config.h>
#include <etl/ETLSource.h>
#include <log/Logger.h>
#include <rpc/Counters.h>
#include <rpc/Errors.h>
#include <rpc/HandlerTable.h>
#include <rpc/RPCHelpers.h>
#include <rpc/common/AnyHandler.h>
#include <rpc/common/Types.h>
#include <rpc/common/impl/AdminVerificationStrategy.h>
#include <util/Taggable.h>
#include <webserver/Context.h>
#include <webserver/DOSGuard.h>

#include <boost/asio/spawn.hpp>
#include <boost/json.hpp>
#include <fmt/core.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

class WsBase;
class SubscriptionManager;
class ETLLoadBalancer;
class ReportingETL;

namespace RPC {

/**
 * @brief The RPC engine that ties all RPC-related functionality together
 */
template <typename AdminVerificationStrategyType>
class RPCEngineBase
{
    clio::Logger perfLog_{"Performance"};
    clio::Logger log_{"RPC"};

    std::shared_ptr<BackendInterface> backend_;
    std::shared_ptr<SubscriptionManager> subscriptions_;
    std::shared_ptr<ETLLoadBalancer> balancer_;
    std::reference_wrapper<clio::DOSGuard const> dosGuard_;
    std::reference_wrapper<WorkQueue> workQueue_;
    std::reference_wrapper<Counters> counters_;

    HandlerTable handlerTable_;
    AdminVerificationStrategyType adminVerifier_;

public:
    RPCEngineBase(
        std::shared_ptr<BackendInterface> const& backend,
        std::shared_ptr<SubscriptionManager> const& subscriptions,
        std::shared_ptr<ETLLoadBalancer> const& balancer,
        std::shared_ptr<ReportingETL> const& etl,
        clio::DOSGuard const& dosGuard,
        WorkQueue& workQueue,
        Counters& counters,
        std::shared_ptr<HandlerProvider const> const& handlerProvider)
        : backend_{backend}
        , subscriptions_{subscriptions}
        , balancer_{balancer}
        , dosGuard_{std::cref(dosGuard)}
        , workQueue_{std::ref(workQueue)}
        , counters_{std::ref(counters)}
        , handlerTable_{handlerProvider}
    {
    }

    static std::shared_ptr<RPCEngineBase>
    make_RPCEngine(
        clio::Config const& config,
        std::shared_ptr<BackendInterface> const& backend,
        std::shared_ptr<SubscriptionManager> const& subscriptions,
        std::shared_ptr<ETLLoadBalancer> const& balancer,
        std::shared_ptr<ReportingETL> const& etl,
        clio::DOSGuard const& dosGuard,
        WorkQueue& workQueue,
        Counters& counters,
        std::shared_ptr<HandlerProvider const> const& handlerProvider)
    {
        return std::make_shared<RPCEngineBase>(
            backend, subscriptions, balancer, etl, dosGuard, workQueue, counters, handlerProvider);
    }

    /**
     * @brief Main request processor routine
     * @param ctx The @ref Context of the request
     */
    Result
    buildResponse(Web::Context const& ctx)
    {
        if (shouldForwardToRippled(ctx))
        {
            auto toForward = ctx.params;
            toForward["command"] = ctx.method;

            auto const res = balancer_->forwardToRippled(toForward, ctx.clientIp, ctx.yield);
            notifyForwarded(ctx.method);

            if (!res)
                return Status{RippledError::rpcFAILED_TO_FORWARD};

            return *res;
        }

        if (backend_->isTooBusy())
        {
            log_.error() << "Database is too busy. Rejecting request";
            return Status{RippledError::rpcTOO_BUSY};
        }

        auto const method = handlerTable_.getHandler(ctx.method);
        if (!method)
            return Status{RippledError::rpcUNKNOWN_COMMAND};

        try
        {
            perfLog_.debug() << ctx.tag() << " start executing rpc `" << ctx.method << '`';

            auto const isAdmin = adminVerifier_.isAdmin(ctx.clientIp);
            auto const context = Context{ctx.yield, ctx.session, isAdmin, ctx.clientIp};
            auto const v = (*method).process(ctx.params, context);

            perfLog_.debug() << ctx.tag() << " finish executing rpc `" << ctx.method << '`';

            if (v)
                return v->as_object();
            else
                return Status{v.error()};
        }
        catch (InvalidParamsError const& err)
        {
            return Status{RippledError::rpcINVALID_PARAMS, err.what()};
        }
        catch (AccountNotFoundError const& err)
        {
            return Status{RippledError::rpcACT_NOT_FOUND, err.what()};
        }
        catch (Backend::DatabaseTimeout const& t)
        {
            log_.error() << "Database timeout";
            return Status{RippledError::rpcTOO_BUSY};
        }
        catch (std::exception const& err)
        {
            log_.error() << ctx.tag() << " caught exception: " << err.what();
            return Status{RippledError::rpcINTERNAL};
        }
    }

    /**
     * @brief Used to schedule request processing onto the work queue
     * @param func The lambda to execute when this request is handled
     * @param ip The ip address for which this request is being executed
     */
    template <typename Fn>
    bool
    post(Fn&& func, std::string const& ip)
    {
        return workQueue_.get().postCoro(std::forward<Fn>(func), dosGuard_.get().isWhiteListed(ip));
    }

    /**
     * @brief Notify the system that specified method was executed
     * @param method
     * @param duration The time it took to execute the method specified in
     * microseconds
     */
    void
    notifyComplete(std::string const& method, std::chrono::microseconds const& duration)
    {
        if (validHandler(method))
            counters_.get().rpcComplete(method, duration);
    }

    /**
     * @brief Notify the system that specified method failed to execute
     * @param method
     */
    void
    notifyErrored(std::string const& method)
    {
        if (validHandler(method))
            counters_.get().rpcErrored(method);
    }

    /**
     * @brief Notify the system that specified method execution was forwarded to rippled
     * @param method
     */
    void
    notifyForwarded(std::string const& method)
    {
        if (validHandler(method))
            counters_.get().rpcForwarded(method);
    }

private:
    bool
    shouldForwardToRippled(Web::Context const& ctx) const
    {
        auto request = ctx.params;

        if (isClioOnly(ctx.method))
            return false;

        if (isForwardCommand(ctx.method))
            return true;

        if (specifiesCurrentOrClosedLedger(request))
            return true;

        if (ctx.method == "account_info" && request.contains("queue") && request.at("queue").as_bool())
            return true;

        return false;
    }

    bool
    isForwardCommand(std::string const& method) const
    {
        static std::unordered_set<std::string> const FORWARD_COMMANDS{
            "submit",
            "submit_multisigned",
            "fee",
            "ledger_closed",
            "ledger_current",
            "ripple_path_find",
            "manifest",
            "channel_authorize",
            "channel_verify",
        };

        return FORWARD_COMMANDS.contains(method);
    }

    bool
    isClioOnly(std::string const& method) const
    {
        return handlerTable_.isClioOnly(method);
    }

    bool
    validHandler(std::string const& method) const
    {
        return handlerTable_.contains(method) || isForwardCommand(method);
    }
};

using RPCEngine = RPCEngineBase<detail::IPAdminVerificationStrategy>;

}  // namespace RPC
