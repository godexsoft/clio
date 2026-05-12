/** @file */
#pragma once

#include "rpc/Errors.hpp"
#include "rpc/common/spec/Concepts.hpp"
#include "rpc/common/spec/Types.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace rpc::spec {

/**
 * @brief A meta-processor that wraps a requirement or modifier and substitutes a custom
 * @ref rpc::Status when the wrapped processor fails.
 *
 * The custom error is expressed as a @ref rpc::CombinedError code plus an optional message
 * string. The @ref rpc::Status is constructed lazily on the error path only.
 *
 * @tparam Wrapped A type satisfying @ref SomeRequirement or @ref SomeModifier.
 */
template <typename Wrapped>
    requires SomeRequirement<Wrapped> || SomeModifier<Wrapped>
class WithCustomError {
    Wrapped wrapped_;
    rpc::CombinedError code_;
    std::string_view message_;  // empty -> use Status{code} only

public:
    static constexpr std::string_view kNAME = "withCustomError";

    /**
     * @brief Constructs a WithCustomError wrapper.
     *
     * @param w       The requirement or modifier to run.
     * @param code    The error code to report when @p w fails.
     * @param message An optional message appended to the status (defaults to empty).
     *                Should point at static storage in normal usage.
     */
    consteval WithCustomError(Wrapped w, rpc::CombinedError code, std::string_view message = {})
        : wrapped_{std::move(w)}, code_{code}, message_{message}
    {
    }

    /// Read-only access used by the spec dumper.
    [[nodiscard]] Wrapped const&
    wrapped() const noexcept
    {
        return wrapped_;
    }

    /// The wire-format error message attached to the failure (empty if none).
    [[nodiscard]] std::string_view
    message() const noexcept
    {
        return message_;
    }

    /**
     * @brief Runs the wrapped requirement and returns the custom error if it fails.
     *
     * @param fa Field view for the field under validation.
     * @return Empty on success; the custom @ref rpc::Status on failure.
     */
    template <SomeFieldView FA>
    [[nodiscard]] MaybeError
    verify(FA const& fa) const
        requires SomeRequirement<Wrapped>
    {
        if (auto const r = wrapped_.verify(fa); !r)
            return std::unexpected{makeStatus()};
        return {};
    }

    /**
     * @brief Runs the wrapped modifier and returns the custom error if it fails.
     *
     * @param fa Mutable field view for the field under modification.
     * @return Empty on success; the custom @ref rpc::Status on failure.
     */
    template <SomeFieldView FA>
    [[nodiscard]] MaybeError
    modify(FA& fa) const
        requires SomeModifier<Wrapped>
    {
        if (auto const r = wrapped_.modify(fa); !r)
            return std::unexpected{makeStatus()};
        return {};
    }

private:
    [[nodiscard]] rpc::Status
    makeStatus() const
    {
        return message_.empty() ? rpc::Status{code_} : rpc::Status{code_, std::string{message_}};
    }
};

}  // namespace rpc::spec
