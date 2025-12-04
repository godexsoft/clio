//------------------------------------------------------------------------------
/*
    This file is part of clio: https://github.com/XRPLF/clio
    Copyright (c) 2025, the clio developers.

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

#include <boost/signals2/connection.hpp>
#include <boost/signals2/signal.hpp>
#include <boost/signals2/variadic_signal.hpp>

#include <concepts>

namespace util {

/**
 * @brief Concept defining types that can be observed for changes.
 *
 * A type is Observable if it satisfies all requirements for being stored
 * and monitored in an ObservableValue container:
 *
 * - Must be equality comparable to detect changes
 * - Must be copy constructible for capturing old values in guards
 * - Must be move constructible for efficient value updates
 *
 * @note Copy assignment is intentionally not required since we use move semantics
 *       for value updates and only need copy construction for change detection.
 */
template <typename T>
concept Observable = std::equality_comparable<T> && std::copy_constructible<T> && std::move_constructible<T>;

/**
 * @brief An observable value container that notifies observers when the value changes.
 *
 * ObservableValue wraps a value of type T and provides a mechanism to observe changes to that value.
 * When the value is modified (and actually changes), all registered observers are notified.
 *
 * @tparam T The type of value to observe. Must satisfy the Observable concept.
 *
 * @par Thread Safety
 * - Observer subscription/unsubscription (observe() and connection.disconnect()) are thread-safe
 * - Value modification operations (set(), operator=) are NOT thread-safe and require external synchronization
 * - Observer callbacks are invoked synchronously on the same thread that triggered the value change
 * - If observers need to perform work on different threads, they must handle dispatch themselves
 *   (e.g., using an async execution context or message queue)
 *
 * @par Exception Handling
 * - If an observer callback throws an exception, the exception will propagate to the caller
 * - The value will still be updated even if observers throw exceptions
 * - No guarantee is made about whether other observers will be called if one throws
 * - It is the caller's responsibility to handle exceptions from observer callbacks
 *
 * @par Usage Examples
 * @code
 * // Basic usage
 * util::ObservableValue<int> counter{0};
 *
 * // Subscribe to changes
 * auto connection = counter.observe([](int const& value) {
 *     std::cout << "Counter: " << value << std::endl;
 * });
 *
 * counter = 42;  // Prints "Counter: 42"
 *
 * // Deferred notification using operator->
 * {
 *     auto guard = counter.operator->();
 *     int& ref = guard;
 *     ref = 100;  // No immediate notification
 * } // Notification happens here when guard is destroyed
 *
 * // Manual unsubscribe
 * connection.disconnect();
 *
 * // Automatic unsubscribe using scoped_connection
 * {
 *     boost::signals2::scoped_connection scoped = counter.observe([](int const& value) {
 *         std::cout << "Scoped observer: " << value << std::endl;
 *     });
 *     counter = 200;  // Prints "Scoped observer: 200"
 * } // Automatically disconnects here
 * counter = 300;  // No output - observer was disconnected
 * @endcode
 */
template <Observable T>
class ObservableValue {
    T value_;
    boost::signals2::signal<void(T const&)> onUpdate_;

    /**
     * @brief RAII guard for deferred notification of value changes.
     *
     * ObservableGuard captures the current value when created and compares it
     * with the final value when destroyed. If the values differ, observers
     * are notified. This allows for multiple modifications to the value with
     * only a single notification at the end.
     *
     * @note This class is returned by operator->() and should not be used directly.
     */
    struct ObservableGuard {
        T const oldValue;         ///< Value captured at construction time
        ObservableValue<T>& ref;  ///< Reference to the observable value

        /**
         * @brief Constructs guard and captures current value.
         * @param observable The ObservableValue to guard
         */
        ObservableGuard(ObservableValue<T>& observable) : oldValue(observable), ref(observable)
        {
        }

        /**
         * @brief Destructor that triggers notification if value changed.
         *
         * Compares the captured value with the current value. If they differ,
         * notifies all observers with the current value.
         */
        ~ObservableGuard()
        {
            if (oldValue != ref.value_)
                ref.onUpdate_(ref.value_);
        }

        /**
         * @brief Provides mutable access to the underlying value.
         * @return Mutable reference to the wrapped value
         */
        [[nodiscard]]
        operator T&()
        {
            return ref.value_;
        }
    };

public:
    /**
     * @brief Constructs ObservableValue with initial value.
     * @param value Initial value (must be convertible to T)
     */
    ObservableValue(std::convertible_to<T> auto&& value) : value_{std::forward<decltype(value)>(value)}
    {
    }

    /**
     * @brief Constructs ObservableValue with default initial value.
     */
    ObservableValue()
        requires std::default_initializable<T>
        : value_{}
    {
    }

    ObservableValue(ObservableValue const&) = delete;
    ObservableValue(ObservableValue&&) = default;
    ObservableValue&
    operator=(ObservableValue const&) = delete;
    ObservableValue&
    operator=(ObservableValue&&) = default;

    /**
     * @brief Assignment operator that updates value and notifies observers.
     *
     * Updates the stored value and notifies observers if the new value
     * differs from the current value (using operator!=).
     *
     * @param val New value (must be convertible to T)
     * @return Reference to this object for chaining
     *
     * @throws Any exception thrown by observer callbacks will propagate
     */
    ObservableValue&
    operator=(std::convertible_to<T> auto&& val)
    {
        set(val);
        return *this;
    }

    /**
     * @brief Provides deferred notification access to the value.
     *
     * Returns an ObservableGuard that allows modification of the value
     * with notification deferred until the guard is destroyed.
     *
     * @return ObservableGuard for deferred notification
     *
     * @par Example
     * @code
     * obs->someMethod();  // Call a method on inner value
     * @endcode
     */
    [[nodiscard]] ObservableGuard
    operator->()
    {
        return {*this};
    }

    /**
     * @brief Implicit conversion to const reference of the value.
     * @return Const reference to the stored value
     */
    [[nodiscard]]
    operator T const&() const
    {
        return value_;
    }

    /**
     * @brief Registers an observer callback for value changes.
     *
     * The callback will be invoked synchronously whenever the value changes.
     * The callback receives a const reference to the new value.
     *
     * @param fn Callback function/lambda that accepts T const&
     * @return Connection object for managing the subscription
     *
     * @note The returned connection can be used to manually disconnect the observer by calling
     *       connection.disconnect(). The connection object itself does NOT automatically disconnect
     *       when destroyed - the subscription remains active until explicitly disconnected.
     *
     * @note For automatic disconnection when leaving scope, cast to boost::signals2::scoped_connection:
     * @code
     * // Manual disconnection (subscription persists until disconnect() is called)
     * auto conn = obs.observe(callback);
     * conn.disconnect();  // Explicit disconnection required
     *
     * // Automatic disconnection using scoped_connection
     * {
     *     boost::signals2::scoped_connection scoped = obs.observe(callback);
     *     // ... use observer
     * } // Automatically disconnects here when scoped goes out of scope
     * @endcode
     *
     * @throws Any exception thrown by the callback will propagate to the caller
     *
     * @par Thread Safety
     * - Subscription/unsubscription is thread-safe
     * - The callback is invoked synchronously on the same thread that triggers the value change
     * - If the callback needs to perform work on a different thread, it must handle dispatch itself
     */
    boost::signals2::connection
    observe(std::invocable<T const&> auto&& fn)
    {
        return onUpdate_.connect(std::forward<decltype(fn)>(fn));
    }

    /**
     * @brief Explicitly gets the current value.
     * @return Const reference to the stored value
     */
    [[nodiscard]] T const&
    get() const
    {
        return value_;
    }

    /**
     * @brief Sets a new value and notifies observers if changed.
     *
     * Updates the stored value and notifies all observers if the new value
     * differs from the current value (using operator!=). If the values are
     * equal, no notification occurs.
     *
     * @param val New value (must be convertible to T)
     *
     * @throws Any exception thrown by observer callbacks will propagate
     *
     * @par Thread Safety
     * - This method is NOT thread-safe and requires external synchronization for concurrent access
     * - Observer callbacks are invoked synchronously on the calling thread
     */
    void
    set(std::convertible_to<T> auto&& val)
    {
        if (value_ != val) {
            value_ = std::forward<decltype(val)>(val);
            onUpdate_(value_);
        }
    }

    /**
     * @brief Checks if there are any active observers.
     * @return true if there are observers, false otherwise
     */
    [[nodiscard]] bool
    hasObservers() const
    {
        return not onUpdate_.empty();
    }
};

}  // namespace util
