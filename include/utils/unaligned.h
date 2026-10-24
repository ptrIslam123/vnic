#pragma once

#include <type_traits>

#include <cstring>

/**
 * @file
 * @brief Helpers for alignment-safe access to packed on-the-wire data.
 */

namespace vnic::utils {

/**
 * @brief Copy a trivially copyable value from a possibly unaligned address.
 *
 * Frame headers are stored at arbitrary offsets inside byte buffers, so the
 * usual `reinterpret_cast` would violate alignment requirements. `memcpy`
 * performs the copy without alignment assumptions.
 *
 * @tparam T Trivially copyable value type.
 * @param src Source address (may be unaligned).
 * @return A copy of the value.
 * @ingroup group_utils
 */
template <typename T>
    requires std::is_trivially_copyable_v<T>
[[nodiscard]] T LoadUnaligned(const void* src) noexcept {
    T value{};
    std::memcpy(&value, src, sizeof(T));
    return value;
}

/**
 * @brief Store a trivially copyable value at a possibly unaligned address.
 *
 * @tparam T Trivially copyable value type.
 * @param dst Destination address (may be unaligned).
 * @param value Value to store.
 * @ingroup group_utils
 */
template <typename T>
    requires std::is_trivially_copyable_v<T>
void StoreUnaligned(void* dst, const T& value) noexcept {
    std::memcpy(dst, &value, sizeof(T));
}

} // namespace vnic::utils
