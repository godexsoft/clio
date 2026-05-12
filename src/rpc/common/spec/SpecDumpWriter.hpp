/** @file */
#pragma once

#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace rpc::spec {

/**
 * @brief Indented YAML-ish writer used by the spec dumper.
 *
 * Output shape example:
 *
 *   - account:
 *       - account
 *   - signer_lists:
 *       - type
 *           of: bool
 */
class SpecDumpWriter {
    std::ostream* os_;
    int indent_ = 0;

public:
    explicit SpecDumpWriter(std::ostream& os) noexcept : os_{&os}
    {
    }

    void
    push() noexcept
    {
        ++indent_;
    }

    void
    pop() noexcept
    {
        --indent_;
    }

    [[nodiscard]] std::ostream&
    stream() const noexcept
    {
        return *os_;
    }

    /**
     * @brief Emit a top-of-line "key:" header followed by an indented block built by @p body.
     */
    template <typename Fn>
        requires std::invocable<Fn&>
    void
    header(std::string_view key, Fn&& body)
    {
        writeIndent();
        *os_ << key << ":\n";
        push();
        body();
        pop();
    }

    /**
     * @brief Emit a list bullet "- name" line, then run @p body indented under it.
     */
    template <typename Fn>
        requires std::invocable<Fn&>
    void
    bullet(std::string_view name, Fn&& body)
    {
        writeIndent();
        *os_ << "- " << name << '\n';
        push();
        body();
        pop();
    }

    /**
     * @brief Emit "- name:" for a field-like bullet and run @p body indented under it.
     */
    template <typename Fn>
        requires std::invocable<Fn&>
    void
    bulletGroup(std::string_view name, Fn&& body)
    {
        writeIndent();
        *os_ << "- " << name << ":\n";
        push();
        body();
        pop();
    }

    /**
     * @brief Emit a "key: value" parameter line at the current indent.
     */
    template <typename T>
    void
    param(std::string_view key, T const& value)
    {
        writeIndent();
        *os_ << key << ": ";
        writeScalar(value);
        *os_ << '\n';
    }

    /**
     * @brief Emit a "key: [a, b, c]" parameter line at the current indent.
     */
    template <typename Range>
    void
    paramList(std::string_view key, Range const& values)
    {
        writeIndent();
        *os_ << key << ": [";
        bool first = true;
        for (auto const& v : values) {
            if (!first)
                *os_ << ", ";
            writeScalar(v);
            first = false;
        }
        *os_ << "]\n";
    }

    template <typename T>
    void
    paramList(std::string_view key, std::initializer_list<T> values)
    {
        paramList<std::initializer_list<T>>(key, values);
    }

    /**
     * @brief Emit a single-line plain line at the current indent.
     */
    void
    line(std::string_view text)
    {
        writeIndent();
        *os_ << text << '\n';
    }

private:
    void
    writeIndent() const
    {
        for (int i = 0; i < indent_; ++i)
            *os_ << "  ";
    }

    template <typename T>
    void
    writeScalar(T const& v) const
    {
        using D = std::decay_t<T>;
        if constexpr (std::is_same_v<D, bool>) {
            *os_ << (v ? "true" : "false");
        } else if constexpr (
            std::is_same_v<D, std::string_view> || std::is_same_v<D, std::string> ||
            std::is_same_v<D, char const*>
        ) {
            *os_ << '"' << v << '"';
        } else {
            *os_ << v;
        }
    }
};

}  // namespace rpc::spec
