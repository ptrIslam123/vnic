#ifndef VS_SCOPED_LOCK_H
#define VS_SCOPED_LOCK_H

#include <optional>
#include <type_traits>

/**
 * @file
 * @brief RAII helper that runs a callback on scope exit.
 */

namespace vnic::utils {

/**
 * @brief RAII guard that invokes a callback on scope exit unless cancelled.
 *
 * Useful for cleanup/rollback: acquire a resource, register a release callback,
 * and call `cancel()` once the operation has committed.
 *
 * @tparam T Callback type (a nullary invocable).
 *
 * @warning The guard stores a copy of @p T, so keep the captured state small.
 * @warning The callback runs in the destructor; do not let it throw.
 *
 * Example:
 * @code
 * {
 *     auto fd = open(...);
 *     ScopedGuard guard{[fd] { close(fd); }};
 *     if (ok) {
 *         guard.cancel();   // keep the resource
 *     }
 * }   // otherwise the callback runs here
 * @endcode
 *
 * @ingroup group_utils
 */
template <typename T> class ScopedGuard final {
public:
    static_assert(std::is_invocable_v<T>, "Attempt to construct ScopedGuard with a non-invocable type");
    /// @brief Alias of the stored callback type.
    using UnlockFunctorType = T;

    /// @brief Store the callback to run on scope exit.
    explicit ScopedGuard(T unlockCallback);
    ~ScopedGuard();

    /// @brief Disarm the guard (the callback will not run).
    void cancel();

    /// @brief Run the callback now (if not already disarmed).
    void unlock();

    /// @brief `true` while the callback is still armed.
    explicit operator bool() const;

    /// @brief Whether the guard has already been disarmed.
    bool wasUnlocked() const;

    ScopedGuard(const ScopedGuard&)            = delete;
    ScopedGuard(ScopedGuard&&)                 = delete;
    ScopedGuard& operator=(const ScopedGuard&) = delete;
    ScopedGuard& operator=(ScopedGuard&&)      = delete;

private:
    std::optional<UnlockFunctorType> m_unlockCallback;
};

template <typename T>
inline ScopedGuard<T>::ScopedGuard(T unlockCallback) : m_unlockCallback(std::move(unlockCallback)) {
    static_assert(std::is_invocable_v<T>);
}

template <typename T> inline ScopedGuard<T>::~ScopedGuard() { unlock(); }

template <typename T> inline void ScopedGuard<T>::unlock() {
    if (!wasUnlocked()) {
        m_unlockCallback->operator()();
        cancel();
    }
}

template <typename T> inline void ScopedGuard<T>::cancel() { m_unlockCallback.reset(); }

template <typename T> inline ScopedGuard<T>::operator bool() const { return !wasUnlocked(); }

template <typename T> inline bool ScopedGuard<T>::wasUnlocked() const { return !m_unlockCallback.has_value(); }

} // namespace vnic::utils

#endif //! VS_SCOPED_LOCK_H
