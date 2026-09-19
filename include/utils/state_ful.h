#pragma once

#include <atomic>
#include <span>

#include <cstdint>

namespace vnic::utils {

template<typename Derived>
class Stateful {
public:
    enum class State : std::uint8_t {
        Configuring = 0,
        Configured,
        Starting,
        Started,
        Stopping,
        Stopped,
        Error,
    };

    Stateful() = default;

    Stateful(const Stateful& other) { (void)this->operator=(other); }
    Stateful& operator=(const Stateful& other) {
        state_ = static_cast<decltype(state_)>(other.getState());
        return *this;
    }

    [[nodiscard]] State getState(
        std::memory_order memoryOrder = std::memory_order_acquire
        ) const noexcept {
        const auto raw = __atomic_load_n(
            &state_,
            std_memory_order_to_int(memoryOrder)
            );
        return static_cast<State>(raw);
    }

    template<typename... Args>
    [[nodiscard]] bool configure(Args&&... args) {
        // Разрешено из Stopped или Configured
        if (!cas({{State::Stopped, State::Configured, State::Error}}, State::Configuring)) [[unlikely]] {
            return false;
        }

        if (!self().configureImpl(std::forward<Args>(args)...)) [[unlikely]] {
            setState(State::Error);
            return false;
        }

        setState(State::Configured);
        return true;
    }

    template<typename... Args>
    [[nodiscard]] bool start(Args&&... args) {
        if (!cas(State::Configured, State::Starting)) [[unlikely]] {
            return false;
        }

        if (!self().startImpl(std::forward<Args>(args)...)) [[unlikely]] {
            setState(State::Error);
            return false;
        }

        setState(State::Started);
        return true;
    }

    template<typename... Args>
    [[nodiscard]] bool stop(Args&&... args) {
        if (!cas(State::Started, State::Stopping)) [[unlikely]] {
            return false;
        }

        if (!self().stopImpl(std::forward<Args>(args)...)) [[unlikely]] {
            setState(State::Error);
            return false;
        }

        setState(State::Stopped);
        return true;
    }

protected:
    void setState(
        State newState,
        std::memory_order memoryOrder = std::memory_order_release
    ) noexcept {
        __atomic_store_n(
            &state_,
            static_cast<std::uint8_t>(newState),
            std_memory_order_to_int(memoryOrder)
        );
    }

    [[nodiscard]] bool cas(State expected, State newState) noexcept {
        auto expectedRaw = static_cast<std::uint8_t>(expected);
        const auto desiredRaw = static_cast<std::uint8_t>(newState);
        return __atomic_compare_exchange_n(
            &state_,
            &expectedRaw,
            desiredRaw,
            false,  // weak = false (strong)
            __ATOMIC_ACQ_REL,
            __ATOMIC_ACQUIRE
        );
    }

    [[nodiscard]] bool cas(
        std::span<const State> expected,
        State newState
    ) noexcept {
        // Пробуем каждое ожидание
        for (auto exp : expected) {
            if (cas(exp, newState)) {
                return true;
            }
        }
        return false;
    }

private:
    static constexpr int std_memory_order_to_int(std::memory_order order) noexcept {
        switch (order) {
            case std::memory_order_relaxed: return __ATOMIC_RELAXED;
            case std::memory_order_consume: return __ATOMIC_CONSUME;
            case std::memory_order_acquire: return __ATOMIC_ACQUIRE;
            case std::memory_order_release: return __ATOMIC_RELEASE;
            case std::memory_order_acq_rel: return __ATOMIC_ACQ_REL;
            case std::memory_order_seq_cst: return __ATOMIC_SEQ_CST;
        }
        return __ATOMIC_SEQ_CST;
    }

    Derived& self() noexcept {
        return *static_cast<Derived*>(this);
    }

    const Derived& self() const noexcept {
        return *static_cast<const Derived*>(this);
    }

    mutable std::uint8_t state_{static_cast<std::uint8_t>(State::Stopped)};
};

} // namespace vnic::utils