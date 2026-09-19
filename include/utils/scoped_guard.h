#ifndef VS_SCOPED_LOCK_H
#define VS_SCOPED_LOCK_H

#include <optional>

namespace vnic::utils {

/**
* @brief This class represents RAII util.
* @details This class expands ability of boots::scoped_prt.
* This class just do two things: lock in constructor(by functor) and unlock(by functor) resources.
* see for more information: https://www.boostcpp.org/doc/libs/1_48_0/libs/smart_ptr/scoped_ptr.htm
* @tparam C - lock functor(callback) type.
* @tparam T - unlock functor(callback) type.
* @warning Size of this class = sizeof(T2).
* @warning Make sure that we don`t lock a really big object in your functor.
* @warning Make sure that we don`t lock a shared resources.
* @example:
* {     // Start scoped
*       auto sockfd = socket(...);
*       ScopedGuard raii([sockfd] { close(sockfd); });
*       if (...) {
*           // do smth1
*       } else if (...) {
*           // do smth2
*           raii.cancel();
*       } ...
*
* }     // End scoped (will call [sockfd] { close(sockfd); } if raii has not unlcoked yet)
*/
template<typename T>
class ScopedGuard final {
public:
    static_assert(std::is_invocable_v<T>, "Attempt to construct ScopeLock with invocable type T");
    using UnlockFunctorType = T;

    explicit ScopedGuard(T unlockCallback);
    ~ScopedGuard();

    void cancel();
    void unlock();
    explicit operator bool() const;
    bool wasUnlocked() const;

    ScopedGuard(const ScopedGuard&) = delete;
    ScopedGuard(ScopedGuard&&) = delete;
    ScopedGuard& operator=(const ScopedGuard&) = delete;
    ScopedGuard& operator=(ScopedGuard&&) = delete;

private:
    std::optional<UnlockFunctorType> m_unlockCallback;
};

template<typename T>
inline ScopedGuard<T>::ScopedGuard(T unlockCallback) :
    m_unlockCallback(std::move(unlockCallback))
{
    static_assert(std::is_invocable_v<T>);
}

template<typename T>
inline ScopedGuard<T>::~ScopedGuard()
{
    unlock();
}

template<typename T>
inline void ScopedGuard<T>::unlock()
{
    if (!wasUnlocked()) {
        m_unlockCallback->operator()();
        cancel();
    }
}

template<typename T>
inline void ScopedGuard<T>::cancel()
{
    m_unlockCallback.reset();
}

template<typename T>
inline ScopedGuard<T>::operator bool() const
{
    return !wasUnlocked();
}

template<typename T>
inline bool ScopedGuard<T>::wasUnlocked() const
{
    return !m_unlockCallback.has_value();
}

} //! namespace atom::utils

#endif //! VS_SCOPED_LOCK_H
